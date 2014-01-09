/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIMachineLogicFullscreen class declaration
 */

/*
 * Copyright (C) 2010-2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __UIMachineLogicFullscreen_h__
#define __UIMachineLogicFullscreen_h__

/* Local includes: */
#include "UIMachineLogic.h"

/* Forward declarations: */
class UIMultiScreenLayout;

/* Fullscreen machine logic implementation: */
class UIMachineLogicFullscreen : public UIMachineLogic
{
    Q_OBJECT;

protected:

    /* Constructor/destructor: */
    UIMachineLogicFullscreen(QObject *pParent, UISession *pSession);
    virtual ~UIMachineLogicFullscreen();

    /* Check if this logic is available: */
    bool checkAvailability();

    /* Prepare logic: */
    void prepare();

    int hostScreenForGuestScreen(int screenId) const;

private slots:

#ifdef RT_OS_DARWIN
    void sltChangePresentationMode(bool fEnabled);
    void sltScreenLayoutChanged();
#endif /* RT_OS_DARWIN */

private:

    /* Prepare helpers: */
    void prepareActionGroups();
    void prepareMachineWindows();
#ifdef Q_WS_MAC
    void prepareFullscreenConnections();
#endif /* Q_WS_MAC */

    /* Cleanup helpers: */
    void cleanupMachineWindows();
    void cleanupActionGroups();
#ifdef Q_WS_MAC
    //void cleanupCommonConnections() {}
#endif /* Q_WS_MAC */

#ifdef Q_WS_MAC
    void setPresentationModeEnabled(bool fEnabled);
#endif /* Q_WS_MAC */

    UIMultiScreenLayout *m_pScreenLayout;

    /* Friend classes: */
    friend class UIMachineLogic;
    friend class UIMachineWindowFullscreen;
    friend class UIMachineViewFullscreen;
};

#endif // __UIMachineLogicFullscreen_h__

