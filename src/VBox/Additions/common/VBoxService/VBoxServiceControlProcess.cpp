/* $Id$ */
/** @file
 * VBoxServiceControlThread - Guest process handling.
 */

/*
 * Copyright (C) 2012-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/env.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/handle.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/pipe.h>
#include <iprt/poll.h>
#include <iprt/process.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>
#include <iprt/thread.h>

#include <VBox/VBoxGuestLib.h>
#include <VBox/HostServices/GuestControlSvc.h>

#include "VBoxServiceInternal.h"
#include "VBoxServiceControl.h"

using namespace guestControl;

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int                  gstcntlProcessAssignPID(PVBOXSERVICECTRLPROCESS pThread, uint32_t uPID);
static int                  gstcntlProcessRequestCancel(PVBOXSERVICECTRLREQUEST pThread);
static int                  gstcntlProcessSetupPipe(const char *pszHowTo, int fd, PRTHANDLE ph, PRTHANDLE *pph, PRTPIPE phPipe);

/**
 * Initialies the passed in thread data structure with the parameters given.
 *
 * @return  IPRT status code.
 * @param   pProcess                    Process to initialize.
 * @param   pSession                    Guest session the process is bound to.
 * @param   pStartupInfo                Startup information.
 * @param   u32ContextID                The context ID bound to this request / command.
 */
static int gstcntlProcessInit(PVBOXSERVICECTRLPROCESS pProcess,
                              const PVBOXSERVICECTRLSESSION pSession,
                              const PVBOXSERVICECTRLPROCSTARTUPINFO pStartupInfo,
                              uint32_t u32ContextID)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStartupInfo, VERR_INVALID_POINTER);

    /* General stuff. */
    pProcess->pSession   = pSession;
    pProcess->pAnchor    = NULL;
    pProcess->Node.pPrev = NULL;
    pProcess->Node.pNext = NULL;

    pProcess->fShutdown = false;
    pProcess->fStarted  = false;
    pProcess->fStopped  = false;

    pProcess->uContextID = u32ContextID;
    /* ClientID will be assigned when thread is started; every guest
     * process has its own client ID to detect crashes on a per-guest-process
     * level. */

    int rc = RTCritSectInit(&pProcess->CritSect);
    if (RT_FAILURE(rc))
        return rc;

    pProcess->uPID         = 0;          /* Don't have a PID yet. */
    pProcess->pRequest     = NULL;       /* No request assigned yet. */
    pProcess->uFlags       = pProcess->uFlags;
    pProcess->uTimeLimitMS = (   pProcess->uTimeLimitMS == UINT32_MAX
                              || pProcess->uTimeLimitMS == 0)
                           ? RT_INDEFINITE_WAIT : pProcess->uTimeLimitMS;

    /* Prepare argument list. */
    pProcess->uNumArgs = 0; /* Initialize in case of RTGetOptArgvFromString() is failing ... */
    rc = RTGetOptArgvFromString(&pProcess->papszArgs, (int*)&pProcess->uNumArgs,
                                (pStartupInfo->uNumArgs > 0) ? pStartupInfo->szArgs : "", NULL);
    /* Did we get the same result? */
    Assert(pStartupInfo->uNumArgs == pProcess->uNumArgs);

    if (RT_SUCCESS(rc))
    {
        /* Prepare environment list. */
        pProcess->uNumEnvVars = 0;
        if (pProcess->uNumEnvVars)
        {
            pProcess->papszEnv = (char **)RTMemAlloc(pStartupInfo->uNumEnvVars * sizeof(char*));
            AssertPtr(pProcess->papszEnv);
            pProcess->uNumEnvVars = pProcess->uNumEnvVars;

            const char *pszCur = pStartupInfo->szEnv;
            uint32_t i = 0;
            uint32_t cbLen = 0;
            while (cbLen < pStartupInfo->cbEnv)
            {
                /* sanity check */
                if (i >= pStartupInfo->uNumEnvVars)
                {
                    rc = VERR_INVALID_PARAMETER;
                    break;
                }
                int cbStr = RTStrAPrintf(&pProcess->papszEnv[i++], "%s", pszCur);
                if (cbStr < 0)
                {
                    rc = VERR_NO_STR_MEMORY;
                    break;
                }
                pszCur += cbStr + 1; /* Skip terminating '\0' */
                cbLen  += cbStr + 1; /* Skip terminating '\0' */
            }
            Assert(i == pProcess->uNumEnvVars);
        }

        /* The actual command to execute. */
        pProcess->pszCmd      = RTStrDup(pStartupInfo->szCmd);
        AssertPtr(pProcess->pszCmd);

        /* User management. */
        pProcess->pszUser     = RTStrDup(pStartupInfo->szUser);
        AssertPtr(pProcess->pszUser);
        pProcess->pszPassword = RTStrDup(pStartupInfo->szPassword);
        AssertPtr(pProcess->pszPassword);
    }

    if (RT_FAILURE(rc)) /* Clean up on failure. */
        GstCntlProcessFree(pProcess);
    return rc;
}


/**
 * Frees a guest process.
 *
 * @return  IPRT status code.
 * @param   pProcess                Guest process to free.
 */
int GstCntlProcessFree(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);

    VBoxServiceVerbose(3, "[PID %u]: Freeing ...\n",
                       pProcess->uPID);

    int rc = RTCritSectEnter(&pProcess->CritSect);
    if (RT_SUCCESS(rc))
    {
        if (pProcess->uNumEnvVars)
        {
            for (uint32_t i = 0; i < pProcess->uNumEnvVars; i++)
                RTStrFree(pProcess->papszEnv[i]);
            RTMemFree(pProcess->papszEnv);
        }
        RTGetOptArgvFree(pProcess->papszArgs);

        RTStrFree(pProcess->pszCmd);
        RTStrFree(pProcess->pszUser);
        RTStrFree(pProcess->pszPassword);

        VBoxServiceVerbose(3, "[PID %u]: Setting stopped state\n",
                           pProcess->uPID);

        rc = RTCritSectLeave(&pProcess->CritSect);
        AssertRC(rc);
    }

    /*
     * Destroy other thread data.
     */
    if (RTCritSectIsInitialized(&pProcess->CritSect))
        RTCritSectDelete(&pProcess->CritSect);

    /*
     * Destroy thread structure as final step.
     */
    RTMemFree(pProcess);
    pProcess = NULL;

    return rc;
}


/**
 * Signals a guest process thread that we want it to shut down in
 * a gentle way.
 *
 * @return  IPRT status code.
 * @param   pThread             Thread to shut down.
 */
int GstCntlProcessStop(const PVBOXSERVICECTRLPROCESS pThread)
{
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);

    VBoxServiceVerbose(3, "[PID %u]: Stopping ...\n",
                       pThread->uPID);

    int rc = gstcntlProcessRequestCancel(pThread->pRequest);
    if (RT_FAILURE(rc))
        VBoxServiceError("[PID %u]: Signalling request event failed, rc=%Rrc\n",
                         pThread->uPID, rc);

    /* Do *not* set pThread->fShutdown or other stuff here!
     * The guest thread loop will do that as soon as it processes the quit message. */

    PVBOXSERVICECTRLREQUEST pRequest;
    rc = GstCntlProcessRequestAlloc(&pRequest, VBOXSERVICECTRLREQUEST_QUIT);
    if (RT_SUCCESS(rc))
    {
        rc = GstCntlProcessPerform(pThread, pRequest);
        if (RT_FAILURE(rc))
            VBoxServiceVerbose(3, "[PID %u]: Sending quit request failed with rc=%Rrc\n",
                               pThread->uPID, rc);

        GstCntlProcessRequestFree(pRequest);
    }
    return rc;
}


/**
 * Releases (unlocks) a previously locked guest process.
 *
 * @param   pThread                 Thread to unlock.
 */
void GstCntlProcessRelease(const PVBOXSERVICECTRLPROCESS pThread)
{
    AssertPtr(pThread);

    int rc = RTCritSectLeave(&pThread->CritSect);
    AssertRC(rc);
}


/**
 * Wait for a guest process thread to shut down.
 *
 * @return  IPRT status code.
 * @param   pThread             Thread to wait shutting down for.
 * @param   RTMSINTERVAL        Timeout in ms to wait for shutdown.
 * @param   prc                 Where to store the thread's return code. Optional.
 */
int GstCntlProcessWait(const PVBOXSERVICECTRLPROCESS pThread,
                       RTMSINTERVAL msTimeout, int *prc)
{
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    /* prc is optional. */

    int rc = VINF_SUCCESS;
    if (   pThread->Thread != NIL_RTTHREAD
        && ASMAtomicReadBool(&pThread->fStarted))
    {
        VBoxServiceVerbose(2, "[PID %u]: Waiting for shutdown of pThread=0x%p = \"%s\"...\n",
                           pThread->uPID, pThread, pThread->pszCmd);

        /* Wait a bit ... */
        int rcThread;
        rc = RTThreadWait(pThread->Thread, msTimeout, &rcThread);
        if (RT_FAILURE(rc))
        {
            VBoxServiceError("[PID %u]: Waiting for shutting down thread returned error rc=%Rrc\n",
                             pThread->uPID, rc);
        }
        else
        {
            VBoxServiceVerbose(3, "[PID %u]: Thread reported exit code=%Rrc\n",
                               pThread->uPID, rcThread);
            if (prc)
                *prc = rcThread;
        }
    }
    return rc;
}


