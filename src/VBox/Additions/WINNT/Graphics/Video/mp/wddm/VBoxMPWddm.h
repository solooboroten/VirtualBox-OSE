/* $Id: VBoxMPWddm.h $ */
/** @file
 * VBox WDDM Miniport driver
 */

/*
 * Copyright (C) 2011-2013 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef ___VBoxMPWddm_h___
#define ___VBoxMPWddm_h___

#ifdef VBOX_WDDM_WIN8
# define VBOX_WDDM_DRIVERNAME L"VBoxVideoW8"
#else
# define VBOX_WDDM_DRIVERNAME L"VBoxVideoWddm"
#endif

#ifndef DEBUG_misha
# ifdef Assert
#  error "VBoxMPWddm.h must be included first."
# endif
# define RT_NO_STRICT
#endif
#include "common/VBoxMPUtils.h"
#include "common/VBoxMPDevExt.h"
#include "../../common/VBoxVideoTools.h"

//#define VBOXWDDM_DEBUG_VIDPN

#define VBOXWDDM_CFG_DRV_DEFAULT                        0
#define VBOXWDDM_CFG_DRV_SECONDARY_TARGETS_CONNECTED    1

#define VBOXWDDM_CFG_DRVTARGET_CONNECTED                1

#define VBOXWDDM_CFG_LOG_UM_BACKDOOR 0x00000001
#define VBOXWDDM_CFG_LOG_UM_DBGPRINT 0x00000002
#define VBOXWDDM_CFG_STR_LOG_UM L"VBoxLogUm"

#define VBOXWDDM_REG_DRV_FLAGS_NAME L"VBoxFlags"
#define VBOXWDDM_REG_DRV_DISPFLAGS_PREFIX L"VBoxDispFlags"

#define VBOXWDDM_REG_DRVKEY_PREFIX L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Class\\"

#define VBOXWDDM_REG_DISPLAYSETTINGSVIDEOKEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Video\\"
#define VBOXWDDM_REG_DISPLAYSETTINGSVIDEOKEY_SUBKEY L"\\Video"


#define VBOXWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_VISTA L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\Current\\System\\CurrentControlSet\\Control\\VIDEO\\"
#define VBOXWDDM_REG_DISPLAYSETTINGSKEY_PREFIX_WIN7 L"\\Registry\\Machine\\System\\CurrentControlSet\\Hardware Profiles\\UnitedVideo\\CONTROL\\VIDEO\\"

#define VBOXWDDM_REG_DISPLAYSETTINGS_ATTACH_RELX L"Attach.RelativeX"
#define VBOXWDDM_REG_DISPLAYSETTINGS_ATTACH_RELY L"Attach.RelativeY"
#define VBOXWDDM_REG_DISPLAYSETTINGS_ATTACH_DESKTOP L"Attach.ToDesktop"

extern DWORD g_VBoxLogUm;

RT_C_DECLS_BEGIN
NTSTATUS DriverEntry(IN PDRIVER_OBJECT DriverObject, IN PUNICODE_STRING RegistryPath);
RT_C_DECLS_END

PVOID vboxWddmMemAlloc(IN SIZE_T cbSize);
PVOID vboxWddmMemAllocZero(IN SIZE_T cbSize);
VOID vboxWddmMemFree(PVOID pvMem);

NTSTATUS vboxWddmCallIsr(PVBOXMP_DEVEXT pDevExt);

DECLINLINE(PVBOXWDDM_RESOURCE) vboxWddmResourceForAlloc(PVBOXWDDM_ALLOCATION pAlloc)
{
#if 0
    if(pAlloc->iIndex == VBOXWDDM_ALLOCATIONINDEX_VOID)
        return NULL;
    PVBOXWDDM_RESOURCE pRc = (PVBOXWDDM_RESOURCE)(((uint8_t*)pAlloc) - RT_OFFSETOF(VBOXWDDM_RESOURCE, aAllocations[pAlloc->iIndex]));
    return pRc;
#else
    return pAlloc->pResource;
#endif
}

VOID vboxWddmAllocationDestroy(PVBOXWDDM_ALLOCATION pAllocation);

DECLINLINE(VOID) vboxWddmAllocationRelease(PVBOXWDDM_ALLOCATION pAllocation)
{
    uint32_t cRefs = ASMAtomicDecU32(&pAllocation->cRefs);
    Assert(cRefs < UINT32_MAX/2);
    if (!cRefs)
    {
        vboxWddmAllocationDestroy(pAllocation);
    }
}

DECLINLINE(VOID) vboxWddmAllocationRetain(PVBOXWDDM_ALLOCATION pAllocation)
{
    ASMAtomicIncU32(&pAllocation->cRefs);
}

DECLINLINE(BOOLEAN) vboxWddmAddrSetVram(PVBOXWDDM_ADDR pAddr, UINT SegmentId, VBOXVIDEOOFFSET offVram)
{
    if (pAddr->SegmentId == SegmentId && pAddr->offVram == offVram)
        return FALSE;

    pAddr->SegmentId = SegmentId;
    pAddr->offVram = offVram;
    return TRUE;
}

DECLINLINE(bool) vboxWddmAddrVramEqual(PVBOXWDDM_ADDR pAddr1, PVBOXWDDM_ADDR pAddr2)
{
    return pAddr1->SegmentId == pAddr2->SegmentId && pAddr1->offVram == pAddr2->offVram;
}

DECLINLINE(VBOXVIDEOOFFSET) vboxWddmVramAddrToOffset(PVBOXMP_DEVEXT pDevExt, PHYSICAL_ADDRESS Addr)
{
    PVBOXMP_COMMON pCommon = VBoxCommonFromDeviceExt(pDevExt);
    AssertRelease(pCommon->phVRAM.QuadPart <= Addr.QuadPart);
    return (VBOXVIDEOOFFSET)Addr.QuadPart - pCommon->phVRAM.QuadPart;
}

#ifdef VBOXWDDM_RENDER_FROM_SHADOW
DECLINLINE(void) vboxWddmAssignShadow(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, PVBOXWDDM_ALLOCATION pAllocation, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
#ifdef VBOX_WITH_CROGL
    if (pDevExt->fCmdVbvaEnabled)
    {
        WARN(("Trying to assign shadow surface for CmdVbva enabled mode!"));
        return;
    }
#endif

    if (pSource->pShadowAllocation == pAllocation)
    {
        Assert(pAllocation->bAssigned);
        return;
    }

    if (pSource->pShadowAllocation)
    {
        PVBOXWDDM_ALLOCATION pOldAlloc = pSource->pShadowAllocation;
        /* clear the visibility info fo the current primary */
        pOldAlloc->bVisible = FALSE;
        pOldAlloc->bAssigned = FALSE;
        Assert(pOldAlloc->AllocData.SurfDesc.VidPnSourceId == srcId);
        /* release the shadow surface */
        pOldAlloc->AllocData.SurfDesc.VidPnSourceId = D3DDDI_ID_UNINITIALIZED;
    }

    if (pAllocation)
    {
        Assert(!pAllocation->bAssigned);
        Assert(!pAllocation->bVisible);
        /* this check ensures the shadow is not used for other source simultaneously */
        Assert(pAllocation->AllocData.SurfDesc.VidPnSourceId == D3DDDI_ID_UNINITIALIZED);
        pAllocation->AllocData.SurfDesc.VidPnSourceId = srcId;
        pAllocation->bAssigned = TRUE;
        pAllocation->bVisible = pSource->bVisible;

        if(!vboxWddmAddrVramEqual(&pSource->AllocData.Addr, &pAllocation->AllocData.Addr))
        {
            pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
            pSource->AllocData.Addr = pAllocation->AllocData.Addr;
        }
        if (pSource->AllocData.hostID != pAllocation->AllocData.hostID)
        {
            pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
            pSource->AllocData.hostID = pAllocation->AllocData.hostID;
        }
    }
    else
        pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */

    pSource->pShadowAllocation = pAllocation;
}
#endif

