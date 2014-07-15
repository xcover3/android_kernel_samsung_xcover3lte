/*
 * b52isp.c
 *
 * Marvell B52 ISP driver, based on soc-isp framework
 *
 * Copyright:  (C) Copyright 2013 Marvell Technology Shanghai Ltd.
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
#include <linux/firmware.h>
#include <linux/pm_qos.h>
#include <media/v4l2-ctrls.h>
#include <media/mrvl-camera.h>
#include <media/b52-sensor.h>
#include <uapi/media/b52_api.h>

#include "power_domain_isp.h"

#include "plat_cam.h"
#include "b52isp.h"
#include "b52-reg.h"

#define B52ISP_DRV_NAME		"ovt-isp-drv"
#define B52ISP_NAME		"ovt-isp"
#define B52ISP_IRQ_NAME		"ovt-isp-irq"
#define MS_PER_JIFFIES  10

#define PATH_PER_PIPE 3
#if 0
static struct pm_qos_request b52isp_qos_idle;
static int b52isp_req_qos;
#endif
static void b52isp_tasklet(unsigned long data);

static int trace = 2;
module_param(trace, int, 0644);
MODULE_PARM_DESC(trace,
		"how many trace do you want to see? (0-4)"
		"0 - mute"
		"1 - only actual errors"
		"2 - milestone log"
		"3 - briefing log"
		"4 - detailed log");

#define mac_wport_mask(n)	\
	(((1 << B52AXI_PORT_W1) | (1 << B52AXI_PORT_W2))	\
		<< (B52AXI_PORT_CNT * n))
#define mac_rport_mask(n)	\
	((1 << B52AXI_PORT_R1) << (B52AXI_PORT_CNT * n))
static uint output_mask[9] = {
	mac_wport_mask(0) | mac_wport_mask(1) | mac_wport_mask(2),
	mac_wport_mask(0) | mac_wport_mask(1) | mac_wport_mask(2),
	mac_rport_mask(0) | mac_rport_mask(1) | mac_rport_mask(2),
	mac_wport_mask(0) | mac_wport_mask(1) | mac_wport_mask(2),
	mac_wport_mask(0) | mac_wport_mask(1) | mac_wport_mask(2),
	mac_rport_mask(0) | mac_rport_mask(1) | mac_rport_mask(2),
	mac_wport_mask(0) | mac_wport_mask(1) | mac_wport_mask(2),
	mac_wport_mask(0) | mac_wport_mask(1) | mac_wport_mask(2),
	mac_rport_mask(0) | mac_rport_mask(1) | mac_rport_mask(2),
};
module_param_array(output_mask, uint, NULL, 0644);
MODULE_PARM_DESC(output_mask,
		"Each of the array element is a mask used to specify which "
		"physical ports are allowed to connect to logical output. For "
		"example, output_mask[0] = 0xDB means WRITE#0/WRITE#1 on each "
		"MAC are allowed to connect to output#0.");

struct b52isp_hw_desc b52isp_hw_table[] = {
	[B52ISP_SINGLE] = {
		.nr_pipe	= 1,
		.nr_axi		= 2,
	},
	[B52ISP_V3_2_4] = {
		.nr_pipe	= 2,
		.nr_axi		= 3,
	},
};

static char b52isp_block_name[][32] = {
	[B52ISP_BLK_IDI]	= "b52blk-IDI",
	[B52ISP_BLK_PIPE1]	= "b52blk-pipeline#1",
	[B52ISP_BLK_DUMP1]	= "b52blk-datadump#1",
	[B52ISP_BLK_PIPE2]	= "b52blk-pipeline#2",
	[B52ISP_BLK_DUMP2]	= "b52blk-datadump#2",
	[B52ISP_BLK_AXI1]	= "b52blk-AXI-Master#1",
	[B52ISP_BLK_AXI2]	= "b52blk-AXI-Master#2",
	[B52ISP_BLK_AXI3]	= "b52blk-AXI-Master#3",
};

static char b52isp_ispsd_name[][32] = {
	[B52ISP_ISD_IDI]	= B52_IDI_NAME,
	[B52ISP_ISD_PIPE1]	= B52_PATH_YUV_1_NAME,
	[B52ISP_ISD_DUMP1]	= B52_PATH_RAW_1_NAME,
	[B52ISP_ISD_MS1]	= B52_PATH_M2M_1_NAME,
	[B52ISP_ISD_PIPE2]	= B52_PATH_YUV_2_NAME,
	[B52ISP_ISD_DUMP2]	= B52_PATH_RAW_2_NAME,
	[B52ISP_ISD_MS2]	= B52_PATH_M2M_2_NAME,
	[B52ISP_ISD_HS]		= "b52isd-HighSpeed",
	[B52ISP_ISD_HDR]	= "b52isd-HDRProcess",
	[B52ISP_ISD_3D]		= "b52isd-3DStereo",
	[B52ISP_ISD_A1W1]	= B52_OUTPUT_A_NAME,
	[B52ISP_ISD_A1W2]	= B52_OUTPUT_B_NAME,
	[B52ISP_ISD_A1R1]	= B52_INPUT_A_NAME,
	[B52ISP_ISD_A2W1]	= B52_OUTPUT_C_NAME,
	[B52ISP_ISD_A2W2]	= B52_OUTPUT_D_NAME,
	[B52ISP_ISD_A2R1]	= B52_INPUT_B_NAME,
	[B52ISP_ISD_A3W1]	= B52_OUTPUT_E_NAME,
	[B52ISP_ISD_A3W2]	= B52_OUTPUT_F_NAME,
	[B52ISP_ISD_A3R1]	= B52_INPUT_C_NAME,
};

enum {
	ISP_CLK_AXI = 0,
	ISP_CLK_CORE,
	ISP_CLK_PIPE,
	ISP_CLK_AHB,
	ISP_CLK_END
};

static const char *clock_name[ISP_CLK_END];

/* FIXME this W/R for DE limitation, since AXI clk need enable before release reset*/
static struct isp_res_req b52idi_req[] = {
	{ISP_RESRC_MEM, 0,      0},
	{ISP_RESRC_IRQ},
	{ISP_RESRC_CLK, ISP_CLK_AXI},
	{ISP_RESRC_CLK, ISP_CLK_CORE},
	{ISP_RESRC_CLK, ISP_CLK_PIPE},
	{ISP_RESRC_CLK, ISP_CLK_AHB},
	{ISP_RESRC_END}
};

static struct isp_res_req b52pipe_req[] = {
	{ISP_RESRC_MEM, 0,      0},
	{ISP_RESRC_IRQ},
/*	{ISP_RESRC_CLK, ISP_CLK_PIPE},*/
	{ISP_RESRC_CLK, ISP_CLK_AHB},
	{ISP_RESRC_END}
};

static struct isp_res_req b52axi_req[] = {
	{ISP_RESRC_MEM, 0,      0},
/*	{ISP_RESRC_CLK, ISP_CLK_AXI},*/
	{ISP_RESRC_END}
};

static void b52isp_lpm_update(int level)
{
#if 0
	static atomic_t ref_cnt = ATOMIC_INIT(0);
	if (level) {
		if (atomic_inc_return(&ref_cnt) == 1) {
			pm_qos_update_request(&b52isp_qos_idle,
				b52isp_req_qos);
		}
	} else {
		BUG_ON(atomic_read(&ref_cnt) == 0);
		if (atomic_dec_return(&ref_cnt) == 0) {
			pm_qos_update_request(&b52isp_qos_idle,
				PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
		}
	}
#endif
}

static void __maybe_unused dump_mac_reg(void __iomem *mac_base)
{
	char buffer[0xD0];
	int i;
	for (i = 0; i < 0xD0; i++)
		buffer[i] = readb(mac_base + i);

	d_inf(4, "dump MAC registers from %p", mac_base);
	for (i = 0; i < 0xD0; i += 8) {
		d_inf(4, "[0x%02X..0x%02X] = %02X %02X %02X %02X     %02X %02X %02X %02X", i, i + 7,
			buffer[i + 0],
			buffer[i + 1],
			buffer[i + 2],
			buffer[i + 3],
			buffer[i + 4],
			buffer[i + 5],
			buffer[i + 6],
			buffer[i + 7]);
	}
}

static int b52isp_attach_blk_isd(struct isp_subdev *isd, struct isp_block *blk)
{
	struct isp_dev_ptr *desc = devm_kzalloc(blk->dev, sizeof(*desc),
						GFP_KERNEL);

	if (desc == NULL)
		return -ENOMEM;
	if ((blk == NULL) || (isd == NULL))
		return -EINVAL;

	desc->ptr = blk;
	desc->type = ISP_GDEV_BLOCK;
	INIT_LIST_HEAD(&desc->hook);
	list_add_tail(&desc->hook, &isd->gdev_list);
	return 0;
}

int b52isp_detach_blk_isd(struct isp_subdev *isd, struct isp_block *blk)
{
	struct isp_dev_ptr *desc;

	list_for_each_entry(desc, &isd->gdev_list, hook) {
		if (blk == desc->ptr)
			goto find;
	}
	return -ENODEV;

find:
	list_del(&desc->hook);
	devm_kfree(blk->dev, desc);
	return 0;
}

static inline int b52isp_try_apply_cmd(struct b52isp_lpipe *pipe)
{
	struct b52isp_cmd *cmd;
	int i, ret = 0;
	unsigned long output_sel;

	if ((pipe == NULL) || (pipe->cur_cmd == NULL))
		return -EINVAL;
	cmd = pipe->cur_cmd;

	/*
	 * Convert output_map, which is logical, to output_sel, which is
	 * physical. This is because MCU FW only know physical output ports.
	 */
	output_sel = 0;
	for (i = 0; i < pipe->isd.subdev.entity.num_links; i++) {
		struct media_link *link = &pipe->isd.subdev.entity.links[i];
		struct isp_subdev *isd;
		struct b52isp_laxi *laxi;
		int bit;

		if (link->source != pipe->isd.pads + B52PAD_PIPE_OUT)
			continue;
		if ((link->flags & MEDIA_LNK_FL_ENABLED) == 0)
			continue;

		/* Find linked AXI */
		isd = v4l2_get_subdev_hostdata(
			media_entity_to_v4l2_subdev(link->sink->entity));
		if (WARN_ON(isd == NULL))
			return -EPIPE;
		laxi = isd->drv_priv;
		if ((laxi->port < B52AXI_PORT_W1) ||
			(laxi->port > B52AXI_PORT_W2))
			continue;
		bit = laxi->mac * 2 + laxi->port;
		if (test_and_set_bit(bit, &output_sel))
			BUG_ON(1);
	}

	/* handle IMG_CAPTURE and HDR_STILL case */
	if (cmd->src_type == CMD_SRC_AXI) {
		struct isp_vnode *vnode = cmd->mem.vnode;
		/* FIXME: maybe need check laxi->stream here? */
		if ((vnode->format.type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ||
			(vnode->format.fmt.pix_mp.num_planes != 1)) {
			ret = -EINVAL;
			goto err_exit;
		}
		cmd->src_fmt.width = vnode->format.fmt.pix_mp.width;
		cmd->src_fmt.height = vnode->format.fmt.pix_mp.height;
		cmd->src_fmt.pixelformat = vnode->format.fmt.pix_mp.pixelformat;
		cmd->src_fmt.field = vnode->format.fmt.pix_mp.field;
		cmd->src_fmt.colorspace = vnode->format.fmt.pix_mp.colorspace;
		cmd->src_fmt.bytesperline
			= vnode->format.fmt.pix_mp.plane_fmt[0].bytesperline;
		cmd->src_fmt.sizeimage
			= vnode->format.fmt.pix_mp.plane_fmt[0].sizeimage;
	} else {
		struct v4l2_subdev *sensor = cmd->sensor;
		struct v4l2_subdev_format fmt = {
			.pad = 0,
			.which = V4L2_SUBDEV_FORMAT_ACTIVE,
		};
		ret = v4l2_subdev_call(sensor, pad, get_fmt, NULL, &fmt);
		if (ret < 0)
			goto err_exit;
		d_inf(4, "got sensor %s <w%d, h%d, c%X>",
			sensor->name, fmt.format.width,
			fmt.format.height, fmt.format.code);
		cmd->src_fmt.width = fmt.format.width;
		cmd->src_fmt.height = fmt.format.height;
		/* FIXME: hard code format code here */
		cmd->src_fmt.pixelformat = V4L2_PIX_FMT_SBGGR10;
		cmd->src_fmt.bytesperline = cmd->src_fmt.width * 10 / 8;
		cmd->src_fmt.sizeimage =
			cmd->src_fmt.bytesperline * cmd->src_fmt.height;
		cmd->src_fmt.field = 0;
		cmd->src_fmt.colorspace = fmt.format.colorspace;
	}

	if (cmd->cmd_name == CMD_SET_FORMAT ||
		cmd->cmd_name == CMD_CHG_FORMAT) {
		/* FIXME: if all enable port close, will also clean port sel to ISP*/
		if (cmd->enable_map == 0x0)
			output_sel = 0x0;
		if (output_sel == pipe->output_sel) {
			if (cmd->enable_map == pipe->enable_map)
				goto cmd_done;
			else {
				cmd->cmd_name = CMD_CHG_FORMAT;
				d_inf(4, "use CHANGE_FORMAT instead");
			}
		} else {
			cmd->cmd_name = CMD_SET_FORMAT;
		}
	}
	/* MCU command only recognize output select */
	cmd->output_map = output_sel;

	ret = b52_hdl_cmd(cmd);
	if (ret < 0) {
		d_inf(1, "MCU command apply failed: %d", ret);
		goto err_exit;
	}

cmd_done:
	pipe->output_sel = output_sel;
	pipe->enable_map = cmd->enable_map;

	/* FIXME set flags to 0, keep the sensor init once */
	cmd->flags &= ~BIT(CMD_FLAG_INIT);
	cmd->flags |= BIT(CMD_FLAG_LOCK_AEAG);

	d_inf(4, "MCU command apply success: %s", pipe->isd.subdev.name);
	return 0;

err_exit:
	return ret;
}

/********************************* IDI block *********************************/
static int b52isp_idi_hw_open(struct isp_block *block)
{
	b52isp_lpm_update(1);
	return 0;
}

static void b52isp_idi_hw_close(struct isp_block *block)
{
	b52isp_lpm_update(0);
}
static int b52isp_idi_set_power(struct isp_block *block, int level)
{
	int ret = 0;
	ret = b52isp_pwr_ctrl(level);
	b52_set_base_addr(block->reg_base);
	return ret;
}

static int b52isp_idi_set_clock(struct isp_block *block, int rate)
{
	struct clk *axi_clk = block->clock[0];
	struct clk *core_clk = block->clock[1];
	struct clk *pipe_clk = block->clock[2];

	if (rate) {
		clk_set_rate(axi_clk, 312000000);
		clk_set_rate(core_clk, 156000000);
		clk_set_rate(pipe_clk, 312000000);
	}

	return 0;
}

struct isp_block_ops b52isp_idi_hw_ops = {
	.open	= b52isp_idi_hw_open,
	.close	= b52isp_idi_hw_close,
	.set_power	= b52isp_idi_set_power,
	.set_clock  = b52isp_idi_set_clock,
};

/********************************* IDI subdev *********************************/
/* ioctl(subdev, IOCTL_XXX, arg) is handled by this one */
static long b52isp_idi_ioctl(struct v4l2_subdev *sd,
				unsigned int cmd, void *arg)
{
	return 0;
}

static const struct v4l2_subdev_core_ops b52isp_idi_core_ops = {
	.ioctl	= b52isp_idi_ioctl,
};

static int b52isp_idi_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	int ret = 0;
	return ret;
}

static int b52isp_idi_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	int ret = 0;
	return ret;
}

