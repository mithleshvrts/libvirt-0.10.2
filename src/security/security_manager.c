/*
 * security_manager.c: Internal security manager API
 *
 * Copyright (C) 2010-2011 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 */

#include <config.h>


#include "security_driver.h"
#include "security_stack.h"
#include "security_dac.h"
#include "virterror_internal.h"
#include "memory.h"
#include "logging.h"

#define VIR_FROM_THIS VIR_FROM_SECURITY


struct _virSecurityManager {
    virSecurityDriverPtr drv;
    bool allowDiskFormatProbing;
    bool defaultConfined;
    bool requireConfined;
    const char *virtDriver;
};

static virSecurityManagerPtr virSecurityManagerNewDriver(virSecurityDriverPtr drv,
                                                         const char *virtDriver,
                                                         bool allowDiskFormatProbing,
                                                         bool defaultConfined,
                                                         bool requireConfined)
{
    virSecurityManagerPtr mgr;

    VIR_DEBUG("drv=%p (%s) virtDriver=%s allowDiskFormatProbing=%d "
              "defaultConfined=%d requireConfined=%d",
              drv, drv->name, virtDriver,
              allowDiskFormatProbing, defaultConfined,
              requireConfined);

    if (VIR_ALLOC_VAR(mgr, char, drv->privateDataLen) < 0) {
        virReportOOMError();
        return NULL;
    }

    mgr->drv = drv;
    mgr->allowDiskFormatProbing = allowDiskFormatProbing;
    mgr->defaultConfined = defaultConfined;
    mgr->requireConfined = requireConfined;
    mgr->virtDriver = virtDriver;

    if (drv->open(mgr) < 0) {
        virSecurityManagerFree(mgr);
        return NULL;
    }

    return mgr;
}

virSecurityManagerPtr virSecurityManagerNewStack(virSecurityManagerPtr primary)
{
    virSecurityManagerPtr mgr =
        virSecurityManagerNewDriver(&virSecurityDriverStack,
                                    virSecurityManagerGetDriver(primary),
                                    virSecurityManagerGetAllowDiskFormatProbing(primary),
                                    virSecurityManagerGetDefaultConfined(primary),
                                    virSecurityManagerGetRequireConfined(primary));

    if (!mgr)
        return NULL;

    virSecurityStackAddNested(mgr, primary);

    return mgr;
}

int virSecurityManagerStackAddNested(virSecurityManagerPtr stack,
                                     virSecurityManagerPtr nested)
{
    if (!STREQ("stack", stack->drv->name))
        return -1;
    return virSecurityStackAddNested(stack, nested);
}

virSecurityManagerPtr virSecurityManagerNewDAC(const char *virtDriver,
                                               uid_t user,
                                               gid_t group,
                                               bool allowDiskFormatProbing,
                                               bool defaultConfined,
                                               bool requireConfined,
                                               bool dynamicOwnership)
{
    virSecurityManagerPtr mgr =
        virSecurityManagerNewDriver(&virSecurityDriverDAC,
                                    virtDriver,
                                    allowDiskFormatProbing,
                                    defaultConfined,
                                    requireConfined);

    if (!mgr)
        return NULL;

    virSecurityDACSetUser(mgr, user);
    virSecurityDACSetGroup(mgr, group);
    virSecurityDACSetDynamicOwnership(mgr, dynamicOwnership);

    return mgr;
}

virSecurityManagerPtr virSecurityManagerNew(const char *name,
                                            const char *virtDriver,
                                            bool allowDiskFormatProbing,
                                            bool defaultConfined,
                                            bool requireConfined)
{
    virSecurityDriverPtr drv = virSecurityDriverLookup(name, virtDriver);
    if (!drv)
        return NULL;

    /* driver "none" needs some special handling of *Confined bools */
    if (STREQ(drv->name, "none")) {
        if (requireConfined) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                           _("Security driver \"none\" cannot create confined guests"));
            return NULL;
        }

        if (defaultConfined) {
            if (name != NULL) {
                VIR_WARN("Configured security driver \"none\" disables default"
                         " policy to create confined guests");
            } else {
                VIR_DEBUG("Auto-probed security driver is \"none\";"
                          " confined guests will not be created");
            }
            defaultConfined = false;
        }
    }

    return virSecurityManagerNewDriver(drv,
                                       virtDriver,
                                       allowDiskFormatProbing,
                                       defaultConfined,
                                       requireConfined);
}


