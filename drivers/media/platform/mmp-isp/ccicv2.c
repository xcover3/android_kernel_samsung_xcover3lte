/*
 * ccicv2.c
 *
 * Marvell B52 ISP
 *
 * Copyright:  (C) Copyright 2013 Marvell International Ltd.
 *              Jiaquan Su <jqsu@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */

#include <linux/device.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <media/v4l2-ctrls.h>

#include <media/b52-sensor.h>
#include <media/mrvl-camera.h>

#include "power_domain_isp.h"

#include "plat_cam.h"
#include "ccicv2.h"

#define CCIC_DRV_NAME		"ccicv2-drv"
#define CCIC_CSI_NAME		"ccic-csi"
#define CCIC_DMA_NAME		"ccic-dma"
#define CCIC_IRQ_NAME		"ccic-dma-irq"

static int trace = 2;
module_param(trace, int, 0644);
MODULE_PARM_DESC(trace,
		"how many trace do you want to see? (0-4)"
		"0 - mute"
		"1 - only actual errors"
		"2 - milestone log"
		"3 - briefing log"
		"4 - detailed log");


/*************************** Hardware Abstract Code ***************************/

static irqreturn_t csi_irq_handler(struct ccic_ctrl_dev *ccic_ctrl, u32 irq)
{
	return IRQ_HANDLED;
}
static irqreturn_t dma_irq_handler(struct ccic_dma_dev *ccic_dmal, u32 irq)
{
	return IRQ_HANDLED;
}

/********************************* CCICv2 CSI *********************************/
static int ccic_csi_hw_open(struct isp_block *block)
{
	struct ccic_csi *csi = container_of(block, struct ccic_csi, block);

	csi->ccic_ctrl->ops->clk_enable(csi->ccic_ctrl);
#if 0
/*
 * FIXME: ISP and SC2 use separate power, need share it
 */
	csi->ccic_ctrl->ops->power_up(csi->ccic_ctrl);
#endif
	return 0;
}

static void ccic_csi_hw_close(struct isp_block *block)
{
	struct ccic_csi *csi = container_of(block, struct ccic_csi, block);
	csi->ccic_ctrl->ops->clk_disable(csi->ccic_ctrl);
#if 0
/*
 * FIXME: ISP and SC2 use separate power, need share it
 */
	csi->ccic_ctrl->ops->power_down(csi->ccic_ctrl);
#endif
}

static int ccic_csi_hw_s_power(struct isp_block *block, int level)
{
	return b52isp_pwr_ctrl(level);
}

struct isp_block_ops ccic_csi_hw_ops = {
	.open	= ccic_csi_hw_open,
	.close	= ccic_csi_hw_close,
	.set_power = ccic_csi_hw_s_power,
};

static int ccic_csi_s_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_CONTRAST:
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops ccic_csi_ctrl_ops = {
	.s_ctrl = ccic_csi_s_ctrl,
};

static int ccic_csi_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct ccic_csi *ccic_csi = isd->drv_priv;
	int ret = 0;

	if (enable) {
		ret = b52_sensor_call(ccic_csi->sensor, g_csi,
				&ccic_csi->ccic_ctrl->csi);
		if (ret < 0)
			return ret;

		ccic_csi->ccic_ctrl->ops->config_idi(ccic_csi->ccic_ctrl,
				SC2_IDI_SEL_REPACK);
		ccic_csi->ccic_ctrl->ops->config_mbus(ccic_csi->ccic_ctrl,
				V4L2_MBUS_CSI2,	0, 1);
	} else
		ccic_csi->ccic_ctrl->ops->config_mbus(ccic_csi->ccic_ctrl,
				V4L2_MBUS_CSI2,	0, 0);

	return 0;
}

static const struct v4l2_subdev_video_ops ccic_csi_video_ops = {
	.s_stream	= ccic_csi_set_stream,
};

static int ccic_csi_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	int ret = 0;
	return ret;
}

static int ccic_csi_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	int ret = 0;
	return ret;
}

static int ccic_csi_get_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	int ret = 0;
	struct isp_subdev *ispsd = v4l2_get_subdev_hostdata(sd);
	struct ccic_csi *csi = ispsd->drv_priv;
	struct v4l2_subdev *sensorsd = &csi->sensor->sd;
	ret = v4l2_subdev_call(sensorsd, pad, get_fmt, NULL, fmt);
	if (ret < 0) {
		pr_err("camera: set_fmt failed %d\n", __LINE__);
		return ret;
	}
	return ret;
}

static int ccic_csi_set_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	int ret = 0;
	return ret;
}