static int b52isp_idi_get_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	int ret = 0;
	return ret;
}

static int b52isp_idi_set_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	int ret = 0;
	return ret;
}

static int b52isp_idi_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_rect *rect;

	if (sel->pad >= isd->pads_cnt)
		return -EINVAL;
	rect = isd->crop_pad + sel->pad;
	sel->r = *rect;
	return 0;
}

static int b52isp_idi_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_rect *rect;
	int ret = 0;

	if (sel->pad >= isd->pads_cnt)
		return -EINVAL;
	rect = isd->crop_pad + sel->pad;

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		goto exit;
	/* Really apply here if not using MCU */
	d_inf(4, "%s:pad[%d] crop(%d, %d)<>(%d, %d)", sd->name, sel->pad,
		sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	*rect = sel->r;
exit:
	return ret;
}

static const struct v4l2_subdev_pad_ops b52isp_idi_pad_ops = {
	.enum_mbus_code		= b52isp_idi_enum_mbus_code,
	.enum_frame_size	= b52isp_idi_enum_frame_size,
	.get_fmt		= b52isp_idi_get_format,
	.set_fmt		= b52isp_idi_set_format,
	.get_selection		= b52isp_idi_get_selection,
	.set_selection		= b52isp_idi_set_selection,
};

static const struct v4l2_subdev_ops b52isp_idi_subdev_ops = {
	.core	= &b52isp_idi_core_ops,
	.pad	= &b52isp_idi_pad_ops,
};

static int b52isp_idi_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	/* TODO: MUST assign physical device to logical device!!! */
	return 0;
}

static const struct media_entity_operations b52isp_idi_media_ops = {
	.link_setup = b52isp_idi_link_setup,
};

static int b52isp_idi_sd_open(struct isp_subdev *ispsd)
{
	return 0;
}

static void b52isp_idi_sd_close(struct isp_subdev *ispsd)
{
}

struct isp_subdev_ops b52isp_idi_sd_ops = {
	.open		= b52isp_idi_sd_open,
	.close		= b52isp_idi_sd_close,
};

static int b52isp_pwr_enable;
static int b52isp_idi_node_open(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct isp_block *blk = isp_sd2blk(isd);

	b52isp_pwr_enable = 1;
	return isp_block_tune_power(blk, 1);
}

static int b52isp_idi_node_close(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct isp_block *blk = isp_sd2blk(isd);
	return isp_block_tune_power(blk, 0);
}

/* subdev internal operations */
static const struct v4l2_subdev_internal_ops b52isp_idi_node_ops = {
	.open	= b52isp_idi_node_open,
	.close	= b52isp_idi_node_close,
};

static void b52isp_idi_remove(struct b52isp *b52isp)
{
	struct isp_block *blk = b52isp->blk[B52ISP_BLK_IDI];
	struct isp_subdev *isd = b52isp->isd[B52ISP_ISD_IDI];

	b52isp_detach_blk_isd(isd, blk);
	b52isp->blk[B52ISP_BLK_IDI] = NULL;
	b52isp->isd[B52ISP_ISD_IDI] = NULL;
	plat_ispsd_unregister(isd);
	v4l2_ctrl_handler_free(&isd->ctrl_handler);
	devm_kfree(b52isp->dev, isd);
	devm_kfree(b52isp->dev, container_of(blk, struct b52isp_idi, block));
}

static int b52isp_idi_create(struct b52isp *b52isp)
{
	struct b52isp_idi *idi = devm_kzalloc(b52isp->dev, sizeof(*idi),
						GFP_KERNEL);
	struct isp_block *block;
	struct isp_subdev *ispsd;
	struct v4l2_subdev *sd;
	int ret;

	if (idi == NULL)
		return -ENOMEM;
	/* Add ISP Path Blocks */
	block = &idi->block;
	block->id.dev_type = PCAM_IP_B52ISP;
	block->id.dev_id = b52isp->dev->id;
	block->id.mod_id = B52ISP_BLK_IDI;
	block->dev = b52isp->dev;
	block->name = b52isp_block_name[B52ISP_BLK_IDI];
	block->req_list = b52idi_req;
	block->ops = &b52isp_idi_hw_ops;
	b52isp->blk[B52ISP_BLK_IDI] = block;
	idi->parent = b52isp;

	ispsd = devm_kzalloc(b52isp->dev, sizeof(*ispsd), GFP_KERNEL);
	if (ispsd == NULL)
		return -ENOMEM;
	sd = &ispsd->subdev;
	ret = v4l2_ctrl_handler_init(&ispsd->ctrl_handler, 16);
	if (unlikely(ret < 0))
		return ret;
	sd->entity.ops = &b52isp_idi_media_ops;
	v4l2_subdev_init(sd, &b52isp_idi_subdev_ops);
	sd->internal_ops = &b52isp_idi_node_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->ctrl_handler = &ispsd->ctrl_handler;
	strncpy(sd->name, b52isp_ispsd_name[B52ISP_ISD_IDI], sizeof(sd->name));
	ispsd->ops = &b52isp_idi_sd_ops;
	ispsd->drv_priv = b52isp;
	ispsd->pads[B52PAD_IDI_IN1].flags = MEDIA_PAD_FL_SINK;
	ispsd->pads[B52PAD_IDI_IN2].flags = MEDIA_PAD_FL_SINK;
	ispsd->pads[B52PAD_IDI_PIPE1].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads[B52PAD_IDI_DUMP1].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads[B52PAD_IDI_PIPE2].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads[B52PAD_IDI_DUMP2].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads[B52PAD_IDI_BOTH].flags = MEDIA_PAD_FL_SOURCE;
	ispsd->pads_cnt = B52_IDI_PAD_CNT;
	ispsd->single = 1;
	INIT_LIST_HEAD(&ispsd->gdev_list);
	ispsd->sd_code = SDCODE_B52ISP_IDI;
	ret = b52isp_attach_blk_isd(ispsd, block);
	if (ret < 0)
		return ret;
	ret = plat_ispsd_register(ispsd);
	if (ret < 0)
		return ret;
	b52isp->isd[B52ISP_ISD_IDI] = ispsd;
	return 0;
}



/******************************* ISP Path Block *******************************/
static int b52isp_path_hw_open(struct isp_block *block)
{
	int ret;

	ret = b52_load_fw(block->dev, block->reg_base, 1, b52isp_pwr_enable);
	if (ret < 0)
		return ret;
	d_inf(4, "MCU Initialization done");

	b52isp_pwr_enable = 0;
	return 0;
}

static void b52isp_path_hw_close(struct isp_block *block)
{
	b52_load_fw(block->dev, block->reg_base, 0, b52isp_pwr_enable);
}

static int b52isp_path_s_clock(struct isp_block *block, int rate)
{
	return 0;
}

static int b52isp_path_hw_s_power(struct isp_block *block, int level)
{
	return b52isp_pwr_ctrl(level);
}

struct isp_block_ops b52isp_path_hw_ops = {
	.open	= b52isp_path_hw_open,
	.close	= b52isp_path_hw_close,
	.set_power	= b52isp_path_hw_s_power,
	.set_clock  = b52isp_path_s_clock,
};

/****************************** ISP Path Subdev ******************************/
static int b52isp_path_set_profile(struct isp_subdev *isd)
{
	struct media_pad *r_pad = media_entity_remote_pad(
					isd->pads + B52PAD_PIPE_IN);
	struct b52isp_lpipe *pipe = isd->drv_priv;
	struct b52isp_cmd *cur_cmd;
	struct isp_subdev *src;
	int i, j, ret = 0;

	WARN_ON(list_empty(&isd->gdev_list));
	mutex_lock(&pipe->state_lock);

	if (pipe->cur_cmd) {
		/* If already has a snapshot, clean the old command */
		devm_kfree(isd->build->dev, pipe->cur_cmd);
		pipe->cur_cmd = NULL;
	}

	pipe->cur_cmd = devm_kzalloc(isd->build->dev,
					sizeof(*pipe->cur_cmd), GFP_KERNEL);
	if (pipe->cur_cmd == NULL) {
		ret = -ENOMEM;
		goto exit;
	}
	cur_cmd = pipe->cur_cmd;

	INIT_LIST_HEAD(&pipe->cur_cmd->hook);

	if (unlikely(WARN_ON(r_pad == NULL))) {
		ret = -EPERM;
		goto exit;
	}

	/* get pre-scaler */
	src = v4l2_get_subdev_hostdata(
		media_entity_to_v4l2_subdev(r_pad->entity));
	if (unlikely(WARN_ON(src == NULL))) {
		ret = -EPIPE;
		goto exit;
	}

	/* setup source and pre-scaler */
	switch (src->sd_code) {
	case SDCODE_B52ISP_IDI:
		cur_cmd->src_type = CMD_SRC_SNSR;
		switch (isd->sd_code) {
		case SDCODE_B52ISP_PIPE1:
		case SDCODE_B52ISP_DUMP1:
			i = B52PAD_IDI_PIPE1;
			j = B52PAD_IDI_IN1;
			break;
		case SDCODE_B52ISP_PIPE2:
		case SDCODE_B52ISP_DUMP2:
			i = B52PAD_IDI_PIPE2;
			j = B52PAD_IDI_IN2;
			break;
		default:
			ret = -EINVAL;
			goto exit;
		}
		cur_cmd->pre_crop = src->crop_pad[i];
		/* get CCIC-CTRL */
		r_pad = media_entity_remote_pad(src->pads + j);
		if (unlikely(WARN_ON(r_pad == NULL))) {
			ret = -EPIPE;
			goto exit;
		}
		/* get sensor */
		r_pad = media_entity_remote_pad(&r_pad->entity->pads[0]);
		if (unlikely(WARN_ON(r_pad == NULL))) {
			ret = -EPIPE;
			goto exit;
		}
		if (unlikely(WARN_ON(media_entity_type(r_pad->entity)
					!= MEDIA_ENT_T_V4L2_SUBDEV))) {
			ret = -EPIPE;
			goto exit;
		}
		cur_cmd->sensor =
			media_entity_to_v4l2_subdev(r_pad->entity);
		d_inf(4, "sensor is %s", cur_cmd->sensor->name);
		break;
	case SDCODE_B52ISP_A1R1:
	case SDCODE_B52ISP_A2R1:
	case SDCODE_B52ISP_A3R1:
		cur_cmd->src_type = CMD_SRC_AXI;
		cur_cmd->pre_crop = src->crop_pad[B52PAD_AXI_OUT];
		/* get vdev */
		r_pad = media_entity_remote_pad(src->pads + B52PAD_AXI_IN);
		if (unlikely(WARN_ON(r_pad == NULL))) {
			ret = -EPIPE;
			goto exit;
		}
		cur_cmd->mem.vnode = me_to_vnode(r_pad->entity);
		if (unlikely(WARN_ON(cur_cmd->mem.vnode == NULL))) {
			ret = -EPIPE;
			goto exit;
		}
		break;
	}

	/* setup pipeline */
	switch (pipe->isd.sd_code) {
	case SDCODE_B52ISP_PIPE1:
		cur_cmd->path = B52ISP_ISD_PIPE1;
		break;
	case SDCODE_B52ISP_DUMP1:
		cur_cmd->path = B52ISP_ISD_DUMP1;
		break;
	case SDCODE_B52ISP_MS1:
		pipe->cur_cmd->path = B52ISP_ISD_MS1;
		break;
	case SDCODE_B52ISP_PIPE2:
		cur_cmd->path = B52ISP_ISD_PIPE2;
		break;
	case SDCODE_B52ISP_DUMP2:
		cur_cmd->path = B52ISP_ISD_DUMP1;
		break;
	case SDCODE_B52ISP_MS2:
		pipe->cur_cmd->path = B52ISP_ISD_MS2;
		break;
	case SDCODE_B52ISP_HS:
		cur_cmd->path = B52ISP_ISD_HS;
		break;
	case SDCODE_B52ISP_HDR:
		cur_cmd->path = B52ISP_ISD_HDR;
		break;
	case SDCODE_B52ISP_3D:
		cur_cmd->path = B52ISP_ISD_3D;
		break;
	default:
		d_inf(1, "path id <%d> not recongnized", pipe->isd.sd_code);
		ret = -ENODEV;
		goto exit;
	}

	/* setup command name */
	switch (isd->sd_code) {
	case SDCODE_B52ISP_PIPE1:
	case SDCODE_B52ISP_PIPE2:
		switch (src->sd_code) {
		case SDCODE_B52ISP_IDI:
			cur_cmd->cmd_name = CMD_SET_FORMAT;
			cur_cmd->flags = BIT(CMD_FLAG_INIT);
			if (pipe->meta_dma) {
				cur_cmd->meta_dma = pipe->meta_dma;
				cur_cmd->flags |= BIT(CMD_FLAG_META_DATA);
			}
			break;
		case SDCODE_B52ISP_A1R1:
		case SDCODE_B52ISP_A2R1:
		case SDCODE_B52ISP_A3R1:
			cur_cmd->cmd_name = CMD_RAW_PROCESS;
			break;
		}
		break;
	case SDCODE_B52ISP_DUMP1:
	case SDCODE_B52ISP_DUMP2:
		if (src->sd_code != SDCODE_B52ISP_IDI) {
			ret = -EINVAL;
			goto exit;
		}
		cur_cmd->cmd_name = CMD_RAW_DUMP;
		/* TODO: set sensor ID here */
		break;
	case SDCODE_B52ISP_MS1:
	case SDCODE_B52ISP_MS2:
		switch (src->sd_code) {
		case SDCODE_B52ISP_A1R1:
		case SDCODE_B52ISP_A2R1:
		case SDCODE_B52ISP_A3R1:
			pipe->cur_cmd->cmd_name = CMD_SET_FORMAT;
			pipe->cur_cmd->flags |= BIT(CMD_FLAG_MS);
			if (pipe->meta_dma) {
				cur_cmd->meta_dma = pipe->meta_dma;
				cur_cmd->flags |= BIT(CMD_FLAG_META_DATA);
			}
			break;
		default:
			ret = -EINVAL;
			goto exit;
		}
		break;
	case SDCODE_B52ISP_HS:
		cur_cmd->cmd_name = CMD_SET_FORMAT;
		break;
	case SDCODE_B52ISP_3D:
		cur_cmd->cmd_name = CMD_SET_FORMAT;
		break;
	case SDCODE_B52ISP_HDR:
		switch (src->sd_code) {
		case SDCODE_B52ISP_IDI:
			cur_cmd->cmd_name = CMD_SET_FORMAT;
			break;
		case SDCODE_B52ISP_A1R1:
		case SDCODE_B52ISP_A2R1:
		case SDCODE_B52ISP_A3R1:
			cur_cmd->cmd_name = CMD_HDR_STILL;
			break;
		}
		break;
	}

	cur_cmd->post_crop = isd->crop_pad[B52PAD_PIPE_OUT];

exit:
	mutex_unlock(&pipe->state_lock);
	return ret;
}

