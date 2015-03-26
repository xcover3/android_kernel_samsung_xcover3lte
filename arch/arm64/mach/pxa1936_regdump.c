/*
 *  linux/arch/arm64/mach/pxa1936_regdump.c
 *
 *  Copyright (C) 2014 Marvell Technology Group Ltd.
 *  Author: Neil Zhang<zhangwm@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/regdump_ops.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/cputype.h>

#include "regs-addr.h"

static struct regdump_ops pmua_regdump_ops = {
	.dev_name = "pxa1936-common-pmua",
};

static struct regdump_region pmua_dump_region[] = {
	{"PMUA_CC_CP",			0x000, 4, regdump_cond_true},
	{"PMUA_CC_AP",			0x004, 4, regdump_cond_true},
	{"PMUA_DM_CC_CP",		0x008, 4, regdump_cond_false},
	{"PMUA_DM_CC_AP",		0x00c, 4, regdump_cond_false},
	{"PMUA_FC_TIMER",		0x010, 4, regdump_cond_true},
	{"PMUA_CP_IDLE_CFG",		0x014, 4, regdump_cond_true},
	{"PMUA_SQU_CLK_GATE_CTRL",	0x01c, 4, regdump_cond_true},
	{"PMUA_CSI_CCIC2_CLK_RES_CTRL",	0x024, 4, regdump_cond_true},
	{"PMUA_CCIC1_CLK_GATE_CTRL",	0x028, 4, regdump_cond_true},
	{"PMUA_FBRC0_CLK_GATE_CTRL",	0x02c, 4, regdump_cond_true},
	{"PMUA_FBRC1_CLK_GATE_CTRL",	0x030, 4, regdump_cond_true},
	{"PMUA_USB_CLK_GATE_CTRL",	0x034, 4, regdump_cond_true},
	{"PMUA_ISP_CLK_RES_CTRL",	0x038, 4, regdump_cond_true},
	{"PMUA_PMU_CLK_GATE_CTRL",	0x040, 4, regdump_cond_true},
	{"PMUA_DSI_CLK_RES_CTRL",	0x044, 4, regdump_cond_true},
	{"PMUA_LTEDMA_CLK_RES_CTRL",	0x048, 4, regdump_cond_true},
	{"PMUA_LCD_DSI_CLK_RES_CTRL",	0x04c, 4, regdump_cond_true},
	{"PMUA_CCIC_CLK_RES_CTRL",	0x050, 4, regdump_cond_true},
	{"PMUA_SDH0_CLK_RES_CTRL",	0x054, 4, regdump_cond_true},
	{"PMUA_SDH1_CLK_RES_CTRL",	0x058, 4, regdump_cond_true},
	{"PMUA_USB_CLK_RES_CTRL",	0x05c, 4, regdump_cond_true},
	{"PMUA_NF_CLK_RES_CTRL",	0x060, 4, regdump_cond_true},
	{"PMUA_DMA_CLK_RES_CTRL",	0x064, 4, regdump_cond_true},
	{"PMUA_AES_CLK_RES_CTRL",	0x068, 4, regdump_cond_true},
	{"PMUA_MCB_CLK_RES_CTRL",	0x06c, 4, regdump_cond_true},
	{"PMUA_CP_IMR",			0x070, 4, regdump_cond_true},
	{"PMUA_CP_IRWC",		0x074, 4, regdump_cond_true},
	{"PMUA_CP_ISR",			0x078, 4, regdump_cond_true},
	{"PMUA_SD_ROT_WAKE_CLR",	0x07c, 4, regdump_cond_true},
	{"PMUA_AUDIO_CLK",		0x080, 4, regdump_cond_true},
	{"PMUA_PWR_STBL_TIMER",		0x084, 4, regdump_cond_true},
	{"PMUA_DEBUG_REG",		0x088, 4, regdump_cond_true},
	{"PMUA_SRAM_PWR_DWN",		0x08c, 4, regdump_cond_true},
	{"PMUA_CORE_STATUS",		0x090, 4, regdump_cond_true},
	{"PMUA_RES_FRM_SLP_CLR",	0x094, 4, regdump_cond_true},
	{"PMUA_AP_IMR",			0x098, 4, regdump_cond_true},
	{"PMUA_AP_IRWC",		0x09c, 4, regdump_cond_true},
	{"PMUA_AP_ISR",			0x0a0, 4, regdump_cond_true},
	{"PMUA_VPU_CLK_RES_CTRL",	0x0a4, 4, regdump_cond_true},
	{"PMUA_DTC_CLK_RES_CTRL",	0x0ac, 4, regdump_cond_true},
	{"PMUA_MC_HW_SLP_TYPE",		0x0b0, 4, regdump_cond_true},
	{"PMUA_MC_SLP_REQ_AP",		0x0b4, 4, regdump_cond_true},
	{"PMUA_MC_SLP_REQ_CP",		0x0b8, 4, regdump_cond_true},
	{"PMUA_MC_SLP_REQ_MSA",		0x0bc, 4, regdump_cond_true},
	{"PMUA_MC_SW_SLP_TYPE",		0x0c0, 4, regdump_cond_true},
	{"PMUA_PLL_SEL_STATUS",		0x0c4, 4, regdump_cond_true},
	{"PMUA_GC_CLK_RES_CTRL",	0x0cc, 4, regdump_cond_true},
	{"PMUA_SMC_CLK_RES_CTRL",	0x0d4, 4, regdump_cond_true},
	{"PMUA_PWR_CTRL_REG",		0x0d8, 4, regdump_cond_true},
	{"PMUA_PWR_BLK_TMR_REG",	0x0dc, 4, regdump_cond_true},
	{"PMUA_SDH2_CLK_RES_CTRL",	0x0e0, 4, regdump_cond_true},
	{"PMUA_CA7MP_IDLE_CFG1",	0x0e4, 4, regdump_cond_true},
	{"PMUA_MC_CTRL",		0x0e8, 4, regdump_cond_true},
	{"PMUA_PWR_STATUS_REG",		0x0f0, 4, regdump_cond_true},
	{"PMUA_2D_GPU_CLK_RES_CTRL",	0x0f4, 4, regdump_cond_true},
	{"PMUA_SP_IDLE_CFG",	0x0f8, 4, regdump_cond_true},
	{"PMUA_GNSS_PWR_CTRL",	0x0fc, 4, regdump_cond_true},
	{"PMUA_CC2_AP",			0x100, 4, regdump_cond_true},
	{"PMUA_DM_CC2_AP",		0x104, 4, regdump_cond_true},
	{"PMUA_TRACE_CONFIG",		0x108, 4, regdump_cond_true},
	{"PMUA_CA7MP_IDLE_CFG0",	0x120, 4, regdump_cond_true},
	{"PMUA_CA7_CORE0_IDLE_CFG",	0x124, 4, regdump_cond_true},
	{"PMUA_CA7_CORE1_IDLE_CFG",	0x128, 4, regdump_cond_true},
	{"PMUA_CA7_CORE0_WAKEUP",	0x12c, 4, regdump_cond_true},
	{"PMUA_CA7_CORE1_WAKEUP",	0x130, 4, regdump_cond_true},
	{"PMUA_CA7_CORE2_WAKEUP",	0x134, 4, regdump_cond_true},
	{"PMUA_CA7_CORE3_WAKEUP",	0x138, 4, regdump_cond_true},
	{"PMUA_DVC_DEBUG",		0x140, 4, regdump_cond_true},
	{"PMUA_CA7MP_IDLE_CFG2",	0x150, 4, regdump_cond_true},
	{"PMUA_CA7MP_IDLE_CFG3",	0x154, 4, regdump_cond_true},
	{"PMUA_CA7_CORE2_IDLE_CFG",	0x160, 4, regdump_cond_true},
	{"PMUA_CA7_CORE3_IDLE_CFG",	0x164, 4, regdump_cond_true},
	{"PMUA_CA7_PWR_MISC",		0x170, 4, regdump_cond_true},
	{"DFC_AP",			0x180, 4, regdump_cond_true},
	{"DFC_CP",			0x184, 4, regdump_cond_true},
	{"DFC_STATUS",			0x188, 4, regdump_cond_true},
	{"DFC_LEVEL0",			0x190, 4, regdump_cond_true},
	{"DFC_LEVEL1",			0x194, 4, regdump_cond_true},
	{"DFC_LEVEL2",			0x198, 4, regdump_cond_true},
	{"DFC_LEVEL3",			0x19c, 4, regdump_cond_true},
	{"DFC_LEVEL4",			0x1a0, 4, regdump_cond_true},
	{"DFC_LEVEL5",			0x1a4, 4, regdump_cond_true},
	{"DFC_LEVEL6",			0x1a8, 4, regdump_cond_true},
	{"DFC_LEVEL7",			0x1ac, 4, regdump_cond_true},
	{"PMU_DEBUG2_REG",			0x1b0, 4, regdump_cond_true},
	{"GNSS_WAKEUP_CTRL",			0x1b8, 4, regdump_cond_true},
	{"PMU_CCIC2_CLK_GATE_CTRL",			0x1bc, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C0_CTRL",			0x1c0, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C1_CTRL",			0x1c4, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C2_CTRL",			0x1c8, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C3_CTRL",			0x1cc, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C0_ML",			0x1d0, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C0_MH",			0x1d4, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C1_ML",			0x1d8, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C1_MH",			0x1dc, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C2_ML",			0x1e0, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C2_MH",			0x1e4, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C3_ML",			0x1e8, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C3_MH",			0x1ec, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C4_CTRL",			0x208, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C5_CTRL",			0x20c, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C6_CTRL",			0x210, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C7_CTRL",			0x214, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C4_ML",			0x218, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C4_MH",			0x21c, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C5_ML",			0x220, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C5_MH",			0x224, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C6_ML",			0x228, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C6_MH",			0x22c, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C7_ML",			0x230, 4, regdump_cond_true},
	{"PMU_GT_WAKEUP_C7_MH",			0x234, 4, regdump_cond_true},
	{"PMU_CCI_CLK_CTRL",			0x300, 4, regdump_cond_true},
	{"PMU_CA7_CORE4_IDLE_CFG",			0x304, 4, regdump_cond_true},
	{"PMU_CA7_CORE5_IDLE_CFG",			0x308, 4, regdump_cond_true},
	{"PMU_CA7_CORE6_IDLE_CFG",			0x30c, 4, regdump_cond_true},
	{"PMU_CA7_CORE7_IDLE_CFG",			0x310, 4, regdump_cond_true},
	{"PMU_CA7MP_IDLE_CFG4",			0x314, 4, regdump_cond_true},
	{"PMU_CA7MP_IDLE_CFG5",			0x318, 4, regdump_cond_true},
	{"PMU_CA7MP_IDLE_CFG6",			0x31c, 4, regdump_cond_true},
	{"PMU_CA7MP_IDLE_CFG7",			0x320, 4, regdump_cond_true},
	{"PMU_CA7_CORE4_WAKEUP",			0x324, 4, regdump_cond_true},
	{"PMU_CA7_CORE5_WAKEUP",			0x328, 4, regdump_cond_true},
	{"PMU_CA7_CORE6_WAKEUP",			0x32c, 4, regdump_cond_true},
	{"PMU_CA7_CORE7_WAKEUP",			0x330, 4, regdump_cond_true},
	{"FC_LOCK_STATUS",			0x334, 4, regdump_cond_true},

};

static void __init mmp_pmua_regdump_init(void)
{
	pmua_regdump_ops.base = regs_addr_get_va(REGS_ADDR_APMU);
	pmua_regdump_ops.phy_base = regs_addr_get_pa(REGS_ADDR_APMU);
	pmua_regdump_ops.regions = pmua_dump_region;
	pmua_regdump_ops.reg_nums = ARRAY_SIZE(pmua_dump_region);
	register_regdump_ops(&pmua_regdump_ops);

}

static struct regdump_ops pmum_regdump_ops = {
	.dev_name = "pxa1936-common-pmum",
};

static struct regdump_region pmum_dump_region[] = {
	{"CPCR",                0x0000, 4, regdump_cond_true},
	{"CPSR",                0x0004, 4, regdump_cond_true},
	{"FCCR",                0x0008, 4, regdump_cond_true},
	{"POCR",                0x000C, 4, regdump_cond_true},
	{"POSR",                0x0010, 4, regdump_cond_true},
	{"SUCCR",               0x0014, 4, regdump_cond_true},
	{"VRCR",                0x0018, 4, regdump_cond_true},
	{"PLL3CR",              0x001C, 4, regdump_cond_true},
	{"CPRR",                0x0020, 4, regdump_cond_true},
	{"CCGR",                0x0024, 4, regdump_cond_true},
	{"CRSR",                0x0028, 4, regdump_cond_true},
	{"XDCR",                0x002C, 4, regdump_cond_true},
	{"GPCR",                0x0030, 4, regdump_cond_true},
	{"PLL2CR",              0x0034, 4, regdump_cond_true},
	{"SCCR",                0x0038, 4, regdump_cond_true},
	{"MCCR",                0x003C, 4, regdump_cond_true},
	{"ISCCR0",              0x0040, 4, regdump_cond_true},
	{"ISCCR1",              0x0044, 4, regdump_cond_true},
	{"CWUCRS",              0x0048, 4, regdump_cond_true},
	{"CWUCRM",              0x004C, 4, regdump_cond_true},
	{"PLL4CR",              0x0050, 4, regdump_cond_true},
	{"FCAP",                0x0054, 4, regdump_cond_true},
	{"FCCP",                0x0058, 4, regdump_cond_true},
	{"FCDCLK",              0x005C, 4, regdump_cond_true},
	{"FCACLK",              0x0060, 4, regdump_cond_true},
	{"CWUCRS1",             0x0064, 4, regdump_cond_true},
	{"DSOC",                0x0100, 4, regdump_cond_true},
	{"WDTPCR",              0x0200, 4, regdump_cond_true},
	{"CMPR0",               0x0400, 4, regdump_cond_true},
	{"CMPR1",               0x0404, 4, regdump_cond_true},
	{"CMPR2",               0x0408, 4, regdump_cond_true},
	{"CMPR3",               0x0410, 4, regdump_cond_true},
	{"APSLPW",              0x1000, 4, regdump_cond_true},
	{"APSR",                0x1004, 4, regdump_cond_true},
	{"APRR",                0x1020, 4, regdump_cond_true},
	{"ACGR",                0x1024, 4, regdump_cond_true},
	{"ARSR",                0x1028, 4, regdump_cond_true},
	{"PWRMODE_STAUTS",      0x1030, 4, regdump_cond_true},
	{"AWUCRS",              0x1048, 4, regdump_cond_true},
	{"AWUCRM",              0x104C, 4, regdump_cond_true},
	{"APBCSCR",             0x1050, 4, regdump_cond_true},
	{"AWUCRS1",             0x1064, 4, regdump_cond_true},
	{"APCR_CLUSTER0",       0x1080, 4, regdump_cond_true},
	{"APCR_CLUSTER1",       0x1084, 4, regdump_cond_true},
	{"APCR_PER",            0x1088, 4, regdump_cond_true},
	{"SPRR",                0x3000, 4, regdump_cond_true},
};

static void __init mmp_pmum_regdump_init(void)
{
	pmum_regdump_ops.base = regs_addr_get_va(REGS_ADDR_MPMU);
	pmum_regdump_ops.phy_base = regs_addr_get_pa(REGS_ADDR_MPMU);
	pmum_regdump_ops.regions = pmum_dump_region;
	pmum_regdump_ops.reg_nums = ARRAY_SIZE(pmum_dump_region);
	register_regdump_ops(&pmum_regdump_ops);
}

static int __init mmp_regdump_init(void)
{
	if (cpu_is_pxa1936()) {
		mmp_pmua_regdump_init();
		mmp_pmum_regdump_init();
	}
	return 0;
}
arch_initcall(mmp_regdump_init);