/**
 * Closes the stdin pipe of a guest process.
 *
 * @return  IPRT status code.
 * @param   hPollSet            The polling set.
 * @param   phStdInW            The standard input pipe handle.
 */
static int gstcntlProcessCloseStdIn(RTPOLLSET hPollSet, PRTPIPE phStdInW)
{
    AssertPtrReturn(phStdInW, VERR_INVALID_POINTER);

    int rc = RTPollSetRemove(hPollSet, VBOXSERVICECTRLPIPEID_STDIN);
    if (rc != VERR_POLL_HANDLE_ID_NOT_FOUND)
        AssertRC(rc);

    if (*phStdInW != NIL_RTPIPE)
    {
        rc = RTPipeClose(*phStdInW);
        AssertRC(rc);
        *phStdInW = NIL_RTPIPE;
    }

    return rc;
}


/**
 * Handle an error event on standard input.
 *
 * @return  IPRT status code.
 * @param   hPollSet            The polling set.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phStdInW            The standard input pipe handle.
 */
static int gstcntlProcessHandleStdInErrorEvent(RTPOLLSET hPollSet, uint32_t fPollEvt, PRTPIPE phStdInW)
{
    NOREF(fPollEvt);

    return gstcntlProcessCloseStdIn(hPollSet, phStdInW);
}


/**
 * Handle pending output data or error on standard out or standard error.
 *
 * @returns IPRT status code from client send.
 * @param   hPollSet            The polling set.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phPipeR             The pipe handle.
 * @param   idPollHnd           The pipe ID to handle.
 *
 */
static int gstcntlProcessHandleOutputError(RTPOLLSET hPollSet, uint32_t fPollEvt,
                                           PRTPIPE phPipeR, uint32_t idPollHnd)
{
    AssertPtrReturn(phPipeR, VERR_INVALID_POINTER);

#ifdef DEBUG
    VBoxServiceVerbose(4, "gstcntlProcessHandleOutputError: fPollEvt=0x%x, idPollHnd=%u\n",
                       fPollEvt, idPollHnd);
#endif

    /* Remove pipe from poll set. */
    int rc2 = RTPollSetRemove(hPollSet, idPollHnd);
    AssertMsg(RT_SUCCESS(rc2) || rc2 == VERR_POLL_HANDLE_ID_NOT_FOUND, ("%Rrc\n", rc2));

    bool fClosePipe = true; /* By default close the pipe. */

    /* Check if there's remaining data to read from the pipe. */
    size_t cbReadable;
    rc2 = RTPipeQueryReadable(*phPipeR, &cbReadable);
    if (   RT_SUCCESS(rc2)
        && cbReadable)
    {
        VBoxServiceVerbose(3, "gstcntlProcessHandleOutputError: idPollHnd=%u has %ld bytes left, vetoing close\n",
                           idPollHnd, cbReadable);

        /* Veto closing the pipe yet because there's still stuff to read
         * from the pipe. This can happen on UNIX-y systems where on
         * error/hangup there still can be data to be read out. */
        fClosePipe = false;
    }
    else
        VBoxServiceVerbose(3, "gstcntlProcessHandleOutputError: idPollHnd=%u will be closed\n",
                           idPollHnd);

    if (   *phPipeR != NIL_RTPIPE
        && fClosePipe)
    {
        rc2 = RTPipeClose(*phPipeR);
        AssertRC(rc2);
        *phPipeR = NIL_RTPIPE;
    }

    return VINF_SUCCESS;
}


/**
 * Handle pending output data or error on standard out or standard error.
 *
 * @returns IPRT status code from client send.
 * @param   hPollSet            The polling set.
 * @param   fPollEvt            The event mask returned by RTPollNoResume.
 * @param   phPipeR             The pipe handle.
 * @param   idPollHnd           The pipe ID to handle.
 *
 */
static int gstcntlProcessHandleOutputEvent(RTPOLLSET hPollSet, uint32_t fPollEvt,
                                           PRTPIPE phPipeR, uint32_t idPollHnd)
{
#if 0
    VBoxServiceVerbose(4, "GstCntlProcessHandleOutputEvent: fPollEvt=0x%x, idPollHnd=%u\n",
                       fPollEvt, idPollHnd);
#endif

    int rc = VINF_SUCCESS;

#ifdef DEBUG
    size_t cbReadable;
    rc = RTPipeQueryReadable(*phPipeR, &cbReadable);
    if (   RT_SUCCESS(rc)
        && cbReadable)
    {
        VBoxServiceVerbose(4, "gstcntlProcessHandleOutputEvent: cbReadable=%ld\n",
                           cbReadable);
    }
#endif

#if 0
    //if (fPollEvt & RTPOLL_EVT_READ)
    {
        size_t cbRead = 0;
        uint8_t byData[_64K];
        rc = RTPipeRead(*phPipeR,
                        byData, sizeof(byData), &cbRead);
        VBoxServiceVerbose(4, "GstCntlProcessHandleOutputEvent cbRead=%u, rc=%Rrc\n",
                           cbRead, rc);

        /* Make sure we go another poll round in case there was too much data
           for the buffer to hold. */
        fPollEvt &= RTPOLL_EVT_ERROR;
    }
#endif

    if (fPollEvt & RTPOLL_EVT_ERROR)
        rc = gstcntlProcessHandleOutputError(hPollSet, fPollEvt,
                                             phPipeR, idPollHnd);
    return rc;
}


/**
 * Signals the given request.
 *
 * @return  IPRT status code.
 * @param   pRequest                Pointer to request to signal.
 * @param   rc                      rc to set request result to.
 */
static int gstcntlProcessSignalRequest(PVBOXSERVICECTRLREQUEST pRequest, int rc)
{
    AssertPtrReturn(pRequest, VERR_INVALID_POINTER);

    /* Assign overall result. */
    pRequest->rc = rc;

#ifdef _DEBUG
    VBoxServiceVerbose(4, "Handled req=%u, CID=%u, rc=%Rrc, cbData=%u, pvData=%p\n",
                       pRequest->enmType, pRequest->uCID, pRequest->rc,
                       pRequest->cbData, pRequest->pvData);
#endif

    /* In any case, regardless of the result, we notify
     * the main guest control to unblock it. */
    int rc2 = RTSemEventMultiSignal(pRequest->Event);
    AssertRC(rc2);

    return rc2;
}


static int gstcntlProcessHandleRequest(RTPOLLSET hPollSet, uint32_t fPollEvt,
                                       PRTPIPE phStdInW, PRTPIPE phStdOutR, PRTPIPE phStdErrR,
                                       PVBOXSERVICECTRLPROCESS pThread, PVBOXSERVICECTRLREQUEST pRequest)
{
    AssertPtrReturn(phStdInW, VERR_INVALID_POINTER);
    AssertPtrReturn(phStdOutR, VERR_INVALID_POINTER);
    AssertPtrReturn(phStdErrR, VERR_INVALID_POINTER);
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertPtrReturn(pRequest, VERR_INVALID_POINTER);

    /* Drain the notification pipe. */
    uint8_t abBuf[8];
    size_t cbIgnore;
    int rc = RTPipeRead(pThread->hNotificationPipeR, abBuf, sizeof(abBuf), &cbIgnore);
    if (RT_FAILURE(rc))
        VBoxServiceError("Draining IPC notification pipe failed with rc=%Rrc\n", rc);

    bool fDefer = false; /* Whether the request completion should be deferred or not. */
    int rcReq = VINF_SUCCESS; /* Actual request result. */

    switch (pRequest->enmType)
    {
        case VBOXSERVICECTRLREQUEST_QUIT: /* Main control asked us to quit. */
        {
            /** @todo Check for some conditions to check to
             *        veto quitting. */
            ASMAtomicXchgBool(&pThread->fShutdown, true);
            rcReq = VERR_CANCELLED;
            break;
        }

        case VBOXSERVICECTRLREQUEST_PROC_STDIN:
        case VBOXSERVICECTRLREQUEST_PROC_STDIN_EOF:
        {
            size_t cbWritten = 0;
            if (pRequest->cbData)
            {
                AssertPtrReturn(pRequest->pvData, VERR_INVALID_POINTER);
                if (*phStdInW != NIL_RTPIPE)
                {
                    rcReq = RTPipeWrite(*phStdInW,
                                        pRequest->pvData, pRequest->cbData, &cbWritten);
                }
                else
                    rcReq = VINF_EOF;
            }

            /*
             * If this is the last write + we have really have written all data
             * we need to close the stdin pipe on our end and remove it from
             * the poll set.
             */
            if (   pRequest->enmType == VBOXSERVICECTRLREQUEST_PROC_STDIN_EOF
                && pRequest->cbData  == cbWritten)
            {
                rc = gstcntlProcessCloseStdIn(hPollSet, phStdInW);
            }

            /* Report back actual data written (if any). */
            pRequest->cbData = cbWritten;
            break;
        }

        case VBOXSERVICECTRLREQUEST_PROC_STDOUT:
        case VBOXSERVICECTRLREQUEST_PROC_STDERR:
        {
            AssertPtrReturn(pRequest->pvData, VERR_INVALID_POINTER);
            AssertReturn(pRequest->cbData, VERR_INVALID_PARAMETER);

            PRTPIPE pPipeR = pRequest->enmType == VBOXSERVICECTRLREQUEST_PROC_STDERR
                           ? phStdErrR : phStdOutR;
            AssertPtr(pPipeR);

            size_t cbRead = 0;
            if (*pPipeR != NIL_RTPIPE)
            {
                rcReq = RTPipeRead(*pPipeR,
                                   pRequest->pvData, pRequest->cbData, &cbRead);
                if (RT_FAILURE(rcReq))
                {
                    RTPollSetRemove(hPollSet, pRequest->enmType == VBOXSERVICECTRLREQUEST_PROC_STDERR
                                              ? VBOXSERVICECTRLPIPEID_STDERR : VBOXSERVICECTRLPIPEID_STDOUT);
                    RTPipeClose(*pPipeR);
                    *pPipeR = NIL_RTPIPE;
                    if (rcReq == VERR_BROKEN_PIPE)
                        rcReq = VINF_EOF;
                }
            }
            else
                rcReq = VINF_EOF;

            /* Report back actual data read (if any). */
            pRequest->cbData = cbRead;
            break;
        }

        case VBOXSERVICECTRLREQUEST_PROC_TERM:
            ASMAtomicXchgBool(&pThread->fShutdown, true);
            fDefer = true;
            break;

        default:
            rcReq = VERR_NOT_IMPLEMENTED;
            break;
    }

    if (   RT_FAILURE(rc)
        || !fDefer)
    {
        rc = gstcntlProcessSignalRequest(pRequest,
                                         RT_SUCCESS(rc) ? rcReq : rc);

        /* No access to pRequest here anymore -- could be out of scope
         * or modified already! */
        pThread->pRequest = pRequest = NULL;
    }
    else /* Completing the request defered. */
        rc = VINF_AIO_TASK_PENDING; /** @todo Find an own rc! */

    return rc;
}


