/*
 * linux/arch/arm/mach-mmp/reset.c
 *
 * Author:	Neil Zhang <zhangwm@marvell.com>
 * Copyright:	(C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/smp.h>

#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/mach/map.h>
#include <asm/mcpm.h>

#include "reset.h"
#include "regs-addr.h"

#define APMU_APPS_CORE_RESET(x) (0x18 + (x)*4)
/*
 * This function is called from boot_secondary to bootup the secondary cpu.
 */
void mmp_cpu_power_up(unsigned int cpu, unsigned int cluster)
{
	u32 tmp;
	void __iomem *apmu_base;

	apmu_base = regs_addr_get_va(REGS_ADDR_APMU);
	BUG_ON(!apmu_base);

	writel(1, apmu_base + APMU_APPS_CORE_RESET(cpu));
}

#define BXADDR_TABLE		0x20
#define BXADDR_TABLE_SIZE	0x10
void __init mmp_entry_vector_init(void)
{
	void __iomem *bxaddr;
	int i;
	bxaddr = ioremap(BXADDR_TABLE, BXADDR_TABLE_SIZE);
	if (!bxaddr) {
		pr_err("ioremap bxaddr failed\n");
		return;
	}
	for (i = 0; i < BXADDR_TABLE_SIZE/sizeof(unsigned); i++)
		writel(__pa(mcpm_entry_point), bxaddr + i*sizeof(unsigned));
	iounmap(bxaddr);
}
