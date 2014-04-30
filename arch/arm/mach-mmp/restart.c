/*
 * linux/arch/arm/mach-mmp/restart.c
 *
 * Author:	Yilu Mao <ylmao@marvell.com>
 * Copyright:	(C) 2013 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/mm.h>
#include <linux/io.h>
#include <mach/cputype.h>
#include <linux/delay.h>
#include <asm/mach/arch.h>

#define CP_TIMERS2_BASE (0xd4080000)
#define WDT_SIZE       (0xff)

#define RTC_BASE	(0xD4010000)
#define RTC_SIZE	(0x10000)

#define REG_RTC_BR0	(0x14)

#define MPMU_BASE	(0xD4050000)
#define MPMU_SIZE	(0x20000)

#define MPMU_APRR	(0x1020)
#define MPMU_CPRR	(0x0020)
#define MPMU_WDTPCR	(0x0200)

#define MPMU_APRR_WDTR	(1<<4)
#define MPMU_APRR_CPR	(1<<0)
#define MPMU_CPRR_DSPR	(1<<2)
#define MPMU_CPRR_BBR	(1<<3)

extern void pxa_wdt_reset(void __iomem *watchdog_virt_base,
void __iomem *mpmu_vaddr);
/* Using watchdog restart */
static void do_wdt_restart(const char *cmd)
{
	u32 reg, backup;
	s8 magic[5];
	void __iomem *mpmu_vaddr, *rtc_vaddr;
	void __iomem *watchdog_virt_base;

	mpmu_vaddr = ioremap(MPMU_BASE, MPMU_SIZE);
	BUG_ON(!mpmu_vaddr);

	rtc_vaddr = ioremap(RTC_BASE, RTC_SIZE);
	BUG_ON(!rtc_vaddr);

	watchdog_virt_base = ioremap(CP_TIMERS2_BASE, WDT_SIZE);
	BUG_ON(!watchdog_virt_base);

	/* Hold cp to avoid restart watchdog */
	if (cpu_is_pxa1L88()) {
		/* hold CP first */
		reg = readl(mpmu_vaddr + MPMU_APRR) | MPMU_APRR_CPR;
		writel(reg, mpmu_vaddr + MPMU_APRR);
		udelay(10);

		/* CP restart MSA */
		reg = readl(mpmu_vaddr + MPMU_CPRR) | MPMU_CPRR_DSPR | MPMU_CPRR_BBR;
		writel(reg, mpmu_vaddr + MPMU_CPRR);
		udelay(10);
	}

	/* If reboot by recovery, store info for uboot */
	if (cpu_is_pxa1L88()) {
		memset(magic, 0x0, sizeof(magic));
		if (cmd) {
			if (!strcmp(cmd, "recovery"))
				strncpy(magic, cmd, 4);
			else if (!strcmp(cmd, "fastboot"))
				strncpy(magic, "brfb", 4);
			else
				strncpy(magic, "rebt", 4);
		} else {
			strncpy(magic, "rebt", 4);
		}

		backup = magic[0] << 24 | magic[1] << 16 |
			magic[2] << 8 | magic[3];
		do {
			writel(backup, rtc_vaddr + REG_RTC_BR0);
		} while (readl(rtc_vaddr + REG_RTC_BR0) != backup);

	}

	/* Using Watchdog to reset.
	 * Note that every platform should provide such API,
	 * or this part can't pass compiling
	 */
	pxa_wdt_reset(watchdog_virt_base, mpmu_vaddr);

	iounmap(mpmu_vaddr);
	iounmap(rtc_vaddr);
}

void mmp_arch_restart(enum reboot_mode mode, const char *cmd)
{
	if (!cpu_is_pxa1L88()) {
		pr_err("%s: unsupported cpu.\n", __func__);
		return;
	}

	switch (mode) {
	case REBOOT_SOFT:
		/* Jump into ROM at address 0 */
		cpu_reset(0);
		break;
	case REBOOT_WARM:
	default:
		do_wdt_restart(cmd);
		break;
	}
}
