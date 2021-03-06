/*
 * virprocess.h: interaction with processes
 *
 * Copyright (C) 2010-2012 Red Hat, Inc.
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
 */

#ifndef __VIR_PROCESS_H__
# define __VIR_PROCESS_H__

# include <sys/types.h>

# include "internal.h"

char *
virProcessTranslateStatus(int status);

void
virProcessAbort(pid_t pid);

int
virProcessWait(pid_t pid, int *exitstatus)
    ATTRIBUTE_RETURN_CHECK;

int virProcessKill(pid_t pid, int sig);

int virProcessGetStartTime(pid_t pid,
                           unsigned long long *timestamp);

#endif /* __VIR_PROCESS_H__ */
