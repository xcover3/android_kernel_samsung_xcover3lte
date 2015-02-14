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

#define MAX_CPU	4

static void __iomem *apmu_virt_addr;
static void __iomem *mpmu_virt_addr;
static void __iomem *APMU_CORE_IDLE_CFG[MAX_CPU];
static void __iomem *APMU_PCR[MAX_CPU];
static void __iomem *APMU_INT_MASK[MAX_CPU];

/* Registers for different CPUs are quite scattered */
static const unsigned APMU_CORE_IDLE_CFG_OFFS[] = {
	0x34, 0x38, 0x3c, 0x40
};

static const unsigned APMU_PCR_OFFS[] = {
	0x0, 0x4, 0x8, 0xc
};

static const unsigned APMU_INT_MASK_OFFS[] = {
	0x128, 0x12c, 0x130, 0x134
};

static const unsigned APMU_CORE_RESET_OFFS[] = {
	0x18, 0x1c, 0x20, 0x24
};


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
#if (0)
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

/*
 * mcpm_plat_get_cpuidle_states:
 * - referred from cpuidle-mmp-armv7.c (recently aligned with v8 for aarch32.
 * - implemented in arch/arm64/mcpm/mcpm_plat.c
 * mcpm_platform_state_register:
 * - referred from arch/arm/mach-mmp/mmp_cpuidle.c
 * - not implemented
 * Therefore, implement here until we align to pxa1908-aarch32 design
 * which takes modules from arch/arm64 (see arch/arm/mach-mmp/Makfile).
 */
int __init mcpm_plat_get_cpuidle_states(struct cpuidle_state *states)
{
	int i;

	if (!states)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(pxa1978_modes); i++)
		memcpy(&states[i], &pxa1978_modes[i], sizeof(states[0]));

	return i;
}
int __init mcpm_platform_state_register(struct cpuidle_state *states,
					int count)
{
	return count;
}

static void pxa1978_set_dstate(u32 cpu, u32 power_mode)
{
	(void)cpu;
	(void)power_mode;
}

static void pxa1978_set_cstate(u32 cpu, u32 power_mode)
{
	unsigned cfg = readl_relaxed(APMU_CORE_IDLE_CFG[cpu]);
	unsigned mask = readl_relaxed(APMU_INT_MASK[cpu]);

	if (power_mode > POWER_MODE_CORE_POWERDOWN)
		power_mode = POWER_MODE_CORE_POWERDOWN;

	if (power_mode == POWER_MODE_CORE_POWERDOWN) {
		cfg |= 1;
		mask |= 1;
	} else {
		cfg &= ~1;
		mask &= ~1;
	}
	writel_relaxed(cfg, APMU_CORE_IDLE_CFG[cpu]);
	writel_relaxed(mask, APMU_INT_MASK[cpu]);
}


static void pxa1978_clear_state(u32 cpu)
{
	pxa1978_set_cstate(cpu, 0);
	pxa1978_set_dstate(cpu, 0);
}

static void pxa1978_lowpower_config(u32 cpu, u32 power_mode,
				u32 lowpower_enable)
{
	u32 c_state;

	/* clean up register setting */
	if (!lowpower_enable) {
		pxa1978_clear_state(cpu);
		return;
	}

	if (power_mode >= POWER_MODE_APPS_IDLE) {
		pxa1978_set_dstate(cpu, power_mode);
		c_state = POWER_MODE_CORE_POWERDOWN;
	} else
		c_state = power_mode;

	pxa1978_set_cstate(cpu, c_state);
}


static void pxa1978_set_pmu(u32 cpu, u32 power_mode)
{
	pxa1978_lowpower_config(cpu, power_mode, 1);
}

static void pxa1978_clr_pmu(u32 cpu)
{
	pxa1978_lowpower_config(cpu, 0, 0);
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
static void __init pxa1978_mappings(void)
{
	int i;

	apmu_virt_addr = regs_addr_get_va(REGS_ADDR_APMU);

	for (i = 0; i < MAX_CPU; i++) {
		APMU_CORE_IDLE_CFG[i] =
			apmu_virt_addr + APMU_CORE_IDLE_CFG_OFFS[i];
		APMU_PCR[i] =
			apmu_virt_addr + APMU_PCR_OFFS[i];
		APMU_INT_MASK[i] = apmu_virt_addr + APMU_INT_MASK_OFFS[i];
	}

	mpmu_virt_addr = regs_addr_get_va(REGS_ADDR_MPMU);
}

static int __init pxa1978_lowpower_init(void)
{
	if (!of_machine_is_compatible("marvell,pxa1978"))
		return -ENODEV;

	pxa1978_mappings();
	mmp_platform_power_register(&pxa1978_idle);
	return 0;
}
early_initcall(pxa1978_lowpower_init);
