/** $Id: timerlr-generic.cpp 33842 2008-07-30 11:15:45Z bird $ */
/** @file
 * IPRT - Low Resolution Timers, Generic.
 *
 * This code is more or less identicial to timer-generic.cpp, so
 * bugfixes goes into both files.
 */

/*
 * Copyright (C) 2006-2008 Sun Microsystems, Inc.
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
#include <iprt/timer.h>
#include <iprt/thread.h>
#include <iprt/err.h>
#include <iprt/assert.h>
#include <iprt/alloc.h>
#include <iprt/asm.h>
#include <iprt/semaphore.h>
#include <iprt/time.h>
#include <iprt/log.h>
#include "internal/magics.h"


/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/
/**
 * The internal representation of a timer handle.
 */
typedef struct RTTIMERLRINT
{
    /** Magic.
     * This is RTTIMERRT_MAGIC, but changes to something else before the timer
     * is destroyed to indicate clearly that thread should exit. */
    uint32_t volatile       u32Magic;
    /** Flag indicating the the timer is suspended. */
    bool volatile           fSuspended;
    /** Flag indicating that the timer has been destroyed. */
    bool volatile           fDestroyed;
    /** Callback. */
    PFNRTTIMERLR            pfnTimer;
    /** User argument. */
    void                   *pvUser;
    /** The timer thread. */
    RTTHREAD                hThread;
    /** Event semaphore on which the thread is blocked. */
    RTSEMEVENT              hEvent;
    /** The timer interval. 0 if one-shot. */
    uint64_t                u64NanoInterval;
    /** The start of the current run (ns).
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64StartTS;
    /** The start of the current run (ns).
     * This is used to calculate when the timer ought to fire the next time. */
    uint64_t volatile       u64NextTS;
    /** The current tick number (since u64StartTS). */
    uint64_t volatile       iTick;
} RTTIMERLRINT;
typedef RTTIMERLRINT *PRTTIMERLRINT;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) rtTimerLRThread(RTTHREAD hThread, void *pvUser);


RTDECL(int) RTTimerLRCreateEx(RTTIMERLR *phTimerLR, uint64_t u64NanoInterval, uint32_t fFlags, PFNRTTIMERLR pfnTimer, void *pvUser)
{
    AssertPtr(phTimerLR);
    *phTimerLR = NIL_RTTIMERLR;

    /*
     * We don't support the fancy MP features, nor intervals lower than 100 ms.
     */
    if (fFlags & RTTIMER_FLAGS_CPU_SPECIFIC)
        return VERR_NOT_SUPPORTED;
    if (u64NanoInterval && u64NanoInterval < 100*1000*1000)
        return VERR_INVALID_PARAMETER;

    /*
     * Allocate and initialize the timer handle.
     */
    PRTTIMERLRINT pThis = (PRTTIMERLRINT)RTMemAlloc(sizeof(*pThis));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic = RTTIMERLR_MAGIC;
    pThis->fSuspended = true;
    pThis->fDestroyed = false;
    pThis->pfnTimer = pfnTimer;
    pThis->pvUser = pvUser;
    pThis->hThread = NIL_RTTHREAD;
    pThis->hEvent = NIL_RTSEMEVENT;
    pThis->u64NanoInterval = u64NanoInterval;
    pThis->u64StartTS = 0;

    int rc = RTSemEventCreate(&pThis->hEvent);
    if (RT_SUCCESS(rc))
    {
        rc = RTThreadCreate(&pThis->hThread, rtTimerLRThread, pThis, 0, RTTHREADTYPE_TIMER, RTTHREADFLAGS_WAITABLE, "TIMER");
        if (RT_SUCCESS(rc))
        {
            *phTimerLR = pThis;
            return VINF_SUCCESS;
        }

        pThis->u32Magic = 0;
        RTSemEventDestroy(pThis->hEvent);
        pThis->hEvent = NIL_RTSEMEVENT;
    }
    RTMemFree(pThis);

    return rc;
}


RTDECL(int) RTTimerLRDestroy(RTTIMERLR hTimerLR)
{
    /*
     * Validate input, NIL is fine though.
     */
    if (hTimerLR == NIL_RTTIMERLR)
        return VINF_SUCCESS;
    PRTTIMERLRINT pThis = hTimerLR;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTIMERLR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fDestroyed, VERR_INVALID_HANDLE);

    /*
     * If the timer is active, we just flag it to self destruct on the next tick.
     * If it's suspended we can safely set the destroy flag and signal it.
     */
    RTTHREAD hThread = pThis->hThread;
    if (!pThis->fSuspended)
    {
        ASMAtomicWriteBool(&pThis->fSuspended, true);
        ASMAtomicWriteBool(&pThis->fDestroyed, true);
    }
    else
    {
        ASMAtomicWriteBool(&pThis->fDestroyed, true);
        int rc = RTSemEventSignal(pThis->hEvent);
        if (rc == VERR_ALREADY_POSTED)
            rc = VINF_SUCCESS;
        AssertRC(rc);
    }

    RTThreadWait(hThread, 250, NULL);
    return VINF_SUCCESS;
}


