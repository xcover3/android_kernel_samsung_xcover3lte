/*
 * common function for clock framework source file
 *
 * Copyright (C) 2014 Marvell
 * Zhoujie Wu <zjwu@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-private.h>
#include <linux/clk-provider.h>
#include <linux/devfreq.h>
#include <linux/clk/mmpdcstat.h>
#include <linux/clk/mmpcpdvc.h>
#include <linux/debugfs.h>

#include "clk.h"
#include "clk-plat.h"



/* parameter passed from cmdline to identify DDR mode */
enum ddr_type ddr_mode = DDR_400M;
static int __init __init_ddr_mode(char *arg)
{
	int n;
	if (!get_option(&arg, &n))
		return 0;

	if ((n >= DDR_TYPE_MAX) || (n < DDR_400M))
		pr_info("WARNING: unknown DDR type!");
	else
		ddr_mode = n;

	return 1;
}
__setup("ddr_mode=", __init_ddr_mode);

unsigned long max_freq;
static int __init max_freq_setup(char *str)
{
	int n;
	if (!get_option(&str, &n))
		return 0;
	max_freq = n;
	return 1;
}
__setup("max_freq=", max_freq_setup);

/* interface use to get peri clock avaliable op num and rate */
unsigned int mmp_clk_mix_get_opnum(struct clk *clk)
{
	struct clk_hw *hw = clk->hw;
	struct mmp_clk_mix *mix = to_clk_mix(hw);

	return mix->table_size;
}

unsigned long mmp_clk_mix_get_oprate(struct clk *clk,
	unsigned int index)
{
	struct clk_hw *hw = clk->hw;
	struct mmp_clk_mix *mix = to_clk_mix(hw);

	if (!mix->table)
		return 0;
	else
		return mix->table[index].valid ? mix->table[index].rate : 0;
}

#ifdef CONFIG_PM_DEVFREQ
void __init_comp_devfreq_table(struct clk *clk, unsigned int dev_id)
{
	unsigned int freq_num = 0, i = 0;
	struct devfreq_frequency_table *devfreq_tbl;

	freq_num = mmp_clk_mix_get_opnum(clk);
	devfreq_tbl =
		kmalloc(sizeof(struct devfreq_frequency_table)
			* (freq_num + 1), GFP_KERNEL);
	if (!devfreq_tbl)
		return;

	for (i = 0; i < freq_num; i++) {
		devfreq_tbl[i].index = i;
		devfreq_tbl[i].frequency =
			mmp_clk_mix_get_oprate(clk, i) / MHZ_TO_KHZ;
	}

	devfreq_tbl[i].index = i;
	devfreq_tbl[i].frequency = DEVFREQ_TABLE_END;

	devfreq_frequency_table_register(devfreq_tbl, dev_id);
}
#endif

#define MAX_PPNUM	10
void register_mixclk_dcstatinfo(struct clk *clk)
{
	unsigned long pptbl[MAX_PPNUM];
	u32 idx, ppsize;

	/* FIXME: specific for peri clock whose parent is mix, itself is gate */
	ppsize = mmp_clk_mix_get_opnum(clk->parent);
	for (idx = 0; idx < ppsize; idx++)
		pptbl[idx] = mmp_clk_mix_get_oprate(clk->parent, idx);

	clk_register_dcstat(clk, pptbl, ppsize);
}

enum comp {
	CORE,
	DDR,
	AXI,
	BOOT_DVFS_MAX,
};

static unsigned long minfreq[BOOT_DVFS_MAX] = { 0, 0, 0 };
static unsigned long bootfreq[BOOT_DVFS_MAX];
static struct clk *clk[BOOT_DVFS_MAX];
static char *clk_name[BOOT_DVFS_MAX] = {"cpu", "ddr", "axi"};

static struct pm_qos_request sdh_core_qos_max;
static struct pm_qos_request sdh_ddr_qos_max;

