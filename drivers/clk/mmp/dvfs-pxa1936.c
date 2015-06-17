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
#include <linux/mfd/88pm88x.h>
#include <linux/mfd/88pm886.h>
#include <linux/clk/dvfs-dvc.h>
#include <linux/clk/mmpcpdvc.h>
#include <linux/clk/mmpfuse.h>
#include <linux/debugfs-pxa.h>

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
	VM_RAIL_NO_SDH_MAX = SDH0,
};

enum chip_stepping_id {
	TSMC_28LP = 0,
	TSMC_28LP_B020A,
	TSMC_28LP_B121A,
	SEC_28LP_A1,
	SEC_28LP_A1D1A,
	SEC_28LP_A2D2A,
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
#define CONFIG_SVC_TSMC 1

static unsigned int uiprofile;
static unsigned int helan3_maxfreq;
static struct comm_fuse_info fuseinfo;
static unsigned int fab_rev;
static enum chip_stepping_id chip_stepping;
static unsigned int svc_version;
static int min_lv0_voltage;
static int min_cp_voltage;
static int min_gc_voltage;
static char dvfs_comp_name[VM_RAIL_MAX][20] = {
	"CORE", "CORE1", "DDR", "AXI",
	"GC3D", "GC2D", "GC_SHADER", "GCACLK",
	"VPU", "ISP", "SDH0", "SDH1", "SDH2",};
static struct cpmsa_dvc_info cpmsa_dvc_info_temp;
unsigned int uiYldTableEn;
unsigned long (*freqs_cmb)[VL_MAX];
int *millivolts;
int ddr_800M_tsmc_svc[] = {1050, 950, 950, 950, 950, 950, 950, 950, 963,
963, 963, 975, 1000, 1012, 1025, 1050};
int ddr_800M_sec_svc[] = {1075, 975, 975, 975, 975, 975, 975, 975, 975,
975, 975, 975, 1000, 1025, 1050, 1075};

struct svtrng {
	unsigned int min;
	unsigned int max;
	unsigned int profile;
};

static struct svtrng svtrngtb[] = {
	{290, 310, 15},
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
	{425, 440, 1},
};

void convert_max_freq(unsigned int uiCpuFreq)
{
	switch (uiCpuFreq) {
	case 0x0:
	case 0x5:
	case 0x6:
		pr_info("%s Part SKU is 1.5GHz; FuseBank0[179:174] = 0x%X", __func__, uiCpuFreq);
		helan3_maxfreq = CORE_1p5G;
		break;
	case 0x1:
	case 0xA:
		pr_info("%s Part SKU is 1.8GHz; FuseBank0[179:174] = 0x%X", __func__, uiCpuFreq);
		helan3_maxfreq = CORE_1p8G;
		break;
	case 0x2:
	case 0x3:
		pr_info("%s Part SKU is 2GHz; FuseBank0[179:174] = 0x%X", __func__, uiCpuFreq);
		helan3_maxfreq = CORE_2p0G;
		break;
	default:
		pr_info("%s ERROR: Fuse value (0x%X) not supported,default max freq 1.5G",
		__func__, uiCpuFreq);
		helan3_maxfreq = CORE_1p5G;
		break;
	}
	return;
}


unsigned int get_helan3_max_freq(void)
{
	return helan3_maxfreq;
}

static u32 convert_svtdro2profile(unsigned int uisvtdro)
{
	unsigned int uiprofile = 0, idx;

	if (uisvtdro >= 290 && uisvtdro <= 440) {
		for (idx = 0; idx < ARRAY_SIZE(svtrngtb); idx++) {
			if (uisvtdro >= svtrngtb[idx].min &&
				uisvtdro <= svtrngtb[idx].max) {
				uiprofile = svtrngtb[idx].profile;
				break;
			}
		}
	} else {
		uiprofile = 0;
		pr_info("SVTDRO is either not programmed or outside of the SVC spec range: %d",
			uisvtdro);
	}

	pr_info("%s uisvtdro[%d]->profile[%d]\n", __func__, uisvtdro, uiprofile);
	return uiprofile;
}

unsigned int convertFusesToProfile_helan3(unsigned int uiFuses)
{
	unsigned int uiProfile = 0;
	unsigned int uiTemp = 1, uiTemp2 = 1;
	int i;

	for (i = 1; i < NUM_PROFILES; i++) {
		if (uiTemp == uiFuses)
			uiProfile = i;
		uiTemp |= uiTemp2 << (i);
	}

	pr_info("%s uiFuses[0x%x]->profile[%d]\n", __func__, uiFuses, uiProfile);
	return uiProfile;
}

unsigned int convert_fab_revision(unsigned int fab_revision)
{
	unsigned int ui_fab = TSMC;
	if (fab_revision == 0)
		ui_fab = TSMC;
	else if (fab_revision == 1)
		ui_fab = SEC;

	return ui_fab;
}

unsigned int convert_svc_voltage(unsigned int uiGc3d_Vlevel,
	unsigned int uiLPP_LoPP_Profile, unsigned int uiPCPP_Profile)
{
	switch (uiLPP_LoPP_Profile) {
	case 0:
		min_lv0_voltage = 950;
		break;
	case 1:
		min_lv0_voltage = 975;
		break;
	case 3:
		min_lv0_voltage = 1025;
		break;
	default:
		min_lv0_voltage = 950;
		break;
	}
	switch (uiPCPP_Profile) {
	case 0:
		min_cp_voltage = 963;
		break;
	case 1:
		min_cp_voltage = 988;
		break;
	case 3:
		min_cp_voltage = 1038;
		break;
	default:
		min_cp_voltage = 963;
		break;
	}
	switch (uiGc3d_Vlevel) {
	case 0:
		min_gc_voltage = 950;
		break;
	case 1:
		min_gc_voltage = 975;
		break;
	case 3:
		min_gc_voltage = 1050;
		break;
	default:
		min_gc_voltage = 950;
		break;
	}

	return 0;
}


unsigned int get_helan3_svc_version(void)
{
	return svc_version;
}

unsigned int convert_svc_version(unsigned int uiSVCRev, unsigned int uiFabRev)
{
	if (uiSVCRev == 0) {
		if (!uiYldTableEn)
			svc_version = SEC_SVC_1_01;
		else {
			if (uiFabRev == 0)
				svc_version = SVC_1_11;
			else if (uiFabRev == 1)
				svc_version = SEC_SVC_1_01;
			}
	} else if (uiSVCRev == 1) {
		if (uiFabRev == 0)
			svc_version = NO_SUPPORT;
		else if (uiFabRev == 1)
			svc_version = SVC_1_11;
	} else
			svc_version = NO_SUPPORT;

	if (helan3_maxfreq == CORE_1p8G)
		svc_version = SVC_TSMC_1p8G;

	if ((chip_stepping == TSMC_28LP_B020A) ||
		(chip_stepping == TSMC_28LP_B121A))
		svc_version = SVC_TSMC_B0;

	return svc_version;
}

char *convert_step_revision(unsigned int step_id)
{
	if (fab_rev == TSMC) {
		if (step_id == 0x0) {
			chip_stepping = TSMC_28LP;
			return "TSMC 28LP";
		} else if (step_id == 0x1) {
			chip_stepping = TSMC_28LP_B020A;
			return "TSMC 28LP_B020A";
		} else if (step_id == 0x2) {
			chip_stepping = TSMC_28LP_B121A;
			return "TSMC 28LP_B121A";
		}
	} else if (fab_rev == SEC) {
		if (step_id == 0x0) {
			chip_stepping = SEC_28LP_A1;
			return "SEC 28LP_A1";
		} else if (step_id == 0x1) {
			chip_stepping = SEC_28LP_A1D1A;
			return "SEC 28LP_A1D1A";
		} else if (step_id == 0x2) {
			chip_stepping = SEC_28LP_A2D2A;
			return "SEC 28LP_A2D2A";
		}
	}
	return "not SEC and TSMC chip";
}

static int __init __init_read_droinfo(void)
{
	struct fuse_info arg;
	unsigned int __maybe_unused uigeustatus = 0;
	unsigned int uiProfileFuses, uiSVCRev, uiFabRev, guiProfile;
	unsigned int uiBlock0_GEU_FUSE_MANU_PARA_0, uiBlock0_GEU_FUSE_MANU_PARA_1;
	unsigned int uiBlock0_GEU_FUSE_MANU_PARA_2, uiBlock0_GEU_AP_CP_MP_ECC;
	unsigned int uiBlock0_BLOCK0_RESERVED_1, uiBlock4_MANU_PARA1_0;
	unsigned int uiAllocRev, uiRun, uiWafer, uiX, uiY, uiParity, ui_step_id;
	unsigned int uiLVTDRO_Avg, uiSVTDRO_Avg, uiSIDD1p05 = 0, uiSIDD1p30 = 0, smc_ret = 0;
	unsigned int uiCpuFreq, uiBlock4_MANU_PARA1_1;
	unsigned int uiskusetting = 0, uiGc3d_Vlevel;
	unsigned int uiLPP_LoPP_Profile, uiPCPP_Profile, uiNegEdge;
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
	uiRun = ((uiBlock0_GEU_FUSE_MANU_PARA_1 & 0x3) << 24) |
		((uiBlock0_GEU_FUSE_MANU_PARA_0 >> 8) & 0xffffff);
	uiWafer = (uiBlock0_GEU_FUSE_MANU_PARA_1 >>  2) & 0x1f;
	uiX = (uiBlock0_GEU_FUSE_MANU_PARA_1 >>  7) & 0xff;
	uiY = (uiBlock0_GEU_FUSE_MANU_PARA_1 >> 15) & 0xff;
	uiParity = (uiBlock0_GEU_FUSE_MANU_PARA_1 >> 23) & 0x1;
	uiSVTDRO_Avg = ((uiBlock0_GEU_FUSE_MANU_PARA_2 & 0x3) << 8) |
		((uiBlock0_GEU_FUSE_MANU_PARA_1 >> 24) & 0xff);
	uiLVTDRO_Avg = (uiBlock0_GEU_FUSE_MANU_PARA_2 >>  4) & 0x3ff;
	uiProfileFuses = (uiBlock0_BLOCK0_RESERVED_1    >> 16) & 0xffff;
	uiSIDD1p05 = uiBlock4_MANU_PARA1_0 & 0x3ff;
	uiSIDD1p30 = ((uiBlock4_MANU_PARA1_1 & 0x3) << 8) |
		((uiBlock4_MANU_PARA1_0 >> 24) & 0xff);
	uiCpuFreq = (uiBlock0_GEU_FUSE_MANU_PARA_2 >>  14) & 0x3f;
	uiFabRev = (uiBlock4_MANU_PARA1_1 >> 4) & 0x3;
	fab_rev = convert_fab_revision(uiFabRev);
	/*bit 201 ~ 202 for UDR voltage*/
	uiskusetting = (uiBlock4_MANU_PARA1_1 >> 9) & 0x3;
	ui_step_id = (uiBlock0_GEU_FUSE_MANU_PARA_2 >> 2) & 0x3;
	uiSVCRev = (uiBlock4_MANU_PARA1_1 >> 6) & 0x3;
	uiYldTableEn = (uiBlock0_BLOCK0_RESERVED_1 >> 9) & 0x1;
	convert_max_freq(uiCpuFreq);
	convert_step_revision(ui_step_id);
	convert_svc_version(uiSVCRev, uiFabRev);

	if (uiYldTableEn == 1) {
		uiGc3d_Vlevel = (uiBlock0_BLOCK0_RESERVED_1 >> 10) & 0x3;
		uiLPP_LoPP_Profile = (uiBlock0_BLOCK0_RESERVED_1 >> 12) & 0x3;
		uiPCPP_Profile = (uiBlock0_BLOCK0_RESERVED_1 >> 14) & 0x3;
		convert_svc_voltage(uiGc3d_Vlevel, uiLPP_LoPP_Profile,
			uiPCPP_Profile);

		uiNegEdge = (uiBlock0_BLOCK0_RESERVED_1 >> 31) & 0x1;
		/* bit 0 = 700mv,bit 1 = 800mv, opposite*/
		uiskusetting = !uiNegEdge;
	}

	guiProfile = convertFusesToProfile_helan3(uiProfileFuses);

	if (guiProfile == 0)
		guiProfile = convert_svtdro2profile(uiSVTDRO_Avg);

	fuseinfo.fab = fab_rev;
	fuseinfo.lvtdro = uiLVTDRO_Avg;
	fuseinfo.svtdro = uiSVTDRO_Avg;

	fuseinfo.profile = guiProfile;
	fuseinfo.iddq_1050 = uiSIDD1p05;
	fuseinfo.iddq_1030 = uiSIDD1p30;
	fuseinfo.skusetting = uiskusetting;
	plat_fill_fuseinfo(&fuseinfo);

	pr_info(" \n");
	pr_info("     *************************** \n");
	pr_info("     *  ULT: %08X%08X  * \n", uiBlock0_GEU_FUSE_MANU_PARA_1,
		uiBlock0_GEU_FUSE_MANU_PARA_0);
	pr_info("     *************************** \n");
	pr_info("     ULT decoded below \n");
	pr_info("     alloc_rev = %d\n", uiAllocRev);
	pr_info("           fab = %d\n",      fab_rev);
	pr_info("           run = %d (0x%07X)\n", uiRun, uiRun);
	pr_info("         wafer = %d\n",    uiWafer);
	pr_info("             x = %d\n",        uiX);
	pr_info("             y = %d\n",        uiY);
	pr_info("        parity = %d\n",   uiParity);
	pr_info("        UDR voltage bit = %d\n", uiskusetting);
	pr_info("     *************************** \n");
	if (0 == fab_rev)
		pr_info("     *  Fab   = TSMC 28LP (%d)\n",    fab_rev);
	else if (1 == fab_rev)
		pr_info("     *  Fab   = SEC 28LP (%d)\n",    fab_rev);
	else
		pr_info("     *  FabRev (%d) not currently supported\n",    fab_rev);
	pr_info("     *  ui_step_id is %d\n", ui_step_id);
	pr_info("     *  chip_stepping is %d\n", chip_stepping);
	pr_info("     *  chip step is %s\n", convert_step_revision(ui_step_id));
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
	pr_info("     *  SVC Table Version  = %2d\n", svc_version);
	pr_info("     *************************** \n");
	pr_info("\n");

	uiprofile = guiProfile;
	return 0;

}

#define sdh_dvfs { DUMMY_VL_TO_KHZ(0), DUMMY_VL_TO_KHZ(1), DUMMY_VL_TO_KHZ(2), DUMMY_VL_TO_KHZ(3),\
		  DUMMY_VL_TO_KHZ(4), DUMMY_VL_TO_KHZ(5), DUMMY_VL_TO_KHZ(6), DUMMY_VL_TO_KHZ(7)},\
		{ DUMMY_VL_TO_KHZ(0), DUMMY_VL_TO_KHZ(1), DUMMY_VL_TO_KHZ(2), DUMMY_VL_TO_KHZ(3),\
		  DUMMY_VL_TO_KHZ(4), DUMMY_VL_TO_KHZ(5), DUMMY_VL_TO_KHZ(6), DUMMY_VL_TO_KHZ(7)},\
		{ DUMMY_VL_TO_KHZ(0), DUMMY_VL_TO_KHZ(1), DUMMY_VL_TO_KHZ(2), DUMMY_VL_TO_KHZ(3),\
		  DUMMY_VL_TO_KHZ(4), DUMMY_VL_TO_KHZ(5), DUMMY_VL_TO_KHZ(6), DUMMY_VL_TO_KHZ(7)}

