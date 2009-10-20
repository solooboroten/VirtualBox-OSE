/* $Id: thread-r0drv-solaris.c 52665 2009-09-22 12:33:08Z ramshankar $ */
/** @file
 * IPRT - Threads, Ring-0 Driver, Solaris.
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

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-solaris-kernel.h"

#include <iprt/thread.h>
#include <iprt/err.h>
#include <iprt/assert.h>

#define	VBOX_PREEMPT_DISABLE()			\
	{									\
		VBOX_T_PREEMPT++;				\
		ASSERT(VBOX_T_PREEMPT >= 1);		\
	}
#define	VBOX_PREEMPT_ENABLE()			\
	{									\
		ASSERT(VBOX_T_PREEMPT >= 1);		\
		if (--VBOX_T_PREEMPT == 0 &&	\
		    VBOX_CPU_RUNRUN)				\
			kpreempt(KPREEMPT_SYNC);	\
	}

RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)vbi_curthread();
}


RTDECL(int) RTThreadSleep(unsigned cMillies)
{
    clock_t cTicks;
    unsigned long timeout;

    if (!cMillies)
    {
        RTThreadYield();
        return VINF_SUCCESS;
    }

    if (cMillies != RT_INDEFINITE_WAIT)
        cTicks = drv_usectohz((clock_t)(cMillies * 1000L));
    else
        cTicks = 0;

    delay(cTicks);

    return VINF_SUCCESS;
}


RTDECL(bool) RTThreadYield(void)
{
    return vbi_yield();
}


/*
 * Ugly 2.0 Solaris specific hack.
 * Reason: VBI isn't used by the additions and 
 */
RTDECL(void) SolarisThreadPreemptDisable(void)
{
    VBOX_PREEMPT_DISABLE();
}


RTDECL(void) SolarisThreadPreemptRestore(void)
{
    VBOX_PREEMPT_ENABLE();
}


