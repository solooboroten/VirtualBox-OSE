/* $Id: initterm-r0drv-solaris.c 52665 2009-09-22 12:33:08Z ramshankar $ */
/** @file
 * IPRT - Initialization & Termination, R0 Driver, Solaris.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 USA or visit http://www.sun.com if you need
 * additional information or have any questions.
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include "the-solaris-kernel.h"
#include <iprt/err.h>
#include <iprt/assert.h>
#include "internal/initterm.h"


int g_VBoxIsNevada = 0;

#if (ARCH_BITS == 64)
/* CPU */
static int g_VBoxOff_S10_cpu_runrun   = 232;
static int g_VBoxOff_S10_cpu_kprunrun = 233;
/* kthread_t */
static int g_VBoxOff_S10_t_preempt    = 42;

/* 64-bit Solaris 11 (Nevada/OpenSolaris) offsets */
/* CPU */
static int g_VBoxOff_S11_cpu_runrun   = 216;
static int g_VBoxOff_S11_cpu_kprunrun = 217;
/* kthread_t */
static int g_VBoxOff_S11_t_preempt    = 42;

#else

/* 32-bit Solaris 10 offsets */
/* CPU */
static int g_VBoxOff_S10_cpu_runrun   = 124;
static int g_VBoxOff_S10_cpu_kprunrun = 125;
/* kthread_t */
static int g_VBoxOff_S10_t_preempt    = 26;

/* 32-bit Solaris 11 (Nevada/OpenSolaris) offsets */
/* CPU */
static int g_VBoxOff_S11_cpu_runrun   = 112;
static int g_VBoxOff_S11_cpu_kprunrun = 113;
/* kthread_t */
static int g_VBoxOff_S11_t_preempt    = 26;
#endif

/* Which offsets will be used */
int g_VBoxOff_cpu_runrun       = -1;
int g_VBoxOff_cpu_kprunrun     = -1;
int g_VBoxOff_t_preempt        = -1;

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/


int rtR0InitNative(void)
{
	/*
	 * Check if this is S10 or Nevada
	 */
	if (!strncmp(utsname.release, "5.11", sizeof("5.11") - 1))
	{
		/* Nevada detected... */
		g_VBoxIsNevada = 1;

		g_VBoxOff_cpu_runrun = g_VBoxOff_S11_cpu_runrun;
		g_VBoxOff_cpu_kprunrun = g_VBoxOff_S11_cpu_kprunrun;
		g_VBoxOff_t_preempt = g_VBoxOff_S11_t_preempt;
	}
	else
	{
		/* Solaris 10 detected... */
		g_VBoxIsNevada = 0;

		g_VBoxOff_cpu_runrun = g_VBoxOff_S10_cpu_runrun;
		g_VBoxOff_cpu_kprunrun = g_VBoxOff_S10_cpu_kprunrun;
		g_VBoxOff_t_preempt = g_VBoxOff_S10_t_preempt;
	}

	/*
	 * Sanity checking...
	 */
	/* CPU */
	char crr = VBOX_CPU_RUNRUN;
	char krr = VBOX_CPU_KPRUNRUN;
	if (   (crr < 0 || crr > 1)
		|| (krr < 0 || krr > 1))
	{
		cmn_err(CE_NOTE, ":CPU structure sanity check failed! OS version mismatch.\n");
		return VERR_VERSION_MISMATCH;
	}

	/* Thread */
	char t_preempt = VBOX_T_PREEMPT;
	if (t_preempt < 0 || t_preempt > 32)
	{
		cmn_err(CE_NOTE, ":Thread structure sanity check failed! OS version mismatch.\n");
		return VERR_VERSION_MISMATCH;
	}

    return VINF_SUCCESS;
}


void rtR0TermNative(void)
{
}
