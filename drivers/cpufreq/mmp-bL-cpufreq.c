/*
 * drivers/cpufreq/mmp-bL-cpufreq.c
 *
 * Copyright (C) 2014 Marvell Semiconductors Inc.
 *
 * Author: Bill Zhou <zhoub@marvell.com>
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

/* Unit of various frequencies:
 * clock driver: Hz
 * cpufreq framework/driver: KHz
 * qos framework: MHz
 */

#define LITTLE_CLUSTER		0
#define BIG_CLUSTER		1
#define MAX_CLUSTERS		2
#define CORES_PER_CLUSTER	4

static struct clk *clst_clk[MAX_CLUSTERS];
static char *clk_name[MAX_CLUSTERS] = { "clst0", "clst1" };
static struct cpufreq_frequency_table *freq_table[MAX_CLUSTERS];

static int cpufreq_qos_min_id[MAX_CLUSTERS] = {
	PM_QOS_CPUFREQ_L_MIN,
	PM_QOS_CPUFREQ_B_MIN,
};

static int cpufreq_qos_max_id[MAX_CLUSTERS] = {
	PM_QOS_CPUFREQ_L_MAX,
	PM_QOS_CPUFREQ_B_MAX,
};

/* Qos min request clients: cpufreq, driver, policy->cpuinfo.min */
static struct pm_qos_request cpufreq_qos_req_min[MAX_CLUSTERS] = {
	{
	 .name = "l_cpufreq_min",
	 },
	{
	 .name = "b_cpufreq_min",
	 },
};

static struct pm_qos_request cpupolicy_qos_req_min[MAX_CLUSTERS] = {
	{
	 .name = "l_cpupolicy_min",
	 },
	{
	 .name = "b_cpupolicy_min",
	 },
};

/* Qos max request clients: Qos min, driver, policy->cpuinfo.max */
static struct pm_qos_request qosmin_qos_req_max[MAX_CLUSTERS] = {
	{
	 .name = "l_cpuqos_min",
	 },
	{
	 .name = "b_cpuqos_min",
	 },
};

static struct pm_qos_request cpupolicy_qos_req_max[MAX_CLUSTERS] = {
	{
	 .name = "l_cpupolicy_max",
	 },
	{
	 .name = "b_cpupolicy_max",
	 },
};

static unsigned long pm_event;
static struct mutex mmp_cpu_lock;

static int get_clst_index(unsigned int cpu)
{
	int clst_index;

	clst_index = cpu / CORES_PER_CLUSTER;
	BUG_ON(clst_index >= MAX_CLUSTERS);
	return clst_index;
}

static void set_clst_policy_info(struct cpufreq_policy *policy, int clst_index)
{
	int i;
	cpumask_clear(policy->cpus);
	for (i = clst_index * CORES_PER_CLUSTER;
	     i < (clst_index + 1) * CORES_PER_CLUSTER; i++)
		cpumask_set_cpu(i, policy->cpus);

	policy->clk = clst_clk[clst_index];
	policy->cpuinfo.transition_latency = 10 * 1000;
	policy->cur = clk_get_rate(policy->clk) / 1000;
}

static int get_clst_rate_index(int clst_index,
			       unsigned int target_freq,
			       unsigned int relation, unsigned int *index)
{
	struct cpufreq_frequency_table optimal = {
		.driver_data = ~0,
		.frequency = 0,
	};
	struct cpufreq_frequency_table suboptimal = {
		.driver_data = ~0,
		.frequency = 0,
	};
	unsigned int i;

	switch (relation) {
	case CPUFREQ_RELATION_H:
		suboptimal.frequency = ~0;
		break;
	case CPUFREQ_RELATION_L:
		optimal.frequency = ~0;
		break;
	}

	for (i = 0; (freq_table[clst_index][i].frequency != CPUFREQ_TABLE_END);
	     i++) {
		unsigned int freq = freq_table[clst_index][i].frequency;
		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;
		switch (relation) {
		case CPUFREQ_RELATION_H:
			if (freq <= target_freq) {
				if (freq >= optimal.frequency) {
					optimal.frequency = freq;
					optimal.driver_data = i;
				}
			} else {
				if (freq <= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.driver_data = i;
				}
			}
			break;
		case CPUFREQ_RELATION_L:
			if (freq >= target_freq) {
				if (freq <= optimal.frequency) {
					optimal.frequency = freq;
					optimal.driver_data = i;
				}
			} else {
				if (freq >= suboptimal.frequency) {
					suboptimal.frequency = freq;
					suboptimal.driver_data = i;
				}
			}
			break;
		}
	}
	if (optimal.driver_data > i) {
		if (suboptimal.driver_data > i)
			return -EINVAL;
		*index = suboptimal.driver_data;
	} else
		*index = optimal.driver_data;
	return 0;
}

static int cpufreq_min_notify(int clst_index, struct notifier_block *b,
			      unsigned long min, void *v)
{
	if (pm_qos_request_active(&qosmin_qos_req_max[clst_index]))
		pm_qos_update_request(&qosmin_qos_req_max[clst_index], min);
	return NOTIFY_OK;
}

