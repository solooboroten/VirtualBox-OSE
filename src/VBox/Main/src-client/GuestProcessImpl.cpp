
/* $Id: GuestProcessImpl.cpp 42897 2012-08-21 10:03:52Z vboxsync $ */
/** @file
 * VirtualBox Main - XXX.
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/**
 * Locking rules:
 * - When the main dispatcher (callbackDispatcher) is called it takes the
 *   WriteLock while dispatching to the various on* methods.
 * - All other outer functions (accessible by Main) must not own a lock
 *   while waiting for a callback or for an event.
 * - Only keep Read/WriteLocks as short as possible and only when necessary.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "GuestProcessImpl.h"
#include "GuestSessionImpl.h"
#include "GuestCtrlImplPrivate.h"
#include "ConsoleImpl.h"

#include "Global.h"
#include "AutoCaller.h"
#include "VMMDev.h"

#include <memory> /* For auto_ptr. */

#include <iprt/asm.h>
#include <iprt/getopt.h>
#include <VBox/VMMDev.h>
#include <VBox/com/array.h>

#ifdef LOG_GROUP
 #undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUEST_CONTROL
#include <VBox/log.h>


class GuestProcessTask
{
public:

    GuestProcessTask(GuestProcess *pProcess)
        : mProcess(pProcess),
          mRC(VINF_SUCCESS) { }

    virtual ~GuestProcessTask(void) { }

    int rc(void) const { return mRC; }
    bool isOk(void) const { return RT_SUCCESS(mRC); }
    const ComObjPtr<GuestProcess> &Process(void) const { return mProcess; }

protected:

    const ComObjPtr<GuestProcess>    mProcess;
    int                              mRC;
};

class GuestProcessStartTask : public GuestProcessTask
{
public:

    GuestProcessStartTask(GuestProcess *pProcess)
        : GuestProcessTask(pProcess) { }
};


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestProcess)

HRESULT GuestProcess::FinalConstruct(void)
{
    LogFlowThisFuncEnter();

    mData.mExitCode = 0;
    mData.mNextContextID = 0;
    mData.mPID = 0;
    mData.mProcessID = 0;
    mData.mRC = VINF_SUCCESS;
    mData.mStatus = ProcessStatus_Undefined;

    mData.mWaitCount = 0;
    mData.mWaitEvent = NULL;

    HRESULT hr = BaseFinalConstruct();
    return hr;
}

void GuestProcess::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

int GuestProcess::init(Console *aConsole, GuestSession *aSession, ULONG aProcessID, const GuestProcessStartupInfo &aProcInfo)
{
    LogFlowThisFunc(("aConsole=%p, aSession=%p, aProcessID=%RU32\n",
                     aConsole, aSession, aProcessID));

    AssertPtrReturn(aSession, VERR_INVALID_POINTER);

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), VERR_OBJECT_DESTROYED);

    mData.mConsole = aConsole;
    mData.mParent = aSession;
    mData.mProcessID = aProcessID;
    mData.mProcess = aProcInfo;
    /* Everything else will be set by the actual starting routine. */

    /* Confirm a successful initialization when it's the case. */
    autoInitSpan.setSucceeded();

    return VINF_SUCCESS;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestProcess::uninit(void)
{
    LogFlowThisFunc(("mCmd=%s, PID=%RU32\n",
                     mData.mProcess.mCommand.c_str(), mData.mPID));

    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    int vrc = VINF_SUCCESS;

#ifdef VBOX_WITH_GUEST_CONTROL
    /*
     * Cancel all callbacks + waiters.
     * Note: Deleting them is the job of the caller!
     */
    for (GuestCtrlCallbacks::iterator itCallbacks = mData.mCallbacks.begin();
         itCallbacks != mData.mCallbacks.end(); ++itCallbacks)
    {
        GuestCtrlCallback *pCallback = itCallbacks->second;
        AssertPtr(pCallback);
        int rc2 = pCallback->Cancel();
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }
    mData.mCallbacks.clear();

    if (mData.mWaitEvent)
    {
        int rc2 = mData.mWaitEvent->Cancel();
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }

    mData.mStatus = ProcessStatus_Down; /** @todo Correct? */
#endif

    LogFlowFuncLeaveRC(vrc);
}

// implementation of public getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP GuestProcess::COMGETTER(Arguments)(ComSafeArrayOut(BSTR, aArguments))
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutSafeArrayPointerValid(aArguments);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    com::SafeArray<BSTR> collection(mData.mProcess.mArguments.size());
    size_t s = 0;
    for (ProcessArguments::const_iterator it = mData.mProcess.mArguments.begin();
         it != mData.mProcess.mArguments.end();
         it++, s++)
    {
        Bstr tmp = *it;
        tmp.cloneTo(&collection[s]);
    }

    collection.detachTo(ComSafeArrayOutArg(aArguments));

    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::COMGETTER(Environment)(ComSafeArrayOut(BSTR, aEnvironment))
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutSafeArrayPointerValid(aEnvironment);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    com::SafeArray<BSTR> arguments(mData.mProcess.mEnvironment.Size());
    for (size_t i = 0; i < arguments.size(); i++)
    {
        Bstr tmp = mData.mProcess.mEnvironment.Get(i);
        tmp.cloneTo(&arguments[i]);
    }
    arguments.detachTo(ComSafeArrayOutArg(aEnvironment));

    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::COMGETTER(ExecutablePath)(BSTR *aExecutablePath)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aExecutablePath);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData.mProcess.mCommand.cloneTo(aExecutablePath);

    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::COMGETTER(ExitCode)(LONG *aExitCode)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aExitCode);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aExitCode = mData.mExitCode;

    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::COMGETTER(Name)(BSTR *aName)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aName);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData.mProcess.mName.cloneTo(aName);

    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::COMGETTER(PID)(ULONG *aPID)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aPID);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aPID = mData.mPID;

    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::COMGETTER(Status)(ProcessStatus_T *aStatus)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aStatus = mData.mStatus;

    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

// private methods
/////////////////////////////////////////////////////////////////////////////

