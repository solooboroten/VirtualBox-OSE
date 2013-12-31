/** @file
 * VBoxCredProvUtils - Misc. utility functions for VBoxCredProv.
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

#ifndef __VBOX_CREDPROV_UTILS_H__
#define __VBOX_CREDPROV_UTILS_H__

#include <VBox/VBoxGuestLib.h>

extern DWORD g_dwVerbosity;

void VBoxCredProvVerbose(DWORD dwLevel, const char *pszFormat, ...);
int VBoxCredProvReportStatus(VBoxGuestStatusCurrent enmStatus);

#endif /* !__VBOX_CREDPROV_UTILS_H__ */

