/* $Id: VBoxVMInfoNet.cpp 13462 2008-10-22 06:46:45Z vboxsync $ */
/** @file
 * VBoxVMInfoNet - Network information for the host.
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
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */

#include "VBoxService.h"
#include "VBoxVMInfo.h"
#include "VBoxVMInfoNet.h"

int vboxVMInfoNet(VBOXINFORMATIONCONTEXT* a_pCtx)
{
    SOCKET sd = WSASocket(AF_INET, SOCK_DGRAM, 0, 0, 0, 0);
    if (sd == SOCKET_ERROR)
    {
        Log(("vboxVMInfoThread: Failed to get a socket: Error %d\n", WSAGetLastError()));
        return -1;
    }

    INTERFACE_INFO InterfaceList[20];
    unsigned long nBytesReturned;
    if (    WSAIoctl(sd, SIO_GET_INTERFACE_LIST, 0, 0, &InterfaceList,
                     sizeof(InterfaceList), &nBytesReturned, 0, 0)
        ==  SOCKET_ERROR)
    {
        Log(("vboxVMInfoThread: Failed calling WSAIoctl: Error: %d\n", WSAGetLastError()));
        return -1;
    }

    char szPropPath [_MAX_PATH+1] = {0};
    char szTemp [_MAX_PATH+1] = {0};
    int nNumInterfaces = nBytesReturned / sizeof(INTERFACE_INFO);
    int iCurIface = 0;

    RTStrPrintf(szPropPath, sizeof(szPropPath), "GuestInfo/Net/Count");
    vboxVMInfoWritePropInt(a_pCtx, szPropPath, (nNumInterfaces > 1 ? nNumInterfaces-1 : 0));

    /* Later: Use GetAdaptersInfo() and GetAdapterAddresses (IPv4 + IPv6) for more information. */

    for (int i = 0; i < nNumInterfaces; ++i)
    {
        if (InterfaceList[i].iiFlags & IFF_LOOPBACK)    /* Skip loopback device. */
            continue;

        sockaddr_in *pAddress;
        pAddress = (sockaddr_in *) & (InterfaceList[i].iiAddress);
        RTStrPrintf(szPropPath, sizeof(szPropPath), "GuestInfo/Net/%d/V4/IP", iCurIface);
        vboxVMInfoWriteProp(a_pCtx, szPropPath, inet_ntoa(pAddress->sin_addr));

        pAddress = (sockaddr_in *) & (InterfaceList[i].iiBroadcastAddress);
        RTStrPrintf(szPropPath, sizeof(szPropPath), "GuestInfo/Net/%d/V4/Broadcast", iCurIface);
        vboxVMInfoWriteProp(a_pCtx, szPropPath, inet_ntoa(pAddress->sin_addr));

        pAddress = (sockaddr_in *) & (InterfaceList[i].iiNetmask);
        RTStrPrintf(szPropPath, sizeof(szPropPath), "GuestInfo/Net/%d/V4/Netmask", iCurIface);
        vboxVMInfoWriteProp(a_pCtx, szPropPath, inet_ntoa(pAddress->sin_addr));

        u_long nFlags = InterfaceList[i].iiFlags;
        if (nFlags & IFF_UP)
            RTStrPrintf(szTemp, sizeof(szTemp), "Up");
        else
            RTStrPrintf(szTemp, sizeof(szTemp), "Down");

        RTStrPrintf(szPropPath, sizeof(szPropPath), "GuestInfo/Net/%d/Status", iCurIface);
        vboxVMInfoWriteProp(a_pCtx, szPropPath, szTemp);

        iCurIface++;
    }

    closesocket(sd);

    return 0;
}

