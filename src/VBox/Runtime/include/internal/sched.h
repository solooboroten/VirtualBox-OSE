/* $Id: sched.h $ */
/** @file
 * IPRT - Internal RTSched header.
 */

/*
 * Copyright (C) 2006-2007 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___internal_sched_h
#define ___internal_sched_h

#include <iprt/thread.h>
#include "internal/process.h"
#include "internal/thread.h"

__BEGIN_DECLS

/**
 * Calculate the scheduling properties for all the threads in the default
 * process priority, assuming the current thread have the type enmType.
 *
 * @returns iprt status code.
 * @param   enmType     The thread type to be assumed for the current thread.
 */
int rtSchedNativeCalcDefaultPriority(RTTHREADTYPE enmType);

__END_DECLS

#endif
