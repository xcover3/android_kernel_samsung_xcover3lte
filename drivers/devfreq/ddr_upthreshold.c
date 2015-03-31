/*
 * drivers/devfreq/ddr_upthreshold.c
 *
 * Author:	Johnson Lu <lllu@marvell.com>
 * Copyright:	(C) 2015 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kobject.h>
#include <linux/init.h>
#include <linux/kernfs.h>
#include <linux/ddr_upthreshold.h>


static const char *node_name = "ddr_upthreshold";

struct kobject *ddr_upthrd_obj;
EXPORT_SYMBOL(ddr_upthrd_obj);

int __init ddr_upthreshold_init(void)
{
	struct kernfs_node *ddr_upthreshold;

	ddr_upthrd_obj = kobject_create_and_add(node_name, kernel_kobj);
	if (!ddr_upthrd_obj) {
		pr_err("[%s] failed to create a sysfs kobject\n", __func__);
		return 1;
	}

	ddr_upthreshold = kernfs_create_dir(kernel_kobj->sd, node_name,
			S_IRWXU | S_IRUGO | S_IXUGO, ddr_upthrd_obj);
	if (IS_ERR(ddr_upthreshold)) {
		pr_err("[%s] failed to create sysfs node /sys/kernel/%s/\n",
				__func__, node_name);
		return PTR_ERR(ddr_upthreshold);
	}

	return 0;
}

core_initcall_sync(ddr_upthreshold_init);
