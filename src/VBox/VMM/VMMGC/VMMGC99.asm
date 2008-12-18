; $Id: VMMGC99.asm 13813 2008-11-04 21:55:34Z vboxsync $
;; @file
; VMMGC99 - The last object module in the link.
;

; Copyright (C) 2006-2007 Sun Microsystems, Inc.
;
; This file is part of VirtualBox Open Source Edition (OSE), as
; available from http://www.virtualbox.org. This file is free software;
; you can redistribute it and/or modify it under the terms of the GNU
; General Public License (GPL) as published by the Free Software
; Foundation, in version 2 as it comes in the "COPYING" file of the
; VirtualBox OSE distribution. VirtualBox OSE is distributed in the
; hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
;
; Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
; Clara, CA 95054 USA or visit http://www.sun.com if you need
; additional information or have any questions.
;

%include "VMMGC.mac"


;;
; End the Trap0b segment.
VMMR0_SEG Trap0b
GLOBALNAME g_aTrap0bHandlersEnd
    dd 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0


;;
; End the Trap0d segment.
VMMR0_SEG Trap0d
GLOBALNAME g_aTrap0dHandlersEnd
    dd 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0


;;
; End the Trap0e segment.
VMMR0_SEG Trap0e
GLOBALNAME g_aTrap0eHandlersEnd
    dd 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