inline int GuestProcess::callbackAdd(GuestCtrlCallback *pCallback, uint32_t *puContextID)
{
    const ComObjPtr<GuestSession> pSession(mData.mParent);
    Assert(!pSession.isNull());
    ULONG uSessionID = 0;
    HRESULT hr = pSession->COMGETTER(Id)(&uSessionID);
    ComAssertComRC(hr);

    /* Create a new context ID and assign it. */
    int vrc = VERR_NOT_FOUND;

    ULONG uCount = mData.mNextContextID++;
    ULONG uNewContextID = 0;
    ULONG uTries = 0;
    for (;;)
    {
        if (uCount == VBOX_GUESTCTRL_MAX_CONTEXTS)
            uCount = 0;

        /* Create a new context ID ... */
        uNewContextID = VBOX_GUESTCTRL_CONTEXTID_MAKE(uSessionID,
                                                      mData.mProcessID, uCount);

        /* Is the context ID already used?  Try next ID ... */
        if (!callbackExists(uCount))
        {
            /* Callback with context ID was not found. This means
             * we can use this context ID for our new callback we want
             * to add below. */
            vrc = VINF_SUCCESS;
            break;
        }

        uCount++;
        if (++uTries == UINT32_MAX)
            break; /* Don't try too hard. */
    }

    if (RT_SUCCESS(vrc))
    {
        /* Add callback with new context ID to our callback map.
         * Note: This is *not* uNewContextID (which also includes
         *       the session + process ID), just the context count
         *       will be used here. */
        mData.mCallbacks[uCount] = pCallback;
        Assert(mData.mCallbacks.size());

        /* Report back new context ID. */
        if (puContextID)
            *puContextID = uNewContextID;

        LogFlowThisFunc(("Added new callback (Session: %RU32, Process: %RU32, Count=%RU32) CID=%RU32\n",
                         uSessionID, mData.mProcessID, uCount, uNewContextID));
    }

    return vrc;
}

int GuestProcess::callbackDispatcher(uint32_t uContextID, uint32_t uFunction, void *pvData, size_t cbData)
{
#ifdef DEBUG
    LogFlowThisFunc(("uPID=%RU32, uContextID=%RU32, uFunction=%RU32, pvData=%p, cbData=%RU32\n",
                     mData.mPID, uContextID, uFunction, pvData, cbData));
#endif

    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc;

    /* Get the optional callback associated to this context ID.
     * The callback may not be around anymore if just kept locally by the caller when
     * doing the actual HGCM sending stuff. */
    GuestCtrlCallback *pCallback = NULL;
    GuestCtrlCallbacks::const_iterator it
        = mData.mCallbacks.find(VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(uContextID));
    if (it != mData.mCallbacks.end())
    {
        pCallback = it->second;
        AssertPtr(pCallback);
#ifdef DEBUG
        LogFlowThisFunc(("pCallback=%p\n", pCallback));
#endif
    }

    switch (uFunction)
    {
        case GUEST_DISCONNECTED:
        {
            PCALLBACKDATACLIENTDISCONNECTED pCallbackData = reinterpret_cast<PCALLBACKDATACLIENTDISCONNECTED>(pvData);
            AssertPtr(pCallbackData);
            AssertReturn(sizeof(CALLBACKDATACLIENTDISCONNECTED) == cbData, VERR_INVALID_PARAMETER);
            AssertReturn(CALLBACKDATAMAGIC_CLIENT_DISCONNECTED == pCallbackData->hdr.u32Magic, VERR_INVALID_PARAMETER);

            vrc = onGuestDisconnected(pCallback, pCallbackData); /* Affects all callbacks. */
            break;
        }

        case GUEST_EXEC_SEND_STATUS:
        {
            PCALLBACKDATAEXECSTATUS pCallbackData = reinterpret_cast<PCALLBACKDATAEXECSTATUS>(pvData);
            AssertPtr(pCallbackData);
            AssertReturn(sizeof(CALLBACKDATAEXECSTATUS) == cbData, VERR_INVALID_PARAMETER);
            AssertReturn(CALLBACKDATAMAGIC_EXEC_STATUS == pCallbackData->hdr.u32Magic, VERR_INVALID_PARAMETER);

            vrc = onProcessStatusChange(pCallback, pCallbackData);
            break;
        }

        case GUEST_EXEC_SEND_OUTPUT:
        {
            PCALLBACKDATAEXECOUT pCallbackData = reinterpret_cast<PCALLBACKDATAEXECOUT>(pvData);
            AssertPtr(pCallbackData);
            AssertReturn(sizeof(CALLBACKDATAEXECOUT) == cbData, VERR_INVALID_PARAMETER);
            AssertReturn(CALLBACKDATAMAGIC_EXEC_OUT == pCallbackData->hdr.u32Magic, VERR_INVALID_PARAMETER);

            vrc = onProcessOutput(pCallback, pCallbackData);
            break;
        }

        case GUEST_EXEC_SEND_INPUT_STATUS:
        {
            PCALLBACKDATAEXECINSTATUS pCallbackData = reinterpret_cast<PCALLBACKDATAEXECINSTATUS>(pvData);
            AssertPtr(pCallbackData);
            AssertReturn(sizeof(CALLBACKDATAEXECINSTATUS) == cbData, VERR_INVALID_PARAMETER);
            AssertReturn(CALLBACKDATAMAGIC_EXEC_IN_STATUS == pCallbackData->hdr.u32Magic, VERR_INVALID_PARAMETER);

            vrc = onProcessInputStatus(pCallback, pCallbackData);
            break;
        }

        default:
            /* Silently ignore not implemented functions. */
            vrc = VERR_NOT_IMPLEMENTED;
            break;
    }

#ifdef DEBUG
    LogFlowFuncLeaveRC(vrc);
#endif
    return vrc;
}

inline bool GuestProcess::callbackExists(uint32_t uContextID)
{
    GuestCtrlCallbacks::const_iterator it =
        mData.mCallbacks.find(VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(uContextID));
    return (it == mData.mCallbacks.end()) ? false : true;
}

inline int GuestProcess::callbackRemove(uint32_t uContextID)
{
    GuestCtrlCallbacks::iterator it =
        mData.mCallbacks.find(VBOX_GUESTCTRL_CONTEXTID_GET_COUNT(uContextID));
    if (it != mData.mCallbacks.end())
    {
        delete it->second;
        mData.mCallbacks.erase(it);

        return VINF_SUCCESS;
    }

    return VERR_NOT_FOUND;
}

/**
 * Checks if the current assigned PID matches another PID (from a callback).
 *
 * In protocol v1 we don't have the possibility to terminate/kill
 * processes so it can happen that a formerly start process A
 * (which has the context ID 0 (session=0, process=0, count=0) will
 * send a delayed message to the host if this process has already
 * been discarded there and the same context ID was reused by
 * a process B. Process B in turn then has a different guest PID.
 *
 * @return  IPRT status code.
 * @param   uPID                    PID to check.
 */