static int l_cpufreq_min_notify(struct notifier_block *b,
				unsigned long min, void *v)
{
	return cpufreq_min_notify(LITTLE_CLUSTER, b, min, v);
}

static int b_cpufreq_min_notify(struct notifier_block *b,
				unsigned long min, void *v)
{
	return cpufreq_min_notify(BIG_CLUSTER, b, min, v);
}

static struct notifier_block l_cpufreq_min_notifier = {
	.notifier_call = l_cpufreq_min_notify,
};

static struct notifier_block b_cpufreq_min_notifier = {
	.notifier_call = b_cpufreq_min_notify,
};

static int cpufreq_max_notify(int clst_index, struct notifier_block *b,
			      unsigned long max, void *v)
{
	int ret = -1;
	unsigned int index = 0;
	struct cpufreq_freqs freqs;
	struct cpufreq_policy *policy;
	unsigned long freq = 0;

	policy = cpufreq_cpu_get(clst_index * CORES_PER_CLUSTER);

	if (!policy) {
		pr_err("%s: policy is invalid or not available\n", __func__);
		return NOTIFY_BAD;
	}
	/*
	 * find a frequency in table, we didn't use help function
	 * cpufreq_frequency_table_target as it will consider policy->min
	 * and policy->max, which it is not updated here yet, and also new
	 * policy could not get here.
	 */
	ret = get_clst_rate_index(clst_index, max * 1000,
				  CPUFREQ_RELATION_H, &index);

	if (ret != 0) {
		pr_err("%s: cannot get a valid index from freq_table\n",
		       __func__);
		return NOTIFY_BAD;
	}

	freqs.old = clk_get_rate(policy->clk) / 1000;
	freqs.new = freq_table[clst_index][index].frequency;
	freq = freqs.new * 1000;

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_PRECHANGE);

	if (!clk_set_rate(policy->clk, freq))
		ret = NOTIFY_OK;
	else {
		freqs.new = freqs.old;
		ret = NOTIFY_BAD;
	}

	cpufreq_notify_transition(policy, &freqs, CPUFREQ_POSTCHANGE);

	pr_debug("%s: set cluster-%d rate to %lu, %s\n", __func__, clst_index,
		freq, (ret == NOTIFY_OK) ? "ok" : "fail");
	return ret;
}

static int l_cpufreq_max_notify(struct notifier_block *b,
				unsigned long max, void *v)
{
	return cpufreq_max_notify(LITTLE_CLUSTER, b, max, v);
}

static int b_cpufreq_max_notify(struct notifier_block *b,
				unsigned long max, void *v)
{
	return cpufreq_max_notify(BIG_CLUSTER, b, max, v);
}

static struct notifier_block l_cpufreq_max_notifier = {
	.notifier_call = l_cpufreq_max_notify,
};

static struct notifier_block b_cpufreq_max_notifier = {
	.notifier_call = b_cpufreq_max_notify,
};

static int policy_limiter_notify(struct notifier_block *nb,
				 unsigned long val, void *data)
{
	int clst_index = 0;
	struct cpufreq_policy *policy = data;

	clst_index = policy->cpu / CORES_PER_CLUSTER;

	if (val == CPUFREQ_ADJUST) {
		pm_qos_update_request(&cpupolicy_qos_req_min[clst_index],
				      policy->min / 1000);
		pm_qos_update_request(&cpupolicy_qos_req_max[clst_index],
				      policy->max / 1000);
	}
	return NOTIFY_OK;
}

static struct notifier_block policy_limiter_notifier = {
	.notifier_call = policy_limiter_notify,
};

static int mmp_pm_notify(struct notifier_block *nb, unsigned long event,
			 void *dummy)
{
	int i;
	static unsigned int saved_clst_clk[MAX_CLUSTERS];

	mutex_lock(&mmp_cpu_lock);

	if (event == PM_SUSPEND_PREPARE) {
		/* scaling to the min frequency before entering suspend */
		for (i = 0; i < MAX_CLUSTERS; i++) {
			saved_clst_clk[i] =
			    cpufreq_generic_get(i * CORES_PER_CLUSTER);
			pm_qos_update_request(&cpufreq_qos_req_min[i],
					      freq_table[i][0].frequency / 1000);
		}
	} else if (event == PM_POST_SUSPEND) {
		for (i = 0; i < MAX_CLUSTERS; i++)
			pm_qos_update_request(&cpufreq_qos_req_min[i],
					      saved_clst_clk[i] / 1000);
	}

	pm_event = event;
	mutex_unlock(&mmp_cpu_lock);
	return NOTIFY_OK;
}

static struct notifier_block mmp_cpu_pm_notifier = {
	.notifier_call = mmp_pm_notify,
};

static unsigned int mmp_bL_cpufreq_get(unsigned int cpu)
{
	int clst_index = get_clst_index(cpu);

	return clk_get_rate(clst_clk[clst_index]) / 1000;
}

