/*
 * linux/drivers/video/mmp/hw/mmp_ctrl.c
 * Marvell MMP series Display Controller support
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors:  Guoqing Li <ligq@marvell.com>
 *          Lisa Du <cldu@marvell.com>
 *          Zhou Zhu <zzhu3@marvell.com>
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
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "mmp_ctrl.h"

static irqreturn_t ctrl_handle_irq(int irq, void *dev_id)
{
	struct mmphw_ctrl *ctrl = (struct mmphw_ctrl *)dev_id;
	u32 isr, imask, tmp;

	isr = readl_relaxed(ctrl->reg_base + SPU_IRQ_ISR);
	imask = readl_relaxed(ctrl->reg_base + SPU_IRQ_ENA);

	do {
		/* clear clock only */
		tmp = readl_relaxed(ctrl->reg_base + SPU_IRQ_ISR);
		if (tmp & isr)
			writel_relaxed(~isr, ctrl->reg_base + SPU_IRQ_ISR);
	} while ((isr = readl_relaxed(ctrl->reg_base + SPU_IRQ_ISR)) & imask);

	return IRQ_HANDLED;
}

static u32 fmt_to_reg(int overlay_id, int pix_fmt)
{
	u32 rbswap = 0, uvswap = 0, yuvswap = 0,
		csc_en = 0, val = 0,
		vid = overlay_is_vid(overlay_id);

	switch (pix_fmt) {
	case PIXFMT_RGB565:
	case PIXFMT_RGB1555:
	case PIXFMT_RGB888PACK:
	case PIXFMT_RGB888UNPACK:
	case PIXFMT_RGBA888:
		rbswap = 1;
		break;
	case PIXFMT_VYUY:
	case PIXFMT_YVU422P:
	case PIXFMT_YVU420P:
		uvswap = 1;
		break;
	case PIXFMT_YUYV:
		yuvswap = 1;
		break;
	default:
		break;
	}

	switch (pix_fmt) {
	case PIXFMT_RGB565:
	case PIXFMT_BGR565:
		break;
	case PIXFMT_RGB1555:
	case PIXFMT_BGR1555:
		val = 0x1;
		break;
	case PIXFMT_RGB888PACK:
	case PIXFMT_BGR888PACK:
		val = 0x2;
		break;
	case PIXFMT_RGB888UNPACK:
	case PIXFMT_BGR888UNPACK:
		val = 0x3;
		break;
	case PIXFMT_RGBA888:
	case PIXFMT_BGRA888:
		val = 0x4;
		break;
	case PIXFMT_UYVY:
	case PIXFMT_VYUY:
	case PIXFMT_YUYV:
		val = 0x5;
		csc_en = 1;
		break;
	case PIXFMT_YUV422P:
	case PIXFMT_YVU422P:
		val = 0x6;
		csc_en = 1;
		break;
	case PIXFMT_YUV420P:
	case PIXFMT_YVU420P:
		val = 0x7;
		csc_en = 1;
		break;
	default:
		break;
	}

	return (dma_palette(0) | dma_fmt(vid, val) |
		dma_swaprb(vid, rbswap) | dma_swapuv(vid, uvswap) |
		dma_swapyuv(vid, yuvswap) | dma_csc(vid, csc_en));
}

static void overlay_set_fmt(struct mmp_overlay *overlay)
{
	u32 tmp;
	struct mmp_path *path = overlay->path;
	int overlay_id = overlay->id;

	tmp = readl_relaxed(ctrl_regs(path) + dma_ctrl(0, path->id));
	tmp &= ~dma_mask(overlay_is_vid(overlay_id));
	tmp |= fmt_to_reg(overlay_id, overlay->win.pix_fmt);
	writel_relaxed(tmp, ctrl_regs(path) + dma_ctrl(0, path->id));
}

