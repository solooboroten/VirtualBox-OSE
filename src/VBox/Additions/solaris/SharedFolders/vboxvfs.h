/* $Id: vboxvfs.h 34052 2008-08-04 17:27:28Z klaus $ */
/** @file
 * VirtualBox File System Driver for Solaris Guests, Internal Header.
 */

/*
 * Copyright (C) 2008 Sun Microsystems, Inc.
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#ifndef ___VBoxVFS_Solaris_h
#define ___VBoxVFS_Solaris_h

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_HOST_NAME   256
#define MAX_NLS_NAME    32

/* Not sure if we need this; it seems only necessary for kernel mounts. */
typedef struct vboxvfs_mountinfo
{
    char name[MAX_HOST_NAME];
    char nls_name[MAX_NLS_NAME];
    int uid;
    int gid;
    int ttl;
} vboxvfs_mountinfo_t;

#ifdef _KERNEL

#include "../../common/VBoxGuestLib/VBoxCalls.h"
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>

/** Per-file system mount instance data. */
typedef struct vboxvfs_globinfo
{
    VBSFMAP Map;
    int Ttl;
    int Uid;
    int Gid;
    vfs_t *pVFS;
    dev_t Dev;
    vnode_t *pVNodeDev;
    vnode_t *pVNodeRoot;
} vboxvfs_globinfo_t;

extern struct vnodeops *g_pVBoxVFS_vnodeops;
extern const fs_operation_def_t g_VBoxVFS_vnodeops_template[];

#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* ___VBoxVFS_Solaris_h */