static unsigned long freqs_cmb_1936_tsmc[][VM_RAIL_MAX][VL_MAX] = {
	[0] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[1] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 624000, 624000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 667000, 667000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[2] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 624000, 624000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[3] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[4] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 832000, 832000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[5] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[6] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[7] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[8] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[9] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[10] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[11] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[12] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[13] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[14] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[15] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
};

/* 8 VLs PMIC setting */
/* FIXME: adjust according to SVC */
static int vm_millivolts_1936_svc_tsmc[][VL_MAX] = {
	/*LV0,  LV1,  LV2,  LV3,  LV4,  LV5,  LV6,  LV7 */
	{1000, 1038, 1063, 1113, 1125, 1188, 1263, 1300},/* Profile0 */
	{950, 975, 975, 1000, 1000, 1038, 1025, 1150},/* Profile1 */
	{950, 975, 975, 1000, 1000, 1038, 1038, 1163},/* Profile2 */
	{950, 975, 975, 1000, 1000, 1050, 1050, 1175},/* Profile3 */
	{950, 975, 975, 1000, 1000, 1050, 1050, 1188},/* Profile4 */
	{950, 975, 975, 1000, 1013, 1063, 1063, 1200},/* Profile5 */
	{950, 975, 975, 1000, 1013, 1063, 1075, 1213},/* Profile6 */
	{950, 975, 988, 1013, 1025, 1075, 1088, 1225},/* Profile7 */
	{950, 975, 988, 1013, 1025, 1088, 1100, 1238},/* Profile8 */
	{950, 975, 988, 1025, 1038, 1088, 1113, 1250},/* Profile9 */
	{950, 975, 1000, 1038, 1050, 1113, 1138, 1275},/* Profile10 */
	{950, 988, 1013, 1050, 1063, 1125, 1163, 1288},/* Profile11 */
	{963, 1000, 1025, 1063, 1075, 1150, 1188, 1300},/* Profile12 */
	{975, 1000, 1038, 1075, 1088, 1163, 1213, 1275},/* Profile13 */
	{988, 1013, 1050, 1100, 1100, 1175, 1238, 1275},/* Profile14 */
	{1000, 1038, 1063, 1113, 1125, 1188, 1263, 1300},/* Profile15 */
};

