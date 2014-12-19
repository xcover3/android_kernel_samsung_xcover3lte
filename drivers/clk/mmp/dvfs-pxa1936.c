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
/* For chip DRO and profile */
#define NUM_PROFILES	16

#define GEU_FUSE_MANU_PARA_0 0x110
#define GEU_FUSE_MANU_PARA_1 0x114
#define GEU_FUSE_MANU_PARA_2 0x118
#define GEU_AP_CP_MP_ECC 0x11C
#define BLOCK0_RESERVED_1 0x120
#define BLOCK4_MANU_PARA1_0 0x2b4
#define BLOCK4_MANU_PARA1_1 0x2b8

static unsigned int uiprofile;

struct svtrng {
	unsigned int min;
	unsigned int max;
	unsigned int profile;
};

static struct svtrng svtrngtb[] = {
	{0, 310, 15},
	{311, 322, 14},
	{323, 335, 13},
	{336, 348, 12},
	{349, 360, 11},
	{361, 373, 10},
	{374, 379, 9},
	{380, 386, 8},
	{387, 392, 7},
	{393, 398, 6},
	{399, 405, 5},
	{406, 411, 4},
	{412, 417, 3},
	{418, 424, 2},
	{425, 0xffffffff, 1},
};

static u32 convert_svtdro2profile(unsigned int uisvtdro)
{
	unsigned int uiprofile = 0, idx;

	for (idx = 0; idx < ARRAY_SIZE(svtrngtb); idx++) {
		if (uisvtdro >= svtrngtb[idx].min &&
			uisvtdro <= svtrngtb[idx].max) {
			uiprofile = svtrngtb[idx].profile;
			break;
		}
	}

	pr_info("%s uisvtdro[%d]->profile[%d]\n", __func__, uisvtdro, uiprofile);
	return uiprofile;
}

unsigned int convertFusesToProfile_helan3(unsigned int uiFuses)
{
	unsigned int uiProfile = 0;
	unsigned int uiTemp = 3, uiTemp2 = 1;
	int i;

	for (i = 1; i < NUM_PROFILES; i++) {
		uiTemp |= uiTemp2<<(i);
		if (uiTemp == uiFuses)
			uiProfile = i;
	}
	pr_info("%s uiFuses[%d]->profile[%d]\n", __func__, uiFuses, uiProfile);
	return uiProfile;
}

struct fuse_info {
	u32 arg0;
	u32 arg1;
	u32 arg2;
	u32 arg3;
	u32 arg4;
	u32 arg5;
	u32 arg6;
};

