/*
 *  linux/drivers/clk/mmp/dvfs-pxa1936.c
 *
 *  based on drivers/clk/mmp/dvfs-pxa1908.c
 *  Copyright (C) 2014 Mrvl, Inc. by Liang Chen <chl@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/clk/mmp_sdh_tuning.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/88pm80x.h>
#include <linux/clk/dvfs-dvc.h>

#include <linux/cputype.h>
#include "clk-plat.h"
#include "clk.h"

/* components that affect the vmin */
enum dvfs_comp {
	CORE = 0,
	CORE1,
	DDR,
	AXI,
	GC3D,
	GC2D,
	GC_SHADER,
	GCACLK,
	VPU,
	ISP,
	SDH0,
	SDH1,
	SDH2,
	VM_RAIL_MAX,
};

#define VL_MAX	8

#define ACTIVE_RAIL_FLAG	(AFFECT_RAIL_ACTIVE)
#define ACTIVE_M2_RAIL_FLAG	(AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2)
#define ACTIVE_M2_D1P_RAIL_FLAG \
	(AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2 | AFFECT_RAIL_D1P)

#define APMU_BASE		0xd4282800
#define GEU_BASE		0xd4292800
#define HWDVC_BASE		0xd4050000

/* Fuse information related register definition */
#define APMU_GEU		0x068
#define MANU_PARA_31_00		0x410
#define MANU_PARA_63_32		0x414
#define MANU_PARA_95_64		0x418
#define BLOCK0_224_255		0x420
/* For chip unique ID */
#define UID_H_32			0x48C
#define UID_L_32			0x4A8
/* For chip DRO and profile */
#define NUM_PROFILES	16

/* components frequency combination */
/* FIXME: adjust according to SVC */
static unsigned long freqs_cmb_1936[VM_RAIL_MAX][VL_MAX] = {
	/* CLST0 */
	{ 0, 312000, 624000, 832000, 1057000, 1248000, 1248000, 1248000 },
	/* CLST1 */
	{ 0, 312000, 624000, 832000, 1057000, 1248000, 1248000, 1803000 },
	/* DDR */
	{ 312000, 312000, 416000, 528000, 528000, 624000, 667000, 797000},
	/* AXI */
	{ 0, 208000, 208000, 312000, 312000, 312000, 312000, 312000 },
	/* GC3D */
	{ 156000, 312000, 416000, 416000, 710000, 710000, 710000, 832000 },
	/* GC2D */
	{ 156000, 156000, 312000, 312000, 312000, 416000, 416000, 624000 },
	/* GCSHADER */
	{ 156000, 312000, 416000, 416000, 710000, 710000, 710000, 832000 },
	/* GC ACLK */
	{ 312000, 312000, 312000, 416000, 416000, 416000, 624000, 624000 },
	/* VPU */
	{ 156000, 312000, 416000, 528000, 528000, 528000, 528000, 528000 },
	/* ISP */
	{ 0, 0, 208000, 312000, 312000, 312000, 312000, 416000 },
	/* SDH0 dummy dvfs clk*/
	{ DUMMY_VL_TO_KHZ(0), DUMMY_VL_TO_KHZ(1), DUMMY_VL_TO_KHZ(2), DUMMY_VL_TO_KHZ(3),
	  DUMMY_VL_TO_KHZ(4), DUMMY_VL_TO_KHZ(5), DUMMY_VL_TO_KHZ(6), DUMMY_VL_TO_KHZ(7)
	},
	/* SDH1 dummy dvfs clk*/
	{ DUMMY_VL_TO_KHZ(0), DUMMY_VL_TO_KHZ(1), DUMMY_VL_TO_KHZ(2), DUMMY_VL_TO_KHZ(3),
	  DUMMY_VL_TO_KHZ(4), DUMMY_VL_TO_KHZ(5), DUMMY_VL_TO_KHZ(6), DUMMY_VL_TO_KHZ(7)
	},
	/* SDH2 dummy dvfs clk*/
	{ DUMMY_VL_TO_KHZ(0), DUMMY_VL_TO_KHZ(1), DUMMY_VL_TO_KHZ(2), DUMMY_VL_TO_KHZ(3),
	  DUMMY_VL_TO_KHZ(4), DUMMY_VL_TO_KHZ(5), DUMMY_VL_TO_KHZ(6), DUMMY_VL_TO_KHZ(7)
	},
};

