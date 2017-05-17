/*
 * drivers/cpufreq/mmp-cpufreq-comm.c
 *
 * Copyright (C) 2015 Marvell Semiconductors Inc.
 *
 * Author:
 *	Bill Zhou <zhoub@marvell.com>
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
#include <linux/err.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pm_qos.h>
#include <linux/clk-private.h>
#include <linux/sysfs.h>
#include "mmp-cpufreq-comm.h"

char *__cpufreq_printf(const char *fmt, ...)
{
	va_list vargs;
	char *buf;

	va_start(vargs, fmt);
	buf = kvasprintf(GFP_KERNEL, fmt, vargs);
	va_end(vargs);

	return buf;
}

#ifdef CONFIG_DEVFREQ_GOV_THROUGHPUT
#include <linux/ddr_upthreshold.h>

#define CLK_NAME_OFFSET_UPTHRD		strlen("upthrd_");
#define CLK_NAME_OFFSET_UPTHRD_SWAP		strlen("upthrd_swap_")

static DEFINE_MUTEX(list_lock);
static LIST_HEAD(cdu_list_head);

static struct cpufreq_ddr_upthrd *cpufreq_find_cdu(const char *clk_name)
{
	struct cpufreq_ddr_upthrd *cdu = NULL;

	mutex_lock(&list_lock);
	if (!list_empty(&cdu_list_head)) {
		list_for_each_entry(cdu, &cdu_list_head, node) {
			if (!strcmp(cdu->cpu_clk->name, clk_name)) {
				mutex_unlock(&list_lock);
				return cdu;
			}
		}
	}
	mutex_unlock(&list_lock);
	pr_err("%s: not find a valid cdu for %s.\n", __func__, clk_name);
	return NULL;
}

void cpufreq_ddr_upthrd_send_req(struct cpufreq_ddr_upthrd *cdu)
{
	unsigned long ddr_upthrd = PM_QOS_DEFAULT_VALUE;

	mutex_lock(&cdu->data_lock);
	if (cdu->new_cpu_rate >= cdu->busy_upthrd_swap)
		ddr_upthrd = cdu->busy_upthrd;
	pm_qos_update_request(&cdu->ddr_upthrd_max_qos, ddr_upthrd);
	mutex_unlock(&cdu->data_lock);
}

#define __define_kattr_show(CAGEGORY, category)				\
static ssize_t category##_kattr_show(struct kobject *kobj,		\
	struct kobj_attribute *attr, char *buf)				\
{									\
	char *clk_name;							\
	struct cpufreq_ddr_upthrd *cdu = NULL;				\
	clk_name = (char *)attr->attr.name + CLK_NAME_OFFSET_##CAGEGORY;\
	cdu = cpufreq_find_cdu(clk_name);				\
	if (cdu)							\
		return sprintf(buf, "%lu\n", cdu->busy_##category);	\
	return sprintf(buf, "%lu\n", DEFAULT_BUSY_##CAGEGORY);		\
}

__define_kattr_show(UPTHRD, upthrd)
__define_kattr_show(UPTHRD_SWAP, upthrd_swap)

static int upthrd_error_handler(const char *clk_name,
	unsigned long value, int ret)
{
	if ((value >= 100) || (ret != 1)) {
		pr_err("%s: <ERR> wrong parameter.\n", __func__);
		pr_err("echo upthrd(0~100) > upthrd_%s\n", clk_name);
		pr_err("For example: echo 30 > upthrd_%s\n", clk_name);
		return -EINVAL;
	}
	return 0;
}

static int upthrd_swap_error_handler(const char *clk_name,
	unsigned long value, int ret)
{
	if (ret != 0x1) {
		pr_err("%s: <ERR> wrong parameter.\n", __func__);
		return -E2BIG;
	}
	return 0;
}

#define __define_kattr_store(CAGEGORY, category)			\
static ssize_t category##_kattr_store(struct kobject *kobj,		\
	struct kobj_attribute *attr, const char *buf, size_t count)	\
{									\
	int ret;							\
	unsigned long value;						\
	char *clk_name;							\
	struct cpufreq_ddr_upthrd *cdu = NULL;				\
	clk_name = (char *)attr->attr.name + CLK_NAME_OFFSET_##CAGEGORY;\
	ret = sscanf(buf, "%lu", &value);				\
	ret = category##_error_handler(clk_name, value, ret);		\
	if (ret == 0)							\
		cdu = cpufreq_find_cdu(clk_name);			\
	else								\
		return ret;						\
	if (!cdu)							\
		return -EINVAL;						\
	mutex_lock(&cdu->data_lock);					\
	cdu->busy_##category = value;					\
	cdu->new_cpu_rate = clk_get_rate(cdu->cpu_clk) / 1000;		\
	mutex_unlock(&cdu->data_lock);					\
	cpufreq_ddr_upthrd_send_req(cdu);				\
	return count;							\
}

__define_kattr_store(UPTHRD, upthrd)
__define_kattr_store(UPTHRD_SWAP, upthrd_swap)

#define __declare_kattr_struct(category, clk_name)			\
static struct kobj_attribute category##_##clk_name##_attr =		\
	__ATTR(category##_##clk_name, 0644, category##_kattr_show, category##_kattr_store);

__declare_kattr_struct(upthrd, cpu);
__declare_kattr_struct(upthrd, clst0);
__declare_kattr_struct(upthrd, clst1);
__declare_kattr_struct(upthrd_swap, cpu);
__declare_kattr_struct(upthrd_swap, clst0);
__declare_kattr_struct(upthrd_swap, clst1);

struct cpufreq_ddr_upthrd *cpufreq_ddr_upthrd_init(struct clk *clk)
{
	int ret;
	struct kobj_attribute *cpu_kattr = NULL;
	struct kobj_attribute *cpu_swap_kattr = NULL;
	struct cpufreq_ddr_upthrd *cdu = NULL;

	cdu = cpufreq_find_cdu(clk->name);
	if (cdu)
		return cdu;

	cdu = kzalloc(sizeof(struct cpufreq_ddr_upthrd), GFP_KERNEL);
	if (!cdu) {
		pr_err("%s: fail to alloc struct cpufreq_ddr_upthrd.\n",
			__func__);
		return NULL;
	}

	cdu->ddr_upthrd_max_qos.name =
		__cpufreq_printf("%s%s", clk->name, "_ddr_upthrd");
	if (!cdu->ddr_upthrd_max_qos.name) {
		pr_err("%s: no mem for ddr_upthrd_max_qos.name.\n", __func__);
		goto err_out_qos;
	}

	cdu->cpu_clk = clk;
	cdu->new_cpu_rate = clk_get_rate(clk) / 1000;
	cdu->busy_upthrd = DEFAULT_BUSY_UPTHRD;
	cdu->busy_upthrd_swap = DEFAULT_BUSY_UPTHRD_SWAP;
	mutex_init(&cdu->data_lock);
	if (!pm_qos_request_active(&cdu->ddr_upthrd_max_qos))
		pm_qos_add_request(&cdu->ddr_upthrd_max_qos,
			PM_QOS_DDR_DEVFREQ_UPTHRD_MAX, cdu->busy_upthrd);
	cpufreq_ddr_upthrd_send_req(cdu);
	list_add_tail(&cdu->node, &cdu_list_head);

	if (!strcmp(cdu->cpu_clk->name, "cpu")) {
		cpu_kattr = &upthrd_cpu_attr;
		cpu_swap_kattr = &upthrd_swap_cpu_attr;
	} else if (!strcmp(cdu->cpu_clk->name, "clst0")) {
		cpu_kattr = &upthrd_clst0_attr;
		cpu_swap_kattr = &upthrd_swap_clst0_attr;
	} else if (!strcmp(cdu->cpu_clk->name, "clst1")) {
		cpu_kattr = &upthrd_clst1_attr;
		cpu_swap_kattr = &upthrd_swap_clst1_attr;
	} else {
		pr_err("%s: invalid cpu clock name.\n", cdu->cpu_clk->name);
		goto err_out_clk;
	}

	ret = sysfs_create_file(ddr_upthrd_obj, &cpu_kattr->attr);
	if (ret) {
		pr_err("%s: fail to create sysfs attr for upthrd_%s: %d\n",
			__func__, clk->name, ret);
		goto err_out_sysfs_cpu;
	}

	ret = sysfs_create_file(ddr_upthrd_obj, &cpu_swap_kattr->attr);
	if (ret) {
		pr_err("%s: fail to create sysfs attr for upthrd_swap_%s: %d\n",
			__func__, clk->name, ret);
		goto err_out_sysfs_cpu_swap;
	}

	return cdu;

err_out_sysfs_cpu_swap:
	sysfs_remove_file(ddr_upthrd_obj, &cpu_swap_kattr->attr);

err_out_sysfs_cpu:
	sysfs_remove_file(ddr_upthrd_obj, &cpu_kattr->attr);

err_out_clk:
	list_del(&cdu->node);
	pm_qos_remove_request(&cdu->ddr_upthrd_max_qos);
	mutex_destroy(&cdu->data_lock);
	kfree(cdu->ddr_upthrd_max_qos.name);

err_out_qos:
	kfree(cdu);

	return NULL;
}
#endif

