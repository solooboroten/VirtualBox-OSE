/* $Id: HostDnsServiceDarwin.cpp 48492 2013-09-16 15:40:44Z vboxsync $ */
/** @file
 * Darwin specific DNS information fetching.
 */

/*
 * Copyright (C) 2004-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#include <VBox/com/string.h>
#include <VBox/com/ptr.h>

#include "../HostDnsService.h"

#include <iprt/err.h>
#include <iprt/thread.h>
#include <iprt/semaphore.h>

#include <CoreFoundation/CoreFoundation.h>
#include <SystemConfiguration/SCDynamicStore.h>



SCDynamicStoreRef g_store;
CFRunLoopSourceRef g_DnsWatcher;
CFRunLoopRef g_RunLoopRef;
RTTHREAD g_DnsMonitoringThread;
RTSEMEVENT g_DnsInitEvent;

static const CFStringRef kStateNetworkGlobalDNSKey = CFSTR("State:/Network/Global/DNS");

static int hostMonitoringRoutine(RTTHREAD ThreadSelf, void *pvUser)
{
    NOREF(ThreadSelf);
    NOREF(pvUser);
    g_RunLoopRef = CFRunLoopGetCurrent();
    AssertReturn(g_RunLoopRef, VERR_INTERNAL_ERROR);

    CFRetain(g_RunLoopRef);

    CFArrayRef watchingArrayRef = CFArrayCreate(NULL, 
                                                (const void **)&kStateNetworkGlobalDNSKey, 
                                                1, &kCFTypeArrayCallBacks);
    if (!watchingArrayRef)
    {
        CFRelease(g_DnsWatcher);
        return E_OUTOFMEMORY;
    }

    if(SCDynamicStoreSetNotificationKeys(g_store, watchingArrayRef, NULL))
        CFRunLoopAddSource(CFRunLoopGetCurrent(), g_DnsWatcher, kCFRunLoopCommonModes);

    CFRelease(watchingArrayRef);

    RTSemEventSignal(g_DnsInitEvent);

    CFRunLoopRun();

    CFRelease(g_RunLoopRef);
    
    return VINF_SUCCESS;
}

HostDnsServiceDarwin::HostDnsServiceDarwin(){}
HostDnsServiceDarwin::~HostDnsServiceDarwin()
{
    CFRelease(g_DnsWatcher);
    CFRelease(g_store);
}

void HostDnsServiceDarwin::hostDnsServiceStoreCallback(void *arg0, void *arg1, void *info)
{
    HostDnsServiceDarwin *pThis = (HostDnsServiceDarwin *)info;
    
    NOREF(arg0); /* SCDynamicStore */
    NOREF(arg1); /* CFArrayRef */

    RTCritSectEnter(&pThis->m_hCritSect);
    pThis->update();
    RTCritSectLeave(&pThis->m_hCritSect);
}

HRESULT HostDnsServiceDarwin::init()
{
    SCDynamicStoreContext ctx;
    RT_ZERO(ctx);
    
    ctx.info = this;

    g_store = SCDynamicStoreCreate(NULL, CFSTR("org.virtualbox.VBoxSVC"), 
                                   (SCDynamicStoreCallBack)HostDnsServiceDarwin::hostDnsServiceStoreCallback, 
                                   &ctx);
    AssertReturn(g_store, E_FAIL);

    g_DnsWatcher = SCDynamicStoreCreateRunLoopSource(NULL, g_store, 0);
    if (!g_DnsWatcher)
        return E_OUTOFMEMORY;

    HRESULT hrc = HostDnsService::init();
    AssertComRCReturn(hrc, hrc);

    int rc = RTSemEventCreate(&g_DnsInitEvent);
    AssertRCReturn(rc, E_FAIL);
    
    return update();
}



HRESULT HostDnsServiceDarwin::start()
{
    int rc = RTThreadCreate(&g_DnsMonitoringThread, hostMonitoringRoutine,
                            this, 128 * _1K, RTTHREADTYPE_IO, 0, "dns-monitor");
    AssertRCReturn(rc, E_FAIL);

    RTSemEventWait(g_DnsInitEvent, RT_INDEFINITE_WAIT);

    return S_OK;
}


void HostDnsServiceDarwin::stop()
{

    if (g_RunLoopRef)
        CFRunLoopStop(g_RunLoopRef);
}


HRESULT HostDnsServiceDarwin::update()
{
    m_llNameServers.clear();
    m_llSearchStrings.clear();
    m_DomainName.setNull();

    CFPropertyListRef propertyRef = SCDynamicStoreCopyValue(g_store, 
                                                            kStateNetworkGlobalDNSKey);
    /**
     * 0:vvl@nb-mbp-i7-2(0)# scutil 
     * > get State:/Network/Global/DNS
     * > d.show
     * <dictionary> {
     * DomainName : vvl-domain
     * SearchDomains : <array> {
     * 0 : vvl-domain
     * 1 : de.vvl-domain.com
     * }
     * ServerAddresses : <array> {
     * 0 : 192.168.1.4
     * 1 : 192.168.1.1
     * 2 : 8.8.4.4
     *   }
     * }
     */
    
    if (!propertyRef)
        return S_OK;
    
    CFStringRef domainNameRef = (CFStringRef)CFDictionaryGetValue(
      static_cast<CFDictionaryRef>(propertyRef), CFSTR("DomainName"));
    if (domainNameRef)
    {
        const char *pszDomainName = CFStringGetCStringPtr(domainNameRef, 
                                                    CFStringGetSystemEncoding());
        if (pszDomainName)
            m_DomainName = com::Utf8Str(pszDomainName);
    }

    int i, arrayCount;
    CFArrayRef serverArrayRef = (CFArrayRef)CFDictionaryGetValue(
      static_cast<CFDictionaryRef>(propertyRef), CFSTR("ServerAddresses"));
    if (serverArrayRef)
    {
        arrayCount = CFArrayGetCount(serverArrayRef);
        for (i = 0; i < arrayCount; ++i)
        {
            CFStringRef serverAddressRef = (CFStringRef)CFArrayGetValueAtIndex(serverArrayRef, i);
            if (!serverArrayRef)
                continue;

            const char *pszServerAddress = CFStringGetCStringPtr(serverAddressRef, 
                                                           CFStringGetSystemEncoding());
            if (!pszServerAddress)
                continue;
            
            m_llNameServers.push_back(com::Utf8Str(pszServerAddress));
        }
    }

    CFArrayRef searchArrayRef = (CFArrayRef)CFDictionaryGetValue(
      static_cast<CFDictionaryRef>(propertyRef), CFSTR("SearchDomains"));
    if (searchArrayRef)
    {
        arrayCount = CFArrayGetCount(searchArrayRef);
        
        for (i = 0; i < arrayCount; ++i)
        {
            CFStringRef searchStringRef = (CFStringRef)CFArrayGetValueAtIndex(searchArrayRef, i);
            if (!searchArrayRef)
                continue;

            const char *pszSearchString = CFStringGetCStringPtr(searchStringRef, 
                                                          CFStringGetSystemEncoding());
            if (!pszSearchString)
                continue;
            
            m_llSearchStrings.push_back(com::Utf8Str(pszSearchString));
        }
    }

    CFRelease(propertyRef);
    return S_OK;
}
