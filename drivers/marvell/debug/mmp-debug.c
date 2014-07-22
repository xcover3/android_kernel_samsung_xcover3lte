/*
 * linux/drivers/marvell/mmp-debug.c
 *
 * Author:	Neil Zhang <zhangwm@marvell.com>
 * Copyright:	(C) 2013 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/init.h>
#include <linux/reboot.h>
#include <linux/kexec.h>
#include <linux/signal.h>
#include <linux/of_address.h>

#include <asm/cacheflush.h>
#include <asm/system_misc.h>

#define FAB_TIMEOUT_CTRL	0x60
#define FAB_TIMEOUT_STATUS0	0x68
#define FAB_TIMEOUT_STATUS1	0x70

#define STATE_HOLD_CTRL		0x88
#define FABTIMEOUT_HELD_WSTATUS	0x78
#define FABTIMEOUT_HELD_RSTATUS	0x80
#define DVC_HELD_STATUS		0xB0
#define FCDONE_HELD_STATUS	0xB8
#define PMULPM_HELD_STATUS	0xC0
#define CORELPM_HELD_STATUS	0xC8

struct held_status {
	u32 fabws;
	u32 fabrs;
	u32 dvcs;
	u32 fcdones;
	u32 pmulpms;
	u32 corelpms;
};

void __attribute__((weak)) set_emmd_indicator(void) { }

static void __iomem *squ_base;

static u32 fab_timeout_write_addr, fab_timeout_read_addr;
static u32 finish_save_cpu_ctx;
static u32 err_fsr;
static u32 err_addr;

static struct held_status recorded_helds;

static int mmp_axi_timeout(unsigned long addr, unsigned int fsr,
				struct pt_regs *regs)
{
	struct pt_regs fixed_regs;
	u32 tmp;

	tmp = readl_relaxed(squ_base + FAB_TIMEOUT_STATUS0);
	fab_timeout_write_addr = tmp & 0xfffffffc;
	tmp = readl_relaxed(squ_base + FAB_TIMEOUT_STATUS1);
	fab_timeout_read_addr = tmp & 0xfffffffc;

	/* Return For those not caused by AXI timeout */
	if (!fab_timeout_write_addr && !fab_timeout_read_addr) {
		pr_info("AXI timeout not recorded!\n");
		return 1;
	}

	err_fsr = fsr;
	err_addr = addr;

	set_emmd_indicator();

	keep_silent = 1;
	crash_setup_regs(&fixed_regs, regs);
	crash_save_vmcoreinfo();
	machine_crash_shutdown(&fixed_regs);
	finish_save_cpu_ctx = 1;

	flush_cache_all();

	/* Waiting wdt to reset the Soc */
	while (1)
		;

	return 0;
}

/* dump Fabric/LPM/DFC/DVC held status and enable held feature */
static int __init mmp_dump_heldstatus(void __iomem *squ_base)
{
	recorded_helds.fabws =
		readl_relaxed(squ_base + FABTIMEOUT_HELD_WSTATUS);
	recorded_helds.fabrs =
		readl_relaxed(squ_base + FABTIMEOUT_HELD_RSTATUS);
	recorded_helds.dvcs = readl_relaxed(squ_base + DVC_HELD_STATUS);
	recorded_helds.fcdones = readl_relaxed(squ_base + FCDONE_HELD_STATUS);
	recorded_helds.pmulpms = readl_relaxed(squ_base + PMULPM_HELD_STATUS);
	recorded_helds.corelpms = readl_relaxed(squ_base + CORELPM_HELD_STATUS);

	/* after register dump, then enable the held feature for debug */
	writel_relaxed(0x1, squ_base + STATE_HOLD_CTRL);

	pr_info("*************************************\n");
	pr_info("Fabric/LPM/DFC/DVC held status dump:\n");

	if (recorded_helds.fabws)
		pr_info("AXI time out occurred when write address 0x%x!!!\n",
			recorded_helds.fabws & 0xfffffffc);
	else if (recorded_helds.fabrs)
		pr_info("AXI time out occurred when read address 0x%x!!!\n",
			recorded_helds.fabrs & 0xfffffffc);

	pr_info("DVC[%x]\n", recorded_helds.dvcs);
	pr_info("FCDONE[%x]\n", recorded_helds.fcdones);
	pr_info("PMULPM[%x]\n", recorded_helds.pmulpms);
	pr_info("CORELPM[%x]\n", recorded_helds.corelpms);

	pr_info("*************************************\n");
	return 0;
}

static int __init mmp_debug_init(void)
{
	struct device_node *node;
	u32 tmp;

	node = of_find_compatible_node(NULL, NULL, "mrvl,mmp-debug");
	if (!node) {
		pr_info("This platform doesn't support debug feature!\n");
		return -ENODEV;
	}

	squ_base = of_iomap(node, 0);
	if (!squ_base) {
		pr_err("Failed to map squ register\n");
		return -ENOMEM;
	}

	/* configure to data abort mode */
	tmp = readl_relaxed(squ_base + FAB_TIMEOUT_CTRL);
	tmp |= (1 << 29) | (1 << 30);
	writel_relaxed(tmp, squ_base + FAB_TIMEOUT_CTRL);

	/* Register debug fault handler. */
#ifdef CONFIG_ARM
	hook_fault_code(0x8, mmp_axi_timeout, SIGBUS,
			0, "external abort on non-linefetch");
	hook_fault_code(0xc, mmp_axi_timeout, SIGBUS,
			0, "external abort on translation");
	hook_fault_code(0xe, mmp_axi_timeout, SIGBUS,
			0, "external abort on translation");
	hook_fault_code(0x16, mmp_axi_timeout, SIGBUS,
			BUS_OBJERR, "imprecise external abort");
#endif

#ifdef CONFIG_ARM64
	hook_fault_code(0x10, mmp_axi_timeout, SIGBUS,
			0, "synchronous external abort");
	hook_fault_code(0x11, mmp_axi_timeout, SIGBUS,
			0, "asynchronous external abort");
#endif

	mmp_dump_heldstatus(squ_base);
	return 0;
}

arch_initcall(mmp_debug_init);
