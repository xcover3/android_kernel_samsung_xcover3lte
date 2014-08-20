/*
 * linux/arch/arm/mach-mmp/pxa1936_lowpower.c
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
#include <linux/clk/dvfs-dvc.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_address.h>
#include <linux/edge_wakeup_mmp.h>
#include <linux/clk/mmpdcstat.h>
#include <linux/of.h>
#include <linux/irqchip/arm-gic.h>
#include <asm/mcpm.h>
#include <asm/mcpm_plat.h>
#include <asm/cputype.h>

#include "regs-addr.h"
#include "pxa1936_lowpower.h"

#define MAX_CPU	4
#define MAX_CLUS 2
static void __iomem *apmu_virt_addr;
static void __iomem *mpmu_virt_addr;
static void __iomem *APMU_CORE_IDLE_CFG[MAX_CPU*MAX_CLUS];
static void __iomem *APMU_MP_IDLE_CFG[MAX_CPU*MAX_CLUS];
static void __iomem *ICU_GBL_INT_MSK[MAX_CPU*MAX_CLUS];
static u32 s_awucrm;

/* Registers for different CPUs are quite scattered */
static const unsigned APMU_CORE_IDLE_CFG_OFFS[] = {
	0x124, 0x128, 0x160, 0x164, 0x304, 0x308, 0x30c, 0x310
};
static const unsigned APMU_MP_IDLE_CFG_OFFS[] = {
	0x120, 0xe4, 0x150, 0x154, 0x314, 0x318, 0x31c, 0x320
};

static const unsigned ICU_GBL_INT_MSK_OFFS[] = {
	0x228, 0x238, 0x248, 0x258, 0x278, 0x288, 0x298, 0x2a8
};

#define RINDEX(cpu, clus) ((cpu) + (clus)*MAX_CPU)

static struct cpuidle_state pxa1936_modes[] = {
	[0] = {
		.exit_latency		= 18,
		.target_residency	= 36,
		/*
		 * Use CPUIDLE_FLAG_TIMER_STOP flag to let the cpuidle
		 * framework handle the CLOCK_EVENT_NOTIFY_BROADCAST_
		 * ENTER/EXIT when entering idle states.
		 */
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "C1: Core internal clock gated",
		.enter			= cpuidle_simple_enter,
	},
	[1] = {
		.exit_latency		= 350,
		.target_residency	= 700,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "C2",
		.desc			= "C2: Core power down",
	},
	[2] = {
		.exit_latency		= 450,
		.target_residency	= 900,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "MP2",
		.desc			= "MP2: Core subsystem power down",
	},
	[3] = {
		.exit_latency		= 500,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "D1p",
		.desc			= "D1p: AP idle state",
	},
	[4] = {
		.exit_latency		= 600,
		.target_residency	= 1200,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "D1",
		.desc			= "D1: Chip idle state",
	},
};

static void pxa1936_set_dstate(u32 cpu, u32 cluster, u32 power_mode)
{
}

static void pxa1936_set_cstate(u32 cpu, u32 cluster, u32 power_mode)
{
	unsigned cfg = readl(APMU_CORE_IDLE_CFG[RINDEX(cpu, cluster)]);
	unsigned mcfg = readl(APMU_MP_IDLE_CFG[RINDEX(cpu, cluster)]);
	cfg &= ~(CORE_PWRDWN | CORE_IDLE);
	if (power_mode > POWER_MODE_CORE_POWERDOWN)
		power_mode = POWER_MODE_CORE_POWERDOWN;
	switch (power_mode) {
	case POWER_MODE_MP_POWERDOWN:
		/* fall through */
	case POWER_MODE_CORE_POWERDOWN:
		mcfg |= MP_PWRDWN | MP_IDLE;
		cfg |= CORE_IDLE | CORE_PWRDWN;
		cfg |= MASK_GIC_nFIQ_TO_CORE | MASK_GIC_nIRQ_TO_CORE;
		/* fall through */

	case POWER_MODE_CORE_INTIDLE:
		/* cfg |= CORE_IDLE; */
		break;
	}
	writel(cfg, APMU_CORE_IDLE_CFG[RINDEX(cpu, cluster)]);
	writel(mcfg, APMU_MP_IDLE_CFG[RINDEX(cpu, cluster)]);
}


