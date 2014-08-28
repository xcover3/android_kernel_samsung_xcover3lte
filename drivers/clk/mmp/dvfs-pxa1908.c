/*
 *  linux/drivers/clk/mmp/dvfs-pxa1908.c
 *
 *  based on drivers/clk/mmp/dvfs-pxa1u88.c
 *  Copyright (C) 2013 Mrvl, Inc. by Zhoujie Wu <zjwu@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/mfd/88pm80x.h>
#include <linux/clk/dvfs-dvc.h>

#include <linux/cputype.h>

/* components that affect the vmin */
enum dvfs_comp {
	CORE = 0,
	DDR,
	AXI,
	GC3D,
	GC2D,
	GC_SHADER,
	GCACLK,
	VPU,
	ISP,
	/* add fake sdh later */
	VM_RAIL_MAX,
};

#define VL_MAX	8

#define ACTIVE_RAIL_FLAG	(AFFECT_RAIL_ACTIVE)
#define ACTIVE_M2_RAIL_FLAG	(AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2)
#define ACTIVE_M2_D1P_RAIL_FLAG \
	(AFFECT_RAIL_ACTIVE | AFFECT_RAIL_M2 | AFFECT_RAIL_D1P)

#define APMU_BASE		0xd4282800
#define GEU_BASE		0xd4201000
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
#define NUM_PROFILES	13
/* used to save fuse parameter */
static unsigned int uimanupara_31_00;
static unsigned int uimanupara_63_32;
static unsigned int uimanupara_95_64;
static unsigned int uifuses;
static unsigned int uiprofile;
static unsigned int uidro;

struct svtrng {
	unsigned int min;
	unsigned int max;
	unsigned int profile;
};

static struct svtrng svtrngtb_z0[] = {
	{0, 295, 15},
	{296, 308, 14},
	{309, 320, 13},
	{321, 332, 12},
	{333, 344, 11},
	{345, 357, 10},
	{358, 370, 9},
	{371, 382, 8},
	{383, 394, 7},
	{395, 407, 6},
	{408, 420, 5},
	{421, 432, 4},
	{433, 444, 3},
	{445, 0xffffffff, 2},
	/* NOTE: rsved profile 1, by default use profile 0 */
};

static u32 __maybe_unused convert_svtdro2profile(unsigned int uisvtdro)
{
	unsigned int uiprofile = 0, idx;

	for (idx = 0; idx < ARRAY_SIZE(svtrngtb_z0); idx++) {
		if (uisvtdro >= svtrngtb_z0[idx].min &&
			uisvtdro <= svtrngtb_z0[idx].max) {
			uiprofile = svtrngtb_z0[idx].profile;
			break;
		}
	}

	return uiprofile;
}

static int __init __init_read_droinfo(void)
{

	/* FIXME: how to read fuse on ULC? */
	uimanupara_31_00 = 0;
	uimanupara_63_32 = 0;
	uidro		= (uimanupara_95_64 >>  4) & 0x3ff;
	/* bit 240 ~ 255 for Profile information */
	uifuses = (uifuses >> 16) & 0x0000FFFF;

	pr_info("uiprofile = %d, DRO = %d\n", uiprofile, uidro);

	return 0;
}

/* components frequency combination */
/* FIXME: adjust according to SVC */
static unsigned long freqs_cmb_1908[VM_RAIL_MAX][VL_MAX] = {
	/* CORE */
	{ 312000, 624000, 832000, 1057000, 1057000, 1248000, 1248000, 1526000 },
	/* DDR */
	{ 312000, 416000, 416000, 667000,  667000, 667000, 667000, 667000},
	/* AXI */
	{ 0, 156000, 208000, 208000, 312000, 312000, 312000, 312000 },
	/* GC3D */
	{ 0, 0, 416000, 416000, 624000, 624000, 705000, 705000 },
	/* GC2D */
	{ 0, 0, 0, 0, 416000, 416000, 416000, 416000 },
	/* GCSHADER */
	{ 0, 0, 416000, 416000, 624000, 624000, 705000, 705000 },
	/* GC ACLK */
	{ 0, 0, 0, 416000, 416000, 416000, 416000, 416000 },
	/* VPU */
	{ 0, 312000, 312000, 533000, 533000, 533000, 533000, 533000 },
	/* ISP */
	{ 0, 0, 208000, 312000, 312000, 416000, 416000, 416000 },
};

/* 8 VLs PMIC setting */
/* FIXME: adjust according to SVC */
static int vm_millivolts_1908_svcumc[][VL_MAX] = {
	{988, 1025, 1100, 1125, 1175, 1288, 1288, 1288},
};

/*
 * dvfs_rail_component.freqs is inited dynamicly, due to different stepping
 * may have different VL combination
 */
static struct dvfs_rail_component vm_rail_comp_tbl_dvc[VM_RAIL_MAX] = {
	INIT_DVFS("cpu", true, ACTIVE_RAIL_FLAG, NULL),
	INIT_DVFS("ddr", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("axi", true, ACTIVE_M2_RAIL_FLAG, NULL),
	INIT_DVFS("gc3d_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("gc2d_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("gcsh_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("gcbus_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("vpufunc_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
	INIT_DVFS("isp_pipe_clk", true, ACTIVE_M2_D1P_RAIL_FLAG, NULL),
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

static struct dvc_plat_info dvc_pxa1908_info = {
	.comps = vm_rail_comp_tbl_dvc,
	.num_comps = ARRAY_SIZE(vm_rail_comp_tbl_dvc),
	.num_volts = VL_MAX,
	/* FIXME: CP/MSA VL may need to be adjusted according to SVC */
	.cp_pmudvc_lvl = VL0,
	.dp_pmudvc_lvl = VL0,
	.set_vccmain_volt = set_pmic_volt,
	.get_vccmain_volt = get_pmic_volt,
	.pmic_maxvl = 8,
	.pmic_rampup_step = 12500,
	/* by default print the debug msg into logbuf */
	.dbglvl = 0,
	.regname = "BUCK1",
	/* real measured 8us + 4us, PMIC suggestes 16us for 12.5mV/us */
	.extra_timer_dlyus = 16,
};

int __init setup_pxa1908_dvfs_platinfo(void)
{
	void __iomem *hwdvc_base;
	enum dvfs_comp idx;
	struct dvc_plat_info *plat_info = &dvc_pxa1908_info;
	unsigned long (*freqs_cmb)[VL_MAX];

	__init_read_droinfo();

	/* FIXME: Here may need to identify chip stepping and profile */
	dvc_pxa1908_info.millivolts =
		vm_millivolts_1908_svcumc[0];
	freqs_cmb = freqs_cmb_1908;

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