/**
 * Execution loop which runs in a dedicated per-started-process thread and
 * handles all pipe input/output and signalling stuff.
 *
 * @return  IPRT status code.
 * @param   pThread                     The process' thread handle.
 * @param   hProcess                    The actual process handle.
 * @param   cMsTimeout                  Time limit (in ms) of the process' life time.
 * @param   hPollSet                    The poll set to use.
 * @param   hStdInW                     Handle to the process' stdin write end.
 * @param   hStdOutR                    Handle to the process' stdout read end.
 * @param   hStdErrR                    Handle to the process' stderr read end.
 */
static int gstcntlProcessProcLoop(PVBOXSERVICECTRLPROCESS pThread,
                                  RTPROCESS hProcess, RTMSINTERVAL cMsTimeout, RTPOLLSET hPollSet,
                                  PRTPIPE phStdInW, PRTPIPE phStdOutR, PRTPIPE phStdErrR)
{
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertPtrReturn(phStdInW, VERR_INVALID_PARAMETER);
    /* Rest is optional. */

    int                         rc;
    int                         rc2;
    uint64_t const              MsStart             = RTTimeMilliTS();
    RTPROCSTATUS                ProcessStatus       = { 254, RTPROCEXITREASON_ABEND };
    bool                        fProcessAlive       = true;
    bool                        fProcessTimedOut    = false;
    uint64_t                    MsProcessKilled     = UINT64_MAX;
    RTMSINTERVAL const          cMsPollBase         = *phStdInW != NIL_RTPIPE
                                                      ? 100   /* Need to poll for input. */
                                                      : 1000; /* Need only poll for process exit and aborts. */
    RTMSINTERVAL                cMsPollCur          = 0;

    /*
     * Assign PID to thread data.
     * Also check if there already was a thread with the same PID and shut it down -- otherwise
     * the first (stale) entry will be found and we get really weird results!
     */
    rc = gstcntlProcessAssignPID(pThread, hProcess);
    if (RT_FAILURE(rc))
    {
        VBoxServiceError("Unable to assign PID=%u, to new thread, rc=%Rrc\n",
                         hProcess, rc);
        return rc;
    }

    /*
     * Before entering the loop, tell the host that we've started the guest
     * and that it's now OK to send input to the process.
     */
    VBoxServiceVerbose(2, "[PID %u]: Process \"%s\" started, CID=%u, User=%s\n",
                       pThread->uPID, pThread->pszCmd, pThread->uContextID, pThread->pszUser);
    VBGLR3GUESTCTRLCMDCTX ctx = { pThread->uClientID, pThread->uContextID };
    rc = VbglR3GuestCtrlProcCbStatus(&ctx,
                                     pThread->uPID, PROC_STS_STARTED, 0 /* u32Flags */,
                                     NULL /* pvData */, 0 /* cbData */);

    /*
     * Process input, output, the test pipe and client requests.
     */
    PVBOXSERVICECTRLREQUEST pReq = NULL;
    while (   RT_SUCCESS(rc)
           && RT_UNLIKELY(!pThread->fShutdown))
    {
        /*
         * Wait/Process all pending events.
         */
        uint32_t idPollHnd;
        uint32_t fPollEvt;
        rc2 = RTPollNoResume(hPollSet, cMsPollCur, &fPollEvt, &idPollHnd);
        if (pThread->fShutdown)
            continue;

        cMsPollCur = 0; /* No rest until we've checked everything. */

        if (RT_SUCCESS(rc2))
        {
            /*VBoxServiceVerbose(4, "[PID %u}: RTPollNoResume idPollHnd=%u\n",
                                 pThread->uPID, idPollHnd);*/
            switch (idPollHnd)
            {
                case VBOXSERVICECTRLPIPEID_STDIN:
                    rc = gstcntlProcessHandleStdInErrorEvent(hPollSet, fPollEvt, phStdInW);
                    break;

                case VBOXSERVICECTRLPIPEID_STDOUT:
                    rc = gstcntlProcessHandleOutputEvent(hPollSet, fPollEvt,
                                                         phStdOutR, idPollHnd);
                    break;
                case VBOXSERVICECTRLPIPEID_STDERR:
                    rc = gstcntlProcessHandleOutputEvent(hPollSet, fPollEvt,
                                                         phStdErrR, idPollHnd);
                    break;

                case VBOXSERVICECTRLPIPEID_IPC_NOTIFY:
                    pReq = pThread->pRequest; /** @todo Implement request queue. */
                    rc = gstcntlProcessHandleRequest(hPollSet, fPollEvt,
                                                     phStdInW, phStdOutR, phStdErrR,
                                                     pThread, pReq);
                    if (rc != VINF_AIO_TASK_PENDING)
                        pReq = NULL;
                    break;

                default:
                    AssertMsgFailed(("Unknown idPollHnd=%RU32\n", idPollHnd));
                    break;
            }

            if (RT_FAILURE(rc) || rc == VINF_EOF)
                break; /* Abort command, or client dead or something. */

            if (RT_UNLIKELY(pThread->fShutdown))
                break; /* We were asked to shutdown. */

            continue;
        }

#if 0
        VBoxServiceVerbose(4, "[PID %u]: Polling done, pollRC=%Rrc, pollCnt=%u, rc=%Rrc, fShutdown=%RTbool\n",
                           pThread->uPID, rc2, RTPollSetGetCount(hPollSet), rc, pThread->fShutdown);
#endif
        /*
         * Check for process death.
         */
        if (fProcessAlive)
        {
            rc2 = RTProcWaitNoResume(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &ProcessStatus);
            if (RT_SUCCESS_NP(rc2))
            {
                fProcessAlive = false;
                continue;
            }
            if (RT_UNLIKELY(rc2 == VERR_INTERRUPTED))
                continue;
            if (RT_UNLIKELY(rc2 == VERR_PROCESS_NOT_FOUND))
            {
                fProcessAlive = false;
                ProcessStatus.enmReason = RTPROCEXITREASON_ABEND;
                ProcessStatus.iStatus   = 255;
                AssertFailed();
            }
            else
                AssertMsg(rc2 == VERR_PROCESS_RUNNING, ("%Rrc\n", rc2));
        }

        /*
         * If the process has terminated and all output has been consumed,
         * we should be heading out.
         */
        if (   !fProcessAlive
            && *phStdOutR == NIL_RTPIPE
            && *phStdErrR == NIL_RTPIPE)
            break;

        /*
         * Check for timed out, killing the process.
         */
        uint32_t cMilliesLeft = RT_INDEFINITE_WAIT;
        if (cMsTimeout != RT_INDEFINITE_WAIT)
        {
            uint64_t u64Now = RTTimeMilliTS();
            uint64_t cMsElapsed = u64Now - MsStart;
            if (cMsElapsed >= cMsTimeout)
            {
                VBoxServiceVerbose(3, "[PID %u]: Timed out (%ums elapsed > %ums timeout), killing ...\n",
                                   pThread->uPID, cMsElapsed, cMsTimeout);

                fProcessTimedOut = true;
                if (    MsProcessKilled == UINT64_MAX
                    ||  u64Now - MsProcessKilled > 1000)
                {
                    if (u64Now - MsProcessKilled > 20*60*1000)
                        break; /* Give up after 20 mins. */
                    RTProcTerminate(hProcess);
                    MsProcessKilled = u64Now;
                    continue;
                }
                cMilliesLeft = 10000;
            }
            else
                cMilliesLeft = cMsTimeout - (uint32_t)cMsElapsed;
        }

        /* Reset the polling interval since we've done all pending work. */
        cMsPollCur = fProcessAlive
                   ? cMsPollBase
                   : RT_MS_1MIN;
        if (cMilliesLeft < cMsPollCur)
            cMsPollCur = cMilliesLeft;

        /*
         * Need to exit?
         */
        if (pThread->fShutdown)
            break;
    }

    rc2 = RTCritSectEnter(&pThread->CritSect);
    if (RT_SUCCESS(rc2))
    {
        ASMAtomicXchgBool(&pThread->fShutdown, true);

        rc2 = RTCritSectLeave(&pThread->CritSect);
        AssertRC(rc2);
    }

    /*
     * Try kill the process if it's still alive at this point.
     */
    if (fProcessAlive)
    {
        if (MsProcessKilled == UINT64_MAX)
        {
            VBoxServiceVerbose(3, "[PID %u]: Is still alive and not killed yet\n",
                               pThread->uPID);

            MsProcessKilled = RTTimeMilliTS();
            RTProcTerminate(hProcess);
            RTThreadSleep(500);
        }

        for (size_t i = 0; i < 10; i++)
        {
            VBoxServiceVerbose(4, "[PID %u]: Kill attempt %d/10: Waiting to exit ...\n",
                               pThread->uPID, i + 1);
            rc2 = RTProcWait(hProcess, RTPROCWAIT_FLAGS_NOBLOCK, &ProcessStatus);
            if (RT_SUCCESS(rc2))
            {
                VBoxServiceVerbose(4, "[PID %u]: Kill attempt %d/10: Exited\n",
                                   pThread->uPID, i + 1);
                fProcessAlive = false;
                break;
            }
            if (i >= 5)
            {
                VBoxServiceVerbose(4, "[PID %u]: Kill attempt %d/10: Trying to terminate ...\n",
                                   pThread->uPID, i + 1);
                RTProcTerminate(hProcess);
            }
            RTThreadSleep(i >= 5 ? 2000 : 500);
        }

        if (fProcessAlive)
            VBoxServiceVerbose(3, "[PID %u]: Could not be killed\n", pThread->uPID);

        if (   pReq /* Handle deferred termination request. */
            && pReq->enmType == VBOXSERVICECTRLREQUEST_PROC_TERM)
        {
            rc2 = gstcntlProcessSignalRequest(pReq,
                                              fProcessAlive ? VINF_SUCCESS : VERR_PROCESS_RUNNING);
            pReq = NULL;
        }
        else if (pReq)
            AssertMsgFailed(("Unable to handle unknown deferred request (type: %RU32)\n", pReq->enmType));
    }

    /*
     * If we don't have a client problem (RT_FAILURE(rc)) we'll reply to the
     * clients exec packet now.
     */
    if (RT_SUCCESS(rc))
    {
        uint32_t uStatus = PROC_STS_UNDEFINED;
        uint32_t uFlags = 0;

        if (     fProcessTimedOut  && !fProcessAlive && MsProcessKilled != UINT64_MAX)
        {
            VBoxServiceVerbose(3, "[PID %u]: Timed out and got killed\n",
                               pThread->uPID);
            uStatus = PROC_STS_TOK;
        }
        else if (fProcessTimedOut  &&  fProcessAlive && MsProcessKilled != UINT64_MAX)
        {
            VBoxServiceVerbose(3, "[PID %u]: Timed out and did *not* get killed\n",
                               pThread->uPID);
            uStatus = PROC_STS_TOA;
        }
        else if (pThread->fShutdown && (fProcessAlive || MsProcessKilled != UINT64_MAX))
        {
            VBoxServiceVerbose(3, "[PID %u]: Got terminated because system/service is about to shutdown\n",
                               pThread->uPID);
            uStatus = PROC_STS_DWN; /* Service is stopping, process was killed. */
            uFlags = pThread->uFlags; /* Return handed-in execution flags back to the host. */
        }
        else if (fProcessAlive)
        {
            VBoxServiceError("[PID %u]: Is alive when it should not!\n",
                             pThread->uPID);
        }
        else if (MsProcessKilled != UINT64_MAX)
        {
            VBoxServiceError("[PID %u]: Has been killed when it should not!\n",
                             pThread->uPID);
        }
        else if (ProcessStatus.enmReason == RTPROCEXITREASON_NORMAL)
        {
            VBoxServiceVerbose(3, "[PID %u]: Ended with RTPROCEXITREASON_NORMAL (Exit code: %u)\n",
                               pThread->uPID, ProcessStatus.iStatus);

            uStatus = PROC_STS_TEN;
            uFlags = ProcessStatus.iStatus;
        }
        else if (ProcessStatus.enmReason == RTPROCEXITREASON_SIGNAL)
        {
            VBoxServiceVerbose(3, "[PID %u]: Ended with RTPROCEXITREASON_SIGNAL (Signal: %u)\n",
                               pThread->uPID, ProcessStatus.iStatus);

            uStatus = PROC_STS_TES;
            uFlags = ProcessStatus.iStatus;
        }
        else if (ProcessStatus.enmReason == RTPROCEXITREASON_ABEND)
        {
            /* ProcessStatus.iStatus will be undefined. */
            VBoxServiceVerbose(3, "[PID %u]: Ended with RTPROCEXITREASON_ABEND\n",
                               pThread->uPID);

            uStatus = PROC_STS_TEA;
            uFlags = ProcessStatus.iStatus;
        }
        else
            VBoxServiceVerbose(1, "[PID %u]: Handling process status %u not implemented\n",
                               pThread->uPID, ProcessStatus.enmReason);

        VBoxServiceVerbose(2, "[PID %u]: Ended, ClientID=%u, CID=%u, Status=%u, Flags=0x%x\n",
                           pThread->uPID, pThread->uClientID, pThread->uContextID, uStatus, uFlags);

        if (!(pThread->uFlags & EXECUTEPROCESSFLAG_WAIT_START))
        {
            VBGLR3GUESTCTRLCMDCTX ctx = { pThread->uClientID, pThread->uContextID };
            rc2 = VbglR3GuestCtrlProcCbStatus(&ctx,
                                              pThread->uPID, uStatus, uFlags,
                                              NULL /* pvData */, 0 /* cbData */);
            if (RT_FAILURE(rc2))
                VBoxServiceError("[PID %u]: Error reporting final status to host; rc=%Rrc\n",
                                 pThread->uPID, rc2);
            if (RT_SUCCESS(rc))
                rc = rc2;
        }
        else
            VBoxServiceVerbose(3, "[PID %u]: Was started detached, no final status sent to host\n",
                               pThread->uPID);

        VBoxServiceVerbose(3, "[PID %u]: Process loop ended with rc=%Rrc\n",
                           pThread->uPID, rc);
    }
    else
        VBoxServiceError("[PID %u]: Loop failed with rc=%Rrc\n",
                         pThread->uPID, rc);
    return rc;
}