static void pxa1936_clear_state(u32 cpu, u32 cluster)
{
	int idx = RINDEX(cpu, cluster);
	unsigned cfg = readl(APMU_CORE_IDLE_CFG[idx]);
	cfg &= ~(CORE_PWRDWN | CORE_IDLE);
	cfg &= ~(MASK_GIC_nFIQ_TO_CORE | MASK_GIC_nIRQ_TO_CORE);
	writel(cfg, APMU_CORE_IDLE_CFG[idx]);

	cfg = readl(APMU_MP_IDLE_CFG[idx]);
	cfg &= ~(MP_PWRDWN | MP_IDLE);
	writel(cfg, APMU_MP_IDLE_CFG[idx]);
}

static void pxa1936_icu_global_mask(u32 cpu, u32 cluster, u32 mask)
{
	u32 icu_msk;
	int idx = RINDEX(cpu, cluster);

	icu_msk = readl_relaxed(ICU_GBL_INT_MSK[idx]);

	if (mask)
		icu_msk |= ICU_MASK_FIQ | ICU_MASK_IRQ;
	else
		icu_msk &= ~(ICU_MASK_FIQ | ICU_MASK_IRQ);

	writel_relaxed(icu_msk, ICU_GBL_INT_MSK[idx]);
}
/*
 * low power config
 */
static void pxa1936_lowpower_config(u32 cpu, u32 cluster, u32 power_mode,
				u32 vote_state,
				u32 lowpower_enable)
{
	u32 c_state;

	/* clean up register setting */
	if (!lowpower_enable) {
		pxa1936_clear_state(cpu, cluster);
		return;
	}

	if (power_mode >= POWER_MODE_APPS_IDLE) {
		pxa1936_set_dstate(cpu, cluster, power_mode);
		c_state = POWER_MODE_MP_POWERDOWN;
	} else
		c_state = power_mode;

	pxa1936_set_cstate(cpu, cluster, c_state);
}

#define DISABLE_ALL_WAKEUP_PORTS		\
	(PMUM_SLPWP0 | PMUM_SLPWP1 | PMUM_SLPWP2 | PMUM_SLPWP3 |	\
	 PMUM_SLPWP4 | PMUM_SLPWP5 | PMUM_SLPWP6 | PMUM_SLPWP7)
/* Here we don't enable CP wakeup sources since CP will enable them */
#define ENABLE_AP_WAKEUP_SOURCES	\
	(PMUM_AP_ASYNC_INT | PMUM_AP_FULL_IDLE | PMUM_SQU_SDH1 | PMUM_SDH_23 | \
	 PMUM_KEYPRESS | PMUM_WDT | PMUM_RTC_ALARM | PMUM_AP0_2_TIMER_1 | \
	 PMUM_AP0_2_TIMER_2 | PMUM_AP0_2_TIMER_3 | PMUM_AP1_TIMER_1 | \
	 PMUM_AP1_TIMER_2 | PMUM_AP1_TIMER_3 | PMUM_WAKEUP7 | PMUM_WAKEUP6 | \
	 PMUM_WAKEUP5 | PMUM_WAKEUP4 | PMUM_WAKEUP3 | PMUM_WAKEUP2)
/*
 * Enable AP wakeup sources and ports. To enalbe wakeup
 * ports, it needs both AP side to configure MPMU_APCR
 * and CP side to configure MPMU_CPCR to really enable
 * it. To enable wakeup sources, either AP side to set
 * MPMU_AWUCRM or CP side to set MPMU_CWRCRM can really
 * enable it.
 */
