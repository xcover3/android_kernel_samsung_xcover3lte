/*
 *  linux/arch/arm/mach-mmp/px1978-dt.c
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
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/clocksource.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/memblock.h>

#include <asm/mach/arch.h>

#include <linux/cputype.h>

#include "common.h"
#include "regs-addr.h"
#include "mmpx-dt.h"

#ifdef CONFIG_SD8XXX_RFKILL
#include <linux/sd8x_rfkill.h>
#endif

static const struct of_dev_auxdata mmpx_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xD128dc00, "mmp-sspa-dai.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xD128dd00, "mmp-sspa-dai.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxa910-ssp", 0xd42a0c00, "pxa988-ssp.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxa910-ssp", 0xd4039000, "pxa988-ssp.4", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-ssp-dai", 1, "pxa-ssp-dai.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-ssp-dai", 4, "pxa-ssp-dai.2", NULL),
	OF_DEV_AUXDATA("marvell,pxa-88pm805-snd-card", 0, "sound", NULL),
#ifdef CONFIG_SD8XXX_RFKILL
	OF_DEV_AUXDATA("mrvl,sd8x-rfkill", 0, "sd8x-rfkill", NULL),
#endif
	OF_DEV_AUXDATA("marvell,mmp-disp", 0xd420b000, "mmp-disp", NULL),
	{}
};

/* wdt and cp use the clock */
static __init void enable_pxawdt_clock(void)
{
}

#define GENERIC_COUNTER_PHYS_BASE	0xf6130000 /* time stamp */
#define CNTCR				0x00 /* Counter Control Register */
#define CNTCR_EN			(3 << 0) /* The counter is enabled */

#define MCCU_COUNTER_CLK_SEL		0xc

static __init void enable_arch_timer(void)
{
	void __iomem *mccu_base, *tmr_base;

	mccu_base = regs_addr_get_va(REGS_ADDR_CIU);
	if (!mccu_base) {
		pr_err("ioremap mccu_base failed\n");
		return;
	}

	tmr_base = ioremap(GENERIC_COUNTER_PHYS_BASE, SZ_4K);
	if (!tmr_base) {
		pr_err("opremap tmr_base failed\n");
		return;
	}

	writel(0xf, mccu_base + MCCU_COUNTER_CLK_SEL);

	writel(CNTCR_EN, tmr_base + CNTCR);

	iounmap(tmr_base);
}

/* Common APB clock register bit definitions */
#define APBC_APBCLK	(1 << 9)  /* APB Bus Clock Enable */
#define APBC_FNCLK	(1 << 8)  /* Functional Clock Enable */
#define APBC_RST	(1 << 0)  /* Reset Generation */

/* Functional Clock Selection Mask */
#define APBC_FNCLKSEL(x)	(((x) & 0x1f) << 16)
#define TIMER_CLK	0x1b /* VCTCXO/2 */

static u32 timer_clkreg[] = {0xb0, 0xb4, 0xb8, 0x0};

static __init void enable_soc_timer(void)
{
	void __iomem *apbc_base;
	int i;

	apbc_base = regs_addr_get_va(REGS_ADDR_APBC);
	if (!apbc_base) {
		pr_err("ioremap apbc_base failed\n");
		return;
	}

	for (i = 0; timer_clkreg[i]; i++) {
		/* Select the configurable clock rate to be 3.25MHz */
		writel(APBC_APBCLK | APBC_RST, apbc_base + timer_clkreg[i]);
		writel(APBC_APBCLK | APBC_FNCLK | APBC_FNCLKSEL(TIMER_CLK),
			apbc_base + timer_clkreg[i]);
	}
}

static void __init pxa1978_sdhc_reset_all(void)
{
}

static void __init pxa1978_dt_irq_init(void)
{
	irqchip_init();
	/* only for wake up */
	mmp_of_wakeup_init();
}

static __init void pxa1978_timer_init(void)
{
	regs_addr_iomap();

	/* this is early, initialize mmp_chip_id here */
	mmp_chip_id = 0;

	enable_pxawdt_clock();

#ifdef CONFIG_ARM_ARCH_TIMER
	enable_arch_timer();
#endif

	enable_soc_timer();

	of_clk_init(NULL);

	clocksource_of_init();

	pxa1978_sdhc_reset_all();
}

/* For HELANLTE CP memeory reservation, 32MB by default */
static u32 cp_area_size = 0x02000000;
static u32 cp_area_addr = 0x06000000;
static int __init early_cpmem(char *p)
{
	char *endp;

	cp_area_size = memparse(p, &endp);
	if (*endp == '@')
		cp_area_addr = memparse(endp + 1, NULL);

	return 0;
}
early_param("cpmem", early_cpmem);

static void pxa_reserve_cp_memblock(void)
{
	/* Reserve memory for CP */
	BUG_ON(memblock_reserve(cp_area_addr, cp_area_size) != 0);
	memblock_free(cp_area_addr, cp_area_size);
	memblock_remove(cp_area_addr, cp_area_size);
	pr_info("Reserved CP memory: 0x%x@0x%x\n", cp_area_size, cp_area_addr);
}

static void __init pxa1978_init_machine(void)
{
	of_platform_populate(NULL, of_default_bus_match_table,
			     mmpx_auxdata_lookup, &platform_bus);
}

static void pxa_reserve_secmem(void)
{
	unsigned long start = PLAT_PHYS_OFFSET;
	unsigned size = CONFIG_TEXT_OFFSET & ~((1<<21) - 1);
	/* Reserve memory for secure state */
	BUG_ON(memblock_reserve(start, size) != 0);
	memblock_free(start, size);
	memblock_remove(start, size);
	pr_info("Reserved secure memory: 0x%x@0x%lx\n", size, start);
}

static void __init pxa1978_reserve(void)
{
	pxa_reserve_secmem();

	pxa_reserve_cp_memblock();
}

static const char * const pxa1978_dt_board_compat[] __initconst = {
	"marvell,pxa1978",
	NULL,
};

DT_MACHINE_START(PXA1978_DT, "PXA1978")
	.smp_init	= smp_init_ops(mmp_smp_init_ops),
	.init_time      = pxa1978_timer_init,
	.init_machine	= pxa1978_init_machine,
	.dt_compat      = pxa1978_dt_board_compat,
	.reserve	= pxa1978_reserve,
#if (0)
	.restart	= mmp_arch_restart, /* restart.c is mmp specific */
#endif
MACHINE_END

