/*
 * linux/drivers/video/mmp/fb/fb_overlay.c
 * Framebuffer driver for Marvell Display controller.
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors: Zhou Zhu <zzhu3@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include "mmpfb.h"

static int mmpfb_overlay_open(struct fb_info *info, int user)
{
	struct mmpfb_info *fbi = info->par;

	atomic_inc(&fbi->op_count);
	dev_info(info->dev, "fb-overlay open: op_count = %d\n",
		 atomic_read(&fbi->op_count));
	return 0;
}

static int mmpfb_overlay_release(struct fb_info *info, int user)
{
	struct mmpfb_info *fbi = info->par;

	atomic_dec(&fbi->op_count);

	dev_info(info->dev, "fb-overlay release: op_count = %d\n",
		 atomic_read(&fbi->op_count));
	return 0;
}

static struct fb_ops mmpfb_overlay_ops = {
	.fb_open	= mmpfb_overlay_open,
	.fb_ioctl	= mmpfb_ioctl,
#ifdef CONFIG_COMPAT
	.fb_compat_ioctl	= mmpfb_compat_ioctl,
#endif
	.fb_release	= mmpfb_overlay_release,
};

#ifdef CONFIG_OF
static const struct of_device_id mmpfb_overlay_dt_match[] = {
	{ .compatible = "marvell,mmp-fb-overlay" },
	{},
};
#endif

static int mmpfb_overlay_probe(struct platform_device *pdev)
{
	struct mmp_buffer_driver_mach_info *mi;
	struct fb_info *info = 0;
	struct mmpfb_info *fbi = 0;
	const char *path_name;
	int overlay_id = 0, ret;

	/* initialize fb */
	info = framebuffer_alloc(sizeof(struct mmpfb_info), &pdev->dev);
	if (info == NULL)
		return -ENOMEM;
	fbi = info->par;
	if (!fbi) {
		ret = -EINVAL;
		goto failed;
	}

	if (IS_ENABLED(CONFIG_OF)) {
		struct device_node *np = pdev->dev.of_node;

		if (!np)
			return -EINVAL;
		if (of_property_read_string(np, "marvell,fb-name", &fbi->name))
			return -EINVAL;
		if (of_property_read_string(np, "marvell,path-name",
					    &path_name))
			return -EINVAL;
		if (of_property_read_u32(np, "marvell,overlay-id",
					 &overlay_id))
			return -EINVAL;
	} else {
		mi = pdev->dev.platform_data;
		if (mi == NULL) {
			dev_err(&pdev->dev, "no platform data defined\n");
			return -EINVAL;
		}

		fbi->name = mi->name;
		path_name = mi->path_name;
		overlay_id = mi->overlay_id;
	}

	info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_DISABLED;
	info->node = -1;
	strcpy(info->fix.id, fbi->name);
	info->fix.accel = FB_ACCEL_NONE;
	info->fbops = &mmpfb_overlay_ops;
	/* init fb */
	fbi->fb_info = info;
	platform_set_drvdata(pdev, fbi);
	fbi->dev = &pdev->dev;
	mutex_init(&fbi->access_ok);

	/* get display path by name */
	fbi->path = mmp_get_path(path_name);
	if (!fbi->path) {
		dev_err(&pdev->dev, "can't get the path %s\n", path_name);
		ret = -EINVAL;
		goto failed_destroy_mutex;
	}

	dev_info(fbi->dev, "path %s get\n", fbi->path->name);

	/* get overlay */
	fbi->overlay = mmp_path_get_overlay(fbi->path, overlay_id);
	if (!fbi->overlay) {
		ret = -EINVAL;
		goto failed_destroy_mutex;
	}

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register fb: %d\n", ret);
		ret = -ENXIO;
		goto failed_destroy_mutex;
	}

	dev_info(fbi->dev, "loaded to /dev/fb%d <%s>.\n",
		info->node, info->fix.id);

	return 0;

failed_destroy_mutex:
	mutex_destroy(&fbi->access_ok);
failed:
	if (fbi)
		dev_err(fbi->dev, "mmp-fb: frame buffer device init failed\n");
	platform_set_drvdata(pdev, NULL);

	framebuffer_release(info);

	return ret;
}

static struct platform_driver mmpfb_overlay_driver = {
	.driver		= {
		.name	= "mmp-fb-overlay",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mmpfb_overlay_dt_match),
	},
	.probe		= mmpfb_overlay_probe,
};

static int mmpfb_overlay_init(void)
{
	return platform_driver_register(&mmpfb_overlay_driver);
}
module_init(mmpfb_overlay_init);

MODULE_AUTHOR("Zhou Zhu <zhou.zhu@marvell.com>");
MODULE_DESCRIPTION("Framebuffer driver for Marvell displays");
MODULE_LICENSE("GPL");