/**
 * Initializes a pipe's handle and pipe object.
 *
 * @return  IPRT status code.
 * @param   ph                      The pipe's handle to initialize.
 * @param   phPipe                  The pipe's object to initialize.
 */
static int gstcntlProcessInitPipe(PRTHANDLE ph, PRTPIPE phPipe)
{
    AssertPtrReturn(ph, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phPipe, VERR_INVALID_PARAMETER);

    ph->enmType = RTHANDLETYPE_PIPE;
    ph->u.hPipe = NIL_RTPIPE;
    *phPipe     = NIL_RTPIPE;

    return VINF_SUCCESS;
}


/**
 * Allocates a guest thread request with the specified request data.
 *
 * @return  IPRT status code.
 * @param   ppReq                   Pointer that will receive the newly allocated request.
 *                                  Must be freed later with GstCntlProcessRequestFree().
 * @param   enmType                 Request type.
 * @param   pvData                  Payload data, based on request type.
 * @param   cbData                  Size of payload data (in bytes).
 * @param   uCID                    Context ID to which this request belongs to.
 */
int GstCntlProcessRequestAllocEx(PVBOXSERVICECTRLREQUEST   *ppReq,
                                 VBOXSERVICECTRLREQUESTTYPE enmType,
                                 void                      *pvData,
                                 size_t                     cbData,
                                 uint32_t                   uCID)
{
    AssertPtrReturn(ppReq, VERR_INVALID_POINTER);

    PVBOXSERVICECTRLREQUEST pReq = (PVBOXSERVICECTRLREQUEST)RTMemAlloc(sizeof(VBOXSERVICECTRLREQUEST));
    AssertPtrReturn(pReq, VERR_NO_MEMORY);

    RT_ZERO(*pReq);
    pReq->enmType = enmType;
    pReq->uCID    = uCID;
    pReq->cbData  = cbData;
    pReq->pvData  = pvData;

    /* Set request result to some defined state in case
     * it got cancelled. */
    pReq->rc      = VERR_CANCELLED;

    int rc = RTSemEventMultiCreate(&pReq->Event);
    AssertRC(rc);

    if (RT_SUCCESS(rc))
    {
        *ppReq = pReq;
        return VINF_SUCCESS;
    }

    RTMemFree(pReq);
    return rc;
}