/*
 * Must be called before fork()'ing to ensure mutex state
 * is sane for the child to use. A negative return means the
 * child must not be forked; a successful return must be
 * followed by a call to virSecurityManagerPostFork() in both
 * parent and child.
 */
int virSecurityManagerPreFork(virSecurityManagerPtr mgr)
{
    int ret = 0;

    /* XXX Grab our own mutex here instead of relying on caller's mutex */
    if (mgr->drv->preFork) {
        ret = mgr->drv->preFork(mgr);
    }

    return ret;
}


/*
 * Must be called after fork()'ing in both parent and child
 * to ensure mutex state is sane for the child to use
 */
void virSecurityManagerPostFork(virSecurityManagerPtr mgr ATTRIBUTE_UNUSED)
{
    /* XXX Release our own mutex here instead of relying on caller's mutex */
}

void *virSecurityManagerGetPrivateData(virSecurityManagerPtr mgr)
{
    /* This accesses the memory just beyond mgr, which was allocated
     * via VIR_ALLOC_VAR earlier.  */
    return mgr + 1;
}


void virSecurityManagerFree(virSecurityManagerPtr mgr)
{
    if (!mgr)
        return;

    if (mgr->drv->close)
        mgr->drv->close(mgr);

    VIR_FREE(mgr);
}

const char *
virSecurityManagerGetDriver(virSecurityManagerPtr mgr)
{
    return mgr->virtDriver;
}

const char *
virSecurityManagerGetDOI(virSecurityManagerPtr mgr)
{
    if (mgr->drv->getDOI)
        return mgr->drv->getDOI(mgr);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return NULL;
}

const char *
virSecurityManagerGetModel(virSecurityManagerPtr mgr)
{
    if (mgr->drv->getModel)
        return mgr->drv->getModel(mgr);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return NULL;
}

bool virSecurityManagerGetAllowDiskFormatProbing(virSecurityManagerPtr mgr)
{
    return mgr->allowDiskFormatProbing;
}

bool virSecurityManagerGetDefaultConfined(virSecurityManagerPtr mgr)
{
    return mgr->defaultConfined;
}

bool virSecurityManagerGetRequireConfined(virSecurityManagerPtr mgr)
{
    return mgr->requireConfined;
}