static unsigned long freqs_cmb_1936_sec[][VM_RAIL_MAX][VL_MAX] = {
	[0] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 312000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 1057000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 312000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[1] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 624000, 624000, 624000, 624000, 1057000, 1057000, 1248000}, /* CLUSTER0 */
		{832000, 832000, 832000, 832000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{312000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{416000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{312000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[2] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 624000, 624000, 624000, 624000, 1057000, 1057000, 1248000}, /* CLUSTER0 */
		{832000, 832000, 832000, 832000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{312000, 312000, 312000, 312000, 416000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{416000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{312000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[3] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 416000, 416000, 624000, 624000, 1057000, 1057000, 1248000}, /* CLUSTER0 */
		{832000, 832000, 832000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{312000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{416000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{312000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[4] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 416000, 416000, 624000, 624000, 832000, 1057000, 1248000}, /* CLUSTER0 */
		{832000, 832000, 832000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{416000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{312000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[5] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 416000, 416000, 624000, 624000, 832000, 1057000, 1248000}, /* CLUSTER0 */
		{624000, 832000, 832000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{312000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[6] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 416000, 416000, 624000, 624000, 832000, 1057000, 1248000}, /* CLUSTER0 */
		{624000, 832000, 832000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{312000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[7] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 416000, 416000, 624000, 624000, 832000, 1057000, 1248000}, /* CLUSTER0 */
		{624000, 624000, 624000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 208000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{312000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[8] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{624000, 624000, 624000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 312000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[9] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{416000, 416000, 416000, 624000, 624000, 832000, 1057000, 1248000}, /* CLUSTER0 */
		{416000, 624000, 624000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 208000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 312000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[10] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 416000, 624000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[11] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 312000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 416000, 624000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 208000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[12] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 312000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 312000, 624000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 208000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 208000, 312000, 312000, 416000, 416000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[13] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 312000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 312000, 624000, 832000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{156000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{312000, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 208000, 312000, 312000, 416000, 416000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[14] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 312000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 312000, 624000, 1057000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 416000, 416000, 416000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[15] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 312000, 416000, 624000, 624000, 1248000, 1248000, 1248000}, /* CLUSTER0 */
		{312000, 312000, 624000, 1057000, 1057000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 667000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 0, 0, 0, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 312000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
};

/* 8 VLs PMIC setting */
/* FIXME: adjust according to SVC */
static int vm_millivolts_1936_svc_sec[][VL_MAX] = {
	/*LV0,  LV1,  LV2,  LV3,  LV4,  LV5,  LV6,  LV7 */
	{975, 1050, 1063, 1125, 1125, 1200, 1288, 1300},/* Profile0 */
	{975, 988, 988, 1000, 1025, 1150, 1150, 1225},/* Profile1 */
	{975, 988, 988, 1000, 1038, 1150, 1150, 1225},/* Profile2 */
	{975, 988, 988, 1000, 1038, 1150, 1163, 1238},/* Profile3 */
	{975, 988, 988, 1000, 1038, 1150, 1163, 1250},/* Profile4 */
	{975, 988, 988, 1013, 1038, 1150, 1175, 1263},/* Profile5 */
	{975, 988, 988, 1013, 1038, 1150, 1175, 1263},/* Profile6 */
	{975, 988, 988, 1013, 1038, 1150, 1188, 1275},/* Profile7 */
	{975, 988, 988, 1025, 1050, 1150, 1188, 1288},/* Profile8 */
	{975, 988, 988, 1025, 1050, 1150, 1200, 1288},/* Profile9 */
	{975, 988, 1000, 1050, 1063, 1150, 1213, 1300},/* Profile10 */
	{975, 988, 1013, 1050, 1075, 1150, 1225, 1238},/* Profile11 */
	{975, 988, 1025, 1063, 1075, 1150, 1238, 1250},/* Profile12 */
	{975, 1000, 1038, 1075, 1088, 1163, 1250, 1275},/* Profile13 */
	{975, 1013, 1050, 1100, 1100, 1175, 1263, 1300},/* Profile14 */
	{975, 1025, 1063, 1125, 1125, 1200, 1288, 1288},/* Profile15 */
};

static unsigned long freqs_cmb_1936_tsmc_1p8G[][VM_RAIL_MAX][VL_MAX] = {
	[0] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[1] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 624000, 624000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 667000, 667000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[2] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 624000, 624000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[3] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[4] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 832000, 832000, 1491000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[5] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 1057000, 1491000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[6] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[7] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[8] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[9] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[10] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[11] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[12] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[13] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[14] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[15] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1803000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 312000, 624000, 624000, 624000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
};

static int vm_millivolts_1936_tsmc_1p8G[][VL_MAX] = {
	/*LV0,  LV1,  LV2,  LV3,  LV4,  LV5,  LV6,  LV7 */
	{1000, 1038, 1063, 1113, 1125, 1188, 1263, 1300},/* Profile0 */
	{950, 975, 975, 1000, 1000, 1038, 1025, 1175},/* Profile1 */
	{950, 975, 975, 1000, 1000, 1038, 1038, 1188},/* Profile2 */
	{950, 975, 975, 1000, 1000, 1050, 1050, 1200},/* Profile3 */
	{950, 975, 975, 1000, 1000, 1050, 1050, 1213},/* Profile4 */
	{950, 975, 975, 1000, 1013, 1063, 1063, 1225},/* Profile5 */
	{950, 975, 975, 1000, 1013, 1063, 1075, 1238},/* Profile6 */
	{950, 975, 988, 1013, 1025, 1075, 1088, 1250},/* Profile7 */
	{950, 975, 988, 1013, 1025, 1088, 1100, 1250},/* Profile8 */
	{950, 975, 988, 1025, 1038, 1088, 1113, 1263},/* Profile9 */
	{950, 975, 1000, 1038, 1050, 1113, 1138, 1288},/* Profile10 */
	{950, 988, 1013, 1050, 1063, 1125, 1163, 1300},/* Profile11 */
	{963, 1000, 1025, 1063, 1075, 1150, 1188, 1300},/* Profile12 */
	{975, 1000, 1038, 1075, 1088, 1163, 1213, 1275},/* Profile13 */
	{988, 1013, 1050, 1100, 1100, 1175, 1238, 1288},/* Profile14 */
	{1000, 1038, 1063, 1113, 1125, 1188, 1263, 1300},/* Profile15 */
};

static unsigned long freqs_cmb_1936_tsmc_b0[][VM_RAIL_MAX][VL_MAX] = {
	[0] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 528000, 624000, 705000, 705000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 528000, 624000, 705000, 705000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[1] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 624000, 624000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 667000, 667000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[2] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 624000, 624000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[3] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 1057000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{208000, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[4] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 832000, 832000, 832000, 832000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 312000, 312000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GC3D */
		{208000, 312000, 312000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 528000, 528000, 705000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[5] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 1057000, 1491000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 528000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 528000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[6] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 528000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 528000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[7] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{312000, 528000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GC3D */
		{208000, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{312000, 528000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[8] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 416000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 416000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 416000, 416000, 416000, 500000, 528000, 528000, 528000}, /* VPU */
		{208000, 312000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[9] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 528000, 528000, 528000, 528000, 528000}, /* VPU */
		{208000, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[10] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[11] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 528000, 624000, 705000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[12] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{0, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 667000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 312000, 528000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 312000, 528000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{208000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[13] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 528000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 528000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[14] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 528000, 624000, 624000, 832000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 528000, 624000, 624000, 832000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 416000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 500000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 416000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
	[15] = {
		/* LV0,    LV1,    LV2,    LV3,    LV4,    LV5,    LV6,    LV7 */
		{312000, 416000, 416000, 624000, 624000, 832000, 832000, 1248000}, /* CLUSTER0 */
		{312000, 624000, 624000, 832000, 832000, 1248000, 1491000, 1491000}, /* CLUSTER1 */
		{624000, 624000, 624000, 624000, 624000, 624000, 667000, 797000}, /* DDR */
		{208000, 208000, 208000, 312000, 312000, 312000, 312000, 312000}, /* AXI */
		{0, 0, 528000, 624000, 705000, 705000, 832000, 832000}, /* GC3D */
		{0, 208000, 208000, 312000, 312000, 416000, 416000, 416000}, /* GC2D */
		{0, 0, 528000, 624000, 705000, 705000, 832000, 832000}, /* GCSHADER */
		{0, 312000, 312000, 416000, 416000, 416000, 416000, 416000}, /* GCACLK */
		{312000, 312000, 416000, 500000, 528000, 528000, 528000, 528000}, /* VPU */
		{0, 208000, 312000, 312000, 416000, 499000, 499000, 499000}, /* ISP */
		sdh_dvfs,
	},
};

static int vm_millivolts_1936_svc_tsmc_b0[][VL_MAX] = {
	/*LV0,  LV1,  LV2,  LV3,  LV4,  LV5,  LV6,  LV7 */
	{1000, 1038, 1063, 1113, 1125, 1188, 1263, 1300},/* Profile0 */
	{950, 975, 975, 1000, 1000, 1038, 1025, 1150},/* Profile1 */
	{950, 975, 975, 1000, 1000, 1038, 1038, 1163},/* Profile2 */
	{950, 975, 975, 1000, 1000, 1050, 1050, 1175},/* Profile3 */
	{950, 975, 975, 1000, 1000, 1050, 1050, 1188},/* Profile4 */
	{950, 975, 975, 1000, 1013, 1063, 1063, 1200},/* Profile5 */
	{950, 975, 975, 1000, 1013, 1063, 1075, 1213},/* Profile6 */
	{950, 975, 988, 1013, 1025, 1075, 1088, 1225},/* Profile7 */
	{950, 975, 988, 1013, 1025, 1088, 1100, 1238},/* Profile8 */
	{950, 975, 988, 1025, 1038, 1088, 1113, 1250},/* Profile9 */
	{950, 975, 1000, 1038, 1050, 1113, 1138, 1275},/* Profile10 */
	{950, 988, 1013, 1050, 1063, 1125, 1163, 1288},/* Profile11 */
	{963, 1000, 1025, 1063, 1075, 1150, 1188, 1300},/* Profile12 */
	{975, 1000, 1038, 1075, 1088, 1163, 1213, 1275},/* Profile13 */
	{988, 1013, 1050, 1100, 1100, 1175, 1238, 1275},/* Profile14 */
	{1000, 1038, 1063, 1113, 1125, 1188, 1263, 1300},/* Profile15 */
};


#define cp_msa(cp0, cp1, cp2, msa) {.cpdvcinfo[0] = {416, cp0},\
		.cpdvcinfo[1] = {624, cp1},\
		.cpdvcinfo[2] = {832, cp2},\
		.msadvcvl[0] = {416, msa},}


#define cp_lv1 cp_msa(VL1, VL1, VL3, VL1)
#define cp_lv2 cp_msa(VL1, VL3, VL5, VL2)
#define cp_lv3 cp_msa(VL1, VL3, VL5, VL2)
#define cp_lv4 cp_msa(VL1, VL1, VL4, VL1)
#define cp_lv5 cp_msa(VL1, VL1, VL5, VL1)
#define cp_lv6 cp_msa(VL1, VL3, VL5, VL1)
#define cp_lv7 cp_msa(VL1, VL2, VL5, VL2)

static struct cpmsa_dvc_info cpmsa_dvc_info_1936tsmc[NUM_PROFILES] = {
	[0] = cp_lv2,
	[1] = cp_lv6,
	[2] = cp_lv6,
	[3] = cp_lv6,
	[4] = cp_lv6,
	[5] = cp_lv6,
	[6] = cp_lv6,
	[7] = cp_lv7,
	[8] = cp_lv7,
	[9] = cp_lv7,
	[10] = cp_lv7,
	[11] = cp_lv7,
	[12] = cp_lv7,
	[13] = cp_lv2,
	[14] = cp_lv2,
	[15] = cp_lv2,
};

static struct cpmsa_dvc_info cpmsa_dvc_info_1936sec[NUM_PROFILES] = {
	[0] = cp_lv3,
	[1] = cp_lv4,
	[2] = cp_lv4,
	[3] = cp_lv4,
	[4] = cp_lv5,
	[5] = cp_lv5,
	[6] = cp_lv5,
	[7] = cp_lv5,
	[8] = cp_lv5,
	[9] = cp_lv6,
	[10] = cp_lv3,
	[11] = cp_lv3,
	[12] = cp_lv3,
	[13] = cp_lv3,
	[14] = cp_lv3,
	[15] = cp_lv3,
};

void adjust_ddr_svc(void)
{
	int i, ddr_voltage  = 0;
	if (svc_version == SVC_1_11)
		ddr_voltage = ddr_800M_tsmc_svc[uiprofile];
	else if (svc_version == SEC_SVC_1_01)
		ddr_voltage = ddr_800M_sec_svc[uiprofile];
	if (ddr_mode == DDR_800M && get_ddr_800M_4x())
		for (i = 0; i < VL_MAX; i++)
			if ((freqs_cmb[DDR][i] != 0) &&
				(millivolts[i] >= ddr_voltage))
				freqs_cmb[DDR][i] = 797000;

	return;
}


int dump_dvc_table(char *buf, int size)
{
	int i, s = 0, j;
	s += snprintf(buf + s, size - s,
	"=====dump svc voltage and freq table===\nsvc table voltage:\n");
	for (i = 0; i < VL_MAX; i++)
		s += snprintf(buf + s, size - s, "%d ", millivolts[i]);
	s += snprintf(buf + s, size - s,
	"\n========================================\nsvc table freq table:\n");
	for (i = 0; i < VM_RAIL_MAX; i++) {
		s += snprintf(buf + s, size - s, "%s: ", dvfs_comp_name[i]);
		for (j = 0; j < VL_MAX; j++)
			s += snprintf(buf + s, size - s, "%lu ",
				freqs_cmb[i][j]);
		s += snprintf(buf + s, size - s, "\n");
	}
	s += snprintf(buf + s, size - s,
		"========================================\n");

	return s;
}


static ssize_t dvc_table_read(struct file *filp,
		char __user *buffer, size_t count, loff_t *ppos)
{
	char *buf;
	size_t size = PAGE_SIZE;
	int s, ret;

	buf = (char *)__get_free_pages(GFP_KERNEL, 0);
	if (!buf) {
		pr_err("memory alloc for dvc table dump is failed!!\n");
		return -ENOMEM;
	}
	s = dump_dvc_table(buf, size);
	ret = simple_read_from_buffer(buffer, count, ppos, buf, s);
	free_pages((unsigned long)buf, 0);
	return ret;
}

const struct file_operations dvc_table_fops = {
	.read = dvc_table_read,
};

int __init dvc_table_debugfs_init(struct dentry *clk_debugfs_root)
{
	struct dentry *dvc_table;

	dvc_table = debugfs_create_file("dvc_table", 0444,
		clk_debugfs_root, NULL, &dvc_table_fops);
	if (!dvc_table)
		return -ENOENT;
	return 0;
}


int handle_svc_table(void)
{
	int i, j, lv0_change_index = 0, s;
	char *buff = NULL;
	size_t size = PAGE_SIZE - 1;

	if (svc_version == SVC_1_11) {
		millivolts = vm_millivolts_1936_svc_tsmc[uiprofile];
		freqs_cmb = freqs_cmb_1936_tsmc[uiprofile];
	} else if (svc_version == SEC_SVC_1_01) {
		millivolts = vm_millivolts_1936_svc_sec[uiprofile];
		freqs_cmb = freqs_cmb_1936_sec[uiprofile];
	} else if (svc_version == SVC_TSMC_1p8G) {
		millivolts = vm_millivolts_1936_tsmc_1p8G[uiprofile];
		freqs_cmb = freqs_cmb_1936_tsmc_1p8G[uiprofile];
	} else if (svc_version == SVC_TSMC_B0) {
		millivolts = vm_millivolts_1936_svc_tsmc_b0[uiprofile];
		freqs_cmb = freqs_cmb_1936_tsmc_b0[uiprofile];
	}

	if (uiYldTableEn == 1) {
		/* Change LV0 voltage according to fuse value */
		if (min_lv0_voltage >= millivolts[0]) {
			for (i = 0; i < VL_MAX; i++)
				if (min_lv0_voltage >= millivolts[i])
					millivolts[i] = min_lv0_voltage;
				else
					break;

			lv0_change_index = i - 1;
		}

		/*	Change other components(without SDH) frequency
		 *	combination table if LV0 is changed.
		 */
		if ((lv0_change_index) != 0) {
			for (j = 0; j < lv0_change_index; j++)
				for (i = 0; i < VM_RAIL_NO_SDH_MAX; i++)
					freqs_cmb[i][j] =
					freqs_cmb[i][lv0_change_index];
		}

		/* For Gc frequencies that stay at lower voltage level
		 * than fuse gc value, change it to the voltage
		 * level >= fuse gc value
		 */
		for (i = 0; i < VL_MAX; i++)
			if (freqs_cmb[GC3D][i] != 0)
				if (millivolts[i] < min_gc_voltage) {
					freqs_cmb[GC3D][i] = 0;
					freqs_cmb[GC_SHADER][i] = 0;
				}
	}

	adjust_ddr_svc();

	buff = (char *)__get_free_pages(GFP_KERNEL, 0);
	if (!buff) {
		pr_err("memory alloc for dvc table dump is failed!!\n");
		return -ENOMEM;
	}
	s = dump_dvc_table(buff, size);
	printk(buff);
	free_pages((unsigned long)buff, 0);

	return 0;
}

int cp_level(int level)
{
	int i;
	if (millivolts[level] <= min_cp_voltage) {
		for (i = level; i < VL_MAX; i++)
			if (millivolts[i] >= min_cp_voltage) {
				level = i;
				break;
			}
	}
	return level;
}

void handle_svc_cp_table(void)
{
	int level1 = 0, level2 = 0, level3 = 0, level4 = 0;
	struct cpmsa_dvc_info *cpmsa_dvc_info_cp_temp = NULL;

	if (uiYldTableEn == 1) {
		if (svc_version == SVC_1_11 || svc_version == SVC_TSMC_1p8G
				|| svc_version == SVC_TSMC_B0)
			cpmsa_dvc_info_cp_temp = cpmsa_dvc_info_1936tsmc;
		else if (svc_version == SEC_SVC_1_01)
			cpmsa_dvc_info_cp_temp = cpmsa_dvc_info_1936sec;

		level1 = cpmsa_dvc_info_cp_temp[uiprofile].cpdvcinfo[0].cpvl;
		level2 = cpmsa_dvc_info_cp_temp[uiprofile].cpdvcinfo[1].cpvl;
		level3 = cpmsa_dvc_info_cp_temp[uiprofile].cpdvcinfo[2].cpvl;
		level4 = cpmsa_dvc_info_cp_temp[uiprofile].msadvcvl[0].cpvl;

		level1 = cp_level(level1);
		level2 = cp_level(level2);
		level3 = cp_level(level3);
		level4 = cp_level(level4);

		cpmsa_dvc_info_temp.cpdvcinfo[0].cpfreq = 416;
		cpmsa_dvc_info_temp.cpdvcinfo[0].cpvl = level1;
		cpmsa_dvc_info_temp.cpdvcinfo[1].cpfreq = 624;
		cpmsa_dvc_info_temp.cpdvcinfo[1].cpvl = level2;
		cpmsa_dvc_info_temp.cpdvcinfo[2].cpfreq = 832;
		cpmsa_dvc_info_temp.cpdvcinfo[2].cpvl = level3;
		cpmsa_dvc_info_temp.msadvcvl[0].cpfreq = 416;
		cpmsa_dvc_info_temp.msadvcvl[0].cpvl = level4;

		fillcpdvcinfo(&cpmsa_dvc_info_temp);
	} else {
		if (svc_version == SVC_1_11 || svc_version == SVC_TSMC_1p8G
				|| svc_version == SVC_TSMC_B0)
			fillcpdvcinfo(&cpmsa_dvc_info_1936tsmc[uiprofile]);
		else if (svc_version == SEC_SVC_1_01)
			fillcpdvcinfo(&cpmsa_dvc_info_1936sec[uiprofile]);
	}
}

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

/* pm880: 0x80 is the ASCII code of "p", 0x77 is for "m" */
unsigned long __dvc_guard = 0x8077880;
static int set_pmic_volt(unsigned int lvl, unsigned int mv)
{
	switch (__dvc_guard) {
	case 0x8077860:
		return pm8xx_dvc_setvolt(PM800_ID_BUCK1, lvl, mv * mV2uV);
	case 0x8077880:
	default:
		return pm88x_dvc_set_volt(lvl, mv * mV2uV);
	}
}

static int get_pmic_volt(unsigned int lvl)
{
	int uv = 0, ret = 0;

	switch (__dvc_guard) {
	case 0x8077860:
		ret = pm8xx_dvc_getvolt(PM800_ID_BUCK1, lvl, &uv);
		if (ret < 0)
			return ret;
		break;
	case 0x8077880:
	default:
		uv = pm88x_dvc_get_volt(lvl);
		break;
	}
	return DIV_ROUND_UP(uv, mV2uV);
}

static struct dvc_plat_info dvc_pxa1936_info = {
	.comps = vm_rail_comp_tbl_dvc,
	.num_comps = ARRAY_SIZE(vm_rail_comp_tbl_dvc),
	.num_volts = VL_MAX,
	.cp_pmudvc_lvl = VL0,
	.dp_pmudvc_lvl = VL2,
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

	__init_read_droinfo();

	handle_svc_table();

	dvc_pxa1936_info.millivolts = millivolts;
	plat_set_vl_min(0);
	plat_set_vl_max(dvc_pxa1936_info.num_volts);

	handle_svc_cp_table();

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

