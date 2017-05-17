/*
 * SP2529 sensor driver.
 *
 * Copyright (C) 2013 Marvell Internation Ltd.
 * Copyright (C) 2013 GalaxyCore Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include "sp2529.h"

char *sp2529_get_profile(const struct i2c_client *client)
{
	return "pxa-mipi";
}

static int sp2529_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *inter)
{
	inter->interval.numerator = 24;
	inter->interval.denominator = 1;

	return 0;
}

static int __init sp2529_mod_init(void)
{
	return xsd_add_driver(sp2529_drv_table);
}

static void __exit sp2529_mod_exit(void)
{
	xsd_del_driver(sp2529_drv_table);
}

module_init(sp2529_mod_init);
module_exit(sp2529_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("sp2529 Camera Driver");
