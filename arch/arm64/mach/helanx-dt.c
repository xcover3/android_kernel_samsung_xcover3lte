/*
 *  linux/arch/arm64/mach/mmpx-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Dongjiu Geng <djgeng@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/irqchip.h>
#include <linux/irqchip/mmp.h>
#include <linux/of_platform.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>

#include <asm/mach/arch.h>

#include "regs-addr.h"
#include "mmpx-dt.h"
#include "soc_camera_dkb.h"

#ifdef CONFIG_SD8XXX_RFKILL
#include <linux/sd8x_rfkill.h>
#endif

unsigned int mmp_chip_id;
EXPORT_SYMBOL(mmp_chip_id);

static const struct of_dev_auxdata helanx_auxdata_lookup[] __initconst = {
#ifdef CONFIG_MMP_MAP
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xd128dc00, "mmp-sspa-dai.0", NULL),
#endif
	OF_DEV_AUXDATA("soc-camera-pdrv", 0, "soc-camera-pdrv.0", &soc_camera_desc_0),

	OF_DEV_AUXDATA("marvell,mmp-disp", 0xd420b000, "mmp-disp", NULL),
#ifdef CONFIG_SD8XXX_RFKILL
	OF_DEV_AUXDATA("mrvl,sd8x-rfkill", 0, "sd8x-rfkill", NULL),
#endif
	{}
};

#define MPMU_PHYS_BASE		0xd4050000
#define MPMU_APRR		(0x1020)
#define MPMU_WDTPCR		(0x0200)

/* wdt and cp use the clock */
static __init void enable_pxawdt_clock(void)
{
	void __iomem *mpmu_base;

	mpmu_base = ioremap(MPMU_PHYS_BASE, SZ_8K);
	if (!mpmu_base) {
		pr_err("ioremap mpmu_base failed\n");
		return;
	}

	/* reset/enable WDT clock */
	writel(0x7, mpmu_base + MPMU_WDTPCR);
	readl(mpmu_base + MPMU_WDTPCR);
	writel(0x3, mpmu_base + MPMU_WDTPCR);

	iounmap(mpmu_base);
}

#define GENERIC_COUNTER_PHYS_BASE	0xd4101000
#define CNTCR				0x00 /* Counter Control Register */
#define CNTCR_EN			(1 << 0) /* The counter is enabled */
#define CNTCR_HDBG			(1 << 1) /* Halt on debug */

#define APBC_PHY_BASE			0xd4015000
#define APBC_COUNTER_CLK_SEL		0x64
#define FREQ_HW_CTRL			0x1
static __init void enable_arch_timer(void)
{
	void __iomem *apbc_base, *tmr_base;
	u32 tmp;

	apbc_base = ioremap(APBC_PHY_BASE, SZ_4K);
	if (!apbc_base) {
		pr_err("ioremap apbc_base failed\n");
		return;
	}

	tmr_base = ioremap(GENERIC_COUNTER_PHYS_BASE, SZ_4K);
	if (!tmr_base) {
		pr_err("opremap tmr_base failed\n");
		iounmap(apbc_base);
		return;
	}

	tmp = readl(apbc_base + APBC_COUNTER_CLK_SEL);

	/* Default is 26M/32768 = 0x319 */
	if ((tmp >> 16) != 0x319) {
		pr_warn("Generic Counter step of Low Freq is not right\n");
		iounmap(apbc_base);
		iounmap(tmr_base);
		return;
	}
	/*
	 * bit0 = 1: Generic Counter Frequency control by hardware VCTCXO_EN
	 * VCTCXO_EN = 1, Generic Counter Frequency is 26Mhz;
	 * VCTCXO_EN = 0, Generic Counter Frequency is 32KHz.
	 */
	writel(tmp | FREQ_HW_CTRL, apbc_base + APBC_COUNTER_CLK_SEL);

	/*
	 * NOTE: can not read CNTCR before write, otherwise write will fail
	 * Halt on debug;
	 * start the counter
	 */
	writel(CNTCR_HDBG | CNTCR_EN, tmr_base + CNTCR);

	iounmap(tmr_base);
	iounmap(apbc_base);
}