int virSecurityManagerRestoreImageLabel(virSecurityManagerPtr mgr,
                                        virDomainDefPtr vm,
                                        virDomainDiskDefPtr disk)
{
    if (mgr->drv->domainRestoreSecurityImageLabel)
        return mgr->drv->domainRestoreSecurityImageLabel(mgr, vm, disk);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetDaemonSocketLabel(virSecurityManagerPtr mgr,
                                           virDomainDefPtr vm)
{
    if (mgr->drv->domainSetSecurityDaemonSocketLabel)
        return mgr->drv->domainSetSecurityDaemonSocketLabel(mgr, vm);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetSocketLabel(virSecurityManagerPtr mgr,
                                     virDomainDefPtr vm)
{
    if (mgr->drv->domainSetSecuritySocketLabel)
        return mgr->drv->domainSetSecuritySocketLabel(mgr, vm);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerClearSocketLabel(virSecurityManagerPtr mgr,
                                       virDomainDefPtr vm)
{
    if (mgr->drv->domainClearSecuritySocketLabel)
        return mgr->drv->domainClearSecuritySocketLabel(mgr, vm);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetImageLabel(virSecurityManagerPtr mgr,
                                    virDomainDefPtr vm,
                                    virDomainDiskDefPtr disk)
{
    if (mgr->drv->domainSetSecurityImageLabel)
        return mgr->drv->domainSetSecurityImageLabel(mgr, vm, disk);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerRestoreHostdevLabel(virSecurityManagerPtr mgr,
                                          virDomainDefPtr vm,
                                          virDomainHostdevDefPtr dev)
{
    if (mgr->drv->domainRestoreSecurityHostdevLabel)
        return mgr->drv->domainRestoreSecurityHostdevLabel(mgr, vm, dev);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetHostdevLabel(virSecurityManagerPtr mgr,
                                      virDomainDefPtr vm,
                                      virDomainHostdevDefPtr dev)
{
    if (mgr->drv->domainSetSecurityHostdevLabel)
        return mgr->drv->domainSetSecurityHostdevLabel(mgr, vm, dev);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetSavedStateLabel(virSecurityManagerPtr mgr,
                                         virDomainDefPtr vm,
                                         const char *savefile)
{
    if (mgr->drv->domainSetSavedStateLabel)
        return mgr->drv->domainSetSavedStateLabel(mgr, vm, savefile);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerRestoreSavedStateLabel(virSecurityManagerPtr mgr,
                                             virDomainDefPtr vm,
                                             const char *savefile)
{
    if (mgr->drv->domainRestoreSavedStateLabel)
        return mgr->drv->domainRestoreSavedStateLabel(mgr, vm, savefile);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerGenLabel(virSecurityManagerPtr mgr,
                               virDomainDefPtr vm)
{
    int ret = -1;
    size_t i, j;
    virSecurityManagerPtr* sec_managers = NULL;
    virSecurityLabelDefPtr seclabel;
    bool generated = false;

    if (mgr == NULL || mgr->drv == NULL)
        return ret;

    if ((sec_managers = virSecurityManagerGetNested(mgr)) == NULL)
        return ret;

    for (i = 0; i < vm->nseclabels; i++) {
        if (!vm->seclabels[i]->model)
            continue;

        for (j = 0; sec_managers[j]; j++)
            if (STREQ(vm->seclabels[i]->model, sec_managers[j]->drv->name))
                break;

        if (!sec_managers[j]) {
            virReportError(VIR_ERR_CONFIG_UNSUPPORTED,
                           _("Unable to find security driver for label %s"),
                           vm->seclabels[i]->model);
            goto cleanup;
        }
    }

    for (i = 0; sec_managers[i]; i++) {
        generated = false;
        seclabel = virDomainDefGetSecurityLabelDef(vm, sec_managers[i]->drv->name);
        if (!seclabel) {
            if (!(seclabel = virDomainDefGenSecurityLabelDef(sec_managers[i]->drv->name)))
                goto cleanup;
            generated = seclabel->implicit = true;
        }

        if (seclabel->type == VIR_DOMAIN_SECLABEL_DEFAULT) {
            if (sec_managers[i]->defaultConfined) {
                seclabel->type = VIR_DOMAIN_SECLABEL_DYNAMIC;
            } else {
                seclabel->type = VIR_DOMAIN_SECLABEL_NONE;
                seclabel->norelabel = true;
            }
        }

        if (seclabel->type == VIR_DOMAIN_SECLABEL_NONE) {
            if (sec_managers[i]->requireConfined) {
                virReportError(VIR_ERR_CONFIG_UNSUPPORTED, "%s",
                               _("Unconfined guests are not allowed on this host"));
                goto cleanup;
            } else if (vm->nseclabels && generated) {
                VIR_DEBUG("Skipping auto generated seclabel of type none");
                virSecurityLabelDefFree(seclabel);
                seclabel = NULL;
                continue;
            }
        }

        if (!sec_managers[i]->drv->domainGenSecurityLabel) {
            virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
        } else {
            /* The seclabel must be added to @vm prior calling domainGenSecurityLabel
             * which may require seclabel to be presented already */
            if (generated &&
                VIR_APPEND_ELEMENT(vm->seclabels, vm->nseclabels, seclabel) < 0) {
                virReportOOMError();
                goto cleanup;
            }

            if (sec_managers[i]->drv->domainGenSecurityLabel(sec_managers[i], vm) < 0) {
                if (VIR_DELETE_ELEMENT(vm->seclabels,
                                       vm->nseclabels -1, vm->nseclabels) < 0)
                    vm->nseclabels--;
                goto cleanup;
            }

            seclabel = NULL;
        }
    }

    ret = 0;

cleanup:
    if (generated)
        virSecurityLabelDefFree(seclabel);
    VIR_FREE(sec_managers);
    return ret;
}

int virSecurityManagerReserveLabel(virSecurityManagerPtr mgr,
                                   virDomainDefPtr vm,
                                   pid_t pid)
{
    if (mgr->drv->domainReserveSecurityLabel)
        return mgr->drv->domainReserveSecurityLabel(mgr, vm, pid);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerReleaseLabel(virSecurityManagerPtr mgr,
                                   virDomainDefPtr vm)
{
    if (mgr->drv->domainReleaseSecurityLabel)
        return mgr->drv->domainReleaseSecurityLabel(mgr, vm);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetAllLabel(virSecurityManagerPtr mgr,
                                  virDomainDefPtr vm,
                                  const char *stdin_path)
{
    if (mgr->drv->domainSetSecurityAllLabel)
        return mgr->drv->domainSetSecurityAllLabel(mgr, vm, stdin_path);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerRestoreAllLabel(virSecurityManagerPtr mgr,
                                      virDomainDefPtr vm,
                                      int migrated)
{
    if (mgr->drv->domainRestoreSecurityAllLabel)
        return mgr->drv->domainRestoreSecurityAllLabel(mgr, vm, migrated);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerGetProcessLabel(virSecurityManagerPtr mgr,
                                      virDomainDefPtr vm,
                                      pid_t pid,
                                      virSecurityLabelPtr sec)
{
    if (mgr->drv->domainGetSecurityProcessLabel)
        return mgr->drv->domainGetSecurityProcessLabel(mgr, vm, pid, sec);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetProcessLabel(virSecurityManagerPtr mgr,
                                      virDomainDefPtr vm)
{
    if (mgr->drv->domainSetSecurityProcessLabel)
        return mgr->drv->domainSetSecurityProcessLabel(mgr, vm);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerVerify(virSecurityManagerPtr mgr,
                             virDomainDefPtr def)
{
    virSecurityLabelDefPtr secdef;

    if (mgr == NULL || mgr->drv == NULL)
        return 0;

    /* NULL model == dynamic labelling, with whatever driver
     * is active, so we can short circuit verify check to
     * avoid drivers de-referencing NULLs by accident
     */
    secdef = virDomainDefGetSecurityLabelDef(def, mgr->drv->name);
    if (secdef == NULL || secdef->model == NULL)
        return 0;

    if (mgr->drv->domainSecurityVerify)
        return mgr->drv->domainSecurityVerify(mgr, def);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetImageFDLabel(virSecurityManagerPtr mgr,
                                      virDomainDefPtr vm,
                                      int fd)
{
    if (mgr->drv->domainSetSecurityImageFDLabel)
        return mgr->drv->domainSetSecurityImageFDLabel(mgr, vm, fd);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

int virSecurityManagerSetTapFDLabel(virSecurityManagerPtr mgr,
                                    virDomainDefPtr vm,
                                    int fd)
{
    if (mgr->drv->domainSetSecurityTapFDLabel)
        return mgr->drv->domainSetSecurityTapFDLabel(mgr, vm, fd);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return -1;
}

char *virSecurityManagerGetMountOptions(virSecurityManagerPtr mgr,
                                        virDomainDefPtr vm)
{
    if (mgr->drv->domainGetSecurityMountOptions)
        return mgr->drv->domainGetSecurityMountOptions(mgr, vm);

    virReportError(VIR_ERR_NO_SUPPORT, __FUNCTION__);
    return NULL;
}

virSecurityManagerPtr*
virSecurityManagerGetNested(virSecurityManagerPtr mgr)
{
    virSecurityManagerPtr* list = NULL;

    if (STREQ("stack", mgr->drv->name)) {
        return virSecurityStackGetNested(mgr);
    }

    if (VIR_ALLOC_N(list, 2) < 0) {
        virReportOOMError();
        return NULL;
    }

    list[0] = mgr;
    list[1] = NULL;
    return list;
}