/**
 * Allocates a guest thread request with the specified request data.
 *
 * @return  IPRT status code.
 * @param   ppReq                   Pointer that will receive the newly allocated request.
 *                                  Must be freed later with GstCntlProcessRequestFree().
 * @param   enmType                 Request type.
 */
int GstCntlProcessRequestAlloc(PVBOXSERVICECTRLREQUEST *ppReq,
                               VBOXSERVICECTRLREQUESTTYPE enmType)
{
    return GstCntlProcessRequestAllocEx(ppReq, enmType,
                                        NULL /* pvData */, 0 /* cbData */,
                                        0 /* ContextID */);
}


/**
 * Cancels a previously fired off guest thread request.
 *
 * Note: Does *not* do locking since GstCntlProcessRequestWait()
 * holds the lock (critsect); so only trigger the signal; the owner
 * needs to clean up afterwards.
 *
 * @return  IPRT status code.
 * @param   pReq                    Request to cancel.
 */
static int gstcntlProcessRequestCancel(PVBOXSERVICECTRLREQUEST pReq)
{
    if (!pReq) /* Silently skip non-initialized requests. */
        return VINF_SUCCESS;

    VBoxServiceVerbose(4, "Cancelling request=0x%p\n", pReq);

    return RTSemEventMultiSignal(pReq->Event);
}


/**
 * Frees a formerly allocated guest thread request.
 *
 * @return  IPRT status code.
 * @param   pReq                    Request to free.
 */
void GstCntlProcessRequestFree(PVBOXSERVICECTRLREQUEST pReq)
{
    AssertPtrReturnVoid(pReq);

    VBoxServiceVerbose(4, "Freeing request=0x%p (event=%RTsem)\n",
                       pReq, &pReq->Event);

    int rc = RTSemEventMultiDestroy(pReq->Event);
    AssertRC(rc);

    RTMemFree(pReq);
    pReq = NULL;
}


/**
 * Waits for a guest thread's event to get triggered.
 *
 * @return  IPRT status code.
 * @param   pReq                    Request to wait for.
 */
int GstCntlProcessRequestWait(PVBOXSERVICECTRLREQUEST pReq)
{
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);

    /* Wait on the request to get completed (or we are asked to abort/shutdown). */
    int rc = RTSemEventMultiWait(pReq->Event, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        VBoxServiceVerbose(4, "Performed request with rc=%Rrc, cbData=%u\n",
                           pReq->rc, pReq->cbData);

        /* Give back overall request result. */
        rc = pReq->rc;
    }
    else
        VBoxServiceError("Waiting for request failed, rc=%Rrc\n", rc);

    return rc;
}


/**
 * Sets up the redirection / pipe / nothing for one of the standard handles.
 *
 * @returns IPRT status code.  No client replies made.
 * @param   pszHowTo            How to set up this standard handle.
 * @param   fd                  Which standard handle it is (0 == stdin, 1 ==
 *                              stdout, 2 == stderr).
 * @param   ph                  The generic handle that @a pph may be set
 *                              pointing to.  Always set.
 * @param   pph                 Pointer to the RTProcCreateExec argument.
 *                              Always set.
 * @param   phPipe              Where to return the end of the pipe that we
 *                              should service.
 */
static int gstcntlProcessSetupPipe(const char *pszHowTo, int fd,
                                   PRTHANDLE ph, PRTHANDLE *pph, PRTPIPE phPipe)
{
    AssertPtrReturn(ph, VERR_INVALID_POINTER);
    AssertPtrReturn(pph, VERR_INVALID_POINTER);
    AssertPtrReturn(phPipe, VERR_INVALID_POINTER);

    int rc;

    ph->enmType = RTHANDLETYPE_PIPE;
    ph->u.hPipe = NIL_RTPIPE;
    *pph        = NULL;
    *phPipe     = NIL_RTPIPE;

    if (!strcmp(pszHowTo, "|"))
    {
        /*
         * Setup a pipe for forwarding to/from the client.
         * The ph union struct will be filled with a pipe read/write handle
         * to represent the "other" end to phPipe.
         */
        if (fd == 0) /* stdin? */
        {
            /* Connect a wrtie pipe specified by phPipe to stdin. */
            rc = RTPipeCreate(&ph->u.hPipe, phPipe, RTPIPE_C_INHERIT_READ);
        }
        else /* stdout or stderr? */
        {
            /* Connect a read pipe specified by phPipe to stdout or stderr. */
            rc = RTPipeCreate(phPipe, &ph->u.hPipe, RTPIPE_C_INHERIT_WRITE);
        }

        if (RT_FAILURE(rc))
            return rc;

        ph->enmType = RTHANDLETYPE_PIPE;
        *pph = ph;
    }
    else if (!strcmp(pszHowTo, "/dev/null"))
    {
        /*
         * Redirect to/from /dev/null.
         */
        RTFILE hFile;
        rc = RTFileOpenBitBucket(&hFile, fd == 0 ? RTFILE_O_READ : RTFILE_O_WRITE);
        if (RT_FAILURE(rc))
            return rc;

        ph->enmType = RTHANDLETYPE_FILE;
        ph->u.hFile = hFile;
        *pph = ph;
    }
    else /* Add other piping stuff here. */
        rc = VINF_SUCCESS; /* Same as parent (us). */

    return rc;
}


/**
 * Expands a file name / path to its real content. This only works on Windows
 * for now (e.g. translating "%TEMP%\foo.exe" to "C:\Windows\Temp" when starting
 * with system / administrative rights).
 *
 * @return  IPRT status code.
 * @param   pszPath                     Path to resolve.
 * @param   pszExpanded                 Pointer to string to store the resolved path in.
 * @param   cbExpanded                  Size (in bytes) of string to store the resolved path.
 */
static int gstcntlProcessMakeFullPath(const char *pszPath, char *pszExpanded, size_t cbExpanded)
{
    int rc = VINF_SUCCESS;
#ifdef RT_OS_WINDOWS
    if (!ExpandEnvironmentStrings(pszPath, pszExpanded, cbExpanded))
        rc = RTErrConvertFromWin32(GetLastError());
#else
    /* No expansion for non-Windows yet. */
    rc = RTStrCopy(pszExpanded, cbExpanded, pszPath);
#endif
#ifdef DEBUG
    VBoxServiceVerbose(3, "VBoxServiceControlExecMakeFullPath: %s -> %s\n",
                       pszPath, pszExpanded);
#endif
    return rc;
}


/**
 * Resolves the full path of a specified executable name. This function also
 * resolves internal VBoxService tools to its appropriate executable path + name if
 * VBOXSERVICE_NAME is specified as pszFileName.
 *
 * @return  IPRT status code.
 * @param   pszFileName                 File name to resolve.
 * @param   pszResolved                 Pointer to a string where the resolved file name will be stored.
 * @param   cbResolved                  Size (in bytes) of resolved file name string.
 */
