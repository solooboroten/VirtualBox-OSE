/** @file
 * UICocoaApplication - C++ interface to NSApplication for handling -sendEvent.
 */

/*
 * Copyright (C) 2009-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___darwin_VBoxCocoaApplication_h
#define ___darwin_VBoxCocoaApplication_h

/* Qt includes: */
#include <QMap>

/* GUI includes: */
#include "VBoxCocoaHelper.h"

ADD_COCOA_NATIVE_REF(UICocoaApplicationPrivate);
ADD_COCOA_NATIVE_REF(NSAutoreleasePool);
ADD_COCOA_NATIVE_REF(NSString);
ADD_COCOA_NATIVE_REF(NSWindow);

/* Forward declarations: */
class QWidget;

/** Event handler callback.
 * @returns true if handled, false if not.
 * @param   pvCocoaEvent    The Cocoa event.
 * @param   pvCarbonEvent   The Carbon event.
 * @param   pvUser          The user argument.
 */
typedef bool (*PFNVBOXCACALLBACK)(const void *pvCocoaEvent, const void *pvCarbonEvent, void *pvUser);

/** Native notification callback type for QWidget. */
typedef void (*PfnNativeNotificationCallbackForQWidget)(const QString &strNativeNotificationName, QWidget *pWidget);

/* C++ singleton for our private NSApplication object */
class UICocoaApplication
{
public:
    static UICocoaApplication* instance();
    ~UICocoaApplication();

    void registerForNativeEvents(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser);
    void unregisterForNativeEvents(uint32_t fMask, PFNVBOXCACALLBACK pfnCallback, void *pvUser);

    /** Register passed @a pWidget to native notification @a strNativeNotificationName, using @a pCallback as handler. */
    void registerToNativeNotification(const QString &strNativeNotificationName, QWidget *pWidget, PfnNativeNotificationCallbackForQWidget pCallback);
    /** Unregister passed @a pWidget from native notification @a strNativeNotificationName. */
    void unregisterFromNativeNotification(const QString &strNativeNotificationName, QWidget *pWidget);
    /** Redirects native notification @a pstrNativeNotificationName for window @a pWindow to registered listener. */
    void nativeNotificationProxy(NativeNSStringRef pstrNativeNotificationName, NativeNSWindowRef pWindow);

private:
    UICocoaApplication();
    static UICocoaApplication *m_pInstance;
    NativeUICocoaApplicationPrivateRef m_pNative;
    NativeNSAutoreleasePoolRef m_pPool;

    /** Map of notification callbacks registered for corresponding QWidget(s). */
    QMap<QWidget*, QMap<QString, PfnNativeNotificationCallbackForQWidget> > m_callbacks;
};

#endif /* ___darwin_VBoxCocoaApplication_h */