DECLINLINE(VOID) vboxWddmAssignPrimary(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, PVBOXWDDM_ALLOCATION pAllocation, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    /* vboxWddmAssignPrimary can not be run in reentrant order, so safely do a direct unlocked check here */
    if (pSource->pPrimaryAllocation == pAllocation)
        return;

    if (pSource->pPrimaryAllocation)
    {
        PVBOXWDDM_ALLOCATION pOldAlloc = pSource->pPrimaryAllocation;
        /* clear the visibility info fo the current primary */
        pOldAlloc->bVisible = FALSE;
        pOldAlloc->bAssigned = FALSE;
        Assert(pOldAlloc->AllocData.SurfDesc.VidPnSourceId == srcId);

        vboxWddmAllocationRelease(pOldAlloc);
    }

    if (pAllocation)
    {
        Assert(pAllocation->AllocData.SurfDesc.VidPnSourceId == srcId);
        pAllocation->bAssigned = TRUE;
        pAllocation->bVisible = pSource->bVisible;

        if(!vboxWddmAddrVramEqual(&pSource->AllocData.Addr, &pAllocation->AllocData.Addr))
        {
            pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
            pSource->AllocData.Addr = pAllocation->AllocData.Addr;
        }
        if (pSource->AllocData.hostID != pAllocation->AllocData.hostID)
        {
            pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */
            pSource->AllocData.hostID = pAllocation->AllocData.hostID;
        }

        vboxWddmAllocationRetain(pAllocation);
    }
    else
        pSource->u8SyncState &= ~VBOXWDDM_HGSYNC_F_SYNCED_LOCATION; /* force guest->host notification */

    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->AllocationLock, &OldIrql);
    pSource->pPrimaryAllocation = pAllocation;
    KeReleaseSpinLock(&pSource->AllocationLock, OldIrql);
}

