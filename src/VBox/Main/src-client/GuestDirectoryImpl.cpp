
/* $Id: GuestDirectoryImpl.cpp 42897 2012-08-21 10:03:52Z vboxsync $ */
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "GuestDirectoryImpl.h"
#include "GuestSessionImpl.h"
#include "GuestCtrlImplPrivate.h"

#include "Global.h"
#include "AutoCaller.h"

#include <VBox/com/array.h>

#ifdef LOG_GROUP
 #undef LOG_GROUP
#endif
#define LOG_GROUP LOG_GROUP_GUEST_CONTROL
#include <VBox/log.h>


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(GuestDirectory)

HRESULT GuestDirectory::FinalConstruct(void)
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void GuestDirectory::FinalRelease(void)
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

int GuestDirectory::init(GuestSession *aSession,
                         const Utf8Str &strPath, const Utf8Str &strFilter /*= ""*/, uint32_t uFlags /*= 0*/)
{
    LogFlowThisFunc(("strPath=%s, strFilter=%s, uFlags=%x\n",
                     strPath.c_str(), strFilter.c_str(), uFlags));

    /* Enclose the state transition NotReady->InInit->Ready. */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    mData.mSession = aSession;
    mData.mName    = strPath;
    mData.mFilter  = strFilter;
    mData.mFlags   = uFlags;

    /* Start the directory process on the guest. */
    GuestProcessStartupInfo procInfo;
    procInfo.mName      = Utf8StrFmt(tr("Reading directory \"%s\"", strPath.c_str()));
    procInfo.mCommand   = Utf8Str(VBOXSERVICE_TOOL_LS);
    procInfo.mTimeoutMS = 0; /* No timeout. */
    procInfo.mFlags     = ProcessCreateFlag_Hidden | ProcessCreateFlag_WaitForStdOut;

    procInfo.mArguments.push_back(Utf8Str("--machinereadable"));
    /* We want the long output format which contains all the object details. */
    procInfo.mArguments.push_back(Utf8Str("-l"));
#if 0 /* Flags are not supported yet. */
    if (uFlags & DirectoryOpenFlag_NoSymlinks)
        procInfo.mArguments.push_back(Utf8Str("--nosymlinks")); /** @todo What does GNU here? */
#endif
    /** @todo Recursion support? */
    procInfo.mArguments.push_back(strPath); /* The directory we want to open. */

    /*
     * Start the process asynchronously and keep it around so that we can use
     * it later in subsequent read() calls.
     */
    ComObjPtr<GuestProcess> pProcess;
    int rc = mData.mSession->processCreateExInteral(procInfo, pProcess);
    if (RT_SUCCESS(rc))
        rc = pProcess->startProcessAsync();

    LogFlowThisFunc(("rc=%Rrc\n", rc));

    if (RT_SUCCESS(rc))
    {
        mData.mProcess = pProcess;

        /* Confirm a successful initialization when it's the case. */
        autoInitSpan.setSucceeded();
        return rc;
    }

    autoInitSpan.setFailed();
    return rc;
}

/**
 * Uninitializes the instance.
 * Called from FinalRelease().
 */
void GuestDirectory::uninit(void)
{
    LogFlowThisFunc(("\n"));

    /* Enclose the state transition Ready->InUninit->NotReady. */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    LogFlowThisFuncLeave();
}

// implementation of public getters/setters for attributes
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP GuestDirectory::COMGETTER(DirectoryName)(BSTR *aName)
{
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aName);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData.mName.cloneTo(aName);

    return S_OK;
}

STDMETHODIMP GuestDirectory::COMGETTER(Filter)(BSTR *aFilter)
{
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aFilter);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    mData.mFilter.cloneTo(aFilter);

    return S_OK;
}

// private methods
/////////////////////////////////////////////////////////////////////////////

