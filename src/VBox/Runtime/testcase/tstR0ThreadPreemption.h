/* $Id: tstR0ThreadPreemption.h $ */
/** @file
 * IPRT R0 Testcase - Thread Preemption, common header.
 */

/*
 * Copyright (C) 2009-2013 Oracle Corporation
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
 */

#ifdef IN_RING0
RT_C_DECLS_BEGIN
DECLEXPORT(int) TSTR0ThreadPreemptionSrvReqHandler(PSUPDRVSESSION pSession, uint32_t uOperation,
                                                   uint64_t u64Arg, PSUPR0SERVICEREQHDR pReqHdr);
RT_C_DECLS_END
#endif

typedef enum TSTR0THREADPREMEPTION
{
    TSTR0THREADPREMEPTION_SANITY_OK = 1,
    TSTR0THREADPREMEPTION_SANITY_FAILURE,
    TSTR0THREADPREMEPTION_BASIC,
    TSTR0THREADPREMEPTION_IS_TRUSTY,
    TSTR0THREADPREMEPTION_IS_PENDING,
    TSTR0THREADPREMEPTION_NESTED,
    TSTR0THREADPREEMPTION_CTXHOOKS
} TSTR0THREADPREMEPTION;