static int __init __init_read_droinfo(void)
{
	struct fuse_info arg;
	unsigned int __maybe_unused uigeustatus = 0;
	unsigned int uiProfileFuses, uiSVCRev, uiFabRev, guiProfile;
	unsigned int uiBlock0_GEU_FUSE_MANU_PARA_0, uiBlock0_GEU_FUSE_MANU_PARA_1;
	unsigned int uiBlock0_GEU_FUSE_MANU_PARA_2, uiBlock0_GEU_AP_CP_MP_ECC;
	unsigned int  uiBlock0_BLOCK0_RESERVED_1, uiBlock4_MANU_PARA1_0, uiBlock4_MANU_PARA1_1;
	unsigned int uiAllocRev, uiFab, uiRun, uiWafer, uiX, uiY, uiParity;
	unsigned int uiLVTDRO_Avg, uiSVTDRO_Avg, uiSIDD1p05 = 0, uiSIDD1p30 = 0, smc_ret = 0;

	void __iomem *apmu_base, *geu_base;

	apmu_base = ioremap(APMU_BASE, SZ_4K);
	if (apmu_base == NULL) {
		pr_err("error to ioremap APMU base\n");
		return -EINVAL;
	}

	geu_base = ioremap(GEU_BASE, SZ_4K);
	if (geu_base == NULL) {
		pr_err("error to ioremap GEU base\n");
		return -EINVAL;
	}

	uigeustatus = __raw_readl(apmu_base + APMU_GEU);
	if (!(uigeustatus & 0x30)) {
		__raw_writel((uigeustatus | 0x30), apmu_base + APMU_GEU);
		udelay(10);
	}

	smc_ret = smc_get_fuse_info(0xc2003000, (void *)&arg);
	if (smc_ret == 0) {
		/* GEU_FUSE_MANU_PARA_0	0x110	Bank 0 [127: 96] */
		uiBlock0_GEU_FUSE_MANU_PARA_0 = arg.arg0;
		/* GEU_FUSE_MANU_PARA_1	0x114	Bank 0 [159:128] */
		uiBlock0_GEU_FUSE_MANU_PARA_1 = arg.arg1;
		/* GEU_FUSE_MANU_PARA_2	0x118	Bank 0 [191:160] */
		uiBlock0_GEU_FUSE_MANU_PARA_2 = arg.arg2;
		/* GEU_AP_CP_MP_ECC		0x11C	Bank 0 [223:192] */
		uiBlock0_GEU_AP_CP_MP_ECC = arg.arg3;
		/* BLOCK0_RESERVED_1 0x120	Bank 0 [255:224] */
		uiBlock0_BLOCK0_RESERVED_1 = arg.arg4;
		/* Fuse Block 4 191:160 */
		uiBlock4_MANU_PARA1_0 = arg.arg5;
		/* Fuse Block 4 255:192 */
		uiBlock4_MANU_PARA1_1  = arg.arg6;
	} else {
		uiBlock0_GEU_FUSE_MANU_PARA_0 = __raw_readl(geu_base + GEU_FUSE_MANU_PARA_0);
		uiBlock0_GEU_FUSE_MANU_PARA_1 = __raw_readl(geu_base + GEU_FUSE_MANU_PARA_1);
		uiBlock0_GEU_FUSE_MANU_PARA_2 = __raw_readl(geu_base + GEU_FUSE_MANU_PARA_2);
		uiBlock0_GEU_AP_CP_MP_ECC = __raw_readl(geu_base + GEU_AP_CP_MP_ECC);
		uiBlock0_BLOCK0_RESERVED_1 = __raw_readl(geu_base + BLOCK0_RESERVED_1);
		uiBlock4_MANU_PARA1_0 = __raw_readl(geu_base + BLOCK4_MANU_PARA1_0);
		uiBlock4_MANU_PARA1_1  = __raw_readl(geu_base + BLOCK4_MANU_PARA1_1);
	}

	uiAllocRev = uiBlock0_GEU_FUSE_MANU_PARA_0 & 0x7;
	uiFab = (uiBlock0_GEU_FUSE_MANU_PARA_0 >>  3) & 0x1f;
	uiRun = ((uiBlock0_GEU_FUSE_MANU_PARA_1 & 0x3) << 24) |
		((uiBlock0_GEU_FUSE_MANU_PARA_0 >> 8) & 0xffffff);
	uiWafer = (uiBlock0_GEU_FUSE_MANU_PARA_1 >>  2) & 0x1f;
	uiX = (uiBlock0_GEU_FUSE_MANU_PARA_1 >>  7) & 0xff;
	uiY = (uiBlock0_GEU_FUSE_MANU_PARA_1 >> 15) & 0xff;
	uiParity = (uiBlock0_GEU_FUSE_MANU_PARA_1 >> 23) & 0x1;
	uiSVCRev = (uiBlock0_BLOCK0_RESERVED_1    >> 13) & 0x3;
	uiFabRev = (uiBlock0_BLOCK0_RESERVED_1    >> 11) & 0x3;
	uiSVTDRO_Avg = ((uiBlock0_GEU_FUSE_MANU_PARA_2 & 0x3) << 8) |
		((uiBlock0_GEU_FUSE_MANU_PARA_1 >> 24) & 0xff);
	uiLVTDRO_Avg = (uiBlock0_GEU_FUSE_MANU_PARA_2 >>  4) & 0x3ff;
	uiProfileFuses = (uiBlock0_BLOCK0_RESERVED_1    >> 16) & 0xffff;
	uiSIDD1p05 = uiBlock4_MANU_PARA1_0 & 0x3ff;
	uiSIDD1p30 = ((uiBlock4_MANU_PARA1_1 & 0x3) << 8) |
		((uiBlock4_MANU_PARA1_0 >> 24) & 0xff);

	guiProfile = convertFusesToProfile_helan3(uiProfileFuses);

	if (guiProfile == 0)
		guiProfile = convert_svtdro2profile(uiSVTDRO_Avg);

	pr_info(" \n");
	pr_info("     *************************** \n");
	pr_info("     *  ULT: %08X%08X  * \n", uiBlock0_GEU_FUSE_MANU_PARA_1,
		uiBlock0_GEU_FUSE_MANU_PARA_0);
	pr_info("     *************************** \n");
	pr_info("     ULT decoded below \n");
	pr_info("     alloc_rev = %d\n", uiAllocRev);
	pr_info("           fab = %d\n",      uiFab);
	pr_info("           run = %d (0x%07X)\n", uiRun, uiRun);
	pr_info("         wafer = %d\n",    uiWafer);
	pr_info("             x = %d\n",        uiX);
	pr_info("             y = %d\n",        uiY);
	pr_info("        parity = %d\n",   uiParity);
	pr_info("     *************************** \n");
	if (0 == uiFabRev)
		pr_info("     *  Fab   = TSMC 28LP (%d)\n",    uiFabRev);
	else if (1 == uiFabRev)
		pr_info("     *  Fab   = SEC 28LP (%d)\n",    uiFabRev);
	else
		pr_info("     *  FabRev (%d) not currently supported\n",    uiFabRev);
	pr_info("     *  wafer = %d\n", uiWafer);
	pr_info("     *  x     = %d\n",     uiX);
	pr_info("     *  y     = %d\n",     uiY);
	pr_info("     *************************** \n");
	pr_info("     *  Iddq @ 1.05V = %dmA\n",   uiSIDD1p05);
	pr_info("     *  Iddq @ 1.30V = %dmA\n",   uiSIDD1p30);
	pr_info("     *************************** \n");
	pr_info("     *  LVTDRO = %d\n",   uiLVTDRO_Avg);
	pr_info("     *  SVTDRO = %d\n",   uiSVTDRO_Avg);
	pr_info("     *  SVC Revision = %2d\n", uiSVCRev);
	pr_info("     *  SVC Profile  = %2d\n", guiProfile);
	pr_info("     *************************** \n");
	pr_info(" \n");

	uiprofile = guiProfile;
	return 0;

}

