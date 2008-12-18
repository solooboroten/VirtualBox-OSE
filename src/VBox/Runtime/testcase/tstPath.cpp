/* $Id: tstPath.cpp 14324 2008-11-18 19:09:34Z vboxsync $ */
/** @file
 * IPRT Testcase - Test various path functions.
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
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/initterm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/param.h>


#define CHECK_RC(method) \
    do { \
        rc = method; \
        if (RT_FAILURE(rc)) \
        { \
            cErrors++; \
            RTPrintf("\ntstPath: FAILED calling " #method " at line %d: rc=%Rrc\n", __LINE__, rc); \
        } \
    } while (0)

int main()
{
    /*
     * Init RT.
     */
    int rc;
    int cErrors = 0;
    CHECK_RC(RTR3Init());
    if (RT_FAILURE(rc))
        return 1;

    /*
     * RTPathProgram, RTPathUserHome and RTProcGetExecutableName.
     */
    char szPath[RTPATH_MAX];
    CHECK_RC(RTPathProgram(szPath, sizeof(szPath)));
    if (RT_SUCCESS(rc))
        RTPrintf("Program={%s}\n", szPath);
    CHECK_RC(RTPathUserHome(szPath, sizeof(szPath)));
    if (RT_SUCCESS(rc))
        RTPrintf("UserHome={%s}\n", szPath);
    if (RTProcGetExecutableName(szPath, sizeof(szPath)) == szPath)
        RTPrintf("ExecutableName={%s}\n", szPath);
    else
    {
        RTPrintf("tstPath: FAILED - RTProcGetExecutableName\n");
        cErrors++;
    }


    /*
     * RTPathAbsEx
     */
    RTPrintf("tstPath: TESTING RTPathAbsEx()\n");
    static const char *aInput[] =
    {
        // NULL, NULL, -- assertion in RTStrToUtf16
        NULL,                           "/absolute/..",
        NULL,                           "/absolute\\\\../..",
        NULL,                           "/absolute//../path",
        NULL,                           "/absolute/../../path",
        NULL,                           "relative/../dir\\.\\.\\.\\file.txt",
        NULL,                           "\\",
        "relative_base/dir\\",          "\\from_root",
        "relative_base/dir/",           "relative_also",
#if defined (RT_OS_OS2) || defined (RT_OS_WINDOWS)
        NULL,                           "C:\\",
        "C:\\",                         "..",
        "C:\\temp",                     "..",
        "C:\\VirtualBox/Machines",      "..\\VirtualBox.xml",
        "C:\\MustDie",                  "\\from_root/dir/..",
        "C:\\temp",                     "D:\\data",
        NULL,                           "\\\\server\\../share", // -- on Win32, GetFullPathName doesn't remove .. here
        /* the three below use cases should fail with VERR_INVALID_NAME */
        //NULL,                           "\\\\server",
        //NULL,                           "\\\\",
        //NULL,                           "\\\\\\something",
        "\\\\server\\share_as_base",    "/from_root",
        "\\\\just_server",              "/from_root",
        "\\\\server\\share_as_base",    "relative\\data",
        "base",                         "\\\\?\\UNC\\relative/edwef/..",
        "base",                         "\\\\?\\UNC\\relative/edwef/..",
        /* this is not (and I guess should not be) supported, should fail */
        ///@todo "\\\\?\\UNC\\base",             "/from_root",
#else
        "\\temp",                       "..",
        "\\VirtualBox/Machines",        "..\\VirtualBox.xml",
        "\\MustDie",                    "\\from_root/dir/..",
        "\\temp",                       "\\data",
#endif
    };

    for (unsigned i = 0; i < RT_ELEMENTS(aInput); i += 2)
    {
        RTPrintf("tstPath: base={%s}, path={%s}, ", aInput[i], aInput[i + 1]);
        CHECK_RC(RTPathAbsEx(aInput[i], aInput[i + 1], szPath, sizeof(szPath)));
        if (RT_SUCCESS(rc))
            RTPrintf("abs={%s}\n", szPath);
    }

    /*
     * RTPathStripFilename
     */
    RTPrintf("tstPath: RTPathStripFilename...\n");
    static const char *apszStripFilenameTests[] =
    {
        "/usr/include///",              "/usr/include//",
        "/usr/include/",                "/usr/include",
        "/usr/include",                 "/usr",
        "/usr",                         "/",
        "usr",                          ".",
#if defined (RT_OS_OS2) || defined (RT_OS_WINDOWS)
        "c:/windows",                   "c:/",
        "c:/",                          "c:/",
        "D:",                           "D:",
        "C:\\OS2\\DLLS",                "C:\\OS2",
#endif
    };
    for (unsigned i = 0; i < RT_ELEMENTS(apszStripFilenameTests); i += 2)
    {
        const char *pszInput  = apszStripFilenameTests[i];
        const char *pszExpect = apszStripFilenameTests[i + 1];
        char szPath[RTPATH_MAX];
        strcpy(szPath, pszInput);
        RTPathStripFilename(szPath);
        if (strcmp(szPath, pszExpect))
        {
            RTPrintf("tstPath: RTPathStripFilename failed!\n"
                     "   input: '%s'\n"
                     "  output: '%s'\n"
                     "expected: '%s'\n",
                     pszInput, szPath, pszExpect);
            cErrors++;
        }
    }

    /*
     * Summary.
     */
    if (!cErrors)
        RTPrintf("tstPath: SUCCESS\n");
    else
        RTPrintf("tstPath: FAILURE %d errors\n", cErrors);
    return !!cErrors;
}