static int b52isp_config_af_win(struct isp_subdev *isd,
		struct v4l2_rect *r)
{
	int id;
	struct b52isp_lpipe *pipe = isd->drv_priv;
	struct b52isp_ctrls *ctrls = &pipe->ctrls;
	struct isp_block *blk = isp_sd2blk(isd);

	if (!r || !ctrls || !blk) {
		pr_err("%s: paramter is NULL\n", __func__);
		return -EINVAL;
	}

	ctrls->af_win = *r;

	switch (blk->id.mod_id) {
	case B52ISP_BLK_PIPE1:
		id = 0;
		break;
	case B52ISP_BLK_PIPE2:
		id = 1;
		break;
	default:
		pr_err("%s: id error\n", __func__);
		return -EINVAL;
	}

	b52_set_focus_win(r, id);
	return 0;
}

static int b52isp_config_metering_mode(struct isp_subdev *isd,
		struct b52isp_expo_metering *metering)
{
	struct b52isp_lpipe *pipe = isd->drv_priv;
	struct b52isp_ctrls *ctrls = &pipe->ctrls;

	if (!metering || !ctrls) {
		pr_err("%s: paramter is NULL\n", __func__);
		return -EINVAL;
	}
	if (metering->mode >= NR_METERING_MODE) {
		pr_err("%s: mode(%d) is error\n", __func__, metering->mode);
		return -EINVAL;
	}

	ctrls->metering_mode[metering->mode] = *metering;

	return 0;
}

static int b52isp_config_metering_roi(struct isp_subdev *isd,
		struct b52isp_win *r)
{
	struct b52isp_lpipe *pipe = isd->drv_priv;
	struct b52isp_ctrls *ctrls = &pipe->ctrls;

	if (!r || !ctrls) {
		pr_err("%s: paramter is NULL\n", __func__);
		return -EINVAL;
	}

	ctrls->metering_roi = *r;

	return 0;
}

static int b52isp_path_rw_ctdata(struct isp_subdev *isd, int write,
					struct b52_data_node *node)
{
	struct isp_block *blk = isp_sd2blk(isd);
	int ret;
	void    *tmpbuf = NULL;

	if (WARN_ON(node == NULL) ||
		WARN_ON(node->size == 0))
		return -EINVAL;

	/*
	 * TODO: actually the right thing to do here is scan for each physical
	 * device, and apply C&T data accordingly. This is important for:
	 * HDR:		P1 = P2 = sensor
	 * High speed:	P1 + P2 = sensor
	 * 3D stereo:	P1 = sensorL, P2 = sensorR
	 */

	tmpbuf = devm_kzalloc(isd->build->dev, node->size, GFP_KERNEL);
	if (tmpbuf == NULL) {
		d_inf(1, "%s not enough memery, try later",
			isd->subdev.name);
		return -EAGAIN;
	}
	if (copy_from_user(tmpbuf, node->buffer, node->size)) {
		ret = -EAGAIN;
		goto err;
	}

	if (blk == NULL) {
		d_inf(1, "%s not mapped to physical device yet, try later",
			isd->subdev.name);
		ret = -EAGAIN;
		goto err;
	}

	switch (blk->id.mod_id) {
	case B52ISP_BLK_PIPE1:
		ret = b52_rw_pipe_ctdata(1, write, tmpbuf,
					node->size / sizeof(struct b52_regval));
		break;
	case B52ISP_BLK_PIPE2:
		ret = b52_rw_pipe_ctdata(2, write, tmpbuf,
					node->size / sizeof(struct b52_regval));
		break;
	default:
		d_inf(1, "download C&T data not allowed on \"%s\"",
			isd->subdev.name);
		ret = -EACCES;
		goto err;
	}
	if (copy_to_user(node->buffer, tmpbuf, node->size))
		ret = -EAGAIN;
err:
	devm_kfree(isd->build->dev, tmpbuf);
	return ret;
}
static int b52isp_config_awb_gain(struct isp_subdev *isd,
					struct b52isp_awb_gain *awb_gain)
{
	int id = 0;
	struct isp_block *blk = isp_sd2blk(isd);

	if (!blk || !awb_gain) {
		pr_err("%s: paramter is NULL\n", __func__);
		return -EINVAL;
	}

	if (blk->id.mod_id == B52ISP_BLK_PIPE1)
		id = 0;
	else if (blk->id.mod_id == B52ISP_BLK_PIPE2)
		id = 1;
	else {
		pr_err("%s: wrong mod id\n", __func__);
		return -EINVAL;
	}

	return b52isp_rw_awb_gain(awb_gain, id);
}

static int b52isp_config_memory_sensor(struct isp_subdev *isd,
					struct memory_sensor *arg)
{
	int ret = 0;
	struct b52isp_lpipe *pipe = isd->drv_priv;
	pipe->cur_cmd->memory_sensor_data = memory_sensor_match(arg->name);
	if (pipe->cur_cmd->memory_sensor_data == NULL) {
		pr_err("memory sensor can't match the real sensor\n");
		ret = -EINVAL;
	}
	return ret;
}

static int b52isp_config_adv_dns(struct isp_subdev *isd,
		struct b52isp_adv_dns *dns)
{
	int ret = 0;
	struct b52isp_lpipe *pipe = isd->drv_priv;

	if (dns->type >= ADV_DNS_MAX) {
		pr_err("Not have such type: %d\n", dns->type);
		ret = -EINVAL;
	}

	pipe->cur_cmd->adv_dns.type = dns->type;
	pipe->cur_cmd->adv_dns.Y_times = dns->times;
	pipe->cur_cmd->adv_dns.UV_times = dns->times;
	return ret;
}

static int b52isp_set_path_arg(struct isp_subdev *isd,
		struct b52isp_path_arg *arg)
{
	struct b52isp_lpipe *lpipe = isd->drv_priv;

	lpipe->path_arg = *arg;
	d_inf(3, "%s: 3A_lock:%d, nr_bracket:%d, expo_2/1:0x%X, expo_3/1:0x%X, linear_yuv:%d",
		isd->subdev.name, arg->aeag, arg->nr_frame,
		arg->ratio_1_2, arg->ratio_1_3, arg->linear_yuv);
	return 0;
}

static int b52isp_anti_shake(struct isp_subdev *isd,
		struct b52isp_anti_shake_arg *arg)
{
	return b52_cmd_anti_shake(arg->block_size, arg->enable);
}

