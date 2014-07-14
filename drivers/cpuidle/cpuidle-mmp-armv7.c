/*
 * MMP CPU idle driver
 *
 * Author:	Fangsuo Wu <fswu@marvell.com>
 * Copyright:	(C) 2013 marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/clockchips.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/cputype.h>
#include <asm/mcpm.h>
#include <asm/proc-fns.h>
#include <asm/suspend.h>

struct cpuidle_state *mcpm_states;

static struct cpuidle_driver mcpm_idle_driver = {
	.name = "mcpm_idle",
	.owner = THIS_MODULE,
};

static int notrace mcpm_powerdown_finisher(unsigned long arg)
{
	u32 mpidr = read_cpuid_mpidr();
	u32 cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	u32 this_cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	mcpm_set_entry_vector(cpu, this_cluster, cpu_resume);
	mcpm_cpu_suspend(arg);
	return 1;
}

/**
 * mcpm_platform_state_register - register platform specific power states
 *
 * @states: points to platform specific power state array
 * @count:  number of platform power states
 *
 * An error is returned if the registration has been done previously.
 */
int __init mcpm_platform_state_register(struct cpuidle_state *states, int count)
{
	if (mcpm_states)
		return -EBUSY;
	mcpm_states = states;
	mcpm_idle_driver.state_count = count;

	return 0;
}

/*
 * mcpm_enter_powerdown - Programs CPU to enter the specified state
 *
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @idx: state index
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static int mcpm_enter_powerdown(struct cpuidle_device *dev,
				struct cpuidle_driver *drv, int idx)
{
	int *real_idx = &idx;
	int ret;

	BUG_ON(!irqs_disabled());
	local_fiq_disable();

	cpu_pm_enter();

	ret = cpu_suspend((unsigned long)real_idx, mcpm_powerdown_finisher);
	if (ret)
		pr_err("cpu%d failed to enter power down!", dev->cpu);

	mcpm_cpu_powered_up();

	cpu_pm_exit();

	local_fiq_enable();

	return *real_idx;
}

/*
 * mcpm_idle_init
 *
 * Registers the mcpm specific cpuidle driver with the cpuidle
 * framework with the valid set of states.
 */
static int __init mcpm_cpuidle_init(void)
{
	struct cpuidle_driver *drv = &mcpm_idle_driver;
	int i;

	if (!mcpm_states) {
		pr_err("mcpm_states is not initilized!\n");
		return -1;
	}

	for (i = 0; i < drv->state_count; i++) {
		if (!mcpm_states[i].enter)
			mcpm_states[i].enter = mcpm_enter_powerdown;

		memcpy(&drv->states[i], &mcpm_states[i],
				sizeof(struct cpuidle_state));
	}

	return cpuidle_register(&mcpm_idle_driver, NULL);
}

late_initcall(mcpm_cpuidle_init);