static int gstcntlProcessResolveExecutable(const char *pszFileName,
                                           char *pszResolved, size_t cbResolved)
{
    AssertPtrReturn(pszFileName, VERR_INVALID_POINTER);
    AssertPtrReturn(pszResolved, VERR_INVALID_POINTER);
    AssertReturn(cbResolved, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;

    char szPathToResolve[RTPATH_MAX];
    if (    (g_pszProgName && (RTStrICmp(pszFileName, g_pszProgName) == 0))
        || !RTStrICmp(pszFileName, VBOXSERVICE_NAME))
    {
        /* Resolve executable name of this process. */
        if (!RTProcGetExecutablePath(szPathToResolve, sizeof(szPathToResolve)))
            rc = VERR_FILE_NOT_FOUND;
    }
    else
    {
        /* Take the raw argument to resolve. */
        rc = RTStrCopy(szPathToResolve, sizeof(szPathToResolve), pszFileName);
    }

    if (RT_SUCCESS(rc))
    {
        rc = gstcntlProcessMakeFullPath(szPathToResolve, pszResolved, cbResolved);
        if (RT_SUCCESS(rc))
            VBoxServiceVerbose(3, "Looked up executable: %s -> %s\n",
                               pszFileName, pszResolved);
    }

    if (RT_FAILURE(rc))
        VBoxServiceError("Failed to lookup executable \"%s\" with rc=%Rrc\n",
                         pszFileName, rc);
    return rc;
}


/**
 * Constructs the argv command line by resolving environment variables
 * and relative paths.
 *
 * @return IPRT status code.
 * @param  pszArgv0         First argument (argv0), either original or modified version.  Optional.
 * @param  papszArgs        Original argv command line from the host, starting at argv[1].
 * @param  ppapszArgv       Pointer to a pointer with the new argv command line.
 *                          Needs to be freed with RTGetOptArgvFree.
 */
static int gstcntlProcessAllocateArgv(const char *pszArgv0,
                                      const char * const *papszArgs,
                                      bool fExpandArgs, char ***ppapszArgv)
{
    AssertPtrReturn(ppapszArgv, VERR_INVALID_POINTER);

    VBoxServiceVerbose(3, "GstCntlProcessPrepareArgv: pszArgv0=%p, papszArgs=%p, fExpandArgs=%RTbool, ppapszArgv=%p\n",
                       pszArgv0, papszArgs, fExpandArgs, ppapszArgv);

    int rc = VINF_SUCCESS;
    uint32_t cArgs;
    for (cArgs = 0; papszArgs[cArgs]; cArgs++)
    {
        if (cArgs >= UINT32_MAX - 2)
            return VERR_BUFFER_OVERFLOW;
    }

    /* Allocate new argv vector (adding + 2 for argv0 + termination). */
    size_t cbSize = (cArgs + 2) * sizeof(char*);
    char **papszNewArgv = (char**)RTMemAlloc(cbSize);
    if (!papszNewArgv)
        return VERR_NO_MEMORY;

#ifdef DEBUG
    VBoxServiceVerbose(3, "GstCntlProcessAllocateArgv: cbSize=%RU32, cArgs=%RU32\n",
                       cbSize, cArgs);
#endif

    size_t i = 0; /* Keep the argument counter in scope for cleaning up on failure. */

    rc = RTStrDupEx(&papszNewArgv[0], pszArgv0);
    if (RT_SUCCESS(rc))
    {
        for (; i < cArgs; i++)
        {
            char *pszArg;
#if 0 /* Arguments expansion -- untested. */
            if (fExpandArgs)
            {
                /* According to MSDN the limit on older Windows version is 32K, whereas
                 * Vista+ there are no limits anymore. We still stick to 4K. */
                char szExpanded[_4K];
# ifdef RT_OS_WINDOWS
                if (!ExpandEnvironmentStrings(papszArgs[i], szExpanded, sizeof(szExpanded)))
                    rc = RTErrConvertFromWin32(GetLastError());
# else
                /* No expansion for non-Windows yet. */
                rc = RTStrCopy(papszArgs[i], sizeof(szExpanded), szExpanded);
# endif
                if (RT_SUCCESS(rc))
                    rc = RTStrDupEx(&pszArg, szExpanded);
            }
            else
#endif
                rc = RTStrDupEx(&pszArg, papszArgs[i]);

            if (RT_FAILURE(rc))
                break;

            papszNewArgv[i + 1] = pszArg;
        }

        if (RT_SUCCESS(rc))
        {
            /* Terminate array. */
            papszNewArgv[cArgs + 1] = NULL;

            *ppapszArgv = papszNewArgv;
        }
    }

    if (RT_FAILURE(rc))
    {
        for (i; i > 0; i--)
            RTStrFree(papszNewArgv[i]);
        RTMemFree(papszNewArgv);
    }

    return rc;
}


/**
 * Assigns a valid PID to a guest control thread and also checks if there already was
 * another (stale) guest process which was using that PID before and destroys it.
 *
 * @return  IPRT status code.
 * @param   pThread        Thread to assign PID to.
 * @param   uPID           PID to assign to the specified guest control execution thread.
 */
int gstcntlProcessAssignPID(PVBOXSERVICECTRLPROCESS pThread, uint32_t uPID)
{
    AssertPtrReturn(pThread, VERR_INVALID_POINTER);
    AssertReturn(uPID, VERR_INVALID_PARAMETER);

    AssertPtr(pThread->pSession);
    int rc = RTCritSectEnter(&pThread->pSession->csControlThreads);
    if (RT_SUCCESS(rc))
    {
        /* Search old threads using the desired PID and shut them down completely -- it's
         * not used anymore. */
        PVBOXSERVICECTRLPROCESS pThreadCur;
        bool fTryAgain = false;
        do
        {
            RTListForEach(&pThread->pSession->lstControlThreadsActive, pThreadCur, VBOXSERVICECTRLPROCESS, Node)
            {
                if (pThreadCur->uPID == uPID)
                {
                    Assert(pThreadCur != pThread); /* can't happen */
                    uint32_t uTriedPID = uPID;
                    uPID += 391939;
                    VBoxServiceVerbose(2, "PID %RU32 was used before, trying again with %u ...\n",
                                       uTriedPID, uPID);
                    fTryAgain = true;
                    break;
                }
            }
        } while (fTryAgain);

        /* Assign PID to current thread. */
        pThread->uPID = uPID;

        rc = RTCritSectLeave(&pThread->pSession->csControlThreads);
        AssertRC(rc);
    }

    return rc;
}


void gstcntlProcessFreeArgv(char **papszArgv)
{
    if (papszArgv)
    {
        size_t i = 0;
        while (papszArgv[i])
            RTStrFree(papszArgv[i++]);
        RTMemFree(papszArgv);
    }
}


/**
 * Helper function to create/start a process on the guest.
 *
 * @return  IPRT status code.
 * @param   pszExec                     Full qualified path of process to start (without arguments).
 * @param   papszArgs                   Pointer to array of command line arguments.
 * @param   hEnv                        Handle to environment block to use.
 * @param   fFlags                      Process execution flags.
 * @param   phStdIn                     Handle for the process' stdin pipe.
 * @param   phStdOut                    Handle for the process' stdout pipe.
 * @param   phStdErr                    Handle for the process' stderr pipe.
 * @param   pszAsUser                   User name (account) to start the process under.
 * @param   pszPassword                 Password of the specified user.
 * @param   phProcess                   Pointer which will receive the process handle after
 *                                      successful process start.
 */
static int gstcntlProcessCreateProcess(const char *pszExec, const char * const *papszArgs, RTENV hEnv, uint32_t fFlags,
                                       PCRTHANDLE phStdIn, PCRTHANDLE phStdOut, PCRTHANDLE phStdErr, const char *pszAsUser,
                                       const char *pszPassword, PRTPROCESS phProcess)
{
    AssertPtrReturn(pszExec, VERR_INVALID_PARAMETER);
    AssertPtrReturn(papszArgs, VERR_INVALID_PARAMETER);
    AssertPtrReturn(phProcess, VERR_INVALID_PARAMETER);

    int  rc = VINF_SUCCESS;
    char szExecExp[RTPATH_MAX];

    /* Do we need to expand environment variables in arguments? */
    bool fExpandArgs = (fFlags & EXECUTEPROCESSFLAG_EXPAND_ARGUMENTS) ? true  : false;

#ifdef RT_OS_WINDOWS
    /*
     * If sysprep should be executed do this in the context of VBoxService, which
     * (usually, if started by SCM) has administrator rights. Because of that a UI
     * won't be shown (doesn't have a desktop).
     */
    if (!RTStrICmp(pszExec, "sysprep"))
    {
        /* Use a predefined sysprep path as default. */
        char szSysprepCmd[RTPATH_MAX] = "C:\\sysprep\\sysprep.exe";

        /*
         * On Windows Vista (and up) sysprep is located in "system32\\sysprep\\sysprep.exe",
         * so detect the OS and use a different path.
         */
        OSVERSIONINFOEX OSInfoEx;
        RT_ZERO(OSInfoEx);
        OSInfoEx.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
        if (    GetVersionEx((LPOSVERSIONINFO) &OSInfoEx)
            &&  OSInfoEx.dwPlatformId == VER_PLATFORM_WIN32_NT
            &&  OSInfoEx.dwMajorVersion >= 6 /* Vista or later */)
        {
            rc = RTEnvGetEx(RTENV_DEFAULT, "windir", szSysprepCmd, sizeof(szSysprepCmd), NULL);
            if (RT_SUCCESS(rc))
                rc = RTPathAppend(szSysprepCmd, sizeof(szSysprepCmd), "system32\\sysprep\\sysprep.exe");
        }

        if (RT_SUCCESS(rc))
        {
            char **papszArgsExp;
            rc = gstcntlProcessAllocateArgv(szSysprepCmd /* argv0 */, papszArgs,
                                            fExpandArgs, &papszArgsExp);
            if (RT_SUCCESS(rc))
            {
                rc = RTProcCreateEx(szSysprepCmd, papszArgsExp, hEnv, 0 /* fFlags */,
                                    phStdIn, phStdOut, phStdErr, NULL /* pszAsUser */,
                                    NULL /* pszPassword */, phProcess);
                gstcntlProcessFreeArgv(papszArgsExp);
            }
        }

        if (RT_FAILURE(rc))
            VBoxServiceVerbose(3, "Starting sysprep returned rc=%Rrc\n", rc);

        return rc;
    }
#endif /* RT_OS_WINDOWS */

#ifdef VBOXSERVICE_TOOLBOX
    if (RTStrStr(pszExec, "vbox_") == pszExec)
    {
        /* We want to use the internal toolbox (all internal
         * tools are starting with "vbox_" (e.g. "vbox_cat"). */
        rc = gstcntlProcessResolveExecutable(VBOXSERVICE_NAME, szExecExp, sizeof(szExecExp));
    }
    else
    {
#endif
        /*
         * Do the environment variables expansion on executable and arguments.
         */
        rc = gstcntlProcessResolveExecutable(pszExec, szExecExp, sizeof(szExecExp));
#ifdef VBOXSERVICE_TOOLBOX
    }
#endif
    if (RT_SUCCESS(rc))
    {
        char **papszArgsExp;
        rc = gstcntlProcessAllocateArgv(pszExec /* Always use the unmodified executable name as argv0. */,
                                        papszArgs /* Append the rest of the argument vector (if any). */,
                                        fExpandArgs, &papszArgsExp);
        if (RT_FAILURE(rc))
        {
            /* Don't print any arguments -- may contain passwords or other sensible data! */
            VBoxServiceError("Could not prepare arguments, rc=%Rrc\n", rc);
        }
        else
        {
            uint32_t uProcFlags = 0;
            if (fFlags)
            {
                if (fFlags & EXECUTEPROCESSFLAG_HIDDEN)
                    uProcFlags |= RTPROC_FLAGS_HIDDEN;
                if (fFlags & EXECUTEPROCESSFLAG_NO_PROFILE)
                    uProcFlags |= RTPROC_FLAGS_NO_PROFILE;
            }

            /* If no user name specified run with current credentials (e.g.
             * full service/system rights). This is prohibited via official Main API!
             *
             * Otherwise use the RTPROC_FLAGS_SERVICE to use some special authentication
             * code (at least on Windows) for running processes as different users
             * started from our system service. */
            if (pszAsUser && *pszAsUser)
                uProcFlags |= RTPROC_FLAGS_SERVICE;
#ifdef DEBUG
            VBoxServiceVerbose(3, "Command: %s\n", szExecExp);
            for (size_t i = 0; papszArgsExp[i]; i++)
                VBoxServiceVerbose(3, "\targv[%ld]: %s\n", i, papszArgsExp[i]);
#endif
            VBoxServiceVerbose(3, "Starting process \"%s\" ...\n", szExecExp);

            /* Do normal execution. */
            rc = RTProcCreateEx(szExecExp, papszArgsExp, hEnv, uProcFlags,
                                phStdIn, phStdOut, phStdErr,
                                pszAsUser   && *pszAsUser   ? pszAsUser   : NULL,
                                pszPassword && *pszPassword ? pszPassword : NULL,
                                phProcess);

            VBoxServiceVerbose(3, "Starting process \"%s\" returned rc=%Rrc\n",
                               szExecExp, rc);

            gstcntlProcessFreeArgv(papszArgsExp);
        }
    }
    return rc;
}

/**
 * The actual worker routine (loop) for a started guest process.
 *
 * @return  IPRT status code.
 * @param   PVBOXSERVICECTRLPROCESS         Guest process.
 */
static int gstcntlProcessProcessWorker(PVBOXSERVICECTRLPROCESS pProcess)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    VBoxServiceVerbose(3, "Thread of process pThread=0x%p = \"%s\" started\n",
                       pProcess, pProcess->pszCmd);

    int rc = GstCntlSessionListSet(pProcess->pSession,
                                   pProcess, VBOXSERVICECTRLTHREADLIST_RUNNING);
    AssertRC(rc);

    rc = VbglR3GuestCtrlConnect(&pProcess->uClientID);
    if (RT_FAILURE(rc))
    {
        VBoxServiceError("Thread failed to connect to the guest control service, aborted! Error: %Rrc\n", rc);
        RTThreadUserSignal(RTThreadSelf());
        return rc;
    }
    VBoxServiceVerbose(3, "Guest process \"%s\" got client ID=%u, flags=0x%x\n",
                       pProcess->pszCmd, pProcess->uClientID, pProcess->uFlags);

    bool fSignalled = false; /* Indicator whether we signalled the thread user event already. */

    /*
     * Create the environment.
     */
    RTENV hEnv;
    rc = RTEnvClone(&hEnv, RTENV_DEFAULT);
    if (RT_SUCCESS(rc))
    {
        size_t i;
        for (i = 0; i < pProcess->uNumEnvVars && pProcess->papszEnv; i++)
        {
            rc = RTEnvPutEx(hEnv, pProcess->papszEnv[i]);
            if (RT_FAILURE(rc))
                break;
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Setup the redirection of the standard stuff.
             */
            /** @todo consider supporting: gcc stuff.c >file 2>&1.  */
            RTHANDLE    hStdIn;
            PRTHANDLE   phStdIn;
            rc = gstcntlProcessSetupPipe("|", 0 /*STDIN_FILENO*/,
                                         &hStdIn, &phStdIn, &pProcess->pipeStdInW);
            if (RT_SUCCESS(rc))
            {
                RTHANDLE    hStdOut;
                PRTHANDLE   phStdOut;
                RTPIPE      pipeStdOutR;
                rc = gstcntlProcessSetupPipe(  (pProcess->uFlags & EXECUTEPROCESSFLAG_WAIT_STDOUT)
                                             ? "|" : "/dev/null",
                                             1 /*STDOUT_FILENO*/,
                                             &hStdOut, &phStdOut, &pipeStdOutR);
                if (RT_SUCCESS(rc))
                {
                    RTHANDLE    hStdErr;
                    PRTHANDLE   phStdErr;
                    RTPIPE      pipeStdErrR;
                    rc = gstcntlProcessSetupPipe(  (pProcess->uFlags & EXECUTEPROCESSFLAG_WAIT_STDERR)
                                                 ? "|" : "/dev/null",
                                                 2 /*STDERR_FILENO*/,
                                                 &hStdErr, &phStdErr, &pipeStdErrR);
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * Create a poll set for the pipes and let the
                         * transport layer add stuff to it as well.
                         */
                        RTPOLLSET hPollSet;
                        rc = RTPollSetCreate(&hPollSet);
                        if (RT_SUCCESS(rc))
                        {
                            uint32_t uFlags = RTPOLL_EVT_ERROR;
#if 0
                            /* Add reading event to pollset to get some more information. */
                            uFlags |= RTPOLL_EVT_READ;
#endif
                            /* Stdin. */
                            if (RT_SUCCESS(rc))
                                rc = RTPollSetAddPipe(hPollSet, pProcess->pipeStdInW, RTPOLL_EVT_ERROR, VBOXSERVICECTRLPIPEID_STDIN);
                            /* Stdout. */
                            if (RT_SUCCESS(rc))
                                rc = RTPollSetAddPipe(hPollSet, pipeStdOutR, uFlags, VBOXSERVICECTRLPIPEID_STDOUT);
                            /* Stderr. */
                            if (RT_SUCCESS(rc))
                                rc = RTPollSetAddPipe(hPollSet, pipeStdErrR, uFlags, VBOXSERVICECTRLPIPEID_STDERR);
                            /* IPC notification pipe. */
                            if (RT_SUCCESS(rc))
                                rc = RTPipeCreate(&pProcess->hNotificationPipeR, &pProcess->hNotificationPipeW, 0 /* Flags */);
                            if (RT_SUCCESS(rc))
                                rc = RTPollSetAddPipe(hPollSet, pProcess->hNotificationPipeR, RTPOLL_EVT_READ, VBOXSERVICECTRLPIPEID_IPC_NOTIFY);

                            if (RT_SUCCESS(rc))
                            {
                                AssertPtr(pProcess->pSession);
                                bool fNeedsImpersonation = !(pProcess->pSession->uFlags & VBOXSERVICECTRLSESSION_FLAG_FORK);

                                RTPROCESS hProcess;
                                rc = gstcntlProcessCreateProcess(pProcess->pszCmd, pProcess->papszArgs, hEnv, pProcess->uFlags,
                                                                 phStdIn, phStdOut, phStdErr,
                                                                 fNeedsImpersonation ? pProcess->pszUser : NULL,
                                                                 fNeedsImpersonation ? pProcess->pszPassword : NULL,
                                                                 &hProcess);
                                if (RT_FAILURE(rc))
                                    VBoxServiceError("Error starting process, rc=%Rrc\n", rc);
                                /*
                                 * Tell the control thread that it can continue
                                 * spawning services. This needs to be done after the new
                                 * process has been started because otherwise signal handling
                                 * on (Open) Solaris does not work correctly (see @bugref{5068}).
                                 */
                                int rc2 = RTThreadUserSignal(RTThreadSelf());
                                if (RT_SUCCESS(rc))
                                    rc = rc2;
                                fSignalled = true;

                                if (RT_SUCCESS(rc))
                                {
                                    /*
                                     * Close the child ends of any pipes and redirected files.
                                     */
                                    rc2 = RTHandleClose(phStdIn);   AssertRC(rc2);
                                    phStdIn    = NULL;
                                    rc2 = RTHandleClose(phStdOut);  AssertRC(rc2);
                                    phStdOut   = NULL;
                                    rc2 = RTHandleClose(phStdErr);  AssertRC(rc2);
                                    phStdErr   = NULL;

                                    /* Enter the process loop. */
                                    rc = gstcntlProcessProcLoop(pProcess,
                                                                hProcess, pProcess->uTimeLimitMS, hPollSet,
                                                                &pProcess->pipeStdInW, &pipeStdOutR, &pipeStdErrR);

                                    /*
                                     * The handles that are no longer in the set have
                                     * been closed by the above call in order to prevent
                                     * the guest from getting stuck accessing them.
                                     * So, NIL the handles to avoid closing them again.
                                     */
                                    if (RT_FAILURE(RTPollSetQueryHandle(hPollSet, VBOXSERVICECTRLPIPEID_IPC_NOTIFY, NULL)))
                                    {
                                        pProcess->hNotificationPipeR = NIL_RTPIPE;
                                        pProcess->hNotificationPipeW = NIL_RTPIPE;
                                    }
                                    if (RT_FAILURE(RTPollSetQueryHandle(hPollSet, VBOXSERVICECTRLPIPEID_STDERR, NULL)))
                                        pipeStdErrR = NIL_RTPIPE;
                                    if (RT_FAILURE(RTPollSetQueryHandle(hPollSet, VBOXSERVICECTRLPIPEID_STDOUT, NULL)))
                                        pipeStdOutR = NIL_RTPIPE;
                                    if (RT_FAILURE(RTPollSetQueryHandle(hPollSet, VBOXSERVICECTRLPIPEID_STDIN, NULL)))
                                        pProcess->pipeStdInW = NIL_RTPIPE;
                                }
                            }
                            RTPollSetDestroy(hPollSet);

                            RTPipeClose(pProcess->hNotificationPipeR);
                            pProcess->hNotificationPipeR = NIL_RTPIPE;
                            RTPipeClose(pProcess->hNotificationPipeW);
                            pProcess->hNotificationPipeW = NIL_RTPIPE;
                        }
                        RTPipeClose(pipeStdErrR);
                        pipeStdErrR = NIL_RTPIPE;
                        RTHandleClose(phStdErr);
                        if (phStdErr)
                            RTHandleClose(phStdErr);
                    }
                    RTPipeClose(pipeStdOutR);
                    pipeStdOutR = NIL_RTPIPE;
                    RTHandleClose(&hStdOut);
                    if (phStdOut)
                        RTHandleClose(phStdOut);
                }
                RTPipeClose(pProcess->pipeStdInW);
                pProcess->pipeStdInW = NIL_RTPIPE;
                RTHandleClose(phStdIn);
            }
        }
        RTEnvDestroy(hEnv);
    }

    /* Move thread to stopped thread list. */
    int rc2 = GstCntlSessionListSet(pProcess->pSession,
                                    pProcess, VBOXSERVICECTRLTHREADLIST_STOPPED);
    AssertRC(rc2);

    if (pProcess->uClientID)
    {
        if (RT_FAILURE(rc))
        {
            VBGLR3GUESTCTRLCMDCTX ctx = { pProcess->uClientID, pProcess->uContextID };
            rc2 = VbglR3GuestCtrlProcCbStatus(&ctx,
                                              pProcess->uPID, PROC_STS_ERROR, rc,
                                              NULL /* pvData */, 0 /* cbData */);
            if (RT_FAILURE(rc2))
                VBoxServiceError("Could not report process failure error; rc=%Rrc (process error %Rrc)\n",
                                 rc2, rc);
        }

        /* Disconnect this client from the guest control service. This also cancels all
         * outstanding host requests. */
        VBoxServiceVerbose(3, "[PID %u]: Disconnecting (client ID=%u) ...\n",
                           pProcess->uPID, pProcess->uClientID);
        VbglR3GuestCtrlDisconnect(pProcess->uClientID);
        pProcess->uClientID = 0;
    }

    VBoxServiceVerbose(3, "[PID %u]: Thread of process \"%s\" ended with rc=%Rrc\n",
                       pProcess->uPID, pProcess->pszCmd, rc);

    /* Update started/stopped status. */
    ASMAtomicXchgBool(&pProcess->fStopped, true);
    ASMAtomicXchgBool(&pProcess->fStarted, false);

    /*
     * If something went wrong signal the user event so that others don't wait
     * forever on this thread.
     */
    if (RT_FAILURE(rc) && !fSignalled)
        RTThreadUserSignal(RTThreadSelf());

    return rc;
}