static int __init sdh_tuning_init(void)
{
#if defined(CONFIG_CPU_FREQ) && defined(CONFIG_PM_DEVFREQ)
	struct cpufreq_frequency_table *cpufreq_table =
		cpufreq_frequency_get_table(0);
	struct devfreq_frequency_table *ddrfreq_table =
		devfreq_frequency_get_table(DEVFREQ_DDR);

	if (cpufreq_table)
		minfreq[CORE] = cpufreq_table[0].frequency;
	else
		pr_err("%s cpufreq_table get failed, use 0 to set!\n", __func__);

	if (ddrfreq_table)
		minfreq[DDR] = ddrfreq_table[0].frequency;
	else
		pr_err("%s ddrfreq_table get failed, use 0 to set!\n", __func__);
#else
	pr_info("%s CONFIG_CPU_FREQ & CONFIG_PM_DEVFREQ not defined!\n", __func__);
	minfreq[CORE] = 0;
	minfreq[DDR] = 0;
#endif
	sdh_core_qos_max.name = "sdh_tuning";
	pm_qos_add_request(&sdh_core_qos_max,
		PM_QOS_CPUFREQ_MAX, PM_QOS_DEFAULT_VALUE);

	sdh_ddr_qos_max.name = "sdh_tuning";
	pm_qos_add_request(&sdh_ddr_qos_max,
		PM_QOS_DDR_DEVFREQ_MAX, PM_QOS_DEFAULT_VALUE);

	return 0;
}
arch_initcall(sdh_tuning_init);

int sdh_tunning_scaling2minfreq(void)
{
	int i;

	for (i = 0; i < BOOT_DVFS_MAX; i++) {
		if (unlikely(!clk[i])) {
			clk[i] = __clk_lookup(clk_name[i]);
			if (!clk[i]) {
				pr_err("failed to get clk %s\n", clk_name[i]);
				return -1;
			}
		}

		if (!bootfreq[i])
			bootfreq[i] = clk_get_rate(clk[i]);
	}

	clk_set_rate(clk[AXI], minfreq[AXI]);
	pm_qos_update_request(&sdh_core_qos_max, minfreq[CORE]);
	pm_qos_update_request(&sdh_ddr_qos_max, minfreq[DDR]);

	return 0;
}

int sdh_tunning_restorefreq(void)
{
	int i;

	pm_qos_update_request(&sdh_core_qos_max, PM_QOS_DEFAULT_VALUE);
	pm_qos_update_request(&sdh_ddr_qos_max, PM_QOS_DEFAULT_VALUE);

	for (i = 0; i < BOOT_DVFS_MAX; i++) {
		if (!clk[i])
			continue;

		clk_set_rate(clk[i], bootfreq[i]);
	}

	return 0;
}

static unsigned int platvl_min, platvl_max;

void plat_set_vl_min(unsigned int vl_num)
{
	platvl_min = vl_num;
}

unsigned int plat_get_vl_min(void)
{
	return platvl_min;
}
void plat_set_vl_max(unsigned int vl_num)
{
	platvl_max = vl_num;
}

unsigned int plat_get_vl_max(void)
{
	return platvl_max;
}

static struct cpmsa_dvc_info cpmsadvcinfo;

/*
 * This interface will be used by different platform to fill CP DVC info
 */
int fillcpdvcinfo(struct cpmsa_dvc_info *dvc_info)
{
	if (!dvc_info)
		return -EINVAL;

	memcpy(&cpmsadvcinfo, dvc_info, sizeof(struct cpmsa_dvc_info));
	return 0;
}

/*
 * This interface will be used by telephony to get CP DVC info, and
 * they will use ACIPC to pass the info to CP
 */
int getcpdvcinfo(struct cpmsa_dvc_info *dvc_info)
{
	if (!dvc_info)
		return -EINVAL;

	memcpy(dvc_info, &cpmsadvcinfo, sizeof(struct cpmsa_dvc_info));
	return 0;
}
EXPORT_SYMBOL(getcpdvcinfo);

static struct comm_fuse_info comm_fuseinfo;