static void overlay_set_win(struct mmp_overlay *overlay, struct mmp_win *win)
{
	struct lcd_regs *regs = path_regs(overlay->path);
	int overlay_id = overlay->id;

	/* assert win supported */
	memcpy(&overlay->win, win, sizeof(struct mmp_win));

	mutex_lock(&overlay->access_ok);

	if (overlay_is_vid(overlay_id)) {
		writel_relaxed(win->pitch[0], &regs->v_pitch_yc);
		writel_relaxed(win->pitch[2] << 16 |
				win->pitch[1], &regs->v_pitch_uv);

		writel_relaxed((win->ysrc << 16) | win->xsrc, &regs->v_size);
		writel_relaxed((win->ydst << 16) | win->xdst, &regs->v_size_z);
		writel_relaxed(win->ypos << 16 | win->xpos, &regs->v_start);
	} else {
		writel_relaxed(win->pitch[0], &regs->g_pitch);

		writel_relaxed((win->ysrc << 16) | win->xsrc, &regs->g_size);
		writel_relaxed((win->ydst << 16) | win->xdst, &regs->g_size_z);
		writel_relaxed(win->ypos << 16 | win->xpos, &regs->g_start);
	}

	overlay_set_fmt(overlay);
	mutex_unlock(&overlay->access_ok);
}

static void dmafetch_onoff(struct mmp_overlay *overlay, int on)
{
	int overlay_id = overlay->id;
	u32 mask = overlay_is_vid(overlay_id) ? CFG_DMA_ENA_MASK :
		CFG_GRA_ENA_MASK;
	u32 enable = overlay_is_vid(overlay_id) ? CFG_DMA_ENA(1) :
		CFG_GRA_ENA(1);
	u32 tmp;
	struct mmp_path *path = overlay->path;

	/* dma enable control */
	tmp = readl_relaxed(ctrl_regs(path) + dma_ctrl(0, path->id));
	tmp &= ~mask;
	tmp |= (on ? enable : 0);
	writel(tmp, ctrl_regs(path) + dma_ctrl(0, path->id));
}

static void path_enabledisable(struct mmp_path *path, int on)
{
	struct mmphw_path_plat *plat = path_to_path_plat(path);
	struct clk *clk = plat->clk;
	u32 tmp;

	if (!clk)
		return;

	/* path enable control */
	tmp = readl_relaxed(ctrl_regs(path) + intf_ctrl(path->id));
	tmp &= ~CFG_DUMB_ENA_MASK;
	tmp |= (on ? CFG_DUMB_ENA(1) : 0);

	if (on) {
		clk_prepare_enable(clk);
		writel_relaxed(tmp, ctrl_regs(path) + intf_ctrl(path->id));
	} else {
		writel_relaxed(tmp, ctrl_regs(path) + intf_ctrl(path->id));
		clk_disable_unprepare(clk);
	}
}

static void path_set_timing(struct mmp_path *path)
{
	struct lcd_regs *regs = path_regs(path);
	u32 total_x, total_y, vsync_ctrl, tmp,
		link_config = path_to_path_plat(path)->link_config;
	struct mmp_mode *mode = &path->mode;
	struct clk *clk = path_to_path_plat(path)->clk;

	/* polarity of timing signals */
	tmp = readl_relaxed(ctrl_regs(path) + intf_ctrl(path->id)) & 0x1;
	tmp |= mode->vsync_invert ? 0 : 0x8;
	tmp |= mode->hsync_invert ? 0 : 0x4;
	tmp |= link_config & CFG_DUMBMODE_MASK;
	tmp |= CFG_DUMB_ENA(1);
	writel_relaxed(tmp, ctrl_regs(path) + intf_ctrl(path->id));

	/* interface rb_swap setting */
	tmp = readl_relaxed(ctrl_regs(path) + intf_rbswap_ctrl(path->id)) &
		(~(CFG_INTFRBSWAP_MASK));
	tmp |= link_config & CFG_INTFRBSWAP_MASK;
	writel_relaxed(tmp, ctrl_regs(path) + intf_rbswap_ctrl(path->id));

	writel_relaxed((mode->yres << 16) | mode->xres, &regs->screen_active);
	writel_relaxed((mode->left_margin << 16) | mode->right_margin,
		&regs->screen_h_porch);
	writel_relaxed((mode->upper_margin << 16) | mode->lower_margin,
		&regs->screen_v_porch);
	total_x = mode->xres + mode->left_margin + mode->right_margin +
		mode->hsync_len;
	total_y = mode->yres + mode->upper_margin + mode->lower_margin +
		mode->vsync_len;
	writel_relaxed((total_y << 16) | total_x, &regs->screen_size);

	/* vsync ctrl */
	if (path->output_type == PATH_OUT_DSI)
		vsync_ctrl = 0x01330133;
	else
		vsync_ctrl = ((mode->xres + mode->right_margin) << 16)
					| (mode->xres + mode->right_margin);
	writel_relaxed(vsync_ctrl, &regs->vsync_ctrl);

	/* set path_clk */
	if (clk && path->output_type == PATH_OUT_PARALLEL)
		clk_set_rate(clk, mode->pixclock_freq);
}

