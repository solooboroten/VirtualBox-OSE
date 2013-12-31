/* $Id: thread-r0drv-linux.c 20124 2009-05-28 15:40:06Z vboxsync $ */
/** @file
 * IPRT - Threads, Ring-0 Driver, Linux.
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
#include "the-linux-kernel.h"

#include <iprt/thread.h>
#include <iprt/err.h>
#include <iprt/assert.h>


RTDECL(RTNATIVETHREAD) RTThreadNativeSelf(void)
{
    return (RTNATIVETHREAD)current;
}


RTDECL(int)   RTThreadSleep(unsigned cMillies)
{
    long cJiffies = msecs_to_jiffies(cMillies);
    set_current_state(TASK_INTERRUPTIBLE);
    cJiffies = schedule_timeout(cJiffies);
    if (!cJiffies)
        return VINF_SUCCESS;
    return VERR_INTERRUPTED;
}


RTDECL(bool) RTThreadYield(void)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 20)
    yield();
#else
    set_current_state(TASK_RUNNING);
    sys_sched_yield();
    schedule();
#endif
    return true;
}


RTDECL(bool) RTThreadPreemptIsEnabled(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);
#ifdef CONFIG_PREEMPT
# ifdef preemptible
    return preemptible();
# else
    return preempt_count() == 0 && !in_atomic() && !irqs_disabled();
# endif
#else
    return false;
#endif
}


RTDECL(bool) RTThreadPreemptIsPending(RTTHREAD hThread)
{
    Assert(hThread == NIL_RTTHREAD);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 5, 4)
    return test_tsk_thread_flag(current, TIF_NEED_RESCHED);

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 4, 20)
    return need_resched();

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 1, 110)
    return current->need_resched != 0;

#else
    return need_resched != 0;
#endif
}


RTDECL(bool) RTThreadPreemptIsPendingTrusty(void)
{
    /* yes, RTThreadPreemptIsPending is reliable. */
    return true;
}


RTDECL(void) RTThreadPreemptDisable(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->uchDummy != 42);
    pState->uchDummy = 42;

    /*
     * Note: This call is a NOP if CONFIG_PREEMPT is not enabled in the Linux kernel
     * configuration. In that case, schedule() is only called need_resched() is set
     * which is tested just before we return to R3 (not when returning from R0 to R0).
     */
    preempt_disable();
}


RTDECL(void) RTThreadPreemptRestore(PRTTHREADPREEMPTSTATE pState)
{
    AssertPtr(pState);
    Assert(pState->uchDummy == 42);
    pState->uchDummy = 0;

    preempt_enable();
}