/**
 * Thread main routine for a started process.
 *
 * @return IPRT status code.
 * @param  RTTHREAD             Pointer to the thread's data.
 * @param  void*                User-supplied argument pointer.
 *
 */
static DECLCALLBACK(int) gstcntlProcessThread(RTTHREAD ThreadSelf, void *pvUser)
{
    PVBOXSERVICECTRLPROCESS pProcess = (VBOXSERVICECTRLPROCESS*)pvUser;
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    return gstcntlProcessProcessWorker(pProcess);
}


/**
 * Executes (starts) a process on the guest. This causes a new thread to be created
 * so that this function will not block the overall program execution.
 *
 * @return  IPRT status code.
 * @param   pSession                    Guest session.
 * @param   pStartupInfo                Startup info.
 * @param   uContextID                  Context ID to associate the process to start with.

 */
int GstCntlProcessStart(const PVBOXSERVICECTRLSESSION pSession,
                        const PVBOXSERVICECTRLPROCSTARTUPINFO pStartupInfo,
                        uint32_t uContextID)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertPtrReturn(pStartupInfo, VERR_INVALID_POINTER);

    /*
     * Allocate new thread data and assign it to our thread list.
     */
    PVBOXSERVICECTRLPROCESS pProcess = (PVBOXSERVICECTRLPROCESS)RTMemAlloc(sizeof(VBOXSERVICECTRLPROCESS));
    if (!pProcess)
        return VERR_NO_MEMORY;

    int rc = gstcntlProcessInit(pProcess, pSession, pStartupInfo, uContextID);
    if (RT_SUCCESS(rc))
    {
        static uint32_t s_uCtrlExecThread = 0;
        if (s_uCtrlExecThread++ == UINT32_MAX)
            s_uCtrlExecThread = 0; /* Wrap around to not let IPRT freak out. */
        rc = RTThreadCreateF(&pProcess->Thread, gstcntlProcessThread,
                             pProcess /*pvUser*/, 0 /*cbStack*/,
                             RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "gctl%u", s_uCtrlExecThread);
        if (RT_FAILURE(rc))
        {
            VBoxServiceError("Creating thread for guest process failed: rc=%Rrc, pProcess=%p\n",
                             rc, pProcess);
        }
        else
        {
            VBoxServiceVerbose(4, "Waiting for thread to initialize ...\n");

            /* Wait for the thread to initialize. */
            rc = RTThreadUserWait(pProcess->Thread, 60 * 1000 /* 60 seconds max. */);
            AssertRC(rc);
            if (   ASMAtomicReadBool(&pProcess->fShutdown)
                || RT_FAILURE(rc))
            {
                VBoxServiceError("Thread for process \"%s\" failed to start, rc=%Rrc\n",
                                 pStartupInfo->szCmd, rc);
            }
            else
            {
                ASMAtomicXchgBool(&pProcess->fStarted, true);
            }
        }
    }

    if (RT_FAILURE(rc))
        GstCntlProcessFree(pProcess);

    return rc;
}