static const struct v4l2_subdev_pad_ops ccic_csi_pad_ops = {
	.enum_mbus_code		= ccic_csi_enum_mbus_code,
	.enum_frame_size	= ccic_csi_enum_frame_size,
	.get_fmt		= ccic_csi_get_format,
	.set_fmt		= ccic_csi_set_format,
};

static const struct v4l2_subdev_ops ccic_csi_subdev_ops = {
	.video	= &ccic_csi_video_ops,
	.pad	= &ccic_csi_pad_ops,
};

static int ccic_csi_node_open(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
};

static int ccic_csi_node_close(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
};

static const struct v4l2_subdev_internal_ops ccic_csi_node_ops = {
	.open	= ccic_csi_node_open,
	.close	= ccic_csi_node_close,
};

static int ccic_csi_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	struct isp_subdev *ispsd = me_to_ispsd(entity);
	struct ccic_csi *csi = ispsd->drv_priv;
	struct v4l2_subdev *sd;
	switch (local->index) {
	case CCIC_CSI_PAD_IN:
		if (flags & MEDIA_LNK_FL_ENABLED) {
			sd = container_of(remote->entity,
					struct v4l2_subdev, entity);
			csi->sensor = to_b52_sensor(sd);
		} else
			csi->sensor = NULL;
		break;
	case CCIC_CSI_PAD_XFEED:
		d_inf(4, "TODO: add code to connect to other ccic dma output");

		break;
	case CCIC_CSI_PAD_LOCAL:
		d_inf(4, "TODO: add code to connect to local ccic dma output");

		break;
	case CCIC_CSI_PAD_ISP:
		d_inf(4, "TODO: add code to connect to B52 isp output");
		break;
	default:
		break;
	}
	return 0;
}

static const struct media_entity_operations ccic_csi_media_ops = {
	.link_setup = ccic_csi_link_setup,
};

static int ccic_csi_sd_open(struct isp_subdev *ispsd)
{
	return 0;
}

static void ccic_csi_sd_close(struct isp_subdev *ispsd)
{
}

struct isp_subdev_ops ccic_csi_sd_ops = {
	.open		= ccic_csi_sd_open,
	.close		= ccic_csi_sd_close,
};

static void ccic_csi_remove(struct ccic_csi *ccic_csi)
{
	msc2_put_ccic_ctrl(&ccic_csi->ccic_ctrl);
	v4l2_ctrl_handler_free(&ccic_csi->ispsd.ctrl_handler);
	devm_kfree(ccic_csi->dev, ccic_csi);
	ccic_csi = NULL;
}

static int ccic_csi_create(struct ccic_csi *ccic_csi)
{
	int ret = 0;
	struct isp_block *block = &ccic_csi->block;
	struct isp_dev_ptr *desc = &ccic_csi->desc;
	struct isp_subdev *ispsd = &ccic_csi->ispsd;
	struct v4l2_subdev *sd = &ispsd->subdev;

	ret = msc2_get_ccic_ctrl(&ccic_csi->ccic_ctrl,
			ccic_csi->dev->id, csi_irq_handler);
	if (ret < 0) {
		dev_err(ccic_csi->dev, "failed to get ccic_ctrl\n");
		return ret;
	}

	/* H/W block setup */
	block->id.dev_type = PCAM_IP_CCICV2;
	block->id.dev_id = ccic_csi->dev->id;
	block->id.mod_id = CCIC_BLK_CSI;
	block->dev = ccic_csi->dev;
	snprintf(ccic_csi->name, sizeof(ccic_csi->name),
		"ccic-csi #%d", block->id.dev_id);
	block->name = ccic_csi->name;
	block->ops = &ccic_csi_hw_ops;
	INIT_LIST_HEAD(&desc->hook);
	desc->ptr = block;
	desc->type = ISP_GDEV_BLOCK;

	/* isp-subdev setup */
	sd->entity.ops = &ccic_csi_media_ops;
	v4l2_subdev_init(sd, &ccic_csi_subdev_ops);
	sd->internal_ops = &ccic_csi_node_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ret = v4l2_ctrl_handler_init(&ispsd->ctrl_handler, 16);
	if (unlikely(ret < 0))
		return ret;
	sd->ctrl_handler = &ispsd->ctrl_handler;
	ispsd->ops = &ccic_csi_sd_ops;
	ispsd->drv_priv = ccic_csi;

	ispsd->pads[CCIC_CSI_PAD_IN].flags = MEDIA_PAD_FL_SINK;
	ispsd->pads[CCIC_CSI_PAD_LOCAL].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads[CCIC_CSI_PAD_XFEED].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads[CCIC_CSI_PAD_ISP].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads_cnt = CCIC_CSI_PAD_CNT;
	ispsd->single = 1;
	INIT_LIST_HEAD(&ispsd->gdev_list);
	/* Single subdev */
	list_add_tail(&desc->hook, &ispsd->gdev_list);
	ispsd->sd_code = SDCODE_CCICV2_CSI0 + block->id.dev_id;

	ret = plat_ispsd_register(ispsd);
	if (ret < 0)
		goto exit_err;
	return 0;

exit_err:
	ccic_csi_remove(ccic_csi);
	return ret;
}



