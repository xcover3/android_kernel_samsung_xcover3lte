/*
 * linux/arch/arm/mach-mmp/mmp_cpuidle.c
 *
 * Author:	Fangsuo Wu <fswu@marvell.com>
 * Copyright:	(C) 2013 marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/cpu_pm.h>
#include <linux/cpuidle.h>
#include <linux/init.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/kernel.h>
#include <asm/cputype.h>
#include <asm/mcpm.h>
#include <mach/mmp_cpuidle.h>

#include "reset.h"

static arch_spinlock_t mmp_lpm_lock = __ARCH_SPIN_LOCK_UNLOCKED;
static int mmp_pm_use_count[MAX_NR_CLUSTERS][MAX_CPUS_PER_CLUSTER];

/*
 * mmp_pm_down - Programs CPU to enter the specified state
 *
 * @addr: address points to the state selected by cpu governor
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static void mmp_pm_down(unsigned long addr)
{
	int mpidr, cpu, cluster;
	bool skip_wfi = false, last_man = false;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	pr_info("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= MAX_NR_CLUSTERS || cpu >= MAX_CPUS_PER_CLUSTER);

	__mcpm_cpu_going_down(cpu, cluster);

	arch_spin_lock(&mmp_lpm_lock);

	mmp_pm_use_count[cluster][cpu]--;

	if (mmp_pm_use_count[cluster][cpu] == 0) {
		/* TODO: add LPM code here. */
	} else if (mmp_pm_use_count[cluster][cpu] == 1) {
		/*
		 * A power_up request went ahead of us.
		 * Even if we do not want to shut this CPU down,
		 * the caller expects a certain state as if the WFI
		 * was aborted.  So let's continue with cache cleaning.
		 */
		skip_wfi = true;
	} else
		BUG();

	if (last_man && __mcpm_outbound_enter_critical(cpu, cluster)) {
		arch_spin_unlock(&mmp_lpm_lock);
		__mcpm_outbound_leave_critical(cluster, CLUSTER_DOWN);
		__mcpm_cpu_down(cpu, cluster);
	} else {
		arch_spin_unlock(&mmp_lpm_lock);
		__mcpm_cpu_down(cpu, cluster);
	}

	if (!skip_wfi)
		cpu_do_idle();
}

static int mmp_pm_power_up(unsigned int cpu, unsigned int cluster)
{
	pr_info("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	if (cluster >= MAX_NR_CLUSTERS || cpu >= MAX_CPUS_PER_CLUSTER)
		return -EINVAL;

	/*
	 * Since this is called with IRQs enabled, and no arch_spin_lock_irq
	 * variant exists, we need to disable IRQs manually here.
	 */
	local_irq_disable();
	arch_spin_lock(&mmp_lpm_lock);
	/*
	 * TODO: Check if we need to do cluster related ops here?
	 * (Seems no need since this function should be called by
	 * other core, which should not enter lpm at this point).
	 */
	mmp_pm_use_count[cluster][cpu]++;

	if (mmp_pm_use_count[cluster][cpu] == 1) {
		mmp_cpu_power_up(cpu, cluster);
	} else if (mmp_pm_use_count[cluster][cpu] != 2) {
		/*
		 * The only possible values are:
		 * 0 = CPU down
		 * 1 = CPU (still) up
		 * 2 = CPU requested to be up before it had a chance
		 *     to actually make itself down.
		 * Any other value is a bug.
		 */
		BUG();
	}

	arch_spin_unlock(&mmp_lpm_lock);
	local_irq_enable();

	return 0;
}

static void mmp_pm_power_down(void)
{
	mmp_pm_down(0);
}

static void mmp_pm_suspend(u64 addr)
{
	mmp_pm_down(addr);
}

static void mmp_pm_powered_up(void)
{
	int mpidr, cpu, cluster;
	unsigned long flags;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	pr_debug("%s: cpu %u cluster %u\n", __func__, cpu, cluster);
	BUG_ON(cluster >= MAX_NR_CLUSTERS || cpu >= MAX_CPUS_PER_CLUSTER);

	local_irq_save(flags);
	arch_spin_lock(&mmp_lpm_lock);

	if (!mmp_pm_use_count[cluster][cpu])
		mmp_pm_use_count[cluster][cpu] = 1;

	arch_spin_unlock(&mmp_lpm_lock);
	local_irq_restore(flags);
}

static const struct mcpm_platform_ops mmp_pm_power_ops = {
	.power_up	= mmp_pm_power_up,
	.power_down	= mmp_pm_power_down,
	.suspend	= mmp_pm_suspend,
	.powered_up	= mmp_pm_powered_up,
};

static void __init mmp_pm_usage_count_init(void)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	BUG_ON(cpu >= MAX_CPUS_PER_CLUSTER || cluster >= MAX_NR_CLUSTERS);
	memset(mmp_pm_use_count, 0, sizeof(mmp_pm_use_count));
	mmp_pm_use_count[cluster][cpu] = 1;
}


static int __init mmp_pm_init(void)
{
	int ret;

	/*
	 * TODO:Should check if hardware is initialized here.
	 * See vexpress_spc_check_loaded()
	 */
	mmp_pm_usage_count_init();

	mmp_entry_vector_init();

	ret = mcpm_platform_register(&mmp_pm_power_ops);
	if (ret)
		pr_warn("Power ops has already been initialized\n");

	ret = mcpm_sync_init(ca7_power_up_setup);
	if (!ret)
		pr_info("mmp power management initialized\n");

	return ret;
}
early_initcall(mmp_pm_init);