int GuestDirectory::parseData(GuestProcessStreamBlock &streamBlock)
{
    LogFlowThisFunc(("cbStream=%RU32\n", mData.mStream.GetSize()));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    int rc;
    do
    {
        /* Try parsing the data to see if the current block is complete. */
        rc = mData.mStream.ParseBlock(streamBlock);
        if (streamBlock.GetCount())
            break;

    } while (RT_SUCCESS(rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

// implementation of public methods
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP GuestDirectory::Close(void)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    AssertPtr(mData.mSession);
    int rc = mData.mSession->directoryRemoveFromList(this);
    if (mData.mProcess)
    {
        int rc2 = mData.mSession->processRemoveFromList(mData.mProcess);
        if (RT_SUCCESS(rc))
            rc = rc2;
    }

    /*
     * Release autocaller before calling uninit.
     */
    autoCaller.release();

    uninit();

    LogFlowFuncLeaveRC(rc);
    return S_OK;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

STDMETHODIMP GuestDirectory::Read(IFsObjInfo **aInfo)
{
#ifndef VBOX_WITH_GUEST_CONTROL
    ReturnComNotImplemented();
#else
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.rc())) return autoCaller.rc();

    ComObjPtr<GuestProcess> pProcess = mData.mProcess;
    Assert(!pProcess.isNull());

    GuestProcessStreamBlock streamBlock;
    GuestFsObjData objData;

    int rc = parseData(streamBlock);
    if (   RT_FAILURE(rc)
        || streamBlock.IsEmpty()) /* More data needed. */
    {
        rc = pProcess->waitForStart(30 * 1000 /* 30s timeout */);
    }

    if (RT_SUCCESS(rc))
    {
        BYTE byBuf[_64K];
        size_t cbRead = 0;

        /** @todo Merge with GuestSession::queryFileInfoInternal. */
        for (;RT_SUCCESS(rc);)
        {
            GuestProcessWaitResult waitRes;
            rc = pProcess->waitFor(  ProcessWaitForFlag_Terminate
                                   | ProcessWaitForFlag_StdOut,
                                   30 * 1000 /* Timeout */, waitRes);
            if (   RT_FAILURE(rc)
                || waitRes.mResult == ProcessWaitResult_Terminate
                || waitRes.mResult == ProcessWaitResult_Error
                || waitRes.mResult == ProcessWaitResult_Timeout)
            {
                if (RT_FAILURE(waitRes.mRC))
                    rc = waitRes.mRC;
                break;
            }

            rc = pProcess->readData(OUTPUT_HANDLE_ID_STDOUT, sizeof(byBuf),
                                    30 * 1000 /* Timeout */, byBuf, sizeof(byBuf),
                                    &cbRead);
            if (RT_FAILURE(rc))
                break;

            if (cbRead)
            {
                AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

                rc = mData.mStream.AddData(byBuf, cbRead);
                if (RT_FAILURE(rc))
                    break;

                LogFlowThisFunc(("rc=%Rrc, cbRead=%RU64, cbStreamOut=%RU32\n",
                                 rc, cbRead, mData.mStream.GetSize()));

                rc = parseData(streamBlock);
                if (RT_SUCCESS(rc))
                {
                    /* Parsing the current stream block succeeded so
                     * we don't need more at the moment. */
                    break;
                }
            }
        }

        LogFlowThisFunc(("Reading done with rc=%Rrc, cbRead=%RU64, cbStream=%RU32\n",
                         rc, cbRead, mData.mStream.GetSize()));

        if (RT_SUCCESS(rc))
        {
            rc = parseData(streamBlock);
            if (rc == VERR_NO_DATA) /* Since this is the last parsing call, this is ok. */
                rc = VINF_SUCCESS;
        }

        /*
         * Note: The guest process can still be around to serve the next
         *       upcoming stream block next time.
         */
        if (RT_SUCCESS(rc))
        {
            /** @todo Move into common function. */
            ProcessStatus_T procStatus = ProcessStatus_Undefined;
            LONG exitCode = 0;

            HRESULT hr2 = pProcess->COMGETTER(Status(&procStatus));
            ComAssertComRC(hr2);
            hr2 = pProcess->COMGETTER(ExitCode(&exitCode));
            ComAssertComRC(hr2);

            if (   (   procStatus != ProcessStatus_Started
                    && procStatus != ProcessStatus_Paused
                    && procStatus != ProcessStatus_Terminating
                   )
                && exitCode != 0)
            {
                rc = VERR_ACCESS_DENIED;
            }
        }
    }

    if (RT_SUCCESS(rc))
    {
        if (streamBlock.GetCount()) /* Did we get content? */
        {
            rc = objData.FromLs(streamBlock);
            if (RT_FAILURE(rc))
                rc = VERR_PATH_NOT_FOUND;

            if (RT_SUCCESS(rc))
            {
                /* Create the object. */
                ComObjPtr<GuestFsObjInfo> pFsObjInfo;
                HRESULT hr2 = pFsObjInfo.createObject();
                if (FAILED(hr2))
                    rc = VERR_COM_UNEXPECTED;

                if (RT_SUCCESS(rc))
                    rc = pFsObjInfo->init(objData);

                if (RT_SUCCESS(rc))
                {
                    /* Return info object to the caller. */
                    hr2 = pFsObjInfo.queryInterfaceTo(aInfo);
                    if (FAILED(hr2))
                        rc = VERR_COM_UNEXPECTED;
                }
            }
        }
        else
        {
            /* Nothing to read anymore. Tell the caller. */
            rc = VERR_NO_MORE_FILES;
        }
    }

    HRESULT hr = S_OK;

    if (RT_FAILURE(rc)) /** @todo Add more errors here. */
    {
        switch (rc)
        {
            case VERR_ACCESS_DENIED:
                hr = setError(VBOX_E_IPRT_ERROR, tr("Reading directory \"%s\" failed: Unable to read / access denied"),
                              mData.mName.c_str());
                break;

            case VERR_PATH_NOT_FOUND:
                hr = setError(VBOX_E_IPRT_ERROR, tr("Reading directory \"%s\" failed: Path not found"),
                              mData.mName.c_str());
                break;

            case VERR_NO_MORE_FILES:
                hr = setError(VBOX_E_OBJECT_NOT_FOUND, tr("No more entries for directory \"%s\""),
                              mData.mName.c_str());
                break;

            default:
                hr = setError(VBOX_E_IPRT_ERROR, tr("Error while reading directory \"%s\": %Rrc\n"),
                              mData.mName.c_str(), rc);
                break;
        }
    }

    LogFlowFuncLeaveRC(rc);
    return hr;
#endif /* VBOX_WITH_GUEST_CONTROL */
}