static void pxa1936_save_wakeup(void)
{
	s_awucrm = readl_relaxed(mpmu_virt_addr + AWUCRM);
	writel_relaxed(s_awucrm | ENABLE_AP_WAKEUP_SOURCES,
			mpmu_virt_addr + AWUCRM);
}

static void pxa1936_restore_wakeup(void)
{
	writel_relaxed(s_awucrm,
			mpmu_virt_addr + AWUCRM);
}

static void pxa1936_set_pmu(u32 cpu, u32 calc_state, u32 vote_state)
{
	u32 mpidr = read_cpuid_mpidr();
	u32 cluster;
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pxa1936_lowpower_config(cpu, cluster, calc_state, vote_state, 1);
	pxa1936_icu_global_mask(cpu, cluster, 1); /* might be reset by HW */
}

static void pxa1936_clr_pmu(u32 cpu)
{
	u32 mpidr = read_cpuid_mpidr();
	u32 cluster;
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pxa1936_lowpower_config(cpu, cluster, 0, 0, 0);
	pxa1936_icu_global_mask(cpu, cluster, 1); /* might be reset by HW */
}

static struct platform_power_ops pxa1936_power_ops = {
	.set_pmu	= pxa1936_set_pmu,
	.clr_pmu	= pxa1936_clr_pmu,
	.save_wakeup	= pxa1936_save_wakeup,
	.restore_wakeup	= pxa1936_restore_wakeup,
};

static struct platform_idle pxa1936_idle = {
	.cpudown_state		= POWER_MODE_CORE_POWERDOWN,
	.clusterdown_state	= POWER_MODE_MP_POWERDOWN,
	.wakeup_state		= POWER_MODE_APPS_IDLE,
	.hotplug_state		= POWER_MODE_UDR,
	/*
	 * l2_flush_state indicates to the logic in mcpm_plat.c
	 * to trigger calls to save_wakeup/restore_wakeup.
	 */
	.l2_flush_state		= POWER_MODE_UDR,
	.ops			= &pxa1936_power_ops,
	.states			= pxa1936_modes,
	.state_count		= ARRAY_SIZE(pxa1936_modes),
};

static const int intc_wakeups[] = {
	2,
	13,
	14,
	27,
	28,
	29,
	30
};
#define UNMASK_WAKE_VAL 0x51 /* CPU0, IRQ, PRIO=1 */
static void __iomem *icu_init(void)
{
	void __iomem *icu_virt_addr;
	int i;

	icu_virt_addr = regs_addr_get_va(REGS_ADDR_ICU);

	for (i = 0; i < ARRAY_SIZE(intc_wakeups); i++)
		writel(UNMASK_WAKE_VAL, icu_virt_addr + intc_wakeups[i]*4);
	return icu_virt_addr;
}

static void __init pxa1936_mappings(void)
{
	int i;
	void __iomem *icu_virt_addr = icu_init();

	apmu_virt_addr = regs_addr_get_va(REGS_ADDR_APMU);

	for (i = 0; i < (MAX_CPU*MAX_CLUS); i++) {
		APMU_CORE_IDLE_CFG[i] =
			apmu_virt_addr + APMU_CORE_IDLE_CFG_OFFS[i];
		APMU_MP_IDLE_CFG[i] =
			apmu_virt_addr + APMU_MP_IDLE_CFG_OFFS[i];
		ICU_GBL_INT_MSK[i] =
			icu_virt_addr + ICU_GBL_INT_MSK_OFFS[i];
	}

	mpmu_virt_addr = regs_addr_get_va(REGS_ADDR_MPMU);
}

static int __init pxa1936_lowpower_init(void)
{
	if (!of_machine_is_compatible("marvell,pxa1936"))
		return -ENODEV;

	pr_err("Initialize pxa1936 low power controller.\n");

	pxa1936_mappings();
	mcpm_plat_power_register(&pxa1936_idle);

	return 0;
}
early_initcall(pxa1936_lowpower_init);
