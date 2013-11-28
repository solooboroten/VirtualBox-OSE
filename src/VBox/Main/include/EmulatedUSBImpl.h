/* $Id: EmulatedUSBImpl.h $ */

/** @file
 *
 * Emulated USB devices manager.
 */

/*
 * Copyright (C) 2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef EMULATEDUSB_IMPL_H_
#define EMULATEDUSB_IMPL_H_

#include "VirtualBoxBase.h"
#include <vector>

class Console;
class EUSBWEBCAM;

typedef std::map<Utf8Str, EUSBWEBCAM *> WebcamsMap;

/* Not a COM object in 4.2 */
class EmulatedUSB
    : public Lockable /* For AutoLock. */
{
public:

    DECLARE_EMPTY_CTOR_DTOR(EmulatedUSB)

    virtual RWLockHandle *lockHandle() const;

    /* Public initializer/uninitializer for internal purposes only. */
    HRESULT init(Console *pConsole);
    void uninit();

    /* Public methods for internal use. */
    static DECLCALLBACK(int) eusbCallback(void *pv, const char *pszId, uint32_t iEvent,
                                          const void *pvData, uint32_t cbData);

    static DECLCALLBACK(int) eusbCallbackEMT(EmulatedUSB *pThis, char *pszId, uint32_t iEvent,
                                             void *pvData, uint32_t cbData);

    HRESULT webcamPathFromId(com::Utf8Str *pPath, const char *pszId);

    HRESULT getWebcams(std::vector<com::Utf8Str> &aWebcams);

    HRESULT webcamAttach(const com::Utf8Str &aPath,
                         const com::Utf8Str &aSettings);
    HRESULT webcamDetach(const com::Utf8Str &aPath);

    /* Data. */
    struct Data
    {
        Data()
            :
            pConsole(NULL),
            objectLock(NULL)
        {
        }

        Console *pConsole;
        WebcamsMap webcams;
        RWLockHandle *objectLock;
    };

    Data m;
};

#endif // EMULATEDUSB_IMPL_H_

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
