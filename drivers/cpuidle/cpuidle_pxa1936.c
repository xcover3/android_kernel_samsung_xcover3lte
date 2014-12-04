/*
 * Pxa1936(helan3) CPU idle driver.
 *
 * Copyright (C) 2014 Marvell Ltd.
 * Author: Xiaoguang Chen <chenxg@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <asm/suspend.h>
#include <asm/proc-fns.h>

static int arm64_enter_state(struct cpuidle_device *dev,
			     struct cpuidle_driver *drv, int idx);

struct cpuidle_driver arm64_idle_driver = {
	.name = "arm64_idle",
	.owner = THIS_MODULE,
	.states[0] = {
		      .enter = arm64_enter_state,
		      .exit_latency = 18,
		      .target_residency = 36,
		      /*
		       * Use CPUIDLE_FLAG_TIMER_STOP flag to let the cpuidle
		       * framework handle the CLOCK_EVENT_NOTIFY_BROADCAST_
		       * ENTER/EXIT when entering idle states.
		       */
		      .flags = CPUIDLE_FLAG_TIME_VALID,
		      .name = "C1",
		      .desc = "C1: Core internal clock gated",
		      },
	.states[1] = {
		      .enter = arm64_enter_state,
		      .exit_latency = 20,
		      .target_residency = 40,
		      .flags = CPUIDLE_FLAG_TIME_VALID |
		      CPUIDLE_FLAG_TIMER_STOP,
		      .name = "C2",
		      .desc = "C2: Core power down",
		      },
	.states[2] = {
		      .enter = arm64_enter_state,
		      .exit_latency = 450,
		      .target_residency = 900,
		      .flags = CPUIDLE_FLAG_TIME_VALID |
		      CPUIDLE_FLAG_TIMER_STOP,
		      .name = "MP2",
		      .desc = "MP2: Core subsystem power down",
		      },
	.states[3] = {
		      .enter = arm64_enter_state,
		      .exit_latency = 500,
		      .target_residency = 1000,
		      .flags = CPUIDLE_FLAG_TIME_VALID |
		      CPUIDLE_FLAG_TIMER_STOP,
		      .name = "D1p",
		      .desc = "D1p: AP idle state",
		      },
	.states[4] = {
		      .enter = arm64_enter_state,
		      .exit_latency = 600,
		      .target_residency = 1200,
		      .flags = CPUIDLE_FLAG_TIME_VALID |
		      CPUIDLE_FLAG_TIMER_STOP,
		      .name = "D1",
		      .desc = "D1: Chip idle state",
		      },

	.state_count = 5,
};

/*
 * arm64_enter_state - Programs CPU to enter the specified state
 *
 * @dev: cpuidle device
 * @drv: cpuidle driver
 * @idx: state index
 *
 * Called from the CPUidle framework to program the device to the
 * specified target state selected by the governor.
 */
static int arm64_enter_state(struct cpuidle_device *dev,
			     struct cpuidle_driver *drv, int idx)
{
	int ret;

	if (!idx) {
		/*
		 * C1 is just standby wfi, does not require CPU
		 * to be suspended
		 */
		cpu_do_idle();
		return idx;
	}

	cpu_pm_enter();
	/*
	 * Pass C-state index to cpu_suspend which in turn will call
	 * the CPU ops suspend protocol with index as a parameter
	 */
	ret = cpu_suspend((unsigned long)&idx);
	if (ret)
		pr_warn_once("returning from cpu_suspend %s %d\n",
			     __func__, ret);
	/*
	 * Trigger notifier only if cpu_suspend succeeded
	 */
	if (!ret)
		cpu_pm_exit();

	return idx;
}

static const struct of_device_id psci_of_match[] __initconst = {
	{ .compatible = "arm,psci",},
	{},
};

/*
 * arm64_idle_init
 *
 * Registers the arm specific cpuidle driver with the cpuidle
 * framework. It relies on core code to set-up the driver cpumask
 * and initialize it to online CPUs.
 */
int __init arm64_idle_init(void)
{
	struct device_node *np;

	if (!of_machine_is_compatible("marvell,pxa1936"))
		return -ENODEV;

	np = of_find_matching_node(NULL, psci_of_match);
	if (!np || !of_device_is_available(np))
		return -ENODEV;

	return cpuidle_register(&arm64_idle_driver, NULL);
}
device_initcall(arm64_idle_init);