static void path_onoff(struct mmp_path *path, int on)
{
	if (path->status == on) {
		dev_info(path->dev, "path %s is already %s\n",
				path->name, status_name(path->status));
		return;
	}

	mutex_lock(&path->access_ok);

	if (on) {
		path_enabledisable(path, 1);

		if (path->panel && path->panel->set_status)
			path->panel->set_status(path->panel, MMP_ON);
	} else {
		if (path->panel && path->panel->set_status)
			path->panel->set_status(path->panel, MMP_OFF);

		path_enabledisable(path, 0);
	}
	path->status = on;

	mutex_unlock(&path->access_ok);
}

static void overlay_do_onoff(struct mmp_overlay *overlay, int status)
{
	struct mmphw_ctrl *ctrl = path_to_ctrl(overlay->path);
	int on = status_is_on(status);
	struct mmp_path *path = overlay->path;

	mutex_lock(&ctrl->access_ok);

	overlay->status = on;

	if (status == MMP_ON_REDUCED) {
		path_enabledisable(path, 1);
		if (path->panel && path->panel->set_status)
			path->panel->set_status(path->panel, status);
		path->status = on;
	} else if (on) {
		if (path->ops.check_status(path) != path->status)
			path_onoff(path, on);

		dmafetch_onoff(overlay, on);
	} else {
		dmafetch_onoff(overlay, on);

		if (path->ops.check_status(path) != path->status)
			path_onoff(path, on);
	}

	mutex_unlock(&ctrl->access_ok);
}

static void overlay_set_status(struct mmp_overlay *overlay, int status)
{
	int on = status_is_on(status);
	mutex_lock(&overlay->access_ok);
	switch (status) {
	case MMP_ON:
	case MMP_ON_REDUCED:
		if (!atomic_read(&overlay->on_count))
			overlay_do_onoff(overlay, status);
		atomic_inc(&overlay->on_count);
		break;
	case MMP_OFF:
		if (atomic_dec_and_test(&overlay->on_count))
			overlay_do_onoff(overlay, status);
		break;
	case MMP_ON_DMA:
	case MMP_OFF_DMA:
		overlay->status = on;
		dmafetch_onoff(overlay, on);
		break;
	default:
		break;
	}
	dev_dbg(overlay_to_ctrl(overlay)->dev, "set %s: count %d\n",
		status_name(status), atomic_read(&overlay->on_count));
	mutex_unlock(&overlay->access_ok);
}

static int overlay_set_addr(struct mmp_overlay *overlay, struct mmp_addr *addr)
{
	struct lcd_regs *regs = path_regs(overlay->path);
	int overlay_id = overlay->id;

	/* FIXME: assert addr supported */
	memcpy(&overlay->addr, addr, sizeof(struct mmp_addr));

	if (overlay_is_vid(overlay_id)) {
		writel_relaxed(addr->phys[0], &regs->v_y0);
		writel_relaxed(addr->phys[1], &regs->v_u0);
		writel_relaxed(addr->phys[2], &regs->v_v0);
	} else
		writel_relaxed(addr->phys[0], &regs->g_0);

	return overlay->addr.phys[0];
}


static int is_mode_changed(struct mmp_mode *dst, struct mmp_mode *src)
{
	return !src || !dst
		|| src->refresh != dst->refresh
		|| src->xres != dst->xres
		|| src->yres != dst->yres
		|| src->left_margin != dst->left_margin
		|| src->right_margin != dst->right_margin
		|| src->upper_margin != dst->upper_margin
		|| src->lower_margin != dst->lower_margin
		|| src->hsync_len != dst->hsync_len
		|| src->vsync_len != dst->vsync_len
		|| !!(src->hsync_invert) != !!(dst->hsync_invert)
		|| !!(src->vsync_invert) != !!(dst->vsync_invert)
		|| !!(src->invert_pixclock) != !!(dst->invert_pixclock)
		|| src->pixclock_freq / 1024 != src->pixclock_freq / 1024
		|| src->pix_fmt_out != src->pix_fmt_out;
}