DECLINLINE(PVBOXWDDM_ALLOCATION) vboxWddmAquirePrimary(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource, D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId)
{
    PVBOXWDDM_ALLOCATION pPrimary;
    KIRQL OldIrql;
    KeAcquireSpinLock(&pSource->AllocationLock, &OldIrql);
    pPrimary = pSource->pPrimaryAllocation;
    if (pPrimary)
        vboxWddmAllocationRetain(pPrimary);
    KeReleaseSpinLock(&pSource->AllocationLock, OldIrql);
    return pPrimary;
}

bool vboxWddmGhDisplayCheckSetInfoFromSource(PVBOXMP_DEVEXT pDevExt, PVBOXWDDM_SOURCE pSource);

#ifdef VBOX_WITH_CROGL
#define VBOXWDDMENTRY_2_SWAPCHAIN(_pE) ((PVBOXWDDM_SWAPCHAIN)((uint8_t*)(_pE) - RT_OFFSETOF(VBOXWDDM_SWAPCHAIN, DevExtListEntry)))

BOOLEAN DxgkDdiInterruptRoutineNew(
    IN CONST PVOID MiniportDeviceContext,
    IN ULONG MessageNumber
    );
#endif

#ifdef VBOX_WDDM_WIN8
# define VBOXWDDM_IS_DISPLAYONLY() (g_VBoxDisplayOnly)
#else
# define VBOXWDDM_IS_DISPLAYONLY() (FALSE)
#endif

#ifdef VBOXWDDM_RENDER_FROM_SHADOW

# define VBOXWDDM_IS_FB_ALLOCATION(_pDevExt, _pAlloc) ( (_pAlloc)->bAssigned \
        && (  (_pAlloc)->AllocData.hostID \
           || (_pAlloc)->enmType == \
               ((VBOXWDDM_IS_DISPLAYONLY() || (_pDevExt)->fRenderToShadowDisabled) ? VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE : VBOXWDDM_ALLOC_TYPE_STD_SHADOWSURFACE) \
               ))

# define VBOXWDDM_IS_REAL_FB_ALLOCATION(_pDevExt, _pAlloc) ( (_pAlloc)->bAssigned \
        && (  (_pAlloc)->AllocData.hostID \
           || (_pAlloc)->enmType == VBOXWDDM_ALLOC_TYPE_STD_SHAREDPRIMARYSURFACE \
               ))

# define VBOXWDDM_FB_ALLOCATION(_pDevExt, _pSrc) ( ((_pSrc)->pPrimaryAllocation && VBOXWDDM_IS_FB_ALLOCATION(_pDevExt, (_pSrc)->pPrimaryAllocation)) ? \
                (_pSrc)->pPrimaryAllocation : ( \
                        ((_pSrc)->pShadowAllocation && VBOXWDDM_IS_FB_ALLOCATION(_pDevExt, (_pSrc)->pShadowAllocation)) ? \
                                (_pSrc)->pShadowAllocation : NULL \
                        ) \
                )
# define VBOXWDDM_NONFB_ALLOCATION(_pDevExt, _pSrc) ( !((_pSrc)->pPrimaryAllocation && VBOXWDDM_IS_FB_ALLOCATION(_pDevExt, (_pSrc)->pPrimaryAllocation)) ? \
                (_pSrc)->pPrimaryAllocation : ( \
                        ((_pSrc)->pShadowAllocation && VBOXWDDM_IS_FB_ALLOCATION(_pDevExt, (_pSrc)->pShadowAllocation)) ? \
                                (_pSrc)->pShadowAllocation : NULL \
                        ) \
                )
#else
# define VBOXWDDM_FB_ALLOCATION(_pDevExt, _pSrc) ((_pSrc)->pPrimaryAllocation)
#endif

#define VBOXWDDM_CTXLOCK_INIT(_p) do { \
        KeInitializeSpinLock(&(_p)->ContextLock); \
    } while (0)
#define VBOXWDDM_CTXLOCK_DATA KIRQL _ctxLockOldIrql;
#define VBOXWDDM_CTXLOCK_LOCK(_p) do { \
        KeAcquireSpinLock(&(_p)->ContextLock, &_ctxLockOldIrql); \
    } while (0)
#define VBOXWDDM_CTXLOCK_UNLOCK(_p) do { \
        KeReleaseSpinLock(&(_p)->ContextLock, _ctxLockOldIrql); \
    } while (0)

#endif /* #ifndef ___VBoxMPWddm_h___ */