static int mmp_bL_cpufreq_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation)
{
	int ret = 0, clst_index;

	mutex_lock(&mmp_cpu_lock);

	if (pm_event == PM_SUSPEND_PREPARE) {
		ret = -EBUSY;
		goto out;
	}

	clst_index = get_clst_index(policy->cpu);
	pm_qos_update_request(&cpufreq_qos_req_min[clst_index], target_freq / 1000);

out:
	mutex_unlock(&mmp_cpu_lock);
	return ret;
}

static int mmp_bL_cpufreq_init(struct cpufreq_policy *policy)
{
	int ret;
	int clst_index;

	if (policy->cpu >= num_possible_cpus())
		return -EINVAL;

	clst_index = get_clst_index(policy->cpu);
	pr_info("%s: cpu-%d to manage policy on cluster-%d\n", __func__,
		policy->cpu, clst_index);

	clst_clk[clst_index] = __clk_lookup(clk_name[clst_index]);
	if (!clst_clk[clst_index]) {
		pr_err("%s: fail to get clock for cluster-%d\n", __func__,
		       clst_index);
		return -EINVAL;
	}

	freq_table[clst_index] = cpufreq_frequency_get_table(policy->cpu);
	ret = cpufreq_table_validate_and_show(policy, freq_table[clst_index]);
	if (ret) {
		pr_err("%s: invalid frequency table\n", __func__);
		return ret;
	}

	mutex_init(&mmp_cpu_lock);

	set_clst_policy_info(policy, clst_index);

	pm_qos_add_request(&cpufreq_qos_req_min[clst_index],
			   cpufreq_qos_min_id[clst_index], policy->cur / 1000);
	pm_qos_add_request(&cpupolicy_qos_req_min[clst_index],
			   cpufreq_qos_min_id[clst_index], policy->min / 1000);
	pm_qos_add_request(&qosmin_qos_req_max[clst_index],
			   cpufreq_qos_max_id[clst_index],
			   pm_qos_request(cpufreq_qos_min_id[clst_index]));
	pm_qos_add_request(&cpupolicy_qos_req_max[clst_index],
			   cpufreq_qos_max_id[clst_index], policy->max / 1000);

	pr_info("%s: finish initialization on cluster-%d\n", __func__, clst_index);

	return 0;
}

static int mmp_bL_cpufreq_exit(struct cpufreq_policy *policy)
{
	mutex_destroy(&mmp_cpu_lock);
	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static struct cpufreq_driver mmp_bL_cpufreq_driver = {
	.name = "mmp-bL-cpufreq",
	.verify = cpufreq_generic_frequency_table_verify,
	.get = mmp_bL_cpufreq_get,
	.target = mmp_bL_cpufreq_target,
	.init = mmp_bL_cpufreq_init,
	.exit = mmp_bL_cpufreq_exit,
	.attr = cpufreq_generic_attr,
};

static int __init mmp_bL_cpufreq_register(void)
{
	/* big/LITTLE cluster use the same pm notifier */
	register_pm_notifier(&mmp_cpu_pm_notifier);

	pm_qos_add_notifier(PM_QOS_CPUFREQ_L_MIN, &l_cpufreq_min_notifier);
	pm_qos_add_notifier(PM_QOS_CPUFREQ_L_MAX, &l_cpufreq_max_notifier);
	pm_qos_add_notifier(PM_QOS_CPUFREQ_B_MIN, &b_cpufreq_min_notifier);
	pm_qos_add_notifier(PM_QOS_CPUFREQ_B_MAX, &b_cpufreq_max_notifier);

	cpufreq_register_notifier(&policy_limiter_notifier,
				  CPUFREQ_POLICY_NOTIFIER);

	return cpufreq_register_driver(&mmp_bL_cpufreq_driver);
}

static void __exit mmp_bL_cpufreq_unregister(void)
{
	cpufreq_unregister_notifier(&policy_limiter_notifier,
				    CPUFREQ_POLICY_NOTIFIER);

	pm_qos_remove_notifier(PM_QOS_CPUFREQ_L_MIN, &l_cpufreq_min_notifier);
	pm_qos_remove_notifier(PM_QOS_CPUFREQ_L_MAX, &l_cpufreq_max_notifier);
	pm_qos_remove_notifier(PM_QOS_CPUFREQ_B_MIN, &b_cpufreq_min_notifier);
	pm_qos_remove_notifier(PM_QOS_CPUFREQ_B_MAX, &b_cpufreq_max_notifier);

	unregister_pm_notifier(&mmp_cpu_pm_notifier);

	cpufreq_unregister_driver(&mmp_bL_cpufreq_driver);
}

MODULE_DESCRIPTION("cpufreq driver for Marvell MMP SoC w/ bL arch");
MODULE_LICENSE("GPL");
module_init(mmp_bL_cpufreq_register);
module_exit(mmp_bL_cpufreq_unregister);