/*
 * dynamically set mode is not supported.
 * if change mode when path on, path on/off is required.
 * or we would direct set path->mode
*/
static void path_set_mode(struct mmp_path *path, struct mmp_mode *mode)
{
	/* mode unchanged? do nothing */
	if (!is_mode_changed(&path->mode, mode))
		return;

	/* FIXME: assert mode supported */
	memcpy(&path->mode, mode, sizeof(struct mmp_mode));
	if (path->status) {
		path_onoff(path, 0);
		path_set_timing(path);
		path_onoff(path, 1);
	} else {
		path_set_timing(path);
	}
}

static struct mmp_overlay_ops mmphw_overlay_ops = {
	.set_status = overlay_set_status,
	.set_win = overlay_set_win,
	.set_addr = overlay_set_addr,
};

static void ctrl_set_default(struct mmphw_ctrl *ctrl)
{
	u32 tmp, irq_mask;

	/*
	 * LCD Global control(LCD_TOP_CTRL) should be configed before
	 * any other LCD registers read/write, or there maybe issues.
	 */
	tmp = readl_relaxed(ctrl->reg_base + LCD_TOP_CTRL);
	tmp |= 0xfff0;
	writel_relaxed(tmp, ctrl->reg_base + LCD_TOP_CTRL);


	/* disable all interrupts */
	irq_mask = path_imasks(0) | err_imask(0) |
		   path_imasks(1) | err_imask(1);
	tmp = readl_relaxed(ctrl->reg_base + SPU_IRQ_ENA);
	tmp &= ~irq_mask;
	writel_relaxed(tmp, ctrl->reg_base + SPU_IRQ_ENA);
}

static void path_set_default(struct mmp_path *path)
{
	struct lcd_regs *regs = path_regs(path);
	u32 dma_ctrl1, mask, tmp, path_config;

	path_config = path_to_path_plat(path)->path_config;

	/* Configure IOPAD: should be parallel only */
	if (PATH_OUT_PARALLEL == path->output_type) {
		mask = CFG_IOPADMODE_MASK | CFG_BURST_MASK | CFG_BOUNDARY_MASK;
		tmp = readl_relaxed(ctrl_regs(path) + SPU_IOPAD_CONTROL);
		tmp &= ~mask;
		tmp |= path_config;
		writel_relaxed(tmp, ctrl_regs(path) + SPU_IOPAD_CONTROL);
	}

	/*
	 * Configure default bits: vsync triggers DMA,
	 * power save enable, configure alpha registers to
	 * display 100% graphics, and set pixel command.
	 */
	dma_ctrl1 = 0x2032ff81;

	dma_ctrl1 |= CFG_VSYNC_INV_MASK;
	writel_relaxed(dma_ctrl1, ctrl_regs(path) + dma_ctrl(1, path->id));

	/* Configure default register values */
	writel_relaxed(0x00000000, &regs->blank_color);
	writel_relaxed(0x00000000, &regs->g_1);
	writel_relaxed(0x00000000, &regs->g_start);

	/*
	 * 1.enable multiple burst request in DMA AXI
	 * bus arbiter for faster read if not tv path;
	 * 2.enable horizontal smooth filter;
	 */
	mask = CFG_GRA_HSMOOTH_MASK | CFG_DMA_HSMOOTH_MASK | CFG_ARBFAST_ENA(1);
	tmp = readl_relaxed(ctrl_regs(path) + dma_ctrl(0, path->id));
	tmp |= mask;
	if (PATH_TV == path->id)
		tmp &= ~CFG_ARBFAST_ENA(1);
	writel_relaxed(tmp, ctrl_regs(path) + dma_ctrl(0, path->id));
}

static int path_init(struct mmphw_path_plat *path_plat,
		struct mmp_mach_path_config *config)
{
	struct mmphw_ctrl *ctrl = path_plat->ctrl;
	struct mmp_path_info *path_info;
	struct mmp_path *path = NULL;

	dev_info(ctrl->dev, "%s: %s\n", __func__, config->name);

