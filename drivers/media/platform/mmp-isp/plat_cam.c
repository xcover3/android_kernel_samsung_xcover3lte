/*
 * plat_cam.c
 *
 * Marvell Camera/ISP driver - Platform level module
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

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <linux/pm_qos.h>
#include <linux/i2c.h>

#include "plat_cam.h"
#include "b52isp.h"
#include "ccicv2.h"

#define PLAT_CAM_DRV	"platform-cam"

static int trace = 2;
module_param(trace, int, 0644);
MODULE_PARM_DESC(trace,
		"how many trace do you want to see? (0-4):"
		"0 - mute "
		"1 - only actual errors"
		"2 - milestone log"
		"3 - briefing log"
		"4 - detailed log");

#ifdef CONFIG_MARVELL_MEDIA_MMU
/*
 * Get the AXI_ID that is written into MMU channel control register
 */
static inline __u32 plat_mmu_channel_id(__u8 dev_id, __u8 block_id,
				__u8 port_id, __u8 yuv_id)
{
	__u8 grp_id;
	__u8 sel_id = 0;
	switch (block_id) {
	case B52ISP_BLK_AXI1:
	case B52ISP_BLK_AXI2:
	case B52ISP_BLK_AXI3:
		grp_id = (block_id - B52ISP_BLK_AXI1 + 1);
		switch (port_id) {
		case B52AXI_PORT_W1:
			BUG_ON(yuv_id >= 3);
			sel_id = grp_id * 2;
			break;
		case B52AXI_PORT_W2:
			BUG_ON(yuv_id >= 3);
			yuv_id += 3;
			sel_id = grp_id * 2;
			break;
		case B52AXI_PORT_R1:
			BUG_ON(yuv_id >= 2);
			yuv_id += 6;
			sel_id = grp_id * 2 + 1;
			break;
		}
		break;
		/* TODO; Add support for CCIC */
	default:
		BUG_ON(1);
	}
	return (sel_id << 16) | (yuv_id << 3) | grp_id;
}

/*
 * Get the AXI_ID that is written into AXI master(ISP MAC or CCIC)
 */
static __u16 plat_axi_id(__u8 port_id, __u8 yuv_id)
{
	switch (port_id) {
	case B52AXI_PORT_W1:
		BUG_ON(yuv_id >= 3);
		break;
	case B52AXI_PORT_W2:
		BUG_ON(yuv_id >= 3);
		yuv_id += 3;
		break;
	case B52AXI_PORT_R1:
		BUG_ON(yuv_id >= 2);
		yuv_id += 6;
		break;
	}
	/* TODO; Add support for CCIC */
	return yuv_id;
}

static inline void plat_put_mmu_dev(struct plat_cam *pcam)
{
	if (unlikely(WARN_ON(pcam == NULL)))
		return;

	pcam->mmu_ref--;
	if (unlikely(WARN_ON(pcam->mmu_ref < 0)))
		return;
	if (pcam->mmu_ref == 0) {
		msc2_put_sc2(&pcam->mmu_dev);
		pcam->mmu_dev = NULL;
	}
}

static inline int plat_get_mmu_dev(struct plat_cam *pcam)
{
	struct msc2_mmu_dev *mmu;
	int ret;

	if (unlikely(pcam == NULL))
		return -EINVAL;

	if (pcam->mmu_dev == NULL) {
		BUG_ON(pcam->mmu_ref);
		ret = msc2_get_sc2(&pcam->mmu_dev, 0);
		if (ret) {
			d_inf(1, "no mmu device found");
			return -ENODEV;
		}
		mmu = pcam->mmu_dev;

		ret = mmu->ops->rbypass(mmu, 0);
		if (ret) {
			d_inf(1, "unable to enable mmu for read");
			goto put_mmu;
		}
		ret = mmu->ops->wbypass(mmu, 0);
		if (ret) {
			d_inf(1, "unable to enable mmu for write");
			goto put_mmu;
		}
	}
	pcam->mmu_ref++;
	return 0;

put_mmu:
	plat_put_mmu_dev(pcam);
	return ret;
}