int plat_fill_fuseinfo(struct comm_fuse_info *info)
{
	if (!info) {
		pr_err("%s NULL info!\n", __func__);
		return -EINVAL;
	}
	memcpy(&comm_fuseinfo, info, sizeof(struct comm_fuse_info));
	return 0;
}

unsigned int get_chipprofile(void)
{
	return comm_fuseinfo.profile;
}
EXPORT_SYMBOL(get_chipprofile);

unsigned int get_iddq_105(void)
{
	return comm_fuseinfo.iddq_1050;
}
EXPORT_SYMBOL(get_iddq_105);

unsigned int get_iddq_130(void)
{
	return comm_fuseinfo.iddq_1030;
}
EXPORT_SYMBOL(get_iddq_130);

#ifdef CONFIG_DEBUG_FS
static ssize_t fab_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", comm_fuseinfo.fab);
	return simple_read_from_buffer(user_buf, count, ppos, val, strlen(val));
}
static const struct file_operations fab_fuse_ops = {
	.read		= fab_fuse_read,
};

static ssize_t svtdro_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", comm_fuseinfo.svtdro);
	return simple_read_from_buffer(user_buf, count, ppos, val, strlen(val));
}
static const struct file_operations svtdro_fuse_ops = {
	.read		= svtdro_fuse_read,
};

static ssize_t lvtdro_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", comm_fuseinfo.lvtdro);
	return simple_read_from_buffer(user_buf, count, ppos, val, strlen(val));
}
static const struct file_operations lvtdro_fuse_ops = {
	.read		= lvtdro_fuse_read,
};

static ssize_t profile_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", comm_fuseinfo.profile);
	return simple_read_from_buffer(user_buf, count, ppos, val, strlen(val));
}
static const struct file_operations profile_fuse_ops = {
	.read		= profile_fuse_read,
};

static ssize_t iddq_1050_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", comm_fuseinfo.iddq_1050);
	return simple_read_from_buffer(user_buf, count, ppos, val, strlen(val));
}
static const struct file_operations iddq_1050_fuse_ops = {
	.read		= iddq_1050_fuse_read,
};


static struct dentry *fuse;
static int __init __init_fuse_debugfs_node(void)
{
	struct dentry *lvtdro_fuse = NULL, *svtdro_fuse = NULL;
	struct dentry *fab_fuse = NULL;
	struct dentry *profile_fuse = NULL;
	struct dentry *iddq_1050_fuse = NULL;

	fuse = debugfs_create_dir("fuse", NULL);
	if (!fuse)
		return -ENOENT;

	fab_fuse = debugfs_create_file("fab", S_IRUGO, fuse, NULL, &fab_fuse_ops);
	if (!fab_fuse)
		goto err_fab_fuse;

	svtdro_fuse = debugfs_create_file("svtdro", S_IRUGO, fuse, NULL, &svtdro_fuse_ops);
	if (!svtdro_fuse)
		goto err_svtdro_fuse;

	lvtdro_fuse = debugfs_create_file("lvtdro", S_IRUGO, fuse, NULL, &lvtdro_fuse_ops);
	if (!lvtdro_fuse)
		goto err_lvtdro_fuse;

	profile_fuse = debugfs_create_file("profile", S_IRUGO, fuse, NULL, &profile_fuse_ops);
	if (!profile_fuse)
		goto err_profile_fuse;

	iddq_1050_fuse = debugfs_create_file("iddq1050", S_IRUGO, fuse, NULL, &iddq_1050_fuse_ops);
	if (!iddq_1050_fuse)
		goto err_iddq_1050_fuse;

	return 0;

err_iddq_1050_fuse:
	debugfs_remove(profile_fuse);
err_profile_fuse:
	debugfs_remove(lvtdro_fuse);
err_lvtdro_fuse:
	debugfs_remove(svtdro_fuse);
err_svtdro_fuse:
	debugfs_remove(fab_fuse);
err_fab_fuse:
	debugfs_remove(fuse);
	pr_err("debugfs entry created failed in %s\n", __func__);
	return -ENOENT;

}

arch_initcall(__init_fuse_debugfs_node);
#endif