	/* init driver data */
	path_info = kzalloc(sizeof(struct mmp_path_info), GFP_KERNEL);
	if (!path_info) {
		dev_err(ctrl->dev, "%s: unable to alloc path_info for %s\n",
				__func__, config->name);
		return 0;
	}
	path_info->name = config->name;
	path_info->id = path_plat->id;
	path_info->dev = ctrl->dev;
	path_info->overlay_num = config->overlay_num;
	path_info->overlay_table = config->overlay_table;
	path_info->overlay_ops = &mmphw_overlay_ops;
	path_info->plat_data = path_plat;

	/* create/register platform device */
	path = mmp_register_path(path_info);
	if (!path) {
		kfree(path_info);
		return 0;
	}
	path_plat->path = path;
	path_plat->path_config = config->path_config;
	path_plat->link_config = config->link_config;
	/* get clock: path clock name same as path name */
	path_plat->clk = devm_clk_get(ctrl->dev, config->name);
	if (IS_ERR(path_plat->clk)) {
		/* it's possible to not have path_plat->clk */
		dev_info(ctrl->dev, "unable to get clk %s\n", config->name);
		path_plat->clk = NULL;
	}
	/* add operations after path set */
	path->ops.set_mode = path_set_mode;

	path_set_default(path);

	kfree(path_info);
	return 1;
}

static void path_deinit(struct mmphw_path_plat *path_plat)
{
	if (!path_plat)
		return;

	if (path_plat->clk)
		devm_clk_put(path_plat->ctrl->dev, path_plat->clk);

	if (path_plat->path)
		mmp_unregister_path(path_plat->path);
}

#ifdef CONFIG_OF
static const struct of_device_id mmp_disp_dt_match[] = {
	{ .compatible = "marvell,mmp-disp" },
	{},
};
#endif

