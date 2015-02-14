/*
 * linux/arch/arm/mach-mmp/include/mach/help_v7.h
 *
 * Author:	Fangsuo Wu <fswu@marvell.com>
 * Copyright:	(C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MMP_MACH_HELP_V7_H__
#define __MMP_MACH_HELP_V7_H__

#include <asm/cp15.h>
#include <asm/cputype.h>

#ifdef CONFIG_SMP
static inline void core_exit_coherency(void)
{
	unsigned int v;
	asm volatile(
	"       mrc     p15, 0, %0, c1, c0, 1\n"
	"       bic     %0, %0, #(1 << 6)\n"
	"       mcr     p15, 0, %0, c1, c0, 1\n"
	: "=&r" (v) : : "cc");
	isb();
}

static inline void core_enter_coherency(void)
{
	unsigned int v;
	asm volatile(
	"       mrc     p15, 0, %0, c1, c0, 1\n"
	"       orr     %0, %0, #(1 << 6)\n"
	"       mcr     p15, 0, %0, c1, c0, 1\n"
	: "=&r" (v) : : "cc");
	isb();
}
#endif

/*
 * When SCTLR.C is set to 0 behavior is implementation defined:
 * ARM CPU's like Cortex-A7 only disable allocation into D-cache,
 * but existing lines still hit in L1-D and L2.
 * However some CPU's like Whitney-MP make all accesses non-cacheable
 * once SCTLR.C is cleared. This presents some challenges.
 * - ca7_power_down* below are inlined in the mmp_pm_down C function,
 * so if the function returns (skip_wfi case), the stack frame should
 * be available. If we only cleaned L1-D the frame will be dirty in L2
 * and non-cached access will miss L2 and read wrong content from RAM.
 * - cache maintenance functions have stack frames too, which are written
 * into the RAM. If we only clean L1, the dirty content in the location
 * of these stack frames are cleaned into L2, and do not overwrite the
 * frame in RAM. However, if we also clean L2. this dirty contents will
 * corrupt the stack frame in RAM causing a crash on function return.
 * Solution: clean the stack area (of arbitrary size) to PoC (RAM)
 * to prevent both issues above.
 */
static inline void clean_cache_stack(void)
{
#ifdef CONFIG_ARM_DC_DIS_NOHIT
	unsigned int v, l, m;
	asm volatile(
	/* see dcache_line_size */
	"	mrc	p15, 0, %0, c0, c0, 1 @ read CTR\n"
	"	lsr	%0, %0, #16\n"
	"	and	%0, %0, #0xf @ cache line size encoding\n"
	"	mov	%1, #4	@ bytes per word\n"
	"	mov	%1, %1, lsl %0\n"
	"	add	%2, sp, #0x100\n"
	"	sub	%0, sp, #0x100\n"
	"1:	mcr	p15, 0, %0, c7, c14, 1\n"
	"	add	%0, %0, %1\n"
	"	cmp	%0, %2\n"
	"	blt	1b\n"
	: "=&r" (v), "=&r" (l), "=&r" (m) : : "memory", "cc");
#endif
}

static inline void disable_l1_dcache(void)
{
	unsigned int v;
	asm volatile(
	"       mrc     p15, 0, %0, c1, c0, 0\n"
	"       bic     %0, %0, %1\n"
	"       mcr     p15, 0, %0, c1, c0, 0\n"
#ifdef CONFIG_ARM_ERRATA_794322
	"	ldr	%0, =_stext\n"
	"	mcr     p15, 0, %0, c8, c7, 1\n"
	"	dsb\n"
#endif
	: "=&r" (v) : "Ir" (CR_C) : "cc");
	isb();
}

static inline void enable_l1_dcache(void)
{
	unsigned int v;
	asm volatile(
	"       mrc     p15, 0, %0, c1, c0, 0\n"
	"       orr     %0, %0, %1\n"
	"       mcr     p15, 0, %0, c1, c0, 0\n"
	: "=&r" (v) : "Ir" (CR_C) : "cc");
	isb();
}

static inline void ca7_power_down(void)
{
	clean_cache_stack();
	disable_l1_dcache();
	flush_cache_louis();
	asm volatile("clrex\n");
	core_exit_coherency();
}

static inline void ca7_power_down_udr(void)
{
	clean_cache_stack();
	disable_l1_dcache();
	flush_cache_louis();
	asm volatile("clrex\n");
	flush_cache_all();
	core_exit_coherency();
}
#endif