/* 8 VLs PMIC setting */
/* FIXME: adjust according to SVC */
static int vm_millivolts_1936_svcumc[][VL_MAX] = {
	{1250, 1250, 1250, 1250, 1250, 1250, 1250, 1250},
};

/*
 * dvfs_rail_component.freqs is inited dynamicly, due to different stepping
 * may have different VL combination
 */
static struct dvfs_rail_component vm_rail_comp_tbl_dvc[VM_RAIL_MAX] = {
	INIT_DVFS("clst0", true, ACTIVE_RAIL_FLAG, NULL),
	INIT_DVFS("clst1", true, ACTIVE_RAIL_FLAG, NULL),
	INIT_DVFS("ddr", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("axi", true, ACTIVE_M2_RAIL_FLAG, NULL),
	INIT_DVFS("gc3d_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("gc2d_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("gcsh_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("gcbus_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("vpufunc_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("isp_pipe_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("sdh0_dummy", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("sdh1_dummy", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("sdh2_dummy", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
};

static int set_pmic_volt(unsigned int lvl, unsigned int mv)
{
	return pm8xx_dvc_setvolt(PM800_ID_BUCK1, lvl, mv * mV2uV);
}

static int get_pmic_volt(unsigned int lvl)
{
	int uv = 0, ret = 0;

	ret = pm8xx_dvc_getvolt(PM800_ID_BUCK1, lvl, &uv);
	if (ret < 0)
		return ret;
	return DIV_ROUND_UP(uv, mV2uV);
}

static struct dvc_plat_info dvc_pxa1936_info = {
	.comps = vm_rail_comp_tbl_dvc,
	.num_comps = ARRAY_SIZE(vm_rail_comp_tbl_dvc),
	.num_volts = VL_MAX,
	.cp_pmudvc_lvl = VL2,
	.dp_pmudvc_lvl = VL4,
	.set_vccmain_volt = set_pmic_volt,
	.get_vccmain_volt = get_pmic_volt,
	.pmic_maxvl = 8,
	.pmic_rampup_step = 12500,
	/* by default print the debug msg into logbuf */
	.dbglvl = 1,
	.regname = "vccmain",
	/* real measured 8us + 4us, PMIC suggestes 16us for 12.5mV/us */
	.extra_timer_dlyus = 16,
};

int __init setup_pxa1936_dvfs_platinfo(void)
{
	void __iomem *hwdvc_base;
	enum dvfs_comp idx;
	struct dvc_plat_info *plat_info = &dvc_pxa1936_info;
	unsigned long (*freqs_cmb)[VL_MAX];

	/* we don't support get the fuse info now. will add it later.*/

	/* FIXME: Here may need to identify chip stepping and profile */
	dvc_pxa1936_info.millivolts =
		vm_millivolts_1936_svcumc[0];
	freqs_cmb = freqs_cmb_1936;
	plat_set_vl_min(0);
	plat_set_vl_max(dvc_pxa1936_info.num_volts);

	/* register the platform info into dvfs-dvc.c(hwdvc driver) */
	hwdvc_base = ioremap(HWDVC_BASE, SZ_16K);
	if (hwdvc_base == NULL) {
		pr_err("error to ioremap hwdvc base\n");
		return -ENOMEM;
	}
	plat_info->dvc_reg_base = hwdvc_base;
	for (idx = CORE; idx < VM_RAIL_MAX; idx++)
		plat_info->comps[idx].freqs = freqs_cmb[idx];
	return dvfs_setup_dvcplatinfo(plat_info);
}