inline int GuestProcess::checkPID(uint32_t uPID)
{
    /* Was there a PID assigned yet? */
    if (mData.mPID)
    {
        /*

         */
        if (mData.mParent->getProtocolVersion() < 2)
        {
            /* Simply ignore the stale requests. */
            return (mData.mPID == uPID)
                   ? VINF_SUCCESS : VERR_NOT_FOUND;
        }
        /* This should never happen! */
        AssertReleaseMsg(mData.mPID == uPID, ("Unterminated guest process (PID %RU32) sent data to a newly started process (PID %RU32)\n",
                                              uPID, mData.mPID));
    }

    return VINF_SUCCESS;
}

inline bool GuestProcess::isAlive(void)
{
    return (   mData.mStatus == ProcessStatus_Started
            || mData.mStatus == ProcessStatus_Paused
            || mData.mStatus == ProcessStatus_Terminating);
}

bool GuestProcess::isReady(void)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData.mStatus == ProcessStatus_Started)
    {
        Assert(mData.mPID); /* PID must not be 0. */
        return true;
    }

    return false;
}

int GuestProcess::onGuestDisconnected(GuestCtrlCallback *pCallback, PCALLBACKDATACLIENTDISCONNECTED pData)
{
    /* pCallback is optional. */
    AssertPtrReturn(pData, VERR_INVALID_POINTER);

    LogFlowThisFunc(("uPID=%RU32, pCallback=%p, pData=%p\n", mData.mPID, pCallback, pData));

    mData.mStatus = ProcessStatus_Down;

    /* First, signal callback in every case. */
    if (pCallback)
        pCallback->Signal();

    /* Do we need to report a termination? */
    ProcessWaitResult_T waitRes;
    if (mData.mProcess.mFlags & ProcessCreateFlag_IgnoreOrphanedProcesses)
        waitRes = ProcessWaitResult_Status; /* No, just report a status. */
    else
        waitRes = ProcessWaitResult_Terminate;

    /* Signal in any case. */
    int vrc = signalWaiters(waitRes);
    AssertRC(vrc);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::onProcessInputStatus(GuestCtrlCallback *pCallback, PCALLBACKDATAEXECINSTATUS pData)
{
    /* pCallback is optional. */
    AssertPtrReturn(pData, VERR_INVALID_POINTER);

    LogFlowThisFunc(("uPID=%RU32, uStatus=%RU32, uFlags=%RU32, cbProcessed=%RU32, pCallback=%p, pData=%p\n",
                     mData.mPID, pData->u32Status, pData->u32Flags, pData->cbProcessed, pCallback, pData));

    int vrc = checkPID(pData->u32PID);
    if (RT_FAILURE(vrc))
        return vrc;

    /* First, signal callback in every case (if available). */
    if (pCallback)
    {
        vrc = pCallback->SetData(pData, sizeof(CALLBACKDATAEXECINSTATUS));

        int rc2 = pCallback->Signal();
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }

    /* Then do the WaitFor signalling stuff. */
    uint32_t uWaitFlags = mData.mWaitEvent
                        ? mData.mWaitEvent->GetWaitFlags() : 0;
    if (uWaitFlags & ProcessWaitForFlag_StdIn)
    {
        int rc2 = signalWaiters(ProcessWaitResult_StdIn);
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::onProcessNotifyIO(GuestCtrlCallback *pCallback, PCALLBACKDATAEXECSTATUS pData)
{
    /* pCallback is optional. */
    AssertPtrReturn(pData, VERR_INVALID_POINTER);

    return 0;
}

int GuestProcess::onProcessStatusChange(GuestCtrlCallback *pCallback, PCALLBACKDATAEXECSTATUS pData)
{
    /* pCallback is optional. */
    AssertPtrReturn(pData, VERR_INVALID_POINTER);

    LogFlowThisFunc(("uPID=%RU32, uStatus=%RU32, uFlags=%RU32, pCallback=%p, pData=%p\n",
                     pData->u32PID, pData->u32Status, pData->u32Flags, pCallback, pData));

    int vrc = checkPID(pData->u32PID);
    if (RT_FAILURE(vrc))
        return vrc;

    BOOL fSignal = FALSE;
    ProcessWaitResult_T waitRes;
    uint32_t uWaitFlags = mData.mWaitEvent
                        ? mData.mWaitEvent->GetWaitFlags() : 0;
    switch (pData->u32Status)
    {
       case PROC_STS_STARTED:
        {
            fSignal = (uWaitFlags & ProcessWaitForFlag_Start);
            waitRes = ProcessWaitResult_Start;

            mData.mStatus = ProcessStatus_Started;
            mData.mPID = pData->u32PID;
            break;
        }

        case PROC_STS_TEN:
        {
            fSignal = TRUE; /* Signal in any case. */
            waitRes = ProcessWaitResult_Terminate;

            mData.mStatus = ProcessStatus_TerminatedNormally;
            mData.mExitCode = pData->u32Flags; /* Contains the exit code. */
            break;
        }

        case PROC_STS_TES:
        {
            fSignal = TRUE; /* Signal in any case. */
            waitRes = ProcessWaitResult_Terminate;

            mData.mStatus = ProcessStatus_TerminatedSignal;
            mData.mExitCode = pData->u32Flags; /* Contains the signal. */
            break;
        }

        case PROC_STS_TEA:
        {
            fSignal = TRUE; /* Signal in any case. */
            waitRes = ProcessWaitResult_Terminate;

            mData.mStatus = ProcessStatus_TerminatedAbnormally;
            break;
        }

        case PROC_STS_TOK:
        {
            fSignal = TRUE; /* Signal in any case. */
            waitRes = ProcessWaitResult_Timeout;

            mData.mStatus = ProcessStatus_TimedOutKilled;
            break;
        }

        case PROC_STS_TOA:
        {
            fSignal = TRUE; /* Signal in any case. */
            waitRes = ProcessWaitResult_Timeout;

            mData.mStatus = ProcessStatus_TimedOutAbnormally;
            break;
        }

        case PROC_STS_DWN:
        {
            fSignal = TRUE; /* Signal in any case. */
            /* Do we need to report termination? */
            waitRes = (mData.mProcess.mFlags & ProcessCreateFlag_IgnoreOrphanedProcesses)
                    ? ProcessWaitResult_Status : ProcessWaitResult_Terminate;

            mData.mStatus = ProcessStatus_Down;
            break;
        }

        case PROC_STS_ERROR:
        {
            fSignal = TRUE; /* Signal in any case. */
            waitRes = ProcessWaitResult_Error;

            mData.mStatus = ProcessStatus_Error;

            Utf8Str strError = Utf8StrFmt(tr("Guest process \"%s\" could not be started: "), mData.mProcess.mCommand.c_str());

            /* Note: It's not required that the process has been started before. */
            if (mData.mPID)
            {
                strError += Utf8StrFmt(tr("Error vrc=%Rrc occured (PID %RU32)"), vrc, mData.mPID);
            }
            else
            {
                /** @todo pData->u32Flags: int vs. uint32 -- IPRT errors are *negative* !!! */
                switch (pData->u32Flags) /* pData->u32Flags contains the IPRT error code from guest side. */
                {
                    case VERR_FILE_NOT_FOUND: /* This is the most likely error. */
                        strError += Utf8StrFmt(tr("The specified file was not found on guest"));
                        break;

                    case VERR_PATH_NOT_FOUND:
                        strError += Utf8StrFmt(tr("Could not resolve path to specified file was not found on guest"));
                        break;

                    case VERR_BAD_EXE_FORMAT:
                        strError += Utf8StrFmt(tr("The specified file is not an executable format on guest"));
                        break;

                    case VERR_AUTHENTICATION_FAILURE:
                        strError += Utf8StrFmt(tr("The specified user was not able to logon on guest"));
                        break;

                    case VERR_INVALID_NAME:
                        strError += Utf8StrFmt(tr("The specified file is an invalid name"));
                        break;

                    case VERR_TIMEOUT:
                        strError += Utf8StrFmt(tr("The guest did not respond within time"));
                        break;

                    case VERR_CANCELLED:
                        strError += Utf8StrFmt(tr("The execution operation was canceled"));
                        break;

                    case VERR_PERMISSION_DENIED:
                        strError += Utf8StrFmt(tr("Invalid user/password credentials"));
                        break;

                    case VERR_MAX_PROCS_REACHED:
                        strError += Utf8StrFmt(tr("Maximum number of parallel guest processes has been reached"));
                        break;

                    default:
                        strError += Utf8StrFmt(tr("Reported error %Rrc"), pData->u32Flags);
                        break;
                }
            }

            vrc = setErrorInternal(pData->u32Flags, strError);
            AssertRC(vrc);
            break;
        }

        case PROC_STS_UNDEFINED:
        default:
        {
            /* Silently skip this request. */
            fSignal = TRUE; /* Signal in any case. */
            waitRes = ProcessWaitResult_Status;

            mData.mStatus = ProcessStatus_Undefined;
            break;
        }
    }

    LogFlowThisFunc(("Got vrc=%Rrc, waitResult=%d\n", vrc, waitRes));

    /*
     * Now do the signalling stuff.
     */
    if (pCallback)
        vrc = pCallback->Signal();

    if (fSignal)
    {
        int rc2 = signalWaiters(waitRes);
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::onProcessOutput(GuestCtrlCallback *pCallback, PCALLBACKDATAEXECOUT pData)
{
    /* pCallback is optional. */
    AssertPtrReturn(pData, VERR_INVALID_POINTER);

    LogFlowThisFunc(("uPID=%RU32, uHandle=%RU32, uFlags=%RU32, pvData=%p, cbData=%RU32, pCallback=%p, pData=%p\n",
                     mData.mPID, pData->u32HandleId, pData->u32Flags, pData->pvData, pData->cbData, pCallback, pData));

    int vrc = checkPID(pData->u32PID);
    if (RT_FAILURE(vrc))
        return vrc;

    /* First, signal callback in every case (if available). */
    if (pCallback)
    {
        vrc = pCallback->SetData(pData, sizeof(CALLBACKDATAEXECOUT));

        int rc2 = pCallback->Signal();
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }

    /* Then do the WaitFor signalling stuff. */
    BOOL fSignal = FALSE;
    uint32_t uWaitFlags = mData.mWaitEvent
                        ? mData.mWaitEvent->GetWaitFlags() : 0;

    if (    (uWaitFlags & ProcessWaitForFlag_StdOut)
         || (uWaitFlags & ProcessWaitForFlag_StdErr))
    {
        fSignal = TRUE;
    }
    else if (   (uWaitFlags & ProcessWaitForFlag_StdOut)
             && (pData->u32HandleId == OUTPUT_HANDLE_ID_STDOUT))
    {
        fSignal = TRUE;
    }
    else if (   (uWaitFlags & ProcessWaitForFlag_StdErr)
             && (pData->u32HandleId == OUTPUT_HANDLE_ID_STDERR))
    {
        fSignal = TRUE;
    }

    if (fSignal)
    {
        int rc2 = signalWaiters(  pData->u32HandleId == OUTPUT_HANDLE_ID_STDOUT
                                ? ProcessWaitResult_StdOut : ProcessWaitResult_StdErr);
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::readData(uint32_t uHandle, uint32_t uSize, uint32_t uTimeoutMS,
                           void *pvData, size_t cbData, size_t *pcbRead)
{
    LogFlowThisFunc(("uPID=%RU32, uHandle=%RU32, uSize=%RU32, uTimeoutMS=%RU32, pvData=%p, cbData=%RU32\n",
                     mData.mPID, uHandle, uSize, uTimeoutMS, pvData, cbData));
    AssertReturn(uSize, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData >= uSize, VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData.mStatus != ProcessStatus_Started)
    {
        if (pcbRead)
            *pcbRead = 0;
        return VINF_SUCCESS; /* Nothing to read anymore. */
    }

    uint32_t uContextID = 0;
    GuestCtrlCallback *pCallbackRead = new GuestCtrlCallback();
    if (!pCallbackRead)
        return VERR_NO_MEMORY;

    /* Create callback and add it to the map. */
    int vrc = pCallbackRead->Init(VBOXGUESTCTRLCALLBACKTYPE_EXEC_OUTPUT);
    if (RT_SUCCESS(vrc))
        vrc = callbackAdd(pCallbackRead, &uContextID);

    alock.release(); /* Drop the write lock again. */

    if (RT_SUCCESS(vrc))
    {
        VBOXHGCMSVCPARM paParms[5];

        int i = 0;
        paParms[i++].setUInt32(uContextID);
        paParms[i++].setUInt32(mData.mPID);
        paParms[i++].setUInt32(uHandle);
        paParms[i++].setUInt32(0 /* Flags, none set yet. */);

        vrc = sendCommand(HOST_EXEC_GET_OUTPUT, i, paParms);
    }

    if (RT_SUCCESS(vrc))
    {
        /*
         * Let's wait for the process being started.
         * Note: Be sure not keeping a AutoRead/WriteLock here.
         */
        LogFlowThisFunc(("Waiting for callback (%RU32ms) ...\n", uTimeoutMS));
        vrc = pCallbackRead->Wait(uTimeoutMS);
        if (RT_SUCCESS(vrc)) /* Wait was successful, check for supplied information. */
        {
            vrc = pCallbackRead->GetResultCode();
            LogFlowThisFunc(("Callback returned vrc=%Rrc, cbData=%RU32\n", vrc, pCallbackRead->GetDataSize()));

            if (RT_SUCCESS(vrc))
            {
                Assert(pCallbackRead->GetDataSize() == sizeof(CALLBACKDATAEXECOUT));
                PCALLBACKDATAEXECOUT pData = (PCALLBACKDATAEXECOUT)pCallbackRead->GetDataRaw();
                AssertPtr(pData);

                size_t cbRead = pData->cbData;
                if (cbRead)
                {
                    Assert(cbData >= cbRead);
                    memcpy(pvData, pData->pvData, cbRead);
                }

                LogFlowThisFunc(("cbRead=%RU32\n", cbRead));

                if (pcbRead)
                    *pcbRead = cbRead;
            }
        }
        else
            vrc = VERR_TIMEOUT;
    }

    alock.acquire();

    AssertPtr(pCallbackRead);
    int rc2 = callbackRemove(uContextID);
    if (RT_SUCCESS(vrc))
        vrc = rc2;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::sendCommand(uint32_t uFunction,
                              uint32_t uParms, PVBOXHGCMSVCPARM paParms)
{
    LogFlowThisFuncEnter();

    ComObjPtr<Console> pConsole = mData.mConsole;
    Assert(!pConsole.isNull());

    /* Forward the information to the VMM device. */
    VMMDev *pVMMDev = pConsole->getVMMDev();
    AssertPtr(pVMMDev);

    LogFlowThisFunc(("uFunction=%RU32, uParms=%RU32\n", uFunction, uParms));
    int vrc = pVMMDev->hgcmHostCall("VBoxGuestControlSvc", uFunction, uParms, paParms);
    if (RT_FAILURE(vrc))
    {
        int rc2;
        if (vrc == VERR_INVALID_VM_HANDLE)
            rc2 = setErrorInternal(vrc, tr("VMM device is not available (is the VM running?)"));
        else if (vrc == VERR_NOT_FOUND)
            rc2 = setErrorInternal(vrc, tr("The guest execution service is not ready (yet)"));
        else if (vrc == VERR_HGCM_SERVICE_NOT_FOUND)
            rc2 = setErrorInternal(vrc, tr("The guest execution service is not available"));
        else
            rc2 = setErrorInternal(vrc, Utf8StrFmt(tr("The HGCM call failed with error %Rrc"), vrc));
        AssertRC(rc2);
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/* Does not do locking; caller is responsible for that! */
int GuestProcess::setErrorInternal(int vrc, const Utf8Str &strMessage)
{
    LogFlowThisFunc(("vrc=%Rrc, strMsg=%s\n", vrc, strMessage.c_str()));

    Assert(RT_FAILURE(vrc));
    Assert(!strMessage.isEmpty());

#ifdef DEBUG
    /* Do not allow overwriting an already set error. If this happens
     * this means we forgot some error checking/locking somewhere. */
    Assert(RT_SUCCESS(mData.mRC));
    Assert(mData.mErrorMsg.isEmpty());
#endif

    mData.mStatus = ProcessStatus_Error;
    mData.mRC = vrc;
    mData.mErrorMsg = strMessage;

    int rc2 = signalWaiters(ProcessWaitResult_Error);
    LogFlowFuncLeaveRC(rc2);
    return rc2;
}

HRESULT GuestProcess::setErrorExternal(void)
{
    return RT_SUCCESS(mData.mRC)
           ? S_OK : setError(VBOX_E_IPRT_ERROR, "%s", mData.mErrorMsg.c_str());
}

int GuestProcess::signalWaiters(ProcessWaitResult_T enmWaitResult)
{
    LogFlowThisFunc(("enmWaitResult=%d, mWaitCount=%RU32, mWaitEvent=%p\n",
                 enmWaitResult, mData.mWaitCount, mData.mWaitEvent));

    /* Note: No write locking here -- already done in the caller. */

    int vrc = VINF_SUCCESS;
    if (mData.mWaitEvent)
        vrc = mData.mWaitEvent->Signal(enmWaitResult);
    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::startProcess(void)
{
    LogFlowThisFunc(("aCmd=%s, aTimeoutMS=%RU32, fFlags=%x\n",
                     mData.mProcess.mCommand.c_str(), mData.mProcess.mTimeoutMS, mData.mProcess.mFlags));

    /* Wait until the caller function (if kicked off by a thread)
     * has returned and continue operation. */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc;
    uint32_t uContextID = 0;
    GuestCtrlCallback *pCallbackStart = new GuestCtrlCallback();
    if (!pCallbackStart)
        return VERR_NO_MEMORY;

    mData.mStatus = ProcessStatus_Starting;

    /* Create callback and add it to the map. */
    vrc = pCallbackStart->Init(VBOXGUESTCTRLCALLBACKTYPE_EXEC_START);
    if (RT_SUCCESS(vrc))
        vrc = callbackAdd(pCallbackStart, &uContextID);

    if (RT_SUCCESS(vrc))
    {
        GuestSession *pSession = mData.mParent;
        AssertPtr(pSession);

        const GuestCredentials &sessionCreds = pSession->getCredentials();

        /* Prepare arguments. */
        char *pszArgs = NULL;
        size_t cArgs = mData.mProcess.mArguments.size();
        if (cArgs)
        {
            char **papszArgv = (char**)RTMemAlloc(sizeof(char*) * (cArgs + 1));
            AssertReturn(papszArgv, VERR_NO_MEMORY);
            for (size_t i = 0; RT_SUCCESS(vrc) && i < cArgs; i++)
                vrc = RTStrDupEx(&papszArgv[i], mData.mProcess.mArguments[i].c_str());
            papszArgv[cArgs] = NULL;

            if (RT_SUCCESS(vrc))
                vrc = RTGetOptArgvToString(&pszArgs, papszArgv, RTGETOPTARGV_CNV_QUOTE_MS_CRT);
        }
        size_t cbArgs = pszArgs ? strlen(pszArgs) + 1 : 0; /* Include terminating zero. */

        /* Prepare environment. */
        void *pvEnv = NULL;
        size_t cbEnv = 0;
        vrc = mData.mProcess.mEnvironment.BuildEnvironmentBlock(&pvEnv, &cbEnv, NULL /* cEnv */);

        if (RT_SUCCESS(vrc))
        {
            /* Prepare HGCM call. */
            VBOXHGCMSVCPARM paParms[15];
            int i = 0;
            paParms[i++].setUInt32(uContextID);
            paParms[i++].setPointer((void*)mData.mProcess.mCommand.c_str(),
                                    (ULONG)mData.mProcess.mCommand.length() + 1);
            paParms[i++].setUInt32(mData.mProcess.mFlags);
            paParms[i++].setUInt32(mData.mProcess.mArguments.size());
            paParms[i++].setPointer((void*)pszArgs, cbArgs);
            paParms[i++].setUInt32(mData.mProcess.mEnvironment.Size());
            paParms[i++].setUInt32(cbEnv);
            paParms[i++].setPointer((void*)pvEnv, cbEnv);
            paParms[i++].setPointer((void*)sessionCreds.mUser.c_str(), (ULONG)sessionCreds.mUser.length() + 1);
            paParms[i++].setPointer((void*)sessionCreds.mPassword.c_str(), (ULONG)sessionCreds.mPassword.length() + 1);
            /** @todo New command needs the domain as well! */

            /*
             * If the WaitForProcessStartOnly flag is set, we only want to define and wait for a timeout
             * until the process was started - the process itself then gets an infinite timeout for execution.
             * This is handy when we want to start a process inside a worker thread within a certain timeout
             * but let the started process perform lengthly operations then.
             */
            if (mData.mProcess.mFlags & ProcessCreateFlag_WaitForProcessStartOnly)
                paParms[i++].setUInt32(UINT32_MAX /* Infinite timeout */);
            else
                paParms[i++].setUInt32(mData.mProcess.mTimeoutMS);

            /* Note: Don't hold the write lock in here, because setErrorInternal */
            vrc = sendCommand(HOST_EXEC_CMD, i, paParms);
        }

        GuestEnvironment::FreeEnvironmentBlock(pvEnv);
        if (pszArgs)
            RTStrFree(pszArgs);

        uint32_t uTimeoutMS = mData.mProcess.mTimeoutMS;

        /* Drop the write lock again before waiting. */
        alock.release();

        if (RT_SUCCESS(vrc))
        {
            /*
             * Let's wait for the process being started.
             * Note: Be sure not keeping a AutoRead/WriteLock here.
             */
            LogFlowThisFunc(("Waiting for callback (%RU32ms) ...\n", uTimeoutMS));
            vrc = pCallbackStart->Wait(uTimeoutMS);
            if (RT_SUCCESS(vrc)) /* Wait was successful, check for supplied information. */
            {
                vrc = pCallbackStart->GetResultCode();
                LogFlowThisFunc(("Callback returned vrc=%Rrc\n", vrc));
            }
            else
                vrc = VERR_TIMEOUT;
        }

        AutoWriteLock awlock(this COMMA_LOCKVAL_SRC_POS);

        AssertPtr(pCallbackStart);
        int rc2 = callbackRemove(uContextID);
        if (RT_SUCCESS(vrc))
            vrc = rc2;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::startProcessAsync(void)
{
    LogFlowThisFuncEnter();

    int vrc;

    try
    {
        /* Asynchronously start the process on the guest by kicking off a
         * worker thread. */
        std::auto_ptr<GuestProcessStartTask> pTask(new GuestProcessStartTask(this));
        AssertReturn(pTask->isOk(), pTask->rc());

        vrc = RTThreadCreate(NULL, GuestProcess::startProcessThread,
                             (void *)pTask.get(), 0,
                             RTTHREADTYPE_MAIN_WORKER, 0,
                             "gctlPrcStart");
        if (RT_SUCCESS(vrc))
        {
            /* pTask is now owned by startProcessThread(), so release it. */
            pTask.release();
        }
    }
    catch(std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/* static */
DECLCALLBACK(int) GuestProcess::startProcessThread(RTTHREAD Thread, void *pvUser)
{
    LogFlowFunc(("pvUser=%p\n", pvUser));

    std::auto_ptr<GuestProcessStartTask> pTask(static_cast<GuestProcessStartTask*>(pvUser));
    AssertPtr(pTask.get());

    const ComObjPtr<GuestProcess> pProcess(pTask->Process());
    Assert(!pProcess.isNull());

    AutoCaller autoCaller(pProcess);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    int vrc = pProcess->startProcess();
    if (RT_FAILURE(vrc))
    {
        /** @todo What now? */
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::terminateProcess(void)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData.mParent->getProtocolVersion() < 2)
        return VERR_NOT_SUPPORTED;

    LogFlowThisFuncLeave();
    return VERR_NOT_IMPLEMENTED;
}

int GuestProcess::waitFor(uint32_t fWaitFlags, ULONG uTimeoutMS, GuestProcessWaitResult &waitRes)
{
    LogFlowThisFuncEnter();

    AssertReturn(fWaitFlags, VERR_INVALID_PARAMETER);

    LogFlowThisFunc(("fWaitFlags=%x, uTimeoutMS=%RU32, mStatus=%RU32, mWaitCount=%RU32, mWaitEvent=%p\n",
                     fWaitFlags, uTimeoutMS, mData.mStatus, mData.mWaitCount, mData.mWaitEvent));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    ProcessStatus_T curStatus = mData.mStatus;

    /* Did some error occur before? Then skip waiting and return. */
    if (curStatus == ProcessStatus_Error)
    {
        waitRes.mResult = ProcessWaitResult_Error;
        return VINF_SUCCESS;
    }

    waitRes.mResult = ProcessWaitResult_None;
    waitRes.mRC = VINF_SUCCESS;

    if (   (fWaitFlags & ProcessWaitForFlag_Terminate)
        || (fWaitFlags & ProcessWaitForFlag_StdIn)
        || (fWaitFlags & ProcessWaitForFlag_StdOut)
        || (fWaitFlags & ProcessWaitForFlag_StdErr))
    {
        switch (mData.mStatus)
        {
            case ProcessStatus_TerminatedNormally:
            case ProcessStatus_TerminatedSignal:
            case ProcessStatus_TerminatedAbnormally:
            case ProcessStatus_Down:
                waitRes.mResult = ProcessWaitResult_Terminate;
                waitRes.mRC = mData.mRC;
                break;

            case ProcessStatus_TimedOutKilled:
            case ProcessStatus_TimedOutAbnormally:
                waitRes.mResult = ProcessWaitResult_Timeout;
                waitRes.mRC = mData.mRC;
                break;

            case ProcessStatus_Error:
                waitRes.mResult = ProcessWaitResult_Error;
                waitRes.mRC = mData.mRC;
                break;

            case ProcessStatus_Started:
            {
                /* Filter out waits which are *not* supported using
                 * older guest control Guest Additions. */
                if (mData.mParent->getProtocolVersion() < 2)
                {
                    /* We don't support waiting for stdin, out + err,
                     * just skip waiting then. */
                    if (   (fWaitFlags & ProcessWaitForFlag_StdIn)
                        || (fWaitFlags & ProcessWaitForFlag_StdOut)
                        || (fWaitFlags & ProcessWaitForFlag_StdErr))
                    {
                        /* Use _WaitFlagNotSupported because we don't know what to tell the caller. */
                        waitRes.mResult = ProcessWaitResult_WaitFlagNotSupported;
                    }
                }

                break;
            }

            case ProcessStatus_Undefined:
            case ProcessStatus_Starting:
                /* Do the waiting below. */
                break;

            default:
                AssertMsgFailed(("Unhandled process status %ld\n", mData.mStatus));
                return VERR_NOT_IMPLEMENTED;
        }
    }
    else if (fWaitFlags & ProcessWaitForFlag_Start)
    {
        switch (mData.mStatus)
        {
            case ProcessStatus_Started:
            case ProcessStatus_Paused:
            case ProcessStatus_Terminating:
            case ProcessStatus_TerminatedNormally:
            case ProcessStatus_TerminatedSignal:
            case ProcessStatus_TerminatedAbnormally:
            case ProcessStatus_Down:
                waitRes.mResult = ProcessWaitResult_Start;
                break;

            case ProcessStatus_Error:
                waitRes.mResult = ProcessWaitResult_Error;
                waitRes.mRC = mData.mRC;
                break;

            case ProcessStatus_TimedOutKilled:
            case ProcessStatus_TimedOutAbnormally:
                waitRes.mResult = ProcessWaitResult_Timeout;
                waitRes.mRC = mData.mRC;
                break;

            case ProcessStatus_Undefined:
            case ProcessStatus_Starting:
                /* Do the waiting below. */
                break;

            default:
                AssertMsgFailed(("Unhandled process status %ld\n", mData.mStatus));
                return VERR_NOT_IMPLEMENTED;
        }
    }

    LogFlowThisFunc(("waitResult=%ld, waitRC=%Rrc\n", waitRes.mResult, waitRes.mRC));

    /* No waiting needed? Return immediately. */
    if (waitRes.mResult != ProcessWaitResult_None)
        return VINF_SUCCESS;

    if (mData.mWaitCount > 0)
        return VERR_ALREADY_EXISTS;
    mData.mWaitCount++;

    Assert(mData.mWaitEvent == NULL);
    mData.mWaitEvent = new GuestProcessEvent(fWaitFlags);
    AssertPtrReturn(mData.mWaitEvent, VERR_NO_MEMORY);

    alock.release(); /* Release lock before waiting. */

    int vrc = mData.mWaitEvent->Wait(uTimeoutMS);
    if (RT_SUCCESS(vrc))
    {
        waitRes = mData.mWaitEvent->GetResult();
    }
    else if (vrc == VERR_TIMEOUT)
    {
        waitRes.mRC = VINF_SUCCESS;
        waitRes.mResult = ProcessWaitResult_Timeout;

        vrc = VINF_SUCCESS;
    }

    alock.acquire(); /* Get the lock again. */

    /* Note: The caller always is responsible of deleting the
     *       stuff it created before. See close() for more information. */
    delete mData.mWaitEvent;
    mData.mWaitEvent = NULL;

    mData.mWaitCount--;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::waitForStart(uint32_t uTimeoutMS)
{
    LogFlowThisFunc(("uTimeoutMS=%RU32\n", uTimeoutMS));

    int vrc = VINF_SUCCESS;

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    if (mData.mStatus != ProcessStatus_Started)
    {
        alock.release();

        GuestProcessWaitResult waitRes;
        vrc = waitFor(ProcessWaitForFlag_Start, uTimeoutMS, waitRes);
        if (   RT_FAILURE(vrc)
            || waitRes.mResult == ProcessWaitResult_Start)
        {
            if (RT_SUCCESS(vrc))
                vrc = waitRes.mRC;
        }
        /** @todo More error handling needed. */
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

int GuestProcess::writeData(uint32_t uHandle, uint32_t uFlags,
                            void *pvData, size_t cbData, uint32_t uTimeoutMS, uint32_t *puWritten)
{
    LogFlowThisFunc(("uPID=%RU32, uHandle=%RU32, uFlags=%RU32, pvData=%p, cbData=%RU32, uTimeoutMS=%RU32, puWritten=%p\n",
                     mData.mPID, uHandle, uFlags, pvData, cbData, uTimeoutMS, puWritten));
    /* All is optional. There can be 0 byte writes. */

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mData.mStatus != ProcessStatus_Started)
        return VINF_SUCCESS; /* Not available for writing (anymore). */

    uint32_t uContextID = 0;
    GuestCtrlCallback *pCallbackWrite = new GuestCtrlCallback();
    if (!pCallbackWrite)
        return VERR_NO_MEMORY;

    /* Create callback and add it to the map. */
    int vrc = pCallbackWrite->Init(VBOXGUESTCTRLCALLBACKTYPE_EXEC_INPUT_STATUS);
    if (RT_SUCCESS(vrc))
        vrc = callbackAdd(pCallbackWrite, &uContextID);

    alock.release(); /* Drop the write lock again. */

    if (RT_SUCCESS(vrc))
    {
        VBOXHGCMSVCPARM paParms[5];

        int i = 0;
        paParms[i++].setUInt32(uContextID);
        paParms[i++].setUInt32(mData.mPID);
        paParms[i++].setUInt32(uFlags);
        paParms[i++].setPointer(pvData, cbData);
        paParms[i++].setUInt32(cbData);

        vrc = sendCommand(HOST_EXEC_SET_INPUT, i, paParms);
    }

    if (RT_SUCCESS(vrc))
    {
        /*
         * Let's wait for the process being started.
         * Note: Be sure not keeping a AutoRead/WriteLock here.
         */
        LogFlowThisFunc(("Waiting for callback (%RU32ms) ...\n", uTimeoutMS));
        vrc = pCallbackWrite->Wait(uTimeoutMS);
        if (RT_SUCCESS(vrc)) /* Wait was successful, check for supplied information. */
        {
            vrc = pCallbackWrite->GetResultCode();
            LogFlowThisFunc(("Callback returned vrc=%Rrc, cbData=%RU32\n", vrc, pCallbackWrite->GetDataSize()));

            if (RT_SUCCESS(vrc))
            {
                Assert(pCallbackWrite->GetDataSize() == sizeof(CALLBACKDATAEXECINSTATUS));
                PCALLBACKDATAEXECINSTATUS pData = (PCALLBACKDATAEXECINSTATUS)pCallbackWrite->GetDataRaw();
                AssertPtr(pData);

                uint32_t cbWritten = 0;
                switch (pData->u32Status)
                {
                    case INPUT_STS_WRITTEN:
                        cbWritten = pData->cbProcessed;
                        break;

                    case INPUT_STS_ERROR:
                        vrc = pData->u32Flags; /** @todo Fix int vs. uint32_t! */
                        break;

                    case INPUT_STS_TERMINATED:
                        vrc = VERR_CANCELLED;
                        break;

                    case INPUT_STS_OVERFLOW:
                        vrc = VERR_BUFFER_OVERFLOW;
                        break;

                    default:
                        /* Silently skip unknown errors. */
                        break;
                }

                LogFlowThisFunc(("cbWritten=%RU32\n", cbWritten));

                if (puWritten)
                    *puWritten = cbWritten;
            }
        }
        else
            vrc = VERR_TIMEOUT;
    }

    alock.acquire();

    AssertPtr(pCallbackWrite);
    int rc2 = callbackRemove(uContextID);
    if (RT_SUCCESS(vrc))
        vrc = rc2;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

// implementation of public methods
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP GuestProcess::Read(ULONG aHandle, ULONG aToRead, ULONG aTimeoutMS, ComSafeArrayOut(BYTE, aData))
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    if (aToRead == 0)
        return setError(E_INVALIDARG, tr("The size to read is zero"));
    CheckComArgOutSafeArrayPointerValid(aData);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    com::SafeArray<BYTE> data((size_t)aToRead);
    Assert(data.size() >= aToRead);

    size_t cbRead;
    int vrc = readData(aHandle, aToRead, aTimeoutMS, data.raw(), aToRead, &cbRead);
    if (RT_SUCCESS(vrc))
    {
        if (data.size() != cbRead)
            data.resize(cbRead);
        data.detachTo(ComSafeArrayOutArg(aData));
    }

    LogFlowThisFunc(("readData returned %Rrc, cbRead=%RU64\n", vrc, cbRead));

    /** @todo Do setError() here. */
    HRESULT hr = RT_SUCCESS(vrc) ? S_OK : VBOX_E_IPRT_ERROR;
    LogFlowFuncLeaveRC(vrc);

    return hr;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::Terminate(void)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    HRESULT hr = S_OK;

    int vrc = terminateProcess();
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_NOT_IMPLEMENTED:
                ReturnComNotImplemented();
                break; /* Never reached. */

            case VERR_NOT_SUPPORTED:
                hr = setError(VBOX_E_IPRT_ERROR,
                              tr("Terminating process \"%s\" (PID %RU32) not supported by installed Guest Additions"),
                              mData.mProcess.mCommand.c_str(), mData.mPID);
                break;

            default:
                hr = setError(VBOX_E_IPRT_ERROR,
                              tr("Terminating process \"%s\" (PID %RU32) failed: %Rrc"),
                              mData.mProcess.mCommand.c_str(), mData.mPID, vrc);
                break;
        }
    }

    AssertPtr(mData.mParent);
    mData.mParent->processRemoveFromList(this);

    /*
     * Release autocaller before calling uninit.
     */
    autoCaller.release();

    uninit();

    LogFlowFuncLeaveRC(vrc);
    return hr;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::WaitFor(ULONG aWaitFlags, ULONG aTimeoutMS, ProcessWaitResult_T *aReason)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aReason);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /*
     * Note: Do not hold any locks here while waiting!
     */
    HRESULT hr;

    GuestProcessWaitResult waitRes;
    int vrc = waitFor(aWaitFlags, aTimeoutMS, waitRes);
    if (RT_SUCCESS(vrc))
    {
        *aReason = waitRes.mResult;
        hr = setErrorExternal();
    }
    else
    {
        hr = setError(VBOX_E_IPRT_ERROR,
                      tr("Waiting for process \"%s\" (PID %RU32) failed with vrc=%Rrc"),
                      mData.mProcess.mCommand.c_str(), mData.mPID, vrc);
    }
    LogFlowFuncLeaveRC(vrc);
    return hr;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::WaitForArray(ComSafeArrayIn(ProcessWaitForFlag_T, aFlags), ULONG aTimeoutMS, ProcessWaitResult_T *aReason)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aReason);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /*
     * Note: Do not hold any locks here while waiting!
     */
    uint32_t fWaitFor = ProcessWaitForFlag_None;
    com::SafeArray<ProcessWaitForFlag_T> flags(ComSafeArrayInArg(aFlags));
    for (size_t i = 0; i < flags.size(); i++)
        fWaitFor |= flags[i];

    return WaitFor(fWaitFor, aTimeoutMS, aReason);
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::Write(ULONG aHandle, ULONG aFlags,
                                 ComSafeArrayIn(BYTE, aData), ULONG aTimeoutMS, ULONG *aWritten)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aWritten);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    com::SafeArray<BYTE> data(ComSafeArrayInArg(aData));
    int vrc = writeData(aHandle, aFlags, data.raw(), data.size(), aTimeoutMS, (uint32_t*)aWritten);

    LogFlowThisFunc(("writeData returned %Rrc, aWritten=%RU32\n", vrc, aWritten));

    /** @todo Do setError() here. */
    HRESULT hr = RT_SUCCESS(vrc) ? S_OK : VBOX_E_IPRT_ERROR;
    LogFlowFuncLeaveRC(vrc);

    return hr;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestProcess::WriteArray(ULONG aHandle, ComSafeArrayIn(ProcessInputFlag_T, aFlags),
                                      ComSafeArrayIn(BYTE, aData), ULONG aTimeoutMS, ULONG *aWritten)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aWritten);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    /*
     * Note: Do not hold any locks here while writing!
     */
    ULONG fWrite = ProcessInputFlag_None;
    com::SafeArray<ProcessInputFlag_T> flags(ComSafeArrayInArg(aFlags));
    for (size_t i = 0; i < flags.size(); i++)
        fWrite |= flags[i];

    return Write(aHandle, fWrite, ComSafeArrayInArg(aData), aTimeoutMS, aWritten);
#endif /* VBOX_WITH_GUEST_CONTROL */
}