RTDECL(int) RTTimerLRStart(RTTIMERLR hTimerLR, uint64_t u64First)
{
    /*
     * Validate input.
     */
    PRTTIMERLRINT pThis = hTimerLR;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTIMERLR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fDestroyed, VERR_INVALID_HANDLE);

    if (u64First && u64First < 100*1000*1000)
        return VERR_INVALID_PARAMETER;

    if (!pThis->fSuspended)
        return VERR_TIMER_ACTIVE;

    /*
     * Calc when it should start fireing and give the thread a kick so it get going.
     */
    u64First += RTTimeNanoTS();
    ASMAtomicWriteU64(&pThis->iTick, 0);
    ASMAtomicWriteU64(&pThis->u64StartTS, u64First);
    ASMAtomicWriteU64(&pThis->u64NextTS, u64First);
    ASMAtomicWriteBool(&pThis->fSuspended, false);
    int rc = RTSemEventSignal(pThis->hEvent);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);
    return rc;
}


RTDECL(int) RTTimerLRStop(RTTIMERLR hTimerLR)
{
    /*
     * Validate input.
     */
    PRTTIMERLRINT pThis = hTimerLR;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTTIMERLR_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!pThis->fDestroyed, VERR_INVALID_HANDLE);

    if (pThis->fSuspended)
        return VERR_TIMER_SUSPENDED;

    /*
     * Mark it as suspended and kick the thread.
     */
    ASMAtomicWriteBool(&pThis->fSuspended, true);
    int rc = RTSemEventSignal(pThis->hEvent);
    if (rc == VERR_ALREADY_POSTED)
        rc = VINF_SUCCESS;
    AssertRC(rc);
    return rc;
}


static DECLCALLBACK(int) rtTimerLRThread(RTTHREAD hThread, void *pvUser)
{
    PRTTIMERLRINT pThis = (PRTTIMERLRINT)pvUser;

    /*
     * The loop.
     */
    while (!ASMAtomicUoReadBool(&pThis->fDestroyed))
    {
        if (ASMAtomicUoReadBool(&pThis->fSuspended))
        {
            int rc = RTSemEventWait(pThis->hEvent, RT_INDEFINITE_WAIT);
            if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED)
            {
                AssertRC(rc);
                RTThreadSleep(1000); /* Don't cause trouble! */
            }
        }
        else
        {
            const uint64_t u64NanoTS = RTTimeNanoTS();
            if (u64NanoTS >= pThis->u64NextTS)
            {
                pThis->iTick++;
                pThis->pfnTimer(pThis, pThis->pvUser, pThis->iTick);

                /* status changed? */
                if (    ASMAtomicUoReadBool(&pThis->fSuspended)
                    ||  ASMAtomicUoReadBool(&pThis->fDestroyed))
                    continue;

                /* one shot? */
                if (!pThis->u64NanoInterval)
                {
                    ASMAtomicWriteBool(&pThis->fSuspended, true);
                    continue;
                }

                /* calc the next time we should fire. */
                pThis->u64NextTS = pThis->u64StartTS + pThis->iTick * pThis->u64NanoInterval;
                if (pThis->u64NextTS < u64NanoTS)
#ifdef IN_RING3 /* In ring-3 we'll catch up lost ticks immediately. */
                    pThis->u64NextTS = u64NanoTS + 1;
#else
                    pThis->u64NextTS = u64NanoTS + RTTimerGetSystemGranularity() / 2;
#endif
            }

            /* block. */
            uint64_t cNanoSeconds = pThis->u64NextTS - u64NanoTS;
#ifdef IN_RING3 /* In ring-3 we'll catch up lost ticks immediately. */
            if (cNanoSeconds > 10)
#endif
            {
                int rc = RTSemEventWait(pThis->hEvent, cNanoSeconds < 1000000 ? 1 : cNanoSeconds / 1000000);
                if (RT_FAILURE(rc) && rc != VERR_INTERRUPTED && rc != VERR_TIMEOUT)
                {
                    AssertRC(rc);
                    RTThreadSleep(1000); /* Don't cause trouble! */
                }
            }
        }
    }

    /*
     * Release the timer resources.
     */
    ASMAtomicWriteU32(&pThis->u32Magic, ~RTTIMERLR_MAGIC); /* make the handle invalid. */
    int rc = RTSemEventDestroy(pThis->hEvent); AssertRC(rc);
    pThis->hEvent = NIL_RTSEMEVENT;
    pThis->hThread = NIL_RTTHREAD;
    RTMemFree(pThis);

    return VINF_SUCCESS;
}