/* components frequency combination */
/* FIXME: adjust according to SVC */
static unsigned long freqs_cmb_1936[VM_RAIL_MAX][VL_MAX] = {
	/*LV0,     LV1,     LV2,    LV3,    LV4,     LV5,     LV6,     LV7  */
	/* CLST0 */
	{ 312000, 312000, 624000, 832000, 832000, 1057000, 1248000, 1248000 },
	/* CLST1 */
	{ 312000, 312000, 832000, 1248000, 1595000, 1595000, 1595000, 1803000 },
	/* DDR */
	{ 312000,  312000, 528000, 624000, 667000, 667000, 667000, 797000},
	/* AXI */
	{ 208000,  208000, 312000, 312000, 312000, 312000, 312000, 312000 },
	/* GC3D */
	{      0,  416000, 416000, 832000, 832000, 832000, 832000, 832000 },
	/* GC2D */
	{      0,  208000, 312000, 416000, 416000,  416000, 416000, 416000 },
	/* GCSHADER */
	{      0,  416000, 416000, 832000, 832000, 832000, 832000, 832000 },
	/* GC ACLK */
	{      0,  312000, 312000, 416000, 528000, 528000, 528000, 528000 },
	/* VPU */
	{ 208000,  312000, 416000, 528750, 528750, 528750, 528750, 528750 },
	/* ISP */
	{      0,  208000, 312000, 416000, 499000, 499000, 499000, 499000 },
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
	/*LV0, LV1,  LV2,  LV3,  LV4,  LV5,  LV6,  LV7  */
	{975, 1000, 1100, 1200, 1225, 1300, 1300, 1300},/* Profile0 */
	{975, 975,  975,  1038, 1050, 1100, 1138, 1175},/* Profile1 */
	{975, 975,  988,  1038, 1063, 1113, 1150, 1188},/* Profile2 */
	{975, 975,  988,  1050, 1063, 1113, 1163, 1200},/* Profile3 */
	{975, 975,  988,  1050, 1063, 1125, 1175, 1213},/* Profile4 */
	{975, 975,  1000, 1063, 1075, 1138, 1188, 1225},/* Profile5 */
	{975, 975,  1000, 1063, 1075, 1150, 1200, 1238},/* Profile6 */
	{975, 975,  1013, 1075, 1075, 1150, 1213, 1250},/* Profile7 */
	{975, 975,  1013, 1088, 1088, 1163, 1225, 1250},/* Profile8 */
	{975, 975,  1025, 1088, 1088, 1175, 1238, 1263},/* Profile9 */
	{975, 975,  1050, 1125, 1125, 1200, 1263, 1288},/* Profile10 */
	{975, 975,  1050, 1125, 1125, 1225, 1288, 1300},/* Profile11 */
	{975, 975,  1075, 1150, 1150, 1250, 1300, 1300},/* Profile12 */
	{975, 975,  1075, 1175, 1175, 1275, 1300, 1300},/* Profile13 */
	{975, 1000, 1100, 1175, 1200, 1288, 1300, 1300},/* Profile14 */
	{975, 1000, 1100, 1200, 1225, 1300, 1300, 1300},/* Profile14 */
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
	.cp_pmudvc_lvl = VL1,
	.dp_pmudvc_lvl = VL3,
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

	__init_read_droinfo();

	dvc_pxa1936_info.millivolts =
		vm_millivolts_1936_svcumc[uiprofile];

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

