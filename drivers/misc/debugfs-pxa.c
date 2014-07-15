/*
 * drivers/misc/debugfs-pxa.c
 *
 * Author:	Neil Zhang <zhangwm@marvell.com>
 * Copyright:	(C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/debugfs.h>

struct dentry *pxa;

static int __init pxa_debugfs_init(void)
{
	pxa = debugfs_create_dir("pxa", NULL);
	if (!pxa)
		return -ENOENT;

	return 0;
}

core_initcall_sync(pxa_debugfs_init);
