/*
 * drivers/cpufreq/mmp-armv8-cpufreq.c
 *
 * Copyright (C) 2012 Marvell Semiconductors Inc.
 *
 * Author:
 *	Yipeng Yao <ypyao@marvell.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/suspend.h>
#include <linux/pm_qos.h>
#include <linux/clk-private.h>

#define KHZ_TO_HZ	(1000)

static struct clk *cpu_clk;
static DEFINE_MUTEX(mmp_cpu_lock);
static bool is_suspended;
static struct cpufreq_frequency_table *freq_table;

static struct pm_qos_request cpufreq_qos_req_min = {
	.name = "cpu_freqmin",
};

static int mmp_update_cpu_speed(unsigned long rate)
{
	int ret = 0;
	struct cpufreq_freqs freqs;
	struct cpufreq_policy *policy;

	freqs.old = cpufreq_generic_get(0);
	freqs.new = rate;

	if (freqs.old == freqs.new)
		return ret;

	policy = cpufreq_cpu_get(0);
	BUG_ON(!policy);

	pr_debug("cpufreq-mmp: transition: %u --> %u\n", freqs.old, freqs.new);
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);
	ret = clk_set_rate(cpu_clk, freqs.new * KHZ_TO_HZ);
	if (ret) {
		pr_err("cpu-mmp: Failed to set cpu frequency to %d kHz\n",
			freqs.new);
		freqs.new = freqs.old;
	}
	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	cpufreq_cpu_put(policy);
	return ret;
}

static int mmp_target(struct cpufreq_policy *policy,
		      unsigned int target_freq, unsigned int relation)
{
	int idx;
	unsigned int freq;
	int ret = 0;

	mutex_lock(&mmp_cpu_lock);

	if (is_suspended) {
		ret = -EBUSY;
		goto out;
	}

	target_freq = max_t(unsigned int, pm_qos_request(PM_QOS_CPUFREQ_MIN),
			    target_freq);
	cpufreq_frequency_table_target(policy, freq_table, target_freq,
				       relation, &idx);

	freq = freq_table[idx].frequency;

	ret = mmp_update_cpu_speed(freq);
out:
	mutex_unlock(&mmp_cpu_lock);
	return ret;
}

static int mmp_pm_notify(struct notifier_block *nb, unsigned long event,
	void *dummy)
{
	static unsigned int saved_cpuclk;

	mutex_lock(&mmp_cpu_lock);
	if (event == PM_SUSPEND_PREPARE) {
		/* scaling to the min frequency before entering suspend */
		saved_cpuclk = cpufreq_generic_get(0);
		mmp_update_cpu_speed(freq_table[0].frequency);
		is_suspended = true;
		pr_info("%s: disable cpu freq-chg before suspend", __func__);
		pr_info("cur rate %dKhz\n", cpufreq_generic_get(0));
	} else if (event == PM_POST_SUSPEND) {
		is_suspended = false;
		mmp_update_cpu_speed(saved_cpuclk);
		pr_info("%s: enable cpu freq-chg after resume", __func__);
		pr_info("cur rate %dKhz\n", cpufreq_generic_get(0));
	}
	mutex_unlock(&mmp_cpu_lock);
	return NOTIFY_OK;
}

static struct notifier_block mmp_cpu_pm_notifier = {
	.notifier_call = mmp_pm_notify,
};