static int plat_mmu_alloc_channel(struct plat_cam *pcam,
				__u8 blk_id, __u8 port_id, __u8 nr_chnl,
				struct mmu_chs_desc *ch_dsc)
{
	struct mmu_chs_desc chs_desc;
	struct msc2_mmu_dev *mmu;
	int i, ret;

	if (nr_chnl > 3) {
		d_inf(1, "channel number is error %d\n", nr_chnl);
		return -EINVAL;
	}

	chs_desc.nr_chs = nr_chnl;
	ret = plat_get_mmu_dev(pcam);
	if (ret < 0)
		goto err_get_mmu;

	mmu = pcam->mmu_dev;
	for (i = 0; i < nr_chnl; i++) {
		chs_desc.tid[i] = plat_mmu_channel_id(0, blk_id, port_id, i);
		ret = mmu->ops->acquire_ch(mmu, chs_desc.tid[i]);
		if (ret) {
			d_inf(1, "failed to alloc MMU channel for ISP");
			goto err_alloc_mmu;
		}
	}
	ret = mmu->ops->enable_ch(mmu, chs_desc.tid, chs_desc.nr_chs);
	if (ret) {
		d_inf(1, "failed to enable MMU channel for ISP");
		goto err_enable_mmu;
	}
	*ch_dsc = chs_desc;
	return 0;

err_enable_mmu:
err_alloc_mmu:
	for (i--; i >= 0; i--)
		mmu->ops->release_ch(mmu, chs_desc.tid[i]);
	plat_put_mmu_dev(pcam);
err_get_mmu:
	return ret;
}

static void plat_mmu_free_channel(struct plat_cam *pcam,
				struct mmu_chs_desc *ch_dsc)
{
	struct msc2_mmu_dev *mmu;
	int i;

	if (unlikely(WARN_ON(!pcam->mmu_dev)))
		return;

	mmu = pcam->mmu_dev;
	mmu->ops->disable_ch(mmu, ch_dsc->tid, ch_dsc->nr_chs);
	for (i = 0; i < ch_dsc->nr_chs; i++)
		mmu->ops->release_ch(mmu, ch_dsc->tid[i]);
	plat_put_mmu_dev(pcam);
	ch_dsc->nr_chs = 0;
	memset(ch_dsc->tid, 0, sizeof(ch_dsc->tid));
}

static int plat_mmu_fill_channel(struct plat_cam *pcam,
				struct vb2_buffer *vb, int num_planes)
{
	struct isp_videobuf *buf = container_of(vb, struct isp_videobuf, vb);
	struct isp_vnode *vnode = container_of(vb->vb2_queue,
						struct isp_vnode, vq);
	int ch;

	if (unlikely((!pcam->mmu_dev || !vb || !buf))) {
		pr_err("%s: paramter error\n", __func__);
		return -ENODEV;
	}

	for (ch = 0; ch < num_planes; ch++) {
		struct msc2_ch_info *info = &buf->ch_info[ch];
		info->tid = vnode->mmu_ch_dsc.tid[ch];
	}
	return pcam->mmu_dev->ops->config_ch(pcam->mmu_dev, buf->ch_info,
						num_planes);
}
#endif /* #ifdef CONFIG_MARVELL_MEDIA_MMU */

__u32 plat_get_src_tag(struct plat_pipeline *ppl)
{
	if (unlikely(ppl == NULL))
		return -EINVAL;

	/* Get source lag_tag */
	if (ppl->src_type == PLAT_SRC_T_SNSR) {
		struct v4l2_subdev *sensor = ppl->src.sensor;
		return (sensor->entity.id + 1) << 16;
	} else {
		struct isp_vnode *vnode = ppl->src.vnode;
		return (vnode->vdev.entity.id + 1) << 16;
	}
}

int plat_vdev_get_pipeline(struct isp_vnode *vnode,
				struct plat_pipeline *ppl)
{
	struct media_pad *pad;
	struct isp_subdev *isd;
	int i, ret = 0, id, out;

	if (unlikely(vnode == NULL || ppl == NULL))
		return -EINVAL;

