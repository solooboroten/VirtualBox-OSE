/* $Id: WinKeyboard.h $ */
/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * Windows keyboard handling.
 */

/*
 * Copyright (C) 2014 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __WinKeyboard_h__
#define __WinKeyboard_h__

/* Platform includes: */
#include "Windows.h"

void * WinHidDevicesKeepLedsState(void);
void   WinHidDevicesApplyAndReleaseLedsState(void *pData);
void   WinHidDevicesBroadcastLeds(bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn);

bool winHidLedsInSync(bool fNumLockOn, bool fCapsLockOn, bool fScrollLockOn);

#endif /* __WinKeyboard_h__ */