/* ioctl(subdev, IOCTL_XXX, arg) is handled by this one */
static long b52isp_path_ioctl(struct v4l2_subdev *sd,
				unsigned int cmd, void *arg)
{
	int ret = 0;
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);

	switch (cmd) {
	case VIDIOC_PRIVATE_B52ISP_TOPOLOGY_SNAPSHOT:
		ret = b52isp_path_set_profile(isd);
		break;
	case VIDIOC_PRIVATE_B52ISP_CONFIG_AF_WINDONW:
		ret = b52isp_config_af_win(isd, (struct v4l2_rect *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_CONFIG_EXPO_METERING_MODE:
		ret = b52isp_config_metering_mode(isd,
				(struct b52isp_expo_metering *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_CONFIG_EXPO_METERING_ROI:
		ret = b52isp_config_metering_roi(isd,
				(struct b52isp_win *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_DOWNLOAD_CTDATA:
		ret = b52isp_path_rw_ctdata(isd, 1,
				(struct b52_data_node *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_UPLOAD_CTDATA:
		ret = b52isp_path_rw_ctdata(isd, 0,
				(struct b52_data_node *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_CONFIG_AWB_GAIN:
		ret = b52isp_config_awb_gain(isd,
				(struct b52isp_awb_gain *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_CONFIG_MEMORY_SENSOR:
		ret = b52isp_config_memory_sensor(isd,
				(struct memory_sensor *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_CONFIG_ADV_DNS:
		ret = b52isp_config_adv_dns(isd,
				(struct b52isp_adv_dns *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_SET_PATH_ARG:
		ret = b52isp_set_path_arg(isd, (struct b52isp_path_arg *)arg);
		break;
	case VIDIOC_PRIVATE_B52ISP_ANTI_SHAKE:
		ret = b52isp_anti_shake(isd, (struct b52isp_anti_shake_arg *)arg);
		break;
	default:
		d_inf(1, "unknown ioctl '%c', dir=%d, #%d (0x%08x)\n",
			_IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_NR(cmd), cmd);
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
/*FIXME: need to refine return val*/
static int b52_usercopy(struct v4l2_subdev *sd,
		unsigned int cmd, void *arg)
{
	char	sbuf[128];
	void    *mbuf = NULL;
	void	*parg = arg;
	long	err  = -EINVAL;

	/*  Copy arguments into temp kernel buffer  */
	if (_IOC_DIR(cmd) != _IOC_NONE) {
		if (_IOC_SIZE(cmd) <= sizeof(sbuf)) {
			parg = sbuf;
		} else {
			/* too big to allocate from stack */
			mbuf = kmalloc(_IOC_SIZE(cmd), GFP_KERNEL);
			if (NULL == mbuf)
				return -ENOMEM;
			parg = mbuf;
		}

		err = -EFAULT;
		if (_IOC_DIR(cmd) & _IOC_WRITE) {
			unsigned int n = _IOC_SIZE(cmd);

			if (copy_from_user(parg, (void __user *)arg, n))
				goto out;

			/* zero out anything we don't copy from userspace */
			if (n < _IOC_SIZE(cmd))
				memset((u8 *)parg + n, 0, _IOC_SIZE(cmd) - n);
		} else {
			/* read-only ioctl */
			memset(parg, 0, _IOC_SIZE(cmd));
		}
	}

	/* Handles IOCTL */
	err = v4l2_subdev_call(sd, core, ioctl, cmd, parg);
	if (err == -ENOIOCTLCMD)
		err = -ENOTTY;
	if (_IOC_DIR(cmd) & _IOC_READ) {
		unsigned int n = _IOC_SIZE(cmd);
		if (copy_to_user((void __user *)arg, parg, n))
			goto out;
	}
out:
	kfree(mbuf);
	return err;
}

struct b52_data_node32 {
	__u32	size;
	compat_caddr_t buffer;
};

struct b52isp_profile32 {
	unsigned int profile_id;
	compat_caddr_t arg;
};

#define VIDIOC_PRIVATE_B52ISP_TOPOLOGY_SNAPSHOT32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 0, struct b52isp_profile32)
#define VIDIOC_PRIVATE_B52ISP_DOWNLOAD_CTDATA32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 4, struct b52_data_node32)
#define VIDIOC_PRIVATE_B52ISP_UPLOAD_CTDATA32 \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct b52_data_node32)

static int get_b52isp_profile32(struct b52isp_profile *kp,
		struct b52isp_profile32 __user *up)
{
	u32 tmp;
	if (!access_ok(VERIFY_READ, up, sizeof(struct b52isp_profile32)) ||
			get_user(tmp, &up->arg) ||
			get_user(kp->profile_id, &up->profile_id))
		return -EFAULT;

	kp->arg = compat_ptr(tmp);

	return 0;
}

static int get_rw_ctdata32(struct b52_data_node *kp,
		struct b52_data_node32 __user *up)
{
	u32 tmp;
	if (!access_ok(VERIFY_READ, up, sizeof(struct b52_data_node32)) ||
			get_user(tmp, &up->buffer) ||
			get_user(kp->size, &up->size))
		return -EFAULT;

	kp->buffer = compat_ptr(tmp);

	return 0;
}

static long b52isp_compat_ioctl32(struct v4l2_subdev *sd,
				unsigned int cmd, void *arg)
{
	int ret = 0;
	union {
		struct b52isp_profile pf;
		struct b52_data_node nd;
	} karg;
	int compatible_arg = 1;

	switch (cmd) {
	case VIDIOC_PRIVATE_B52ISP_TOPOLOGY_SNAPSHOT32:
		cmd = VIDIOC_PRIVATE_B52ISP_TOPOLOGY_SNAPSHOT;
		get_b52isp_profile32(&karg.pf, arg);
		compatible_arg = 0;
		break;
	case VIDIOC_PRIVATE_B52ISP_DOWNLOAD_CTDATA32:
		cmd = VIDIOC_PRIVATE_B52ISP_DOWNLOAD_CTDATA;
		get_rw_ctdata32(&karg.nd, arg);
		compatible_arg = 0;
		break;
	case VIDIOC_PRIVATE_B52ISP_UPLOAD_CTDATA32:
		cmd = VIDIOC_PRIVATE_B52ISP_UPLOAD_CTDATA;
		get_rw_ctdata32(&karg.nd, arg);
		compatible_arg = 0;
		break;
	case VIDIOC_PRIVATE_B52ISP_CONFIG_AF_WINDONW:
	case VIDIOC_PRIVATE_B52ISP_CONFIG_EXPO_METERING_MODE:
	case VIDIOC_PRIVATE_B52ISP_CONFIG_EXPO_METERING_ROI:
	case VIDIOC_PRIVATE_B52ISP_CONFIG_AWB_GAIN:
	case VIDIOC_PRIVATE_B52ISP_CONFIG_MEMORY_SENSOR:
	case VIDIOC_PRIVATE_B52ISP_CONFIG_ADV_DNS:
	case VIDIOC_PRIVATE_B52ISP_SET_PATH_ARG:
	case VIDIOC_PRIVATE_B52ISP_ANTI_SHAKE:
		break;
	default:
		d_inf(1, "unknown compat ioctl '%c', dir=%d, #%d (0x%08x)\n",
			_IOC_TYPE(cmd), _IOC_DIR(cmd), _IOC_NR(cmd), cmd);
		return -ENXIO;
	}

	if (compatible_arg)
		ret = b52_usercopy(sd, cmd, arg);
	else {
		mm_segment_t old_fs = get_fs();

		set_fs(KERNEL_DS);
		ret = b52_usercopy(sd, cmd, (void *)&karg);
		set_fs(old_fs);
	}

	return ret;
}
#endif

static const struct v4l2_subdev_core_ops b52isp_path_core_ops = {
	.ioctl	= b52isp_path_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = b52isp_compat_ioctl32,
#endif
};

static int b52isp_path_set_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static const struct v4l2_subdev_video_ops b52isp_path_video_ops = {
	.s_stream	= b52isp_path_set_stream,
};

static int b52isp_path_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	int ret = 0;
	return ret;
}

static int b52isp_path_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	int ret = 0;
	return ret;
}

static int b52isp_path_get_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_mbus_framefmt *pad_fmt;

	if (fmt->pad >= isd->pads_cnt)
		return -EINVAL;
	pad_fmt = isd->fmt_pad + fmt->pad;
	fmt->format = *pad_fmt;
	return 0;
}

static int b52isp_path_set_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_mbus_framefmt *pad_fmt;
	int ret = 0;

	if (fmt->pad >= isd->pads_cnt)
		return -EINVAL;
	pad_fmt = isd->fmt_pad + fmt->pad;

	if (fmt->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		goto exit;
	/* Really apply here if not using MCU */
	d_inf(4, "%s:pad[%d] apply format<w%d, h%d, c%X>", sd->name, fmt->pad,
		fmt->format.width, fmt->format.height, fmt->format.code);
	*pad_fmt = fmt->format;
exit:
	return ret;
}

static int b52isp_path_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_rect *rect;

	if (sel->pad >= isd->pads_cnt)
		return -EINVAL;
	rect = isd->crop_pad + sel->pad;
	sel->r = *rect;
	return 0;
}

static int b52isp_path_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_rect *rect;
	int ret = 0;

	if (sel->pad >= isd->pads_cnt)
		return -EINVAL;
	rect = isd->crop_pad + sel->pad;

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		goto exit;
	/* Really apply here if not using MCU */
	d_inf(4, "%s:pad[%d] crop(%d, %d)<>(%d, %d)", sd->name, sel->pad,
		sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	*rect = sel->r;
exit:
	return ret;
}

static const struct v4l2_subdev_pad_ops b52isp_path_pad_ops = {
	.enum_mbus_code		= b52isp_path_enum_mbus_code,
	.enum_frame_size	= b52isp_path_enum_frame_size,
	.get_fmt		= b52isp_path_get_format,
	.set_fmt		= b52isp_path_set_format,
	.get_selection		= b52isp_path_get_selection,
	.set_selection		= b52isp_path_set_selection,
};

static const struct v4l2_subdev_ops b52isp_path_subdev_ops = {
	.core	= &b52isp_path_core_ops,
	.video	= &b52isp_path_video_ops,
	.pad	= &b52isp_path_pad_ops,
};

static int b52isp_path_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct isp_subdev *isd;
	struct b52isp_lpipe *lpipe;
	struct b52isp_ppipe *ppipe = NULL, *ppipe2 = NULL;
	struct b52isp *b52isp;
	int ret = 0;

	if (unlikely(WARN_ON(sd == NULL)))
		return -EPERM;
	isd = v4l2_get_subdev_hostdata(sd);
	if (unlikely(WARN_ON(isd == NULL)))
		return -EPERM;
	lpipe = isd->drv_priv;
	b52isp = lpipe->parent;
	WARN_ON(atomic_read(&lpipe->ref_cnt) < 0);

	if ((flags & MEDIA_LNK_FL_ENABLED) == 0)
		goto link_off;

	/* already mappend? */
	if (atomic_inc_return(&lpipe->ref_cnt) > 1)
		return 0;

	switch (lpipe->isd.sd_code) {
	/*
	 * TODO: For Now, use static mapping between physc_dev and logic_dev,
	 * maybe change to dynamic in future?
	 */
	case SDCODE_B52ISP_PIPE1:
		/* By default, driver assume CMD SET_FORMAT is used */
		lpipe->path_arg.aeag = TYPE_3A_UNLOCK;
		lpipe->path_arg.nr_frame = 0;
		ppipe = container_of(b52isp->blk[B52ISP_BLK_PIPE1],
			struct b52isp_ppipe, block);
		goto grab_single;
	case SDCODE_B52ISP_DUMP1:
		/* By default, driver assume CMD RAW_DUMP is used */
		lpipe->path_arg.aeag = TYPE_3A_UNLOCK;
		lpipe->path_arg.nr_frame = 0;
		ppipe = container_of(b52isp->blk[B52ISP_BLK_DUMP1],
			struct b52isp_ppipe, block);
		goto grab_single;
	case SDCODE_B52ISP_MS1:
		ppipe = container_of(b52isp->blk[B52ISP_BLK_PIPE1],
			struct b52isp_ppipe, block);
		goto grab_single;
	case SDCODE_B52ISP_PIPE2:
		/* By default, driver assume CMD SET_FORMAT is used */
		lpipe->path_arg.aeag = TYPE_3A_UNLOCK;
		lpipe->path_arg.nr_frame = 0;
		ppipe = container_of(b52isp->blk[B52ISP_BLK_PIPE2],
			struct b52isp_ppipe, block);
		goto grab_single;
	case SDCODE_B52ISP_DUMP2:
		/* By default, driver assume CMD RAW_DUMP is used */
		lpipe->path_arg.aeag = TYPE_3A_UNLOCK;
		lpipe->path_arg.nr_frame = 0;
		ppipe = container_of(b52isp->blk[B52ISP_BLK_DUMP2],
			struct b52isp_ppipe, block);
			goto grab_single;
	case SDCODE_B52ISP_MS2:
		ppipe = container_of(b52isp->blk[B52ISP_BLK_PIPE2],
			struct b52isp_ppipe, block);
grab_single:
		mutex_lock(&ppipe->map_lock);
		if (ppipe->isd == NULL) {
			ppipe->isd = &lpipe->isd;
			ret = b52isp_attach_blk_isd(&lpipe->isd, &ppipe->block);
			if (unlikely(ret < 0))
				goto fail_single;
			mutex_unlock(&ppipe->map_lock);
			isp_block_tune_power(&ppipe->block, 1);
			break;
		}
		ret = -EBUSY;
fail_single:
		/* physical pipeline occupied, abort */
		mutex_unlock(&ppipe->map_lock);
		return ret;
	/* For combined use case, grab both physical pipeline */
	case SDCODE_B52ISP_HS:
	case SDCODE_B52ISP_HDR:
	case SDCODE_B52ISP_3D:
		ppipe = container_of(b52isp->blk[B52ISP_BLK_PIPE1],
			struct b52isp_ppipe, block);
		ppipe2 = container_of(b52isp->blk[B52ISP_BLK_PIPE2],
			struct b52isp_ppipe, block);
		mutex_lock(&ppipe->map_lock);
		mutex_lock(&ppipe2->map_lock);
		if (ppipe->isd == NULL && ppipe2->isd == NULL) {
			ppipe->isd = &lpipe->isd;
			ppipe2->isd = &lpipe->isd;
			ret = b52isp_attach_blk_isd(&lpipe->isd, &ppipe->block);
			if (unlikely(ret < 0))
				goto fail_double;
			ret = b52isp_attach_blk_isd(&lpipe->isd,
							&ppipe2->block);
			if (unlikely(ret < 0)) {
				b52isp_detach_blk_isd(&lpipe->isd,
							&ppipe->block);
				goto fail_double;
			isp_block_tune_power(&ppipe->block, 1);
			isp_block_tune_power(&ppipe2->block, 1);
			}
			mutex_unlock(&ppipe->map_lock);
			break;
		}
		ret = -EBUSY;
fail_double:
		mutex_unlock(&ppipe->map_lock);
		mutex_unlock(&ppipe2->map_lock);
		return ret;
	default:
		return -ENODEV;
	}
	/* alloc meta data internal buffer */
	if (lpipe->meta_cpu == NULL) {
		lpipe->meta_size = b52_get_metadata_len(B52ISP_ISD_PIPE1);
		lpipe->meta_cpu = dmam_alloc_coherent(b52isp->dev,
					lpipe->meta_size, &lpipe->meta_dma,
					GFP_KERNEL);
		WARN_ON(lpipe->meta_cpu == NULL);
		d_inf(4, "alloc meta data for %s with %d bytes, VA@%p, PA@%X",
			lpipe->isd.subdev.name, lpipe->meta_size,
			lpipe->meta_cpu, (u32)lpipe->meta_dma);
		memset(lpipe->meta_cpu, 0xCD, lpipe->meta_size);
	}
	if (lpipe->pinfo_buf == NULL) {
		lpipe->pinfo_size = PIPE_INFO_SIZE;
		lpipe->pinfo_buf = devm_kzalloc(b52isp->dev,
				lpipe->pinfo_size, GFP_KERNEL);
		WARN_ON(lpipe->pinfo_buf == NULL);
	}
	if (ppipe2)
		d_inf(4, "%s mapped to [%s | %s]", lpipe->isd.subdev.name,
			ppipe->block.name, ppipe2->block.name);
	else
		d_inf(4, "%s mapped to %s", lpipe->isd.subdev.name,
			ppipe->block.name);
	lpipe->output_sel = 0;
	/* trigger sensor init event here */
	return ret;

link_off:
	if (atomic_dec_return(&lpipe->ref_cnt) > 0)
		return 0;
	do {
		struct isp_dev_ptr *item = list_first_entry(
				&lpipe->isd.gdev_list,
				struct isp_dev_ptr, hook);
		ppipe = container_of(item->ptr, struct b52isp_ppipe, block);
		if (lpipe->meta_cpu) {
			dmam_free_coherent(b52isp->dev, lpipe->meta_size,
				lpipe->meta_cpu, lpipe->meta_dma);
			lpipe->meta_cpu = NULL;
		}
		if (lpipe->pinfo_buf) {
			devm_kfree(b52isp->dev, lpipe->pinfo_buf);
			lpipe->pinfo_buf = NULL;
		}

		isp_block_tune_power(&ppipe->block, 0);
		mutex_lock(&ppipe->map_lock);
		ppipe->isd = NULL;
		b52isp_detach_blk_isd(&lpipe->isd, &ppipe->block);
		mutex_unlock(&ppipe->map_lock);
	} while (!list_empty(&lpipe->isd.gdev_list));
	d_inf(4, "%s and %s ummapped",
		lpipe->isd.subdev.name, ppipe->block.name);
	return 0;
}

static const struct media_entity_operations b52isp_path_media_ops = {
	.link_setup = b52isp_path_link_setup,
};

static int b52isp_path_sd_open(struct isp_subdev *ispsd)
{
	return 0;
}

static void b52isp_path_sd_close(struct isp_subdev *ispsd)
{
}

struct isp_subdev_ops b52isp_path_sd_ops = {
	.open		= b52isp_path_sd_open,
	.close		= b52isp_path_sd_close,
};

static void b52isp_path_remove(struct b52isp *b52isp)
{
	int i;

	for (i = B52ISP_BLK_PIPE1; i <= B52ISP_BLK_DUMP2; i++) {
		struct isp_block *blk = b52isp->blk[i];
		b52isp->blk[i] = NULL;
		if (blk)
			devm_kfree(b52isp->dev,
				container_of(blk, struct b52isp_ppipe, block));
	}

	for (i = B52ISP_ISD_PIPE1; i <= B52ISP_ISD_3D; i++) {
		struct isp_subdev *isd = b52isp->isd[i];
		struct b52isp_lpipe *pipe = container_of(isd,
						struct b52isp_lpipe, isd);
		b52isp->isd[i] = NULL;
		if (isd) {
			mutex_lock(&pipe->state_lock);
			if (pipe->cur_cmd)
				devm_kfree(isd->build->dev, pipe->cur_cmd);
			mutex_unlock(&pipe->state_lock);
			plat_ispsd_unregister(isd);
			v4l2_ctrl_handler_free(&isd->ctrl_handler);
			devm_kfree(b52isp->dev,
				container_of(isd, struct b52isp_lpipe, isd));
		}
	}
}

static int b52isp_path_create(struct b52isp *b52isp)
{
	struct isp_block *block;
	struct isp_subdev *ispsd;
	struct v4l2_subdev *sd;
	int ret, i, pipe_cnt;

	for (i = 0; i < b52isp->hw_desc->nr_pipe * 2; i++) {
		struct b52isp_ppipe *pipe = devm_kzalloc(b52isp->dev,
			sizeof(*pipe), GFP_KERNEL);
		if (pipe == NULL)
			return -ENOMEM;
		/* Add ISP pipeline Blocks */
		block = &pipe->block;
		block->id.dev_type = PCAM_IP_B52ISP;
		block->id.dev_id = b52isp->dev->id;
		block->id.mod_id = B52ISP_BLK_PIPE1 + i;
		block->dev = b52isp->dev;
		block->name = b52isp_block_name[B52ISP_BLK_PIPE1 + i];
		block->req_list = b52pipe_req;
		block->ops = &b52isp_path_hw_ops;
		mutex_init(&pipe->map_lock);
		pipe->parent = b52isp;
		b52isp->blk[B52ISP_BLK_PIPE1 + i] = block;
	}
	if (b52isp->hw_desc->nr_pipe == 1)
		pipe_cnt = PATH_PER_PIPE;
	else
		pipe_cnt = b52isp->hw_desc->nr_pipe * 3 + 3;
	for (i = 0; i < pipe_cnt; i++) {
		/* Add ISP logical pipeline isp-subdev */
		struct b52isp_lpipe *pipe = devm_kzalloc(b52isp->dev,
			sizeof(*pipe), GFP_KERNEL);
		if (pipe == NULL)
			return -ENOMEM;
		ispsd = &pipe->isd;
		sd = &ispsd->subdev;
		ret = b52isp_init_ctrls(&pipe->ctrls);
		if (unlikely(ret < 0))
			return ret;
		sd->entity.ops = &b52isp_path_media_ops;
		v4l2_subdev_init(sd, &b52isp_path_subdev_ops);
		sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
		sd->ctrl_handler = &pipe->ctrls.ctrl_handler;
		strncpy(sd->name, b52isp_ispsd_name[B52ISP_ISD_PIPE1 + i],
			sizeof(sd->name));
		ispsd->ops = &b52isp_path_sd_ops;
		ispsd->drv_priv = pipe;
		ispsd->pads[0].flags = MEDIA_PAD_FL_SINK;
		ispsd->pads[1].flags = MEDIA_PAD_FL_SOURCE;
		ispsd->pads_cnt = 2;
		ispsd->single = 1;
		INIT_LIST_HEAD(&ispsd->gdev_list);
		ispsd->sd_code = SDCODE_B52ISP_PIPE1 + i;
		switch (i+B52ISP_ISD_PIPE1) {
		case B52ISP_ISD_PIPE1:
		case B52ISP_ISD_DUMP1:
		/*
		 * Just attach physcal pipeline to logical pipeline to help
		 * init phys_pipe. After the registeration, they'll be detached
		 */
			if (b52isp->blk[B52ISP_BLK_PIPE1 + i]) {
				ret = b52isp_attach_blk_isd(ispsd,
					b52isp->blk[B52ISP_BLK_PIPE1 + i]);
				if (unlikely(ret < 0))
					return ret;
			}
			ret = plat_ispsd_register(ispsd);
			if (ret < 0)
				return ret;
			b52isp->isd[B52ISP_ISD_PIPE1 + i] = ispsd;
			pipe->parent = b52isp;
			mutex_init(&pipe->state_lock);

			/* Now detach! */
			if (b52isp->blk[B52ISP_BLK_PIPE1 + i])
				b52isp_detach_blk_isd(ispsd,
					b52isp->blk[B52ISP_BLK_PIPE1 + i]);
		/*
		 * From this point, physical pipeline and logical pipeline is
		 * indepedent to each other. Before using any logical pipeline,
		 * it MUST find a physical pipeline and attach to it
		 */
			break;
		case B52ISP_ISD_PIPE2:
		case B52ISP_ISD_DUMP2:
		/*
		* Just attach physcal pipeline to logical pipeline to help
		* init phys_pipe. After the registeration, they'll be detached
		*/
			if (b52isp->blk[B52ISP_BLK_PIPE1 + i - 1]) {
				ret = b52isp_attach_blk_isd(ispsd,
					b52isp->blk[B52ISP_BLK_PIPE1 + i - 1]);
				if (unlikely(ret < 0))
					return ret;
			}
			ret = plat_ispsd_register(ispsd);
			if (ret < 0)
				return ret;
			b52isp->isd[B52ISP_ISD_PIPE1 + i] = ispsd;
			pipe->parent = b52isp;
			mutex_init(&pipe->state_lock);

			/* Now detach! */
			if ((b52isp->blk[B52ISP_BLK_PIPE1 + i - 1]))
				b52isp_detach_blk_isd(ispsd,
					b52isp->blk[B52ISP_BLK_PIPE1 + i - 1]);
		/*
		* From this point, physical pipeline and logical pipeline is
		* indepedent to each other. Before using any logical pipeline,
		* it MUST find a physical pipeline and attach to it
		*/
			break;
		default:
			ret = plat_ispsd_register(ispsd);
			if (ret < 0)
				return ret;
			b52isp->isd[B52ISP_ISD_PIPE1 + i] = ispsd;
			pipe->parent = b52isp;
			mutex_init(&pipe->state_lock);
			break;
		}
		/* For single pipeline ISP, don't create virtual pipes */
		if ((b52isp->hw_desc->nr_pipe < 2) && (i >= B52ISP_ISD_PIPE2))
			break;
	}
	return 0;
}

/****************************** AXI Master Block ******************************/

static int b52isp_axi_hw_open(struct isp_block *block)
{
	return 0;
}

static void b52isp_axi_hw_close(struct isp_block *block)
{

}

static int b52isp_axi_set_clock(struct isp_block *block, int rate)
{
	return 0;
}

struct isp_block_ops b52isp_axi_hw_ops = {
	.open	= b52isp_axi_hw_open,
	.close	= b52isp_axi_hw_close,
	.set_clock = b52isp_axi_set_clock,
};

/***************************** AXI Master Subdev *****************************/
/* ioctl(subdev, IOCTL_XXX, arg) is handled by this one */
static long b52isp_axi_ioctl(struct v4l2_subdev *sd,
				unsigned int cmd, void *arg)
{
	return 0;
}

static const struct v4l2_subdev_core_ops b52isp_axi_core_ops = {
	.ioctl	= b52isp_axi_ioctl,
};

static int b52isp_axi_set_stream(struct v4l2_subdev *sd, int enable)
{
	return 0;
}

static const struct v4l2_subdev_video_ops b52isp_axi_video_ops = {
	.s_stream	= b52isp_axi_set_stream,
};

static int b52isp_axi_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	int ret = 0;
	return ret;
}

static int b52isp_axi_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_fh *fh,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	int ret = 0;
	return ret;
}

static int b52isp_axi_get_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_mbus_framefmt *pad_fmt;

	if (fmt->pad >= isd->pads_cnt)
		return -EINVAL;
	pad_fmt = isd->fmt_pad + fmt->pad;
	fmt->format = *pad_fmt;
	return 0;
}

static int b52isp_axi_set_format(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_format *fmt)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_mbus_framefmt *pad_fmt;
	int ret = 0;

	if (fmt->pad >= isd->pads_cnt)
		return -EINVAL;
	pad_fmt = isd->fmt_pad + fmt->pad;

	if (fmt->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		goto exit;
	/* Really apply here if not using MCU */
	d_inf(4, "%s:pad[%d] apply format<w%d, h%d, c%X>", sd->name, fmt->pad,
		fmt->format.width, fmt->format.height, fmt->format.code);
	*pad_fmt = fmt->format;
exit:
	return ret;
}

static int b52isp_axi_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_rect *rect;

	if (sel->pad >= isd->pads_cnt)
		return -EINVAL;
	rect = isd->crop_pad + sel->pad;
	sel->r = *rect;
	return 0;
}

static int b52isp_axi_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh,
				struct v4l2_subdev_selection *sel)
{
	struct isp_subdev *isd = v4l2_get_subdev_hostdata(sd);
	struct v4l2_rect *rect;
	int ret = 0;

	if (sel->pad >= isd->pads_cnt)
		return -EINVAL;
	rect = isd->crop_pad + sel->pad;

	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		goto exit;
	/* Really apply here if not using MCU */
	d_inf(4, "%s:pad[%d] crop(%d, %d)<>(%d, %d)", sd->name, sel->pad,
		sel->r.left, sel->r.top, sel->r.width, sel->r.height);
	*rect = sel->r;
exit:
	return ret;
}

static const struct v4l2_subdev_pad_ops b52isp_axi_pad_ops = {
	.enum_mbus_code		= b52isp_axi_enum_mbus_code,
	.enum_frame_size	= b52isp_axi_enum_frame_size,
	.get_fmt		= b52isp_axi_get_format,
	.set_fmt		= b52isp_axi_set_format,
	.set_selection		= b52isp_axi_set_selection,
	.get_selection		= b52isp_axi_get_selection,
};

static const struct v4l2_subdev_ops b52isp_axi_subdev_ops = {
	.core	= &b52isp_axi_core_ops,
	.video	= &b52isp_axi_video_ops,
	.pad	= &b52isp_axi_pad_ops,
};

static inline int b52_fill_buf(struct isp_videobuf *buf,
		struct plat_cam *pcam, int num_planes, int mac_id, int port)
{
	int i;
	int ret = 0;
	dma_addr_t dmad[VIDEO_MAX_PLANES];

	if (buf == NULL) {
		d_inf(3, "%s: buffer is NULL", __func__);
		return -EINVAL;
	}

	for (i = 0; i < num_planes; i++)
		dmad[i] = (dma_addr_t)(buf->ch_info[i].daddr);

	ret = b52_update_mac_addr(dmad, 0, num_planes, mac_id, port);
	if (ret < 0) {
		d_inf(1, "Failed to update mac addr");
		return ret;
	}

	if (pcam->fill_mmu_chnl)
		ret = pcam->fill_mmu_chnl(pcam, &buf->vb, num_planes);

	return ret;
}

static inline int b52isp_update_metadata(struct isp_subdev *isd,
	struct isp_vnode *vnode, struct isp_videobuf *buffer)
{
	int i;
	u32 type;
	u16 size;
	u16 *src;
	u16 *dst;
	/*
	 * FIXME: hard code to assume META plane comes right after
	 * image plane, we should check the plane signature to identify
	 * the plane usage.
	 */
	int meta_pid = vnode->format.fmt.pix_mp.num_planes;
	struct media_pad *pad_pipe =
		media_entity_remote_pad(isd->pads + B52PAD_AXI_IN);
	struct isp_subdev *lpipe_isd = v4l2_get_subdev_hostdata(
		media_entity_to_v4l2_subdev(pad_pipe->entity));
	struct b52isp_lpipe *lpipe = container_of(lpipe_isd,
		struct b52isp_lpipe, isd);

	if ((buffer->vb.v4l2_buf.type ==
		 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ||
		(buffer->vb.num_planes <= meta_pid))
		return -EINVAL;

	if (buffer->va[meta_pid] == NULL ||
		lpipe->meta_cpu == NULL ||
		lpipe->pinfo_buf == NULL)
		return -EINVAL;

	type = buffer->vb.v4l2_planes[meta_pid].reserved[0];
	if (type == V4L2_PLANE_SIGNATURE_PIPELINE_INFO)
		b52_read_pipeline_info(0, lpipe->pinfo_buf);

	dst = buffer->va[meta_pid];
	if (type == V4L2_PLANE_SIGNATURE_PIPELINE_INFO) {
		src = (u16 *)lpipe->pinfo_buf;
		size = lpipe->pinfo_size;
	} else if (type == V4L2_PLANE_SIGNATURE_PIPELINE_META) {
		src = (u16 *)lpipe->meta_cpu;
		size = lpipe->meta_size;
	} else
		return -EINVAL;

	if (size > buffer->vb.v4l2_planes[meta_pid].length) {
		d_inf(2, "%s buffer length error: req %x, got %x",
			  isd->subdev.name, size,
			  buffer->vb.v4l2_planes[meta_pid].length);
		return -EINVAL;
	}

	if (type == V4L2_PLANE_SIGNATURE_PIPELINE_INFO)
		for (i = 0; i < size; i += 2)
			*dst++ = *src++;
	else
		for (i = 0; i < size; i += 2)
			*dst++ = swab16(*src++);

	return 0;
}

static int b52isp_mac_handler(struct isp_subdev *isd, unsigned long event)
{
	u8 irqstatus = event & 0xFFFF;
	u8 port = (event >> PORT_ID_SHIFT) & 0xF;
	u8 mac_id = (event >> MAC_ID_SHIFT) & 0xF;
	struct isp_videobuf *buffer;
	struct isp_vnode *vnode = ispsd_get_video(isd);
	struct b52isp_laxi *laxi = isd->drv_priv;
	struct isp_build *build = container_of(isd->subdev.entity.parent,
						struct isp_build, media_dev);
	struct plat_cam *pcam = build->plat_priv;
	int num_planes = vnode->format.fmt.pix_mp.num_planes;
	struct isp_block *blk = isp_sd2blk(&laxi->isd);
	struct b52isp_paxi *paxi = container_of(blk, struct b52isp_paxi, blk);
	int ret = 0;

	BUG_ON(vnode == NULL);

	switch (port) {
	case B52AXI_PORT_R1:
		port = 2;
		break;
	case B52AXI_PORT_W1:
		port = 0;
		break;
	case B52AXI_PORT_W2:
		port = 1;
		break;
	default:
		BUG_ON(1);
		break;
	}

	if (laxi->dma_state == B52DMA_IDLE)
		goto check_sof_irq;
	else
		goto check_eof_irq;

check_sof_irq:
	if (irqstatus & VIRT_IRQ_START) {
		laxi->dma_state = B52DMA_ACTIVE;
		irqstatus &= ~VIRT_IRQ_START;

		buffer = isp_vnode_find_busy_buffer(vnode, 1);
		if (buffer == NULL)
			buffer = isp_vnode_get_idle_buffer(vnode);

		isp_vnode_put_busy_buffer(vnode, buffer);
		b52_fill_buf(buffer, pcam, num_planes, mac_id, port);

		d_inf(4, "%s receive start", isd->subdev.name);
	}

	if ((laxi->dma_state == B52DMA_ACTIVE) &&
		(irqstatus & (VIRT_IRQ_FIFO | VIRT_IRQ_DONE)))
		goto check_eof_irq;

	if (irqstatus & VIRT_IRQ_DROP) {
		laxi->dma_state = B52DMA_IDLE;
		irqstatus &= ~VIRT_IRQ_DROP;

		buffer = isp_vnode_find_busy_buffer(vnode, 0);
		if (!buffer) {
			buffer = isp_vnode_get_idle_buffer(vnode);
			isp_vnode_put_busy_buffer(vnode, buffer);
		} else
			d_inf(1, "%s: buffer in the BusyQ when drop! idle:%d, busy:%d",
				  vnode->vdev.name,
				  vnode->idle_buf_cnt, vnode->busy_buf_cnt);
		b52_fill_buf(buffer, pcam, num_planes, mac_id, port);

		d_inf(3, "%s receive drop frame", isd->subdev.name);
	}

check_eof_irq:
	if (irqstatus & VIRT_IRQ_FIFO) {
		laxi->dma_state = B52DMA_IDLE;
		irqstatus &= ~VIRT_IRQ_FIFO;

		buffer = isp_vnode_find_busy_buffer(vnode, 0);
		b52_fill_buf(buffer, pcam, num_planes, mac_id, port);

		d_inf(3, "%s receive FIFO flow error", isd->subdev.name);
	}

	if (irqstatus & VIRT_IRQ_DONE) {
		irqstatus &= ~VIRT_IRQ_DONE;
		if (laxi->dma_state == B52DMA_IDLE)
			goto recheck;
		laxi->dma_state = B52DMA_IDLE;

		if ((paxi->r_type == B52AXI_REVENT_MEMSENSOR) &&
			(port == B52AXI_PORT_R1)) {
			buffer = isp_vnode_find_busy_buffer(vnode, 0);
			b52_fill_buf(buffer, pcam, num_planes, mac_id, port);
			d_inf(4, "%s f2f kick read port", isd->subdev.name);
			goto recheck;
		}

		buffer = isp_vnode_get_busy_buffer(vnode);
		if (!buffer) {
			d_inf(3, "%s: done busy buffer is NULL", isd->subdev.name);
			goto recheck;
		}

		b52isp_update_metadata(isd, vnode, buffer);
		isp_vnode_export_buffer(buffer);

		d_inf(4, "%s receive done", isd->subdev.name);
	}

recheck:
	if (laxi->dma_state == B52DMA_IDLE) {
		if (irqstatus & (VIRT_IRQ_START | VIRT_IRQ_DROP))
			goto check_sof_irq;
		if (irqstatus & (VIRT_IRQ_FIFO | VIRT_IRQ_DONE))
			d_inf(1, "-----------unmatched EOF IRQ: %X", irqstatus);
	} else {
		if (irqstatus & (VIRT_IRQ_FIFO | VIRT_IRQ_DONE))
			goto check_eof_irq;
		if (irqstatus & (VIRT_IRQ_START | VIRT_IRQ_DROP))
			d_inf(1, "-----------unmatched SOF IRQ: %X", irqstatus);
	}

	return ret;
}

static int b52isp_mac_irq_event(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct isp_subdev *isd = data;
	return b52isp_mac_handler(isd, event);
}

static struct notifier_block b52isp_mac_irq_nb = {
	.notifier_call = b52isp_mac_irq_event,
};

/*
 * Select a physc port for a logic dev
 */
static int b52_map_axi_port(struct b52isp_laxi *laxi, int map,
			struct isp_vnode *vnode, struct plat_pipeline *ppl)
{
	struct b52isp *b52 = laxi->parent;
	struct isp_block *blk;
	struct b52isp_paxi *paxi = NULL;
	int ret = 0, fit_bit, ok_bit;
	__u32 idle_map, tag;
#ifdef CONFIG_MARVELL_MEDIA_MMU
	struct isp_build *build = container_of(laxi->isd.subdev.entity.parent,
					struct isp_build, media_dev);
	struct plat_cam *pcam = build->plat_priv;
#endif

	WARN_ON(atomic_read(&laxi->ref_cnt) < 0);

	if (map == 0)
		goto unmap;

	/* already mappend? */
	if (atomic_inc_return(&laxi->ref_cnt) > 1)
		return 0;

	/* Actually map logic dev to physc dev */
	mutex_lock(&b52->mac_map_lock);
	tag = plat_get_src_tag(ppl);
	idle_map = output_mask[laxi->isd.sd_code - SDCODE_B52ISP_A1W1]
		& (~b52->mac_map);
	fit_bit = ok_bit = -1;

	while (idle_map) {
		unsigned long cur_mask = idle_map - (idle_map & (idle_map - 1));
		__u32 bit = find_first_bit(&cur_mask, BITS_PER_LONG);
		__u32 src;

		blk = b52->blk[B52ISP_BLK_AXI1 + bit / B52AXI_PORT_CNT];
		paxi = container_of(blk, struct b52isp_paxi, blk);
		if ((bit % B52AXI_PORT_CNT) == B52AXI_PORT_R1) {
			WARN_ON(paxi->isd[B52AXI_PORT_R1]);
			fit_bit = ok_bit = bit;
			break;
		}

		src = paxi->src_tag[B52AXI_PORT_W1] |
			paxi->src_tag[B52AXI_PORT_W2];
		if (src == 0) {
			if (ok_bit < 0)
				ok_bit = bit;
		} else if (src == tag) {
			fit_bit = bit;
			break;
		}
		idle_map -= cur_mask;
	}

	if (fit_bit < 0)
		fit_bit = ok_bit;
	if (fit_bit < 0) {
		/* Can't find a physical port to map the logical port */
		ret = -EBUSY;
		goto fail;
	}

	if (WARN_ON(test_and_set_bit(fit_bit, &b52->mac_map))) {
		ret = -EBUSY;
		goto fail;
	}
	WARN_ON(paxi->isd[fit_bit % B52AXI_PORT_CNT]);
	blk = b52->blk[B52ISP_BLK_AXI1 + fit_bit / B52AXI_PORT_CNT];
	paxi = container_of(blk, struct b52isp_paxi, blk);
	paxi->isd[fit_bit % B52AXI_PORT_CNT] = &laxi->isd;
	paxi->src_tag[fit_bit % B52AXI_PORT_CNT] = tag;
	ret = b52isp_attach_blk_isd(&laxi->isd, &paxi->blk);
	if (unlikely(ret < 0))
		goto fail;
	laxi->mac = fit_bit / B52AXI_PORT_CNT;
	laxi->port = fit_bit % B52AXI_PORT_CNT;

	mutex_unlock(&b52->mac_map_lock);

	ret = isp_block_tune_power(&paxi->blk, 1);
	if (unlikely(ret < 0))
		goto fail;

	ret = b52_ctrl_mac_irq(fit_bit, 1);
	if (unlikely(ret < 0))
		goto power_off;

#ifdef CONFIG_MARVELL_MEDIA_MMU
	/* MMU related */
	if (pcam->alloc_mmu_chnl) {
		ret = pcam->alloc_mmu_chnl(pcam, paxi->blk.id.mod_id, port,
					vnode->format.fmt.pix_mp.num_planes,
					&vnode->mmu_ch_dsc);
		if (unlikely(ret < 0))
			goto unmap;
	}
#endif
	d_inf(3, "%s couple to MAC%d, port%d", laxi->isd.subdev.name,
		laxi->mac + 1, laxi->port + 1);
	return 0;

power_off:
	isp_block_tune_power(&paxi->blk, 0);
fail:
	/* failed to map to physical dev, abort */
	mutex_unlock(&b52->mac_map_lock);
	return ret;

unmap:
	if (atomic_dec_return(&laxi->ref_cnt) > 0)
		return 0;
	paxi = container_of(isp_sd2blk(&laxi->isd), struct b52isp_paxi, blk);
#ifdef CONFIG_MARVELL_MEDIA_MMU
	if (pcam->free_mmu_chnl)
		pcam->free_mmu_chnl(pcam, &vnode->mmu_ch_dsc);
#endif
	fit_bit = laxi->mac * B52AXI_PORT_CNT + laxi->port;
	b52_ctrl_mac_irq(fit_bit, 0);
	isp_block_tune_power(&paxi->blk, 0);

	d_inf(3, "%s decouple from MAC%d, port%d", laxi->isd.subdev.name,
		laxi->mac + 1, laxi->port + 1);
	mutex_lock(&b52->mac_map_lock);
	paxi->isd[laxi->port] = NULL;
	paxi->src_tag[laxi->port] = 0;
	paxi->event = 0;
	tasklet_kill(&paxi->tasklet);
	laxi->mac = -1;
	laxi->port = -1;
	b52isp_detach_blk_isd(&laxi->isd, &paxi->blk);
	clear_bit(fit_bit, &b52->mac_map);
	mutex_unlock(&b52->mac_map_lock);
	return 0;
}

static int b52isp_export_cmd_buffer(struct b52isp_cmd *cmd)
{
	struct isp_videobuf *ivb;
	int i;
	if (cmd->src_type == CMD_SRC_AXI) {
		ivb = isp_vnode_get_busy_buffer(cmd->mem.vnode);
		while (ivb) {
			isp_vnode_export_buffer(ivb);
			ivb = isp_vnode_get_busy_buffer(cmd->mem.vnode);
		}
	}

	for (i = 0; i < B52_OUTPUT_PER_PIPELINE; i++) {
		if (cmd->output[i].vnode == NULL)
			continue;
		ivb = isp_vnode_get_busy_buffer(cmd->output[i].vnode);
		while (ivb) {
			isp_vnode_export_buffer(ivb);
			ivb = isp_vnode_get_busy_buffer(cmd->output[i].vnode);
		}
	}
	return 0;
}

#define SETUP_WITH_DEFAULT_FORMAT	\
do {	\
	if (lpipe->output_sel !=	\
		lpipe->cur_cmd->output_map) {	\
		for (i = 0; i < B52_OUTPUT_PER_PIPELINE; i++) {	\
			if (!test_bit(i, &lpipe->cur_cmd->output_map))	\
				continue;	\
			lpipe->cur_cmd->output[i].vnode = ppl.dst[i];	\
			lpipe->cur_cmd->output[i].pix_mp =	\
				vnode->format.fmt.pix_mp;	\
		}	\
	}	\
} while (0)

static int b52isp_stream_handler(struct isp_subdev *isd,
		struct isp_vnode *vnode, int stream)
{
	int ret = 0, port, out_id;
	struct isp_subdev *pipe;
	struct b52isp_laxi *laxi;
	struct isp_build *build = container_of(isd->subdev.entity.parent,
						struct isp_build, media_dev);
	struct plat_cam *pcam = build->plat_priv;
	int num_planes = vnode->format.fmt.pix_mp.num_planes;
	struct b52isp_paxi *paxi;
	struct plat_pipeline ppl;

	d_inf(4, "%s: handling the stream %d event from %s",
		isd->subdev.name, stream, vnode->vdev.name);

	ret = plat_vdev_get_pipeline(vnode, &ppl);
	if (WARN_ON(ret < 0))
		return ret;
	pipe = ppl.path;
	if (WARN_ON(pipe == NULL))
		return -ENODEV;
	out_id = ret;

	laxi = isd->drv_priv;
	laxi->stream = stream;

	/* stream off must happen before unmap LAXI and PAXI */
	if (!laxi->stream) {
		struct b52isp_lpipe *lpipe = pipe->drv_priv;
		struct isp_block *blk = isp_sd2blk(&laxi->isd);
		int mac_id;

		paxi = container_of(blk, struct b52isp_paxi, blk);
		mac_id = laxi->mac;
		port = laxi->port;
		mutex_lock(&lpipe->state_lock);

		lpipe->cur_cmd->output_map = ppl.dst_map;
		if (vnode->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			clear_bit(out_id, &lpipe->cur_cmd->enable_map);

		switch (lpipe->cur_cmd->cmd_name) {
		case CMD_IMG_CAPTURE:
		case CMD_RAW_PROCESS:
		case CMD_HDR_STILL:
			goto after_notifier_unregister;
		default:
			break;
		}

		/* Stop listening to IRQ */
		switch (port) {
		case B52AXI_PORT_W1:
			ret = atomic_notifier_chain_unregister(
					&paxi->irq_w1.head, &b52isp_mac_irq_nb);
			WARN_ON(ret < 0);
			break;
		case B52AXI_PORT_W2:
			ret = atomic_notifier_chain_unregister(
					&paxi->irq_w2.head, &b52isp_mac_irq_nb);
			WARN_ON(ret < 0);
			break;
		case B52AXI_PORT_R1:
			ret = atomic_notifier_chain_unregister(
					&paxi->irq_r1.head, &b52isp_mac_irq_nb);
			WARN_ON(ret < 0);
			break;
		default:
			WARN_ON(1);
		}

after_notifier_unregister:
		switch (lpipe->cur_cmd->cmd_name) {
		case CMD_TEST:
		case CMD_RAW_DUMP:
		case CMD_IMG_CAPTURE:
		case CMD_RAW_PROCESS:
		case CMD_HDR_STILL:
			if (vnode->buf_type ==
				V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
				lpipe->cur_cmd->output[out_id].vnode = NULL;
				memset(&lpipe->cur_cmd->output[out_id].pix_mp,
				0, sizeof(struct v4l2_pix_format_mplane));
			}
			break;
		case CMD_CHG_FORMAT:
		case CMD_SET_FORMAT:
			if (!lpipe->cur_cmd->enable_map)
				lpipe->cur_cmd->cmd_name = CMD_SET_FORMAT;
			/* set the stream off flag for isp cmd*/
			lpipe->cur_cmd->flags |= BIT(CMD_FLAG_STREAM_OFF);
			ret = b52isp_try_apply_cmd(lpipe);
			lpipe->cur_cmd->flags &= ~BIT(CMD_FLAG_STREAM_OFF);
			if (ret < 0) {
				d_inf(1, "apply change cmd failed on port:%d\n",
					out_id);
				break;
			}
			break;
		default:
			d_inf(1, "TODO: add stream off support of %s in command %d",
				isd->subdev.name, lpipe->cur_cmd->cmd_name);
			break;
		}

		mutex_unlock(&lpipe->state_lock);
	}

	/* Map / Unmap Logical AXI and Physical AXI */
	if (laxi == ppl.scalar_a->drv_priv) {
		ret = b52_map_axi_port(laxi, laxi->stream, vnode, &ppl);
		if (unlikely(WARN_ON(ret < 0)))
			return ret;
	} else {
		int sid = 0;
		while (ppl.scalar_b[sid]) {
			ret = b52_map_axi_port(ppl.scalar_b[sid]->drv_priv,
						laxi->stream, vnode, &ppl);
			if (unlikely(WARN_ON(ret < 0)))
				return ret;
			sid++;
		}
	}

	/* Stream on must happen after map LAXI and PAXI */
	if (laxi->stream) {
		struct b52isp_lpipe *lpipe = pipe->drv_priv;
		struct isp_block *blk = isp_sd2blk(&laxi->isd);
		int mac_id, i, nr_rd_buf;
		struct isp_videobuf *isp_vb;

		paxi = container_of(blk, struct b52isp_paxi, blk);
		mac_id = laxi->mac;
		port = laxi->port;
		mutex_lock(&lpipe->state_lock);

		lpipe->cur_cmd->output_map = ppl.dst_map;
		if (vnode->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			set_bit(out_id, &lpipe->cur_cmd->enable_map);

		/*
		 * This is just a W/R. Eventually, all command will be decided
		 * by driver topology and path atgument, just before send MCU
		 * commands
		 */
		switch (lpipe->cur_cmd->cmd_name) {
		case CMD_SET_FORMAT:
			if (lpipe->path_arg.aeag == TYPE_3A_LOCKED)
				lpipe->cur_cmd->cmd_name = CMD_IMG_CAPTURE;
			break;
		default:
			break;
		}

		switch (lpipe->cur_cmd->cmd_name) {
		case CMD_RAW_DUMP:
		case CMD_CHG_FORMAT:
		case CMD_SET_FORMAT:
			if (vnode->buf_type ==
				V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {

				/*
				 * FIXME: Firmware issue require all output port
				 * to be configured with a default format, even
				 * if it's not streamed on
				 */
				SETUP_WITH_DEFAULT_FORMAT;

				paxi->r_type = B52AXI_REVENT_RAWPROCCESS;
				lpipe->cur_cmd->output[out_id].vnode = vnode;
				lpipe->cur_cmd->output[out_id].pix_mp =
					vnode->format.fmt.pix_mp;
				laxi->dma_state = B52DMA_IDLE;

				if (lpipe->cur_cmd->flags & BIT(CMD_FLAG_MS))
					ret = 0;
				else {
					ret = b52isp_try_apply_cmd(lpipe);
					if (ret < 0)
						goto unlock;
				}
			} else if (vnode->buf_type ==
				V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
				isp_vb = isp_vnode_get_idle_buffer(vnode);
				BUG_ON(isp_vb == NULL);
				lpipe->cur_cmd->mem.axi_id = mac_id;
				lpipe->cur_cmd->mem.buf[0] = isp_vb;
				lpipe->cur_cmd->mem.vnode = vnode;
				if (lpipe->cur_cmd->memory_sensor_data ==
									NULL) {
					d_inf(1, "can't match sensor\n");
					d_inf(1, "OV13850 as default sensor\n");
					lpipe->cur_cmd->memory_sensor_data =
					memory_sensor_match("marvell,ov13850");
				}
				if (lpipe->cur_cmd->flags & BIT(CMD_FLAG_MS)) {
					paxi->r_type = B52AXI_REVENT_MEMSENSOR;
					ret = b52isp_try_apply_cmd(lpipe);
					if (ret < 0)
						goto unlock;
					ret = b52_fill_buf(isp_vb, pcam,
						num_planes, mac_id, 2);
					if (ret < 0)
						goto unlock;
					ret = isp_vnode_put_busy_buffer(vnode,
									isp_vb);
					if (unlikely(WARN_ON(ret < 0)))
						goto unlock;
				}
			}
			break;
		case CMD_IMG_CAPTURE:
			for (i = 0; i < lpipe->path_arg.nr_frame; i++) {
				isp_vb = isp_vnode_get_idle_buffer(vnode);
				if (isp_vb == NULL) {
					d_inf(1, "%s: capture buf not ready %d > %d",
						lpipe->isd.subdev.name,
						lpipe->path_arg.nr_frame, i);
					goto unlock;
				}
				ret = isp_vnode_put_busy_buffer(vnode, isp_vb);
				if (unlikely(WARN_ON(ret < 0)))
					goto unlock;
				lpipe->cur_cmd->output[out_id].vnode = vnode;
				lpipe->cur_cmd->output[out_id].pix_mp =
					vnode->format.fmt.pix_mp;
				lpipe->cur_cmd->output[out_id].buf[i] = isp_vb;
				if (!pcam->fill_mmu_chnl)
					continue;
				ret = pcam->fill_mmu_chnl(pcam,	&isp_vb->vb,
					num_planes);
				if (ret < 0) {
					d_inf(1, "%s: failed to config MMU channel",
						vnode->vdev.name);
					goto unlock;
				}
			}
			lpipe->cur_cmd->output[out_id].nr_buffer =
							lpipe->path_arg.nr_frame;
			lpipe->cur_cmd->b_ratio_1 = lpipe->path_arg.ratio_1_2;
			lpipe->cur_cmd->b_ratio_2 = lpipe->path_arg.ratio_1_3;
			if (lpipe->path_arg.linear_yuv)
				lpipe->cur_cmd->flags |=
					BIT(CMD_FLAG_LINEAR_YUV);
			ret = b52isp_try_apply_cmd(lpipe);
			lpipe->cur_cmd->flags &= ~BIT(CMD_FLAG_LINEAR_YUV);
			if (WARN_ON(ret < 0))
				goto unlock;
			b52isp_export_cmd_buffer(lpipe->cur_cmd);
			/* ignore MAC IRQ, so skip notifier chain */
			goto unlock;
		case CMD_HDR_STILL:
			nr_rd_buf = 2;
			goto offline_setup;
		case CMD_RAW_PROCESS:
			nr_rd_buf = 1;
offline_setup:
			if (lpipe->cur_cmd->mem.vnode == vnode) {
				/*
				 * For offline commands, only input vdev
				 * state change will trigger the MCU command
				 */
				lpipe->cur_cmd->mem.axi_id = mac_id;
				for (i = 0; i < nr_rd_buf; i++) {
					isp_vb = isp_vnode_get_idle_buffer(vnode);
					BUG_ON(isp_vb == NULL);
					ret = isp_vnode_put_busy_buffer(vnode, isp_vb);
					if (unlikely(WARN_ON(ret < 0)))
						goto unlock;
					lpipe->cur_cmd->mem.buf[i] = isp_vb;
					if (!pcam->fill_mmu_chnl)
						continue;
					ret = pcam->fill_mmu_chnl(pcam, &isp_vb->vb,
						num_planes);
					if (ret < 0) {
						d_inf(1, "%s: failed to config MMU channel",
							vnode->vdev.name);
						goto unlock;
					}
				}
				lpipe->cur_cmd->mem.vnode = vnode;
				ret = b52isp_try_apply_cmd(lpipe);
				if (ret < 0)
					goto unlock;
				b52isp_export_cmd_buffer(lpipe->cur_cmd);
				goto unlock;
			} else {
				/*
				 * output vdev state change will only
				 * update command struct
				 */
				isp_vb = isp_vnode_get_idle_buffer(vnode);
				BUG_ON(isp_vb == NULL);
				ret = isp_vnode_put_busy_buffer(vnode, isp_vb);
				if (unlikely(WARN_ON(ret < 0)))
					goto unlock;
				lpipe->cur_cmd->output[out_id].vnode = vnode;
				lpipe->cur_cmd->output[out_id].pix_mp =
					vnode->format.fmt.pix_mp;
				lpipe->cur_cmd->output[out_id].buf[0] = isp_vb;
				lpipe->cur_cmd->output[out_id].nr_buffer = 1;
				if (pcam->fill_mmu_chnl) {
					ret = pcam->fill_mmu_chnl(pcam,
						&isp_vb->vb, num_planes);
					if (ret < 0) {
						d_inf(1, "%s: failed to config MMU channel",
							vnode->vdev.name);
						goto unlock;
					}
				}
			}
			goto unlock;
		default:
			d_inf(1, "TODO: add stream on support of %s in command %d",
				isd->subdev.name, lpipe->cur_cmd->cmd_name);
			break;
		}

		/* Begin to accept H/W IRQ */
		switch (port) {
		case B52AXI_PORT_W1:
			ret = atomic_notifier_chain_register(
					&paxi->irq_w1.head, &b52isp_mac_irq_nb);
			if (ret < 0)
				goto unlock;
			break;
		case B52AXI_PORT_W2:
			ret = atomic_notifier_chain_register(
					&paxi->irq_w2.head, &b52isp_mac_irq_nb);
			if (ret < 0)
				goto unlock;
			break;
		case B52AXI_PORT_R1:
			ret = atomic_notifier_chain_register(
					&paxi->irq_r1.head, &b52isp_mac_irq_nb);
			if (ret < 0)
				goto unlock;
			break;
		default:
			WARN_ON(1);
		}
unlock:
		mutex_unlock(&lpipe->state_lock);
	}

	return ret;
}

static int b52isp_video_event(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct isp_vnode *vnode = data;
	struct isp_subdev *isd = vnode->notifier.priv;
	int ret;

	switch (event) {
	case ISP_NOTIFY_STM_ON:
		ret = b52isp_stream_handler(isd, vnode, 1);
		break;
	case ISP_NOTIFY_STM_OFF:
		ret = b52isp_stream_handler(isd, vnode, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static struct notifier_block b52isp_video_nb = {
	.notifier_call = b52isp_video_event,
};

static int b52isp_axi_connect_video(struct isp_subdev *isd,
					struct isp_vnode *vnode)
{
	int ret;

	ret = blocking_notifier_chain_register(&vnode->notifier.head,
			&b52isp_video_nb);
	if (ret < 0)
		return ret;

	vnode->notifier.priv = isd;

	return 0;
}

static int b52isp_axi_disconnect_video(struct isp_subdev *isd,
					struct isp_vnode *vnode)
{
	int ret;

	ret = blocking_notifier_chain_unregister(&vnode->notifier.head,
			&b52isp_video_nb);
	if (ret < 0)
		return ret;

	return 0;
}

static int b52isp_axi_link_setup(struct media_entity *entity,
			      const struct media_pad *local,
			      const struct media_pad *remote, u32 flags)
{
	struct v4l2_subdev *sd = media_entity_to_v4l2_subdev(entity);
	struct isp_subdev *isd;
	int port;
	/* TODO: MUST assign physical device to logical device!!! */
	if (sd == NULL)
		return -ENODEV;
	isd = v4l2_get_subdev_hostdata(sd);
	if (isd == NULL)
		return -ENODEV;

	switch (isd->sd_code) {
	case SDCODE_B52ISP_A1W1:
	case SDCODE_B52ISP_A2W1:
	case SDCODE_B52ISP_A3W1:
		port = B52AXI_PORT_W1;
		break;
	case SDCODE_B52ISP_A1W2:
	case SDCODE_B52ISP_A2W2:
	case SDCODE_B52ISP_A3W2:
		port = B52AXI_PORT_W2;
		break;
	case SDCODE_B52ISP_A1R1:
	case SDCODE_B52ISP_A2R1:
	case SDCODE_B52ISP_A3R1:
		port = B52AXI_PORT_R1;
		break;
	default:
		BUG_ON(1);
	}

	if (me_to_vnode(remote->entity)) {
		struct isp_vnode *vnode = me_to_vnode(remote->entity);
		/* connect to vdev */
		if (flags & MEDIA_LNK_FL_ENABLED)
			b52isp_axi_connect_video(isd, vnode);
		else
			b52isp_axi_disconnect_video(isd, vnode);
	} else {
		/* connect to pipe */
	}
	return 0;
}

static const struct media_entity_operations b52isp_axi_media_ops = {
	.link_setup = b52isp_axi_link_setup,
};

static int b52isp_axi_sd_open(struct isp_subdev *isd)
{
	return 0;
}

static void b52isp_axi_sd_close(struct isp_subdev *isd)
{
}

struct isp_subdev_ops b52isp_axi_sd_ops = {
	.open		= b52isp_axi_sd_open,
	.close		= b52isp_axi_sd_close,
};

static void b52isp_axi_remove(struct b52isp *b52isp)
{
	struct isp_build *build = NULL;
	int i;

	if (b52isp->isd[B52ISP_ISD_A1W1])
		build = b52isp->isd[B52ISP_ISD_A1W1]->build;

	for (i = 0; i < b52isp->hw_desc->nr_axi; i++) {
		struct isp_block *blk = b52isp->blk[B52ISP_BLK_AXI1 + i];
		struct b52isp_paxi *axi;

		axi = container_of(blk, struct b52isp_paxi, blk);
		tasklet_kill(&axi->tasklet);

		b52isp_detach_blk_isd(b52isp->isd[B52ISP_ISD_A1W1 + 3 * i + 0],
					blk);
		b52isp_detach_blk_isd(b52isp->isd[B52ISP_ISD_A1W1 + 3 * i + 1],
					blk);
		b52isp_detach_blk_isd(b52isp->isd[B52ISP_ISD_A1W1 + 3 * i + 2],
					blk);
		devm_kfree(b52isp->dev,
			container_of(blk, struct b52isp_paxi, blk));
		b52isp->blk[B52ISP_BLK_AXI1 + i] = NULL;
	}
	for (i = 0; i < b52isp->hw_desc->nr_axi * 3; i++) {
		struct isp_subdev *isd = b52isp->isd[B52ISP_ISD_A1W1 + i];
		plat_ispsd_unregister(isd);
		v4l2_ctrl_handler_free(&isd->ctrl_handler);
		devm_kfree(b52isp->dev,
			container_of(isd, struct b52isp_laxi, isd));
		b52isp->blk[B52ISP_ISD_A1W1 + i] = NULL;
	}
}

static int b52isp_axi_create(struct b52isp *b52isp)
{
	struct isp_build *build;
	int i, ret = 0;

	/*
	 * B52ISP AXI master is the tricky part: physically, there is only 2/3
	 * AXI master, but each AXI master has 2 write port and 1 read port.
	 * So at logical level, we expose each R/W port as a logical device,
	 * the mapping between p_dev and l_dev is 1:3. The more, the mapping
	 * relationship is dynamic, because we need to carefully select which
	 * AXI ports to use so as to balance bandwidth requirement between AXI
	 * masters and minimize power.
	 */

	for (i = 0; i < b52isp->hw_desc->nr_axi; i++) {
		struct b52isp_paxi *axi =
			devm_kzalloc(b52isp->dev, sizeof(*axi), GFP_KERNEL);
		struct isp_block *block;

		if (unlikely(axi == NULL))
			return -ENOMEM;
		block = &axi->blk;

		block->id.dev_type = PCAM_IP_B52ISP;
		block->id.dev_id = b52isp->dev->id;
		block->id.mod_id = B52ISP_BLK_AXI1 + i;
		block->dev = b52isp->dev;
		block->name = b52isp_block_name[B52ISP_BLK_AXI1 + i];
		block->req_list = b52axi_req;
		block->ops = &b52isp_axi_hw_ops;
		b52isp->blk[B52ISP_BLK_AXI1 + i] = block;
		axi->parent = b52isp;
	}
	/* isp-subdev common part setup */
	for (i = 0; i < b52isp->hw_desc->nr_axi * 3; i++) {
		struct b52isp_laxi *axi =
			devm_kzalloc(b52isp->dev, sizeof(*axi), GFP_KERNEL);
		struct isp_subdev *ispsd;
		struct v4l2_subdev *sd;

		if (axi == NULL)
			return -ENOMEM;
		/* Add ISP AXI Master isp-subdev */
		ispsd = &axi->isd;
		sd = &ispsd->subdev;
		ret = v4l2_ctrl_handler_init(&ispsd->ctrl_handler, 16);
		if (unlikely(ret < 0))
			return ret;
		sd->entity.ops = &b52isp_axi_media_ops;
		v4l2_subdev_init(sd, &b52isp_axi_subdev_ops);
		sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
		sd->ctrl_handler = &ispsd->ctrl_handler;
		strncpy(sd->name, b52isp_ispsd_name[B52ISP_ISD_A1W1 + i],
			sizeof(sd->name));
		ispsd->ops = &b52isp_axi_sd_ops;
		ispsd->pads[0].flags = MEDIA_PAD_FL_SINK;
		ispsd->pads[1].flags = MEDIA_PAD_FL_SOURCE;
		ispsd->pads_cnt = 2;
		ispsd->single = 1;
		INIT_LIST_HEAD(&ispsd->gdev_list);
		ispsd->sd_code = SDCODE_B52ISP_A1W1 + i;
		ispsd->drv_priv = axi;
		b52isp->isd[B52ISP_ISD_A1W1 + i] = ispsd;
		axi->parent = b52isp;
		axi->mac = -1;
		axi->port = -1;
	}
	for (i = 0; i < b52isp->hw_desc->nr_axi; i++) {
		struct isp_subdev *ispsd;

		ispsd = b52isp->isd[B52ISP_ISD_A1W1 + 3 * i + 0];
		ispsd->sd_type = ISD_TYPE_DMA_OUT;

		ret = plat_ispsd_register(ispsd);
		if (ret < 0)
			return ret;

		ispsd = b52isp->isd[B52ISP_ISD_A1W1 + 3 * i + 1];
		ispsd->sd_type = ISD_TYPE_DMA_OUT;
		ret = plat_ispsd_register(ispsd);
		if (ret < 0)
			return ret;

		ispsd = b52isp->isd[B52ISP_ISD_A1W1 + 3 * i + 2];
		ispsd->sd_type = ISD_TYPE_DMA_IN;
		ret = b52isp_attach_blk_isd(ispsd,
				b52isp->blk[B52ISP_BLK_AXI1 + i]);
		if (unlikely(ret < 0))
			return ret;
		ret = plat_ispsd_register(ispsd);
		if (ret < 0)
			return ret;
		ret = b52isp_detach_blk_isd(ispsd,
				b52isp->blk[B52ISP_BLK_AXI1 + i]);
		if (unlikely(ret < 0))
			return ret;
		build = ispsd->build;
	}
	/* after register to platform camera manager, can use SoCISP functions*/
	for (i = 0; i < b52isp->hw_desc->nr_axi; i++) {
		struct isp_block *blk = b52isp->blk[B52ISP_BLK_AXI1 + i];
		struct b52isp_paxi *axi;

		if (blk == NULL)
			continue;
		axi = container_of(blk, struct b52isp_paxi, blk);
		/* register irq events used to broadcase irq to ports */
		ATOMIC_INIT_NOTIFIER_HEAD(&axi->irq_w1.head);
		ATOMIC_INIT_NOTIFIER_HEAD(&axi->irq_w2.head);
		ATOMIC_INIT_NOTIFIER_HEAD(&axi->irq_r1.head);
		axi->id = i;
		spin_lock_init(&axi->lock);
		tasklet_init(&axi->tasklet, b52isp_tasklet, (unsigned long)axi);
	}
	return 0;
}

/***************************** The B52ISP subdev *****************************/
static void b52isp_cleanup(struct b52isp *b52isp)
{
	b52isp_axi_remove(b52isp);
	b52isp_path_remove(b52isp);
	b52isp_idi_remove(b52isp);
}

#define b52_add_link(isp, src, spad, dst, dpad) \
do { \
	ret = media_entity_create_link( \
		&(isp)->isd[(src)]->subdev.entity, (spad), \
		&(isp)->isd[(dst)]->subdev.entity, (dpad), 0); \
	if (ret < 0) {\
		d_inf(1, "failed to create link[%s:%d]==>[%s:%d]", \
			(isp)->isd[(src)]->subdev.entity.name, (spad), \
			(isp)->isd[(dst)]->subdev.entity.name, (dpad)); \
		goto exit_err; \
	} \
} while (0)

static int b52isp_setup(struct b52isp *b52isp)
{
	int ret;

	b52isp->mac_map = 0;
	mutex_init(&b52isp->mac_map_lock);

	ret = b52isp_idi_create(b52isp);
	if (unlikely(ret < 0))
		goto exit_err;
	/* Number of pipeline depends on ISP version */
	ret = b52isp_path_create(b52isp);
	if (unlikely(ret < 0))
		goto exit_err;

	/* Number of AXI master depends on ISP version */
	ret = b52isp_axi_create(b52isp);
	if (unlikely(ret < 0))
		goto exit_err;

	/* First, the single pipeline links */
	b52_add_link(b52isp, B52ISP_ISD_IDI, B52PAD_IDI_PIPE1,
			B52ISP_ISD_PIPE1, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_IDI, B52PAD_IDI_DUMP1,
			B52ISP_ISD_DUMP1, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_A1R1, B52PAD_AXI_OUT,
			B52ISP_ISD_PIPE1, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A2R1, B52PAD_AXI_OUT,
			B52ISP_ISD_PIPE1, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_PIPE1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);

	b52_add_link(b52isp, B52ISP_ISD_DUMP1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);

	b52_add_link(b52isp, B52ISP_ISD_A1R1, B52PAD_AXI_OUT,
			B52ISP_ISD_MS1, B52PAD_MS_IN);
	b52_add_link(b52isp, B52ISP_ISD_A2R1, B52PAD_AXI_OUT,
			B52ISP_ISD_MS1, B52PAD_MS_IN);

	b52_add_link(b52isp, B52ISP_ISD_MS1, B52PAD_MS_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS1, B52PAD_MS_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS1, B52PAD_MS_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS1, B52PAD_MS_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);

	if (b52isp->hw_desc->nr_pipe < 2)
		return 0;
	/* Then, the double pipeline links */
	b52_add_link(b52isp, B52ISP_ISD_IDI, B52PAD_IDI_PIPE2,
			B52ISP_ISD_PIPE2, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_IDI, B52PAD_IDI_DUMP2,
			B52ISP_ISD_DUMP2, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_A3R1, B52PAD_AXI_OUT,
			B52ISP_ISD_PIPE1, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A1R1, B52PAD_AXI_OUT,
			B52ISP_ISD_PIPE2, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A2R1, B52PAD_AXI_OUT,
			B52ISP_ISD_PIPE2, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A3R1, B52PAD_AXI_OUT,
			B52ISP_ISD_PIPE2, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_PIPE1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP1, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);

	b52_add_link(b52isp, B52ISP_ISD_PIPE2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_PIPE2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);

	b52_add_link(b52isp, B52ISP_ISD_DUMP2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_DUMP2, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);

	/* Last, the logical pipeline links */
	b52_add_link(b52isp, B52ISP_ISD_IDI, B52PAD_IDI_BOTH,
			B52ISP_ISD_HS, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_IDI, B52PAD_IDI_BOTH,
			B52ISP_ISD_HDR, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_IDI, B52PAD_IDI_BOTH,
			B52ISP_ISD_3D, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_A1R1, B52PAD_AXI_OUT,
			B52ISP_ISD_HS, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A2R1, B52PAD_AXI_OUT,
			B52ISP_ISD_HS, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A3R1, B52PAD_AXI_OUT,
			B52ISP_ISD_HS, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_A1R1, B52PAD_AXI_OUT,
			B52ISP_ISD_HDR, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A2R1, B52PAD_AXI_OUT,
			B52ISP_ISD_HDR, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A3R1, B52PAD_AXI_OUT,
			B52ISP_ISD_HDR, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_A1R1, B52PAD_AXI_OUT,
			B52ISP_ISD_3D, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A2R1, B52PAD_AXI_OUT,
			B52ISP_ISD_3D, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A3R1, B52PAD_AXI_OUT,
			B52ISP_ISD_3D, B52PAD_PIPE_IN);

	b52_add_link(b52isp, B52ISP_ISD_HS, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HS, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HS, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HS, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HS, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HS, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);

	b52_add_link(b52isp, B52ISP_ISD_HDR, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HDR, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HDR, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HDR, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HDR, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_HDR, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);

	b52_add_link(b52isp, B52ISP_ISD_3D, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_3D, B52PAD_PIPE_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_3D, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_3D, B52PAD_PIPE_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_3D, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_3D, B52PAD_PIPE_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);


	b52_add_link(b52isp, B52ISP_ISD_A3R1, B52PAD_AXI_OUT,
			B52ISP_ISD_MS1, B52PAD_PIPE_IN);
	b52_add_link(b52isp, B52ISP_ISD_A1R1, B52PAD_AXI_OUT,
			B52ISP_ISD_MS2, B52PAD_MS_IN);
	b52_add_link(b52isp, B52ISP_ISD_A2R1, B52PAD_AXI_OUT,
			B52ISP_ISD_MS2, B52PAD_MS_IN);
	b52_add_link(b52isp, B52ISP_ISD_A3R1, B52PAD_AXI_OUT,
			B52ISP_ISD_MS2, B52PAD_MS_IN);

	b52_add_link(b52isp, B52ISP_ISD_MS1, B52PAD_MS_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS1, B52PAD_MS_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);

	b52_add_link(b52isp, B52ISP_ISD_MS2, B52PAD_MS_OUT,
			B52ISP_ISD_A1W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS2, B52PAD_MS_OUT,
			B52ISP_ISD_A1W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS2, B52PAD_MS_OUT,
			B52ISP_ISD_A2W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS2, B52PAD_MS_OUT,
			B52ISP_ISD_A2W2, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS2, B52PAD_MS_OUT,
			B52ISP_ISD_A3W1, B52PAD_AXI_IN);
	b52_add_link(b52isp, B52ISP_ISD_MS2, B52PAD_MS_OUT,
			B52ISP_ISD_A3W2, B52PAD_AXI_IN);
	return 0;

exit_err:
	b52isp_cleanup(b52isp);
	return ret;
}

/***************************** The B52ISP IP Core *****************************/
static void b52isp_tasklet(unsigned long data)
{
	int ret;
	u32 event;
	unsigned long msg;
	unsigned long irq_flags;
	struct b52isp_paxi *paxi = (struct b52isp_paxi *)data;

	spin_lock_irqsave(&paxi->lock, irq_flags);
	event = paxi->event;
	paxi->event = 0;
	spin_unlock_irqrestore(&paxi->lock, irq_flags);

	if (event & 0x0F) {
		msg = (paxi->id << MAC_ID_SHIFT) |
			(B52AXI_PORT_W1 << PORT_ID_SHIFT) |
			((event >> 0) & 0xF);
		ret = atomic_notifier_call_chain(&paxi->irq_w1.head,
					msg, paxi->isd[B52AXI_PORT_W1]);
		if (ret < 0)
			d_inf(3, "notifer error for w1: %d", ret);
	}

	if (event & 0xF0) {
		msg = (paxi->id << MAC_ID_SHIFT) |
			(B52AXI_PORT_W2 << PORT_ID_SHIFT) |
			((event >> 4) & 0xF);
		ret = atomic_notifier_call_chain(&paxi->irq_w2.head,
					msg, paxi->isd[B52AXI_PORT_W2]);
		if (ret < 0)
			d_inf(3, "notifer error for w2: %d", ret);
		}

	if (event & 0xF00) {
		msg = (paxi->id << MAC_ID_SHIFT) |
			(B52AXI_PORT_R1 << PORT_ID_SHIFT) |
			((event >> 8) & 0xF);
		ret = atomic_notifier_call_chain(&paxi->irq_r1.head,
					msg, paxi->isd[B52AXI_PORT_R1]);
		if (ret < 0)
			d_inf(3, "notifer error for r1: %d", ret);
	}
}


static irqreturn_t b52isp_irq_handler(int irq, void *data)
{
	int i;
	u32 event[3];
	struct b52isp *b52isp = data;
	struct b52isp_paxi *paxi;

	b52_ack_xlate_irq(event);

	for (i = 0; i < b52isp->hw_desc->nr_axi; i++) {
		if (event[i] == 0)
			continue;

		paxi = container_of(b52isp->blk[B52ISP_BLK_AXI1 + i],
							struct b52isp_paxi, blk);

		spin_lock(&paxi->lock);
		if (paxi->event & event[i])
			d_inf(2, "mac bit already set,%x:%x", paxi->event, event[i]);
		paxi->event |= event[i];
		spin_unlock(&paxi->lock);

		tasklet_hi_schedule(&paxi->tasklet);
	}

	return IRQ_HANDLED;
}

static const struct of_device_id b52isp_dt_match[] = {
	{
		.compatible = "ovt,single-pipeline ISP",
		.data = b52isp_hw_table + B52ISP_SINGLE,
	},
	{
		.compatible = "ovt,double-pipeline ISP",
		.data = b52isp_hw_table + B52ISP_V3_2_4,
	},
	{},
};
MODULE_DEVICE_TABLE(of, b52isp_dt_match);

static int b52isp_probe(struct platform_device *pdev)
{
	int count;
	struct b52isp *b52isp;
	const struct of_device_id *of_id =
				of_match_device(b52isp_dt_match, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	struct resource clk;
	struct block_id pdev_mask = {
		.dev_type = PCAM_IP_B52ISP,
		.dev_id = pdev->dev.id,
		.mod_type = 0xFF,
		.mod_id = 0xFF,
	};
	int ret, i;

	b52isp = devm_kzalloc(&pdev->dev, sizeof(struct b52isp),
				GFP_KERNEL);
	if (unlikely(b52isp == NULL)) {
		dev_err(&pdev->dev, "could not allocate memory\n");
		return -ENOMEM;
	}

	if (unlikely(of_id == NULL)) {
		dev_err(&pdev->dev, "failed to find matched device\n");
		return -ENODEV;
	}
	b52isp->hw_desc = of_id->data;
	dev_info(&pdev->dev, "Probe OVT ISP with %d pipeline, %d AXI Master\n",
		b52isp->hw_desc->nr_pipe, b52isp->hw_desc->nr_axi);

	platform_set_drvdata(pdev, b52isp);
	b52isp->dev = &pdev->dev;

	/* register all platform resource to map manager */
	for (i = 0;; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (res == NULL)
			break;
		ret = plat_resrc_register(&pdev->dev, res, "b52isp-reg",
			pdev_mask, i, NULL, NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed register mem resource %s",
				res->name);
			return ret;
		}
	}

	/* get irqs */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get irq resource");
		return -ENXIO;
	}
	ret = plat_resrc_register(&pdev->dev, res, B52ISP_IRQ_NAME,
				pdev_mask, 0,
				/* irq handler */
				&b52isp_irq_handler,
				/* irq context*/
				b52isp);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed register irq resource %s",
			res->name);
		return ret;
	}

	/* get clock(s) */
	count = of_property_count_strings(np, "clock-names");
	if (count < 1 || count > ISP_CLK_END) {
		pr_err("%s: clock count error %d\n", __func__, count);
		return -EINVAL;
	}
/* the clocks order in ISP_CLK_END need align in DTS */
	for (i = 0; i < count; i++) {
		ret = of_property_read_string_index(np, "clock-names",
					    i, &clock_name[i]);
		if (ret) {
			pr_err("%s: unable to get clock %d\n", __func__, count);
			return -ENODEV;
		}
	}

	clk.flags = ISP_RESRC_CLK;
	for (i = 0; i < ISP_CLK_END; i++) {
		clk.name = clock_name[i];
		ret = plat_resrc_register(&pdev->dev, &clk, NULL, pdev_mask,
						i, NULL, NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed register clock resource %s",
				res->name);
			return ret;
		}
	}

#if 0
	ret = of_property_read_u32(np, "lpm-qos", &b52isp_req_qos);
	if (ret)
		return ret;
	b52isp_qos_idle.name = B52ISP_DRV_NAME;
	pm_qos_add_request(&b52isp_qos_idle, PM_QOS_CPUIDLE_BLOCK,
			PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
#endif
	ret = b52isp_setup(b52isp);
	if (unlikely(ret < 0)) {
		dev_err(&pdev->dev, "failed to break down %s into isp-subdev\n",
			B52ISP_NAME);
		goto out;
	}

	return 0;

out:
#if 0
	pm_qos_remove_request(&b52isp_qos_idle);
#endif
	return ret;
}

static int b52isp_remove(struct platform_device *pdev)
{
	struct b52isp *b52isp = platform_get_drvdata(pdev);

#if 0
	pm_qos_remove_request(&b52isp_qos_idle);
#endif
	devm_kfree(b52isp->dev, b52isp);
	return 0;
}

struct platform_driver b52isp_driver = {
	.driver = {
		.name	= B52ISP_DRV_NAME,
		.of_match_table = of_match_ptr(b52isp_dt_match),
	},
	.probe	= b52isp_probe,
	.remove	= b52isp_remove,
};

module_platform_driver(b52isp_driver);

MODULE_AUTHOR("Jiaquan Su <jqsu@marvell.com>");
MODULE_DESCRIPTION("Marvell B52 ISP driver, based on soc-isp framework");
MODULE_LICENSE("GPL");