	/* mutex_lock(&vnode->vdev.entity.parent->graph_mutex); */

	if (vnode->buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		/* output vdev */

		/* get post-scalar subdev */
		pad = media_entity_remote_pad(&vnode->pad);
		if (WARN_ON(pad == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		isd = v4l2_get_subdev_hostdata(
				media_entity_to_v4l2_subdev(pad->entity));
		if (WARN_ON(isd == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}

		/* get path subdev */
		pad = media_entity_remote_pad(isd->pads + B52PAD_AXI_IN);
		if (WARN_ON(pad == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		isd = v4l2_get_subdev_hostdata(
				media_entity_to_v4l2_subdev(pad->entity));
		if (WARN_ON(isd == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		ppl->path = isd;

		/* get pre-scalar subdev */
		pad = media_entity_remote_pad(isd->pads + B52PAD_PIPE_IN);
		if (WARN_ON(pad == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		isd = v4l2_get_subdev_hostdata(
				media_entity_to_v4l2_subdev(pad->entity));
		if (WARN_ON(isd == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		ppl->scalar_a = isd;

		switch (isd->sd_code) {
		/* In case of Memory-To-Memory process */
		case SDCODE_B52ISP_A1R1:
		case SDCODE_B52ISP_A2R1:
		case SDCODE_B52ISP_A3R1:
			ppl->crop_a = isd->crop_pad + B52PAD_AXI_OUT;
			ppl->src_type = PLAT_SRC_T_VDEV;
			pad = media_entity_remote_pad(
				isd->pads + B52PAD_AXI_IN);
			if (WARN_ON(pad == NULL)) {
				ret = -EPIPE;
				goto unlock;
			}
			ppl->src.vnode = me_to_vnode(pad->entity);
			if (WARN_ON(ppl->src.vnode == NULL)) {
				ret = -EPIPE;
				goto unlock;
			}
			break;
		/* In case of Sensor-To-Memory process */
		case SDCODE_B52ISP_IDI:
			ppl->src_type = PLAT_SRC_T_SNSR;
			/* FIXME: can only handle input1 to pipeline1 case */
			ppl->crop_a = isd->crop_pad + B52PAD_IDI_PIPE1;

			/* get CCIC-CTRL */
			pad = media_entity_remote_pad(
				isd->pads + B52PAD_IDI_IN1);
			if (WARN_ON(pad == NULL)) {
				ret = -EPIPE;
				goto unlock;
			}
			isd = v4l2_get_subdev_hostdata(
				media_entity_to_v4l2_subdev(pad->entity));
			WARN_ON((SDCODE_CCICV2_CSI0 != isd->sd_code) &&
				(SDCODE_CCICV2_CSI1 != isd->sd_code));

			/* get sensor */
			pad = media_entity_remote_pad(
				isd->pads + CCIC_CSI_PAD_IN);
			if (WARN_ON(pad == NULL)) {
				ret = -EPIPE;
				goto unlock;
			}
			ppl->src.sensor =
				media_entity_to_v4l2_subdev(pad->entity);
			if (WARN_ON(ppl->src.sensor == NULL)) {
				ret = -EPIPE;
				goto unlock;
			}
			if (WARN_ON(pad->entity->type !=
				MEDIA_ENT_T_V4L2_SUBDEV_SENSOR)) {
				ret = -EPIPE;
				goto unlock;
			}
			break;
		default:
			WARN_ON(1);
			ret = -EPIPE;
			goto unlock;
		}
	} else {
		/* input vdev*/
		ppl->src_type = PLAT_SRC_T_VDEV;
		ppl->src.vnode = vnode;

		/* get input AXI subdev */
		pad = media_entity_remote_pad(&vnode->pad);
		if (WARN_ON(pad == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		isd = v4l2_get_subdev_hostdata(
				media_entity_to_v4l2_subdev(pad->entity));
		if (WARN_ON(isd == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		ppl->scalar_a = isd;
		ppl->crop_a = isd->crop_pad + B52PAD_AXI_OUT;

		/* get path subdev */
		pad = media_entity_remote_pad(isd->pads + B52PAD_AXI_OUT);
		if (WARN_ON(pad == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		isd = v4l2_get_subdev_hostdata(
				media_entity_to_v4l2_subdev(pad->entity));
		if (WARN_ON(isd == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		ppl->path = isd;
	}

	ppl->crop_b = ppl->path->crop_pad + B52PAD_PIPE_OUT;
	ppl->dst_map = 0;
	out = -1;
	for (i = 0; i < MAX_OUTPUT_PER_PIPELINE; i++) {
		ppl->scalar_b[i] = NULL;
		ppl->dst[i] = NULL;
	}

	/* Figure out outputs */
	for (i = 0, id = 0; i < ppl->path->subdev.entity.num_links; i++) {
		struct media_link *link = &ppl->path->subdev.entity.links[i];
		struct isp_vnode *dst;

		if (link->source != ppl->path->pads + B52PAD_PIPE_OUT)
			continue;
		out++;
		if ((link->flags & MEDIA_LNK_FL_ENABLED) == 0)
			continue;

		/* Find linked AXI */
		isd = v4l2_get_subdev_hostdata(
			media_entity_to_v4l2_subdev(link->sink->entity));
		if (WARN_ON(isd == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		ppl->scalar_b[id] = isd;

		/* Find output vnode */
		pad = media_entity_remote_pad(isd->pads + B52PAD_AXI_OUT);
		if (WARN_ON(pad == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		dst = me_to_vnode(pad->entity);
		if (WARN_ON(dst == NULL)) {
			ret = -EPIPE;
			goto unlock;
		}
		if (WARN_ON(dst->buf_type !=
			V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)) {
			ret = -EPIPE;
			goto unlock;
		}

		ppl->dst_map |= BIT(out);
		ppl->dst[id] = dst;
		if (dst == vnode)
			ret = id;
		id++;
	}

unlock:
	/* mutex_unlock(&vnode->vdev.entity.parent->graph_mutex); */
	if ((ppl->src_type == PLAT_SRC_T_VDEV) && (vnode == ppl->src.vnode))
		ret = MAX_OUTPUT_PER_PIPELINE;
	return ret;
}

static int plat_add_vdev(struct isp_build *build, struct isp_subdev *ispsd)
{
	struct plat_vnode *pvnode;
	struct plat_cam *pcam = build->plat_priv;
	int i, ret = 0;

	if (ispsd->single == 0)
		return 0;

	switch (ispsd->sd_type) {
	case ISD_TYPE_DMA_OUT:
		/* find output pad in i*/
		for (i = 0; i < ispsd->subdev.entity.num_pads; i++) {
			if (ispsd->subdev.entity.pads[i].flags
				& MEDIA_PAD_FL_SOURCE)
				goto attach_output;
		}
		d_inf(1, "dma output subdev %s don't have a output pad?",
			ispsd->subdev.name);
		return -EPERM;
attach_output:
		pvnode = kzalloc(sizeof(struct plat_vnode), GFP_KERNEL);
		if (pvnode == NULL)
			return -ENOMEM;
		snprintf(pvnode->vnode.vdev.name,
			sizeof(pvnode->vnode.vdev.name),
			"vout<%s>", ispsd->subdev.name);
		ret = isp_vnode_add(&pvnode->vnode, &ispsd->build->v4l2_dev,
					0, -1);
		if (ret < 0)
			return ret;
		/* create link between subdev and associated video_device */
		ret = media_entity_create_link(&ispsd->subdev.entity, i,
			&pvnode->vnode.vdev.entity, 0, 0);
		if (ret < 0)
			return ret;
		break;

	case ISD_TYPE_DMA_IN:
		/* find input pad in i*/
		for (i = 0; i < ispsd->subdev.entity.num_pads; i++) {
			if (ispsd->subdev.entity.pads[i].flags
				& MEDIA_PAD_FL_SINK)
				goto attach_input;
		}
		d_inf(1, "dma input subdev %s don't have a input pad?",
			ispsd->subdev.name);
		return -EPERM;
attach_input:
		pvnode = kzalloc(sizeof(struct plat_vnode), GFP_KERNEL);
		if (pvnode == NULL)
			return -ENOMEM;
		snprintf(pvnode->vnode.vdev.name,
			sizeof(pvnode->vnode.vdev.name),
			"vin<%s>", ispsd->subdev.name);
		ret = isp_vnode_add(&pvnode->vnode, &ispsd->build->v4l2_dev,
					1, -1);
		if (ret < 0)
			return ret;
		/* create link between subdev and associated video_device */
		ret = media_entity_create_link(&pvnode->vnode.vdev.entity, 0,
			&ispsd->subdev.entity, i, 0);
		if (ret < 0)
			return ret;
		break;
	default:
		return 0;
	}
	INIT_LIST_HEAD(&pvnode->hook);
	list_add_tail(&pvnode->hook, &pcam->vnode_pool);
	return ret;
}

static void plat_close_vdev(struct isp_build *build)
{
	struct plat_cam *pcam = build->plat_priv;
	struct plat_vnode *pvnode;

	list_for_each_entry(pvnode, &pcam->vnode_pool, hook) {
		int ret;
		if (pvnode->vnode.file == NULL)
			continue;
		ret = pvnode->vnode.vdev.fops->release(pvnode->vnode.file);
		d_inf(1, "force release of vdev %s, ret %d",
			pvnode->vnode.vdev.name, ret);
	}
}

#define pcam_add_link(src, spad, dst, dpad) \
do { \
	ret = media_entity_create_link((src), (spad), (dst), (dpad), 0); \
	if (ret < 0) \
		return ret; \
} while (0)

static int pcam_setup_links(struct isp_build *build,
				struct v4l2_subdev **sensor_sd)
{
	struct isp_subdev *sd[SDCODE_CNT], *tmp;
	int ret;

	memset(sd, 0, sizeof(sd));
	list_for_each_entry(tmp, &build->ispsd_list, hook) {
		BUG_ON(tmp->sd_code >= SDCODE_CNT);
		sd[tmp->sd_code] = tmp;
	}

	if (sd[SDCODE_B52ISP_IDI] == NULL) {
		d_inf(1, "b52isp not found in %s", build->name);
		return -ENODEV;
	}

	if (!sd[SDCODE_CCICV2_CSI0] || !sd[SDCODE_CCICV2_CSI1])
		return 0;

	/* CCIC #0: CSI=>DMA */
	pcam_add_link(&sd[SDCODE_CCICV2_CSI0]->subdev.entity,
			CCIC_CSI_PAD_LOCAL,
			&sd[SDCODE_CCICV2_DMA0]->subdev.entity,
			CCIC_DMA_PAD_IN);
	/* CCIC #1: CSI=>DMA */
	pcam_add_link(&sd[SDCODE_CCICV2_CSI1]->subdev.entity,
			CCIC_CSI_PAD_LOCAL,
			&sd[SDCODE_CCICV2_DMA1]->subdev.entity,
			CCIC_DMA_PAD_IN);
	/* CCIC: CSI #0 => DMA #1 */
	pcam_add_link(&sd[SDCODE_CCICV2_CSI0]->subdev.entity,
			CCIC_CSI_PAD_XFEED,
			&sd[SDCODE_CCICV2_DMA1]->subdev.entity,
			CCIC_DMA_PAD_IN);
	/* CCIC: CSI #1 => DMA #0 */
	pcam_add_link(&sd[SDCODE_CCICV2_CSI1]->subdev.entity,
			CCIC_CSI_PAD_XFEED,
			&sd[SDCODE_CCICV2_DMA0]->subdev.entity,
			CCIC_DMA_PAD_IN);

	/* CCIC: CSI #1 => ISP */
	pcam_add_link(&sd[SDCODE_CCICV2_CSI0]->subdev.entity,
			CCIC_CSI_PAD_ISP,
			&sd[SDCODE_B52ISP_IDI]->subdev.entity,
			B52PAD_IDI_IN1);
	/* CCIC: CSI #0 => ISP */
	pcam_add_link(&sd[SDCODE_CCICV2_CSI1]->subdev.entity,
			CCIC_CSI_PAD_ISP,
			&sd[SDCODE_B52ISP_IDI]->subdev.entity,
			B52PAD_IDI_IN2);

	if (sensor_sd[0])
		pcam_add_link(&sensor_sd[0]->entity, 0,
			&sd[SDCODE_CCICV2_CSI0]->subdev.entity,
			CCIC_CSI_PAD_IN);

	if (sensor_sd[1])
		pcam_add_link(&sensor_sd[1]->entity, 0,
			&sd[SDCODE_CCICV2_CSI1]->subdev.entity,
			CCIC_CSI_PAD_IN);

	return 0;
}

static struct isp_build plat_cam = {
	.resrc_pool	= LIST_HEAD_INIT(plat_cam.resrc_pool),
	.ispsd_list	= LIST_HEAD_INIT(plat_cam.ispsd_list),
	.event_pool	= LIST_HEAD_INIT(plat_cam.event_pool),
	.name		= PLAT_CAM_DRV,
};

int plat_ispsd_register(struct isp_subdev *ispsd)
{
	return isp_subdev_register(ispsd, &plat_cam);
}
EXPORT_SYMBOL(plat_ispsd_register);

void plat_ispsd_unregister(struct isp_subdev *ispsd)
{
	isp_subdev_unregister(ispsd);
}
EXPORT_SYMBOL(plat_ispsd_unregister);

int plat_resrc_register(struct device *dev, struct resource *res,
	const char *name, struct block_id mask,
	int res_id, void *handle, void *priv)
{
	if (isp_resrc_register(dev, res, &plat_cam.resrc_pool, name, mask,
				 res_id, handle, priv) == NULL)
		return -ENOMEM;
	else
		return 0;
}
EXPORT_SYMBOL(plat_resrc_register);

static struct v4l2_subdev *b52_detect_sensor(
		struct isp_build *isb, char *name)
{
	int ret;
	u32 nr;
	char const *s;
	struct i2c_board_info info;
	struct v4l2_subdev *subdev;
	struct i2c_adapter *adapter;
	struct device_node *subdev_np = NULL, *sensor_np = NULL;
	sensor_np = of_get_child_by_name(isb->dev->of_node, name);
	if (sensor_np == NULL)
		return NULL;
	memset(&info, 0, sizeof(info));
	do {
		subdev_np = of_get_next_available_child(sensor_np,
					subdev_np);
		if (subdev_np == NULL) {
			pr_err("%s No sensor need to be registered\n",
				__func__);
			return NULL;
		}
		ret = of_property_read_string(subdev_np,
					"compatible", &s);
		if (ret < 0) {
			pr_err("%s Unable to get sensor full name\n", __func__);
			return NULL;
		}
		strcpy(info.type, s);
		ret = of_property_read_u32(subdev_np,
				"adapter", &nr);
		if (ret < 0) {
			pr_err("%s Unable to get I2C bus number\n", __func__);
			return NULL;
		}
		adapter = i2c_get_adapter(nr);
		if (adapter == NULL) {
			pr_err("%s:Unable to get I2C adapter %d for device %s\n",
				__func__,
				nr,
				info.type);
			return NULL;
		}
		ret = of_property_read_u32(subdev_np,
				"reg", (u32 *)&(info.addr));
		if (ret < 0) {
			pr_err("%s Unable to get I2C address\n", __func__);
			return NULL;
		}
		info.of_node = subdev_np;
		subdev = v4l2_i2c_new_subdev_board(&isb->v4l2_dev,
						adapter,
						&info, NULL);
		if (subdev != NULL)
			return subdev;
	} while (subdev_np != NULL);
	return NULL;
}

static int plat_tune_power(struct isp_build *isb,
		enum plat_subdev_code code, int enable)
{
	struct isp_subdev *isd;
	struct isp_block *blk;

	list_for_each_entry(isd, &isb->ispsd_list, hook)
		if (isd->sd_code == code)
			break;

	if (&isd->hook == &isb->ispsd_list ||
			isd->sd_code != code)
		return -ENODEV;

	blk = isp_sd2blk(isd);
	if (!blk)
		return -EINVAL;

	isp_block_tune_power(blk, enable);

	return 0;
}

static int plat_setup_sensor(struct isp_build *isb,
		struct v4l2_subdev **sensor_sd)
{
	int ret;

	ret = plat_tune_power(isb, SDCODE_B52ISP_IDI, 1);
	ret |= plat_tune_power(isb, SDCODE_CCICV2_CSI0, 1);
	ret |= plat_tune_power(isb, SDCODE_CCICV2_CSI1, 1);
	if (ret < 0) {
		pr_err("%s: tune power failed\n", __func__);
		return ret;
	}

	sensor_sd[0] = b52_detect_sensor(isb, "backsensor");
	if (!(sensor_sd[0]))
		pr_info("plat detect back sensor failed\n");
	sensor_sd[1] = b52_detect_sensor(isb, "frontsensor");
	if (!(sensor_sd[1]))
		pr_info("detect front sensor failed\n");

	ret = plat_tune_power(isb, SDCODE_CCICV2_CSI1, 0);
	ret |= plat_tune_power(isb, SDCODE_CCICV2_CSI0, 0);
	ret |= plat_tune_power(isb, SDCODE_B52ISP_IDI, 0);

	return ret;
}
static int plat_cam_remove(struct platform_device *pdev)
{
	struct plat_cam *cam = platform_get_drvdata(pdev);

	kfree(cam);
	isp_block_pool_clean(&plat_cam.resrc_pool);

	return 0;
}

static int plat_cam_probe(struct platform_device *pdev)
{
	struct plat_cam *cam;
	struct v4l2_subdev *sensor_sd[2];
	int ret;

	/* by this time, suppose all agents are registered */
	cam = kzalloc(sizeof(*cam), GFP_KERNEL);
	if (!cam) {
		dev_err(&pdev->dev, "could not allocate memory\n");
		return -ENOMEM;
	}

	cam->isb = &plat_cam;
	cam->isb->dev = &pdev->dev;

	platform_set_drvdata(pdev, cam);

	cam->isb->name = PLAT_CAM_DRV;
	cam->isb->plat_priv = cam;
	cam->isb->add_vdev = plat_add_vdev;
	cam->isb->close_vdev = plat_close_vdev;
	INIT_LIST_HEAD(&cam->vnode_pool);
#ifdef CONFIG_MARVELL_MEDIA_MMU
	cam->alloc_mmu_chnl = &plat_mmu_alloc_channel;
	cam->free_mmu_chnl = &plat_mmu_free_channel;
	cam->fill_mmu_chnl = &plat_mmu_fill_channel;
	cam->get_axi_id = &plat_axi_id;
#endif
	ret = isp_build_init(cam->isb);
	if (ret < 0)
		return ret;

	memset(sensor_sd, 0, sizeof(sensor_sd));
	plat_setup_sensor(cam->isb, sensor_sd);

	/* Create all the file nodes for each subdev */
	ret = isp_build_attach_ispsd(cam->isb);
	if (ret < 0)
		return ret;

	/* Setup the link between entities, this is totally platform specific */
	ret = pcam_setup_links(cam->isb, sensor_sd);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct of_device_id plat_cam_dt_match[] = {
	{ .compatible = "marvell,platform-cam", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, plat_isp_dt_match);

static struct platform_driver plat_cam_driver = {
	.probe = plat_cam_probe,
	.remove = plat_cam_remove,
	.driver = {
		.owner	= THIS_MODULE,
		.name	= PLAT_CAM_DRV,
		.of_match_table = of_match_ptr(plat_cam_dt_match)
	},
};

module_platform_driver(plat_cam_driver);

MODULE_AUTHOR("Jiaquan Su <jqsu@marvell.com>");
MODULE_DESCRIPTION("Marvell ISP/Camera Platform Level Driver");
MODULE_LICENSE("GPL");
