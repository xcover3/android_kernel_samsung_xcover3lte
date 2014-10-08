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

static unsigned long minfreq[BOOT_DVFS_MAX] = {
	156 * MHZ, 156 * MHZ, 78 * MHZ
};
static unsigned long bootfreq[BOOT_DVFS_MAX];
static struct clk *clk[BOOT_DVFS_MAX];
static char *clk_name[BOOT_DVFS_MAX] = {"cpu", "ddr", "axi"};

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

		clk_set_rate(clk[i], minfreq[i]);
	}

	return 0;
}

int sdh_tunning_restorefreq(void)
{
	int i;

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
