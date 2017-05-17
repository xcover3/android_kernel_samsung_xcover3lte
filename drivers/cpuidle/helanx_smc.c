/*
 * helanx smc generic driver.
 *
 * Copyright (C) 2015 Marvell Ltd.
 * Author: Xiaoguang Chen <chenxg@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/compiler.h>
#include <asm-generic/int-ll64.h>
#include <linux/helanx_smc.h>
#include <linux/edge_wakeup_mmp.h>

static unsigned int get_gpio_address(int gpio)
{
	if (gpio <= 54)
		return GPIO_0_ADDR + gpio * 4;
	else if (gpio <= 59)
		BUG_ON("GPIO number doesn't exist!\n");
	else if (gpio <= 66)
		return GPIO_60_ADDR + (gpio - 60) * 4;
	else if (gpio <= 98)
		return GPIO_67_ADDR + (gpio - 67) * 4;
	else if (gpio == 99)
		return GPIO_99_ADDR;
	else if (gpio <= 109)
		return GPIO_100_ADDR + (gpio - 100) * 4;
	else if (gpio <= 116)
		return GPIO_110_ADDR + (gpio - 110) * 4;
	else if (gpio <= 120)
		return GPIO_117_ADDR + (gpio - 117) * 4;
	else if (gpio == 121)
		return GPIO_121_ADDR;
	else if (gpio <= 125)
		return GPIO_122_ADDR + (gpio - 122) * 4;
	else if (gpio <= 127)
		return GPIO_126_ADDR + (gpio - 126) * 4;
	else
		BUG_ON("GPIO number doesn't exist!\n");
}


int mfp_edge_wakeup_notify(struct notifier_block *nb,
			   unsigned long val, void *data)
{
	int error = 0, gpio;
	unsigned long addr;
	int (*invoke_smc_fn)(u64, u64, u64, u64) = __invoke_fn_smc;
	gpio = *(int *)data;
	addr = get_gpio_address(gpio);
	switch (val) {
	case GPIO_ADD:
		error = invoke_smc_fn(LC_ADD_GPIO_EDGE_WAKEUP, addr, 0, 0);
		break;
	case GPIO_REMOVE:
		error = invoke_smc_fn(LC_REMOVE_GPIO_EDGE_WAKEUP, addr, 0, 0);
		break;
	default:
		BUG_ON("Wrong LC command for GPIO edge wakeup!");
	};

	if (!error)
		return NOTIFY_OK;
	else {
		pr_warn("GPIO %d %s failed!\n", gpio,
			(val == GPIO_ADD) ? "add" : "remove");
		return NOTIFY_BAD;
	}
}

int store_share_address(unsigned long addr, unsigned long len)
{
	int error = 0;
	int (*invoke_smc_fn)(u64, u64, u64, u64) = __invoke_fn_smc;
	error = invoke_smc_fn(LC_ADD_SHARE_ADDRESS, addr, len, 0);
	return error;
}

noinline int __invoke_fn_smc(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (function_id)
		: "r" (arg0), "r" (arg1), "r" (arg2));

	return function_id;
}