/********************************* CCICv2 DMA *********************************/
static int ccic_dma_hw_open(struct isp_block *block)
{
	return 0;
}

static void ccic_dma_hw_close(struct isp_block *block)
{
}

struct isp_block_ops ccic_dma_hw_ops = {
	.open	= ccic_dma_hw_open,
	.close	= ccic_dma_hw_close,
};

static int ccic_dma_s_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_CONTRAST:
		break;
	}

	return 0;
}

static const struct v4l2_ctrl_ops ccic_dma_ctrl_ops = {
	.s_ctrl = ccic_dma_s_ctrl,
};

static int ccic_dma_set_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static const struct v4l2_subdev_video_ops ccic_dma_video_ops = {
	.s_stream	= ccic_dma_set_stream,
};

static int ccic_dma_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	int ret = 0;
	return ret;
}

static int ccic_dma_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	int ret = 0;
	return ret;
}

static int ccic_dma_get_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	int ret = 0;
	return ret;
}

static int ccic_dma_set_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	int ret = 0;
	return ret;
}

static const struct v4l2_subdev_pad_ops ccic_dma_pad_ops = {
	.enum_mbus_code		= ccic_dma_enum_mbus_code,
	.enum_frame_size	= ccic_dma_enum_frame_size,
	.get_fmt		= ccic_dma_get_format,
	.set_fmt		= ccic_dma_set_format,
};

static const struct v4l2_subdev_ops ccic_dma_subdev_ops = {
	.video	= &ccic_dma_video_ops,
	.pad	= &ccic_dma_pad_ops,
};

static int ccic_dma_node_open(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
};

static int ccic_dma_node_close(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
};

static const struct v4l2_subdev_internal_ops ccic_dma_node_ops = {
	.open	= ccic_dma_node_open,
	.close	= ccic_dma_node_close,
};

static int ccic_dma_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	switch (local->index) {
	case CCIC_DMA_PAD_IN:
		d_inf(2, "TODO: add code to connect to ccic-dma input");
		break;
	case CCIC_DMA_PAD_OUT:
		d_inf(2, "TODO: add code to connect to dma video port");
		break;
	default:
		break;
	}
	return 0;
}

static const struct media_entity_operations ccic_dma_media_ops = {
	.link_setup = ccic_dma_link_setup,
};

static int ccic_dma_sd_open(struct isp_subdev *ispsd)
{
	return 0;
}

static void ccic_dma_sd_close(struct isp_subdev *ispsd)
{
}

struct isp_subdev_ops ccic_dma_sd_ops = {
	.open		= ccic_dma_sd_open,
	.close		= ccic_dma_sd_close,
};

static void ccic_dma_remove(struct ccic_dma *ccic_dma)
{
	msc2_put_ccic_dma(&ccic_dma->ccic_dma);
	v4l2_ctrl_handler_free(&ccic_dma->ispsd.ctrl_handler);
	devm_kfree(ccic_dma->dev, ccic_dma);
	ccic_dma = NULL;
}

