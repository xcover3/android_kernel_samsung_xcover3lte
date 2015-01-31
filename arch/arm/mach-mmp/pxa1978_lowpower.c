/*
 * linux/arch/arm/mach-mmp/pxa1978_lowpower.c
 *
 * Author:	Raul Xiong <xjian@marvell.com>
 *		Fangsuo Wu <fswu@marvell.com>
 * Copyright:	(C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/edge_wakeup_mmp.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <asm/cpuidle.h>
#include <asm/mach/map.h>
#include <asm/mcpm.h>
#include <mach/addr-map.h>
#include <linux/cputype.h>
#include <mach/mmp_cpuidle.h>

#include "regs-addr.h"

enum pxa1978_lowpower_state {
	POWER_MODE_CORE_INTIDLE,	/* used for C1 */
	POWER_MODE_CORE_POWERDOWN,	/* used for C2 */
	POWER_MODE_APPS_IDLE,		/* used for D1P */
	POWER_MODE_SYS_SLEEP,		/* used for non-udr chip sleep, D1 */
	POWER_MODE_UDR_VCTCXO,		/* used for udr with vctcxo, D2 */
	POWER_MODE_UDR,			/* used for udr D2, suspend */
	POWER_MODE_MAX = 15,		/* maximum lowpower states */
};

static struct cpuidle_state pxa1978_modes[] = {
	[POWER_MODE_CORE_INTIDLE] = {
		.exit_latency		= 18,
		.target_residency	= 36,
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "C1: Core internal clock gated",
		.enter			= arm_cpuidle_simple_enter,
	},
#if (0)
	[POWER_MODE_CORE_POWERDOWN] = {
		.exit_latency		= 350,
		.target_residency	= 700,
		/*
		 * Use CPUIDLE_FLAG_TIMER_STOP flag to let the cpuidle
		 * framework handle the CLOCK_EVENT_NOTIFY_BROADCAST_
		 * ENTER/EXIT when entering idle states.
		 */
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "C2",
		.desc			= "C2: Core power down",
	},
	[POWER_MODE_APPS_IDLE] = {
		.exit_latency		= 500,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "D1p",
		.desc			= "D1p: AP idle state",
	},
	[POWER_MODE_SYS_SLEEP] = {
		.exit_latency		= 600,
		.target_residency	= 1200,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "D1",
		.desc			= "D1: Chip idle state",
	},
#endif
};

/* All pxa988_lowpower.c code has been removed: look in the original file */
static void pxa1978_set_pmu(u32 cpu, u32 power_mode)
{
}

static void pxa1978_clr_pmu(u32 cpu)
{
}

static struct platform_power_ops pxa1978_power_ops = {
	.set_pmu	= pxa1978_set_pmu,
	.clr_pmu	= pxa1978_clr_pmu,
};

static struct platform_idle pxa1978_idle = {
	.cpudown_state	= POWER_MODE_CORE_POWERDOWN,
	.wakeup_state	= POWER_MODE_SYS_SLEEP,
	.hotplug_state	= POWER_MODE_UDR,
	.l2_flush_state	= POWER_MODE_UDR,
	.ops		= &pxa1978_power_ops,
	.states		= pxa1978_modes,
	.state_count	= ARRAY_SIZE(pxa1978_modes),
};

static int __init pxa1978_lowpower_init(void)
{
	mmp_platform_power_register(&pxa1978_idle);
	return 0;
}
early_initcall(pxa1978_lowpower_init);