static int mmphw_probe(struct platform_device *pdev)
{
	struct mmp_mach_plat_info *mi;
	struct resource *res;
	int ret, i, size, irq, path_num;
	struct mmphw_path_plat *path_plat;
	struct mmphw_ctrl *ctrl = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *child_np;
	struct mmp_mach_path_config *paths_config;
	struct mmp_mach_path_config dt_paths_config[MAX_PATH];
	u32 overlay_num[MAX_PATH][MAX_OVERLAY];

	/* register lcd internal clock firstly */
	mmp_display_clk_init();

	/* get resources from platform data */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "%s: no IO memory defined\n", __func__);
		ret = -ENOENT;
		goto failed;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "%s: no IRQ defined\n", __func__);
		ret = -ENOENT;
		goto failed;
	}

	if (IS_ENABLED(CONFIG_OF)) {
		if (of_property_read_u32(np, "marvell,path-num", &path_num)) {
			ret = -EINVAL;
			goto failed;
		}
		/* allocate ctrl */
		size = sizeof(struct mmphw_ctrl) +
			sizeof(struct mmphw_path_plat) * path_num;
		ctrl = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
		if (!ctrl) {
			ret = -ENOMEM;
			goto failed;
		}

		ctrl->path_num = path_num;
		if (of_property_read_string(np, "marvell,disp-name",
					&ctrl->name)) {
			ret = -EINVAL;
			goto failed;
		}

		if (of_get_child_count(np) != ctrl->path_num) {
			dev_err(&pdev->dev, "%s: path_num not match!\n",
					__func__);
			ret = -EINVAL;
			goto failed;
		}

		i = 0;
		for_each_child_of_node(np, child_np) {
			if (of_property_read_string(child_np,
					"marvell,path-name",
					&dt_paths_config[i].name)) {
				ret = -EINVAL;
				goto failed;
			}
			if (of_property_read_u32(child_np,
					"marvell,overlay-num",
					&dt_paths_config[i].overlay_num)) {
				ret = -EINVAL;
				goto failed;
			}
			if (of_property_read_u32_array(child_np,
					"marvell,overlay-table",
					overlay_num[i],
					dt_paths_config[i].overlay_num)) {
				ret = -EINVAL;
				goto failed;
			}
			dt_paths_config[i].overlay_table = overlay_num[i];
			if (of_property_read_u32(child_np,
					"marvell,output-type",
					&dt_paths_config[i].output_type)) {
				ret = -EINVAL;
				goto failed;
			}
			if (of_property_read_u32(child_np,
					"marvell,path-config",
					&dt_paths_config[i].path_config)) {
				ret = -EINVAL;
				goto failed;
			}
			if (of_property_read_u32(child_np,
					"marvell,link-config",
					&dt_paths_config[i].link_config)) {
				ret = -EINVAL;
				goto failed;
			}
			i++;
		}
		paths_config = dt_paths_config;
	} else {
		/* get configs from platform data */
		mi = pdev->dev.platform_data;
		if (mi == NULL || !mi->path_num || !mi->paths) {
			dev_err(&pdev->dev, "%s: no platform data defined\n",
					__func__);
			ret = -EINVAL;
			goto failed;
		}

		/* allocate ctrl */
		size = sizeof(struct mmphw_ctrl) +
			sizeof(struct mmphw_path_plat) * mi->path_num;
		ctrl = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
		if (!ctrl) {
			ret = -ENOMEM;
			goto failed;
		}

		ctrl->path_num = mi->path_num;
		ctrl->name = mi->name;
		paths_config = mi->paths;
	}

	ctrl->dev = &pdev->dev;
	ctrl->irq = irq;
	platform_set_drvdata(pdev, ctrl);
	mutex_init(&ctrl->access_ok);

	/* map registers.*/
	if (!devm_request_mem_region(ctrl->dev, res->start,
			resource_size(res), ctrl->name)) {
		dev_err(ctrl->dev,
			"can't request region for resource %pR\n", res);
		ret = -EINVAL;
		goto failed;
	}

	ctrl->reg_base = devm_ioremap_nocache(ctrl->dev,
			res->start, resource_size(res));
	if (ctrl->reg_base == NULL) {
		dev_err(ctrl->dev, "%s: res %lx - %lx map failed\n", __func__,
			(unsigned long)res->start, (unsigned long)res->end);
		ret = -ENOMEM;
		goto failed;
	}

	/* request irq */
	ret = devm_request_irq(ctrl->dev, ctrl->irq, ctrl_handle_irq,
		IRQF_SHARED, "lcd_controller", ctrl);
	if (ret < 0) {
		dev_err(ctrl->dev, "%s unable to request IRQ %d\n",
				__func__, ctrl->irq);
		ret = -ENXIO;
		goto failed;
	}

	/* get clock */
	ctrl->clk = devm_clk_get(ctrl->dev, "LCDCIHCLK");
	if (IS_ERR(ctrl->clk)) {
		dev_err(ctrl->dev, "unable to get clk LCDCIHCLK\n");
		ret = -ENOENT;
		goto failed;
	}
	clk_prepare_enable(ctrl->clk);

	/* init global regs */
	ctrl_set_default(ctrl);

	/* init pathes from machine info and register them */
	for (i = 0; i < ctrl->path_num; i++) {
		/* get from config and machine info */
		path_plat = &ctrl->path_plats[i];
		path_plat->id = i;
		path_plat->ctrl = ctrl;

		/* path init */
		if (!path_init(path_plat, (paths_config + i))) {
			ret = -EINVAL;
			goto failed_path_init;
		}
	}

#ifdef CONFIG_MMP_DISP_SPI
	ret = lcd_spi_register(ctrl);
	if (ret < 0)
		goto failed_path_init;
#endif

	dev_info(ctrl->dev, "device init done\n");

	return 0;

failed_path_init:
	for (i = 0; i < ctrl->path_num; i++) {
		path_plat = &ctrl->path_plats[i];
		path_deinit(path_plat);
	}

	clk_disable_unprepare(ctrl->clk);
failed:
	dev_err(&pdev->dev, "device init failed\n");

	return ret;
}

static struct platform_driver mmphw_driver = {
	.driver		= {
		.name	= "mmp-disp",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mmp_disp_dt_match),
	},
	.probe		= mmphw_probe,
};

static int mmphw_init(void)
{
	return platform_driver_register(&mmphw_driver);
}
module_init(mmphw_init);

MODULE_AUTHOR("Li Guoqing<ligq@marvell.com>");
MODULE_DESCRIPTION("Framebuffer driver for mmp");
MODULE_LICENSE("GPL");
