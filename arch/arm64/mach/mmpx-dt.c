/*
 *  linux/arch/arm64/mach/mmpx-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/clocksource.h>

#include <asm/mach/arch.h>
#include <linux/cputype.h>

#include "mmpx-dt.h"
#include "regs-addr.h"

static const struct of_dev_auxdata mmpx_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xc0ffdc00, "mmp-sspa-dai.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xc0ffdd00, "mmp-sspa-dai.1", NULL),
	{}
};

#define MPMU_PHYS_BASE		0xd4050000
#define GEN_TMR2_PHYS_BASE	0xd4080000
#define MPMU_PRR_SP		0x0020
#define MPMU_WDTPCR		0x0200
#define MPMU_WDTPCR1		0x0204
#define MPMU_PRR_PJ		0x1020

#define MPMU_PRR_SP_WDTR	(1 << 4)
#define MPMU_PRR_SP_CPR		(1 << 0)

#define TMR_WFAR               (0x009c)
#define TMR_WSAR               (0x00A0)

#define GEN_TMR_CFG            (0x00B0)
#define GEN_TMR_LD1            (0x00B8)

/* Get SoC Access to Generic Timer */
static void arch_timer_soc_access_enable(void __iomem *gen_tmr_base)
{
	__raw_writel(0xbaba, gen_tmr_base + TMR_WFAR);
	__raw_writel(0xeb10, gen_tmr_base + TMR_WSAR);
}

static void arch_timer_soc_config(void __iomem *mpmu_base)
{
	void __iomem *gen_tmr2_base;
	u32 tmp;

	gen_tmr2_base = ioremap(GEN_TMR2_PHYS_BASE, SZ_4K);
	if (!gen_tmr2_base) {
		pr_err("ioremap gen_tmr_base failed\n");
		return;
	}

	/* Enable WDTR2*/
	tmp  = __raw_readl(mpmu_base + MPMU_PRR_SP);
	tmp = tmp | MPMU_PRR_SP_WDTR;
	__raw_writel(tmp, mpmu_base + MPMU_PRR_SP);

	/* Initialize Counter to zero */
	arch_timer_soc_access_enable(gen_tmr2_base);
	__raw_writel(0x0, gen_tmr2_base + GEN_TMR_LD1);

	/* Program Generic Timer Clk Frequency */
	arch_timer_soc_access_enable(gen_tmr2_base);
	tmp = __raw_readl(gen_tmr2_base + GEN_TMR_CFG);
	tmp |= (3 << 4); /* 3.25MHz/32KHz Counter auto switch enabled */
	arch_timer_soc_access_enable(gen_tmr2_base);
	__raw_writel(tmp, gen_tmr2_base + GEN_TMR_CFG);

	/* Start the Generic Timer Counter */
	arch_timer_soc_access_enable(gen_tmr2_base);
	tmp = __raw_readl(gen_tmr2_base + GEN_TMR_CFG);
	tmp |= 0x3;
	arch_timer_soc_access_enable(gen_tmr2_base);
	__raw_writel(tmp, gen_tmr2_base + GEN_TMR_CFG);

	iounmap(gen_tmr2_base);
}

static __init void pxa1928_timer_init(void)
{
	void __iomem *mpmu_base;
	void __iomem *chip_id;

	regs_addr_iomap();

	mpmu_base = regs_addr_get_va(REGS_ADDR_MPMU);
	if (!mpmu_base) {
		pr_err("ioremap mpmu_base failed");
		return;
	}

	/* this is early, initialize mmp_chip_id here */
	chip_id = regs_addr_get_va(REGS_ADDR_CIU);
	mmp_chip_id = readl_relaxed(chip_id);

#ifdef CONFIG_ARM_ARCH_TIMER
	arch_timer_soc_config(mpmu_base);
#endif
	/*
	 * bit 7.enable wdt reset from thermal
	 * bit 6.enable wdt reset from timer2
	 */
	__raw_writel(0xD3, mpmu_base + MPMU_WDTPCR);

	clocksource_of_init();
}

static void __init pxa1928_init_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     mmpx_auxdata_lookup, &platform_bus);
}

static const char *pxa1928_dt_board_compat[] __initdata = {
	"marvell,pxa1928",
	NULL,
};

DT_MACHINE_START(PXA1928_DT, "PXA1928")
	.init_time      = pxa1928_timer_init,
	.init_machine	= pxa1928_init_machine,
	.dt_compat      = pxa1928_dt_board_compat,
MACHINE_END