static int cpufreq_min_notify(struct notifier_block *b,
			      unsigned long min, void *v)
{
	int ret;
	unsigned long freq, val = min;
	struct cpufreq_policy *policy;
	int cpu = 0;

	policy = cpufreq_cpu_get(cpu);
	if ((!policy) || (!policy->governor))
		return NOTIFY_BAD;

	/* return directly when the governor needs a fixed frequency */
	if (!strcmp(policy->governor->name, "userspace") ||
	    !strcmp(policy->governor->name, "powersave") ||
	    !strcmp(policy->governor->name, "performance")) {
		cpufreq_cpu_put(policy);
		return NOTIFY_OK;
	}

	freq = cpufreq_generic_get(cpu);
	if (freq >= val)
		return NOTIFY_OK;

	ret = __cpufreq_driver_target(policy, val, CPUFREQ_RELATION_L);
	cpufreq_cpu_put(policy);
	if (ret < 0)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static struct notifier_block cpufreq_min_notifier = {
	.notifier_call = cpufreq_min_notify,
};

static int cpufreq_max_notify(struct notifier_block *b,
			      unsigned long max, void *v)
{
	int ret;
	unsigned long freq, val = max;
	struct cpufreq_policy *policy;
	int cpu = 0;

	freq = cpufreq_generic_get(cpu);
	if (freq <= val)
		return NOTIFY_OK;

	policy = cpufreq_cpu_get(cpu);
	if (!policy)
		return NOTIFY_BAD;

	ret = __cpufreq_driver_target(policy, val, CPUFREQ_RELATION_H);
	cpufreq_cpu_put(policy);
	if (ret < 0)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static struct notifier_block cpufreq_max_notifier = {
	.notifier_call = cpufreq_max_notify,
};

static int mmp_cpufreq_init(struct cpufreq_policy *policy)
{
	if (policy->cpu >= num_possible_cpus())
		return -EINVAL;

	if (unlikely(!cpu_clk)) {
		cpu_clk = __clk_lookup("cpu");
		if (!cpu_clk)
			return -EINVAL;
	}
	policy->clk = cpu_clk;

	freq_table = cpufreq_frequency_get_table(policy->cpu);
	BUG_ON(!freq_table);
	/*
	 * FIXME: what's the actual transition time?
	 * use 10ms as sampling rate for bring up
	 */
	cpufreq_generic_init(policy, freq_table, 10 * 1000);

	policy->cur = cpufreq_generic_get(policy->cpu);

	if (!pm_qos_request_active(&cpufreq_qos_req_min))
		pm_qos_add_request(&cpufreq_qos_req_min,
				   PM_QOS_CPUFREQ_MIN,
				   policy->cpuinfo.min_freq);

	return 0;
}

static int mmp_cpufreq_exit(struct cpufreq_policy *policy)
{
	cpufreq_frequency_table_cpuinfo(policy, freq_table);
	return 0;
}

static struct cpufreq_driver mmp_cpufreq_driver = {
	.verify = cpufreq_generic_frequency_table_verify,
	.target = mmp_target,
	.get = cpufreq_generic_get,
	.init = mmp_cpufreq_init,
	.exit = mmp_cpufreq_exit,
	.name = "mmp-cpufreq",
	.attr = cpufreq_generic_attr,
};

static int __init cpufreq_init(void)
{
	register_pm_notifier(&mmp_cpu_pm_notifier);
	pm_qos_add_notifier(PM_QOS_CPUFREQ_MIN, &cpufreq_min_notifier);
	pm_qos_add_notifier(PM_QOS_CPUFREQ_MAX, &cpufreq_max_notifier);
	return cpufreq_register_driver(&mmp_cpufreq_driver);
}

static void __exit cpufreq_exit(void)
{
	struct cpufreq_frequency_table *cpufreq_tbl;
	int i;

	unregister_pm_notifier(&mmp_cpu_pm_notifier);
	for_each_possible_cpu(i) {
		cpufreq_tbl = cpufreq_frequency_get_table(i);
		kfree(cpufreq_tbl);
		cpufreq_frequency_table_put_attr(i);
	}
	pm_qos_remove_notifier(PM_QOS_CPUFREQ_MIN, &cpufreq_min_notifier);
	pm_qos_remove_notifier(PM_QOS_CPUFREQ_MAX, &cpufreq_max_notifier);
	cpufreq_unregister_driver(&mmp_cpufreq_driver);
}

MODULE_DESCRIPTION("cpufreq driver for Marvell MMP SoC");
MODULE_LICENSE("GPL");
module_init(cpufreq_init);
module_exit(cpufreq_exit);