/* Common APB clock register bit definitions */
#define APBC_APBCLK	(1 << 0)  /* APB Bus Clock Enable */
#define APBC_FNCLK	(1 << 1)  /* Functional Clock Enable */
#define APBC_RST	(1 << 2)  /* Reset Generation */

/* Functional Clock Selection Mask */
#define APBC_FNCLKSEL(x)	(((x) & 0xf) << 4)

#define APBC_TIMER0		0x34
#define APBC_TIMER1		0x44
#define APBC_TIMER2		0x68
static u32 timer_clkreg[] = {
	APBC_TIMER0, APBC_TIMER1, APBC_TIMER2
};

static __init void enable_soc_timer(void)
{
	void __iomem *apbc_base;
	struct device_node *np;
	int tid;

	apbc_base = ioremap(APBC_PHY_BASE, SZ_4K);
	if (!apbc_base) {
		pr_err("ioremap apbc_base failed\n");
		return;
	}

	/* timer APMU clock initialization */
	for_each_compatible_node(np, NULL, "marvell,mmp-timer") {
		if (of_device_is_available(np) &&
			!of_property_read_u32(np, "marvell,timer-id", &tid)) {
			if (tid >= ARRAY_SIZE(timer_clkreg))
				continue;
			/* Select the configurable clock rate to be 3.25MHz */
			writel(APBC_APBCLK | APBC_RST, apbc_base + timer_clkreg[tid]);
			writel(APBC_APBCLK | APBC_FNCLK | APBC_FNCLKSEL(3),
				apbc_base + timer_clkreg[tid]);
		}
	}

	iounmap(apbc_base);
}

#define APMU_SDH0      0x54
static void __init pxa1908_sdhc_reset_all(void)
{
	unsigned int reg_tmp;
	void __iomem *apmu_base;

	apmu_base = regs_addr_get_va(REGS_ADDR_APMU);
	if (!apmu_base) {
		pr_err("failed to get apmu_base va\n");
		return;
	}

	/* use bit0 to reset all 3 sdh controls */
	reg_tmp = __raw_readl(apmu_base + APMU_SDH0);
	__raw_writel(reg_tmp & (~1), apmu_base + APMU_SDH0);
	udelay(10);
	__raw_writel(reg_tmp | (1), apmu_base + APMU_SDH0);
}

static void __init helanx_irq_init(void)
{
	irqchip_init();
	/* only for wake up */
	mmp_of_wakeup_init();
}

void __init helanx_timer_init(void)
{
	void __iomem *chip_id;

	regs_addr_iomap();

	/* this is early, initialize mmp_chip_id here */
	chip_id = regs_addr_get_va(REGS_ADDR_CIU);
	mmp_chip_id = readl_relaxed(chip_id);

	enable_pxawdt_clock();

#ifdef CONFIG_ARM_ARCH_TIMER
	enable_arch_timer();
#endif

	enable_soc_timer();

	clocksource_of_init();

	pxa1908_sdhc_reset_all();
}

void __init helanx_init_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     helanx_auxdata_lookup, &platform_bus);
}

static const char * const pxa1908_dt_board_compat[] __initconst = {
	"marvell,pxa1908",
	NULL,
};

DT_MACHINE_START(PXA1908_DT, "PXA1908")
	.init_time      = helanx_timer_init,
	.init_irq	= helanx_irq_init,
	.init_machine   = helanx_init_machine,
	.dt_compat      = pxa1908_dt_board_compat,
MACHINE_END

static const char * const pxa1936_dt_board_compat[] __initconst = {
	"marvell,pxa1936",
	NULL,
};

DT_MACHINE_START(PXA1936_DT, "PXA1936")
	.init_time      = helanx_timer_init,
	.init_machine   = helanx_init_machine,
	.dt_compat      = pxa1936_dt_board_compat,
MACHINE_END