/**
 * Performs a request to a specific (formerly started) guest process and waits
 * for its response.
 * Note: Caller is responsible of locking!
 *
 * @return  IPRT status code.
 * @param   pProcess            Guest process to perform operation on.
 * @param   pRequest            Pointer to request  to perform.
 */
int GstCntlProcessPerform(PVBOXSERVICECTRLPROCESS pProcess,
                          PVBOXSERVICECTRLREQUEST pRequest)
{
    AssertPtrReturn(pProcess, VERR_INVALID_POINTER);
    AssertPtrReturn(pRequest, VERR_INVALID_POINTER);
    AssertReturn(pRequest->enmType > VBOXSERVICECTRLREQUEST_UNKNOWN, VERR_INVALID_PARAMETER);
    /* Rest in pRequest is optional (based on the request type). */

    int rc = VINF_SUCCESS;

    if (ASMAtomicReadBool(&pProcess->fShutdown))
    {
        rc = VERR_CANCELLED;
    }
    else
    {
        /* Set request structure pointer. */
        pProcess->pRequest = pRequest;

        /** @todo To speed up simultaneous guest process handling we could add a worker threads
         *        or queue in order to wait for the request to happen. Later. */
        /* Wake up guest thrad by sending a wakeup byte to the notification pipe so
         * that RTPoll unblocks (returns) and we then can do our requested operation. */
        Assert(pProcess->hNotificationPipeW != NIL_RTPIPE);
        size_t cbWritten;
        if (RT_SUCCESS(rc))
            rc = RTPipeWrite(pProcess->hNotificationPipeW, "i", 1, &cbWritten);

        if (   RT_SUCCESS(rc)
            && cbWritten)
        {
            VBoxServiceVerbose(3, "[PID %u]: Waiting for response on enmType=%u, pvData=0x%p, cbData=%u\n",
                               pProcess->uPID, pRequest->enmType, pRequest->pvData, pRequest->cbData);

            rc = GstCntlProcessRequestWait(pRequest);
        }
    }

    VBoxServiceVerbose(3, "[PID %u]: Performed enmType=%u, uCID=%u, pvData=0x%p, cbData=%u, rc=%Rrc\n",
                       pProcess->uPID, pRequest->enmType, pRequest->uCID, pRequest->pvData, pRequest->cbData, rc);
    return rc;
}