static int ccic_dma_create(struct ccic_dma *ccic_dma)
{
	int ret = 0;
	struct isp_block *block = &ccic_dma->block;
	struct isp_dev_ptr *desc = &ccic_dma->desc;
	struct isp_subdev *ispsd = &ccic_dma->ispsd;
	struct v4l2_subdev *sd = &ispsd->subdev;

	ret = msc2_get_ccic_dma(&ccic_dma->ccic_dma,
			ccic_dma->dev->id, &dma_irq_handler);
	if (ret < 0) {
		dev_err(ccic_dma->dev, "failed to get ccic_dma\n");
		return ret;
	}

	/* H/W block setup */
	block->id.dev_type = PCAM_IP_CCICV2;
	block->id.dev_id = ccic_dma->dev->id;
	block->id.mod_id = CCIC_BLK_DMA;
	block->dev = ccic_dma->dev;
	snprintf(ccic_dma->name, sizeof(ccic_dma->name),
		"ccic-dma #%d", ccic_dma->dev->id);
	block->name = ccic_dma->name;
	block->ops = &ccic_dma_hw_ops;
	INIT_LIST_HEAD(&desc->hook);
	desc->ptr = block;
	desc->type = ISP_GDEV_BLOCK;

	/* isp-subdev setup */
	sd->entity.ops = &ccic_dma_media_ops;
	v4l2_subdev_init(sd, &ccic_dma_subdev_ops);
	sd->internal_ops = &ccic_dma_node_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ret = v4l2_ctrl_handler_init(&ispsd->ctrl_handler, 16);
	if (unlikely(ret < 0))
		return ret;
	sd->ctrl_handler = &ispsd->ctrl_handler;
	ispsd->ops = &ccic_dma_sd_ops;
	ispsd->drv_priv = ccic_dma;

	ispsd->pads[CCIC_DMA_PAD_IN].flags = MEDIA_PAD_FL_SINK; /* CSI input */
	ispsd->pads[CCIC_DMA_PAD_OUT].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads_cnt = CCIC_DMA_PAD_CNT;
	ispsd->single = 1;
	INIT_LIST_HEAD(&ispsd->gdev_list);
	/* Single subdev */
	list_add_tail(&desc->hook, &ispsd->gdev_list);
	ispsd->sd_code = SDCODE_CCICV2_DMA0 + block->id.dev_id;
	ispsd->sd_type = ISD_TYPE_DMA_OUT;

	ret = plat_ispsd_register(ispsd);
	if (ret < 0)
		goto exit_err;
	return 0;

exit_err:
	ccic_dma_remove(ccic_dma);
	return ret;
}

/***************************** The CCICV2 IP Core *****************************/



static const struct of_device_id ccicv2_dt_match[] = {
	{
		.compatible = "marvell,ccicv2",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ccicv2_dt_match);

static int ccicv2_probe(struct platform_device *pdev)
{
	struct ccic_csi *ccic_csi;
	struct ccic_dma *ccic_dma;
	struct device_node *np = pdev->dev.of_node;
	u32 module_type, i;
	int ret;

	ret = of_property_read_u32(np, "cciv2_type", &module_type);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get type %d\n", module_type);
		return -ENODEV;
	}

	if (module_type == CCIC_BLK_CSI) {
		ccic_csi = devm_kzalloc(&pdev->dev,
				sizeof(struct ccic_csi), GFP_KERNEL);
		if (unlikely(ccic_csi == NULL)) {
			dev_err(&pdev->dev, "could not allocate memory\n");
			return -ENOMEM;
		}
		of_property_read_u32(np, "csi_id", &i);
		pdev->id = pdev->dev.id = i;
		platform_set_drvdata(pdev, ccic_csi);
		ccic_csi->dev = &(pdev->dev);

		ret = ccic_csi_create(ccic_csi);
		if (unlikely(ret < 0)) {
			dev_err(&pdev->dev, "failed to build CCICv2 sci-subdev\n");
			return ret;
		}
	} else if (module_type == CCIC_BLK_DMA) {
		ccic_dma = devm_kzalloc(&pdev->dev,
				sizeof(struct ccic_dma), GFP_KERNEL);
		if (unlikely(ccic_dma == NULL)) {
			dev_err(&pdev->dev, "could not allocate memory\n");
			return -ENOMEM;
		}
		of_property_read_u32(np, "dma_id", &i);
		pdev->id = pdev->dev.id = i;
		platform_set_drvdata(pdev, ccic_dma);
		ccic_dma->dev = &pdev->dev;

		ret = ccic_dma_create(ccic_dma);
		if (unlikely(ret < 0)) {
			dev_err(&pdev->dev, "failed to build CCICv2 dma-subdev\n");
			return ret;
		}
	} else
		return -ENODEV;

	return 0;
}

static int ccicv2_remove(struct platform_device *pdev)
{
	struct ccic_dma *ccic_dma = platform_get_drvdata(pdev);
	struct ccic_csi *ccic_csi = platform_get_drvdata(pdev);
	if (ccic_dma->block.id.mod_id == CCIC_BLK_DMA)
		ccic_dma_remove(ccic_dma);
	else if (ccic_csi->block.id.mod_id == CCIC_BLK_CSI)
		ccic_csi_remove(ccic_csi);
	return 0;
}

struct platform_driver ccicv2_driver = {
	.driver = {
		.name	= CCIC_DRV_NAME,
		.of_match_table = of_match_ptr(ccicv2_dt_match),
	},
	.probe	= ccicv2_probe,
	.remove	= ccicv2_remove,
};

module_platform_driver(ccicv2_driver);
