/* $Id: comparepaths.cpp $ */
/** @file
 * IPRT - Path Comparison.
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
#include "internal/iprt.h"
#include <iprt/path.h>
#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/uni.h>


/**
 * Helper for RTPathCompare() and RTPathStartsWith().
 *
 * @returns similar to strcmp.
 * @param   pszPath1        Path to compare.
 * @param   pszPath2        Path to compare.
 * @param   fLimit          Limit the comparison to the length of \a pszPath2
 * @internal
 */
static int rtPathCompare(const char *pszPath1, const char *pszPath2, bool fLimit)
{
    if (pszPath1 == pszPath2)
        return 0;
    if (!pszPath1)
        return -1;
    if (!pszPath2)
        return 1;

#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
    PRTUNICP puszPath1;
    int rc = RTStrToUni(pszPath1, &puszPath1);
    if (RT_FAILURE(rc))
        return -1;
    PRTUNICP puszPath2;
    rc = RTStrToUni(pszPath2, &puszPath2);
    if (RT_FAILURE(rc))
    {
        RTUniFree(puszPath1);
        return 1;
    }

    int iDiff = 0;
    PRTUNICP puszTmpPath1 = puszPath1;
    PRTUNICP puszTmpPath2 = puszPath2;
    for (;;)
    {
        register RTUNICP uc1 = *puszTmpPath1;
        register RTUNICP uc2 = *puszTmpPath2;
        if (uc1 != uc2)
        {
            if (uc1 == '\\')
                uc1 = '/';
            else
                uc1 = RTUniCpToUpper(uc1);
            if (uc2 == '\\')
                uc2 = '/';
            else
                uc2 = RTUniCpToUpper(uc2);
            if (uc1 != uc2)
            {
                iDiff = uc1 > uc2 ? 1 : -1; /* (overflow/underflow paranoia) */
                if (fLimit && uc2 == '\0')
                    iDiff = 0;
                break;
            }
        }
        if (!uc1)
            break;
        puszTmpPath1++;
        puszTmpPath2++;

    }

    RTUniFree(puszPath2);
    RTUniFree(puszPath1);
    return iDiff;

#else
    if (!fLimit)
        return strcmp(pszPath1, pszPath2);
    return strncmp(pszPath1, pszPath2, strlen(pszPath2));
#endif
}


/**
 * Compares two paths.
 *
 * The comparison takes platform-dependent details into account,
 * such as:
 * <ul>
 * <li>On DOS-like platforms, both separator chars (|\| and |/|) are considered
 *     to be equal.
 * <li>On platforms with case-insensitive file systems, mismatching characters
 *     are uppercased and compared again.
 * </ul>
 *
 * @returns @< 0 if the first path less than the second path.
 * @returns 0 if the first path identical to the second path.
 * @returns @> 0 if the first path greater than the second path.
 *
 * @param   pszPath1    Path to compare (must be an absolute path).
 * @param   pszPath2    Path to compare (must be an absolute path).
 *
 * @remarks File system details are currently ignored. This means that you won't
 *          get case-insentive compares on unix systems when a path goes into a
 *          case-insensitive filesystem like FAT, HPFS, HFS, NTFS, JFS, or
 *          similar. For NT, OS/2 and similar you'll won't get case-sensitve
 *          compares on a case-sensitive file system.
 */
RTDECL(int) RTPathCompare(const char *pszPath1, const char *pszPath2)
{
    return rtPathCompare(pszPath1, pszPath2, false /* full path lengths */);
}


/**
 * Checks if a path starts with the given parent path.
 *
 * This means that either the path and the parent path matches completely, or
 * that the path is to some file or directory residing in the tree given by the
 * parent directory.
 *
 * The path comparison takes platform-dependent details into account,
 * see RTPathCompare() for details.
 *
 * @returns |true| when \a pszPath starts with \a pszParentPath (or when they
 *          are identical), or |false| otherwise.
 *
 * @param   pszPath         Path to check, must be an absolute path.
 * @param   pszParentPath   Parent path, must be an absolute path.
 *                          No trailing directory slash!
 *
 * @remarks This API doesn't currently handle root directory compares in a
 *          manner consistant with the other APIs. RTPathStartsWith(pszSomePath,
 *          "/") will not work if pszSomePath isn't "/".
 */
RTDECL(bool) RTPathStartsWith(const char *pszPath, const char *pszParentPath)
{
    if (pszPath == pszParentPath)
        return true;
    if (!pszPath || !pszParentPath)
        return false;

    if (rtPathCompare(pszPath, pszParentPath, true /* limited by path 2 */) != 0)
        return false;

    const size_t cchParentPath = strlen(pszParentPath);
    return RTPATH_IS_SLASH(pszPath[cchParentPath])
        || pszPath[cchParentPath] == '\0';
}

