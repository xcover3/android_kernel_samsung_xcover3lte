/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/driver.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/b52-sensor.h>
#include <linux/leds.h>
#include <linux/math64.h>

#ifdef CONFIG_ISP_USE_TWSI3
#include <linux/i2c.h>
int twsi_read_i2c_bb(u16 addr, u8 reg, u8 *val)
{
	int ret;
	struct i2c_adapter *adapter;
	struct i2c_msg msg[] = {
		{
			.addr	= addr,
			.flags	= 0,
			.len	= 1,
			.buf	= (u8 *)&reg,
		},
		{
			.addr	= addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= val,
		},
	};
	*val = 0;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	ret = i2c_transfer(adapter, msg, 2);
	return 0;
}
int twsi_read_i2c(u16 addr, u16 reg, u16 *val)
{
	int ret;
	struct i2c_adapter *adapter;
	struct i2c_msg msg[] = {
		{
			.addr	= addr,
			.flags	= 0,
			.len	= 2,
			.buf	= (u8 *)&reg,
		},
		{
			.addr	= addr,
			.flags	= I2C_M_RD,
			.len	= 1,
			.buf	= (u8 *)val,
		},
	};
	*val = 0;
	reg = swab16(reg);
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	ret = i2c_transfer(adapter, msg, 2);
	return 0;
}
struct reg_tab_wb {
		u16 reg;
		u8 val;
} __packed;
struct reg_tab_bb {
		u8 reg;
		u8 val;
} __packed;

int twsi_write_i2c(u16 addr, u16 reg, u8 val)
{
	struct i2c_msg msg;
	struct reg_tab_wb buf;
	int ret = 0;
	struct i2c_adapter *adapter;
	reg = swab16(reg);
	buf.reg = reg;
	buf.val = val;
	msg.addr	= addr;
	msg.flags	= 0;
	msg.len		= 3;
	msg.buf		= (u8 *)&buf;
	adapter = i2c_get_adapter(2);
	ret = i2c_transfer(adapter, &msg, 1);
	if (ret < 0)
		return ret;
	return 0;
}
#define DM9714L_DATA_SHIFT 0x4
static int b52_sensor_g_focus_twsi(struct v4l2_subdev *sd, u16 *val)
{
	int ret = 0;
	u8 reg;
	u8 val1;
	struct i2c_adapter *adapter;
	struct b52_sensor_vcm *vcm;
	enum b52_sensor_vcm_type vcm_type;
	struct i2c_msg msg = {
			.flags	= I2C_M_RD,
			.len	= 2,
			.buf	= (u8 *)val,
	};
	struct b52_sensor *sensor = to_b52_sensor(sd);

	vcm = sensor->drvdata->module->vcm;
	vcm_type =  vcm->type;
	msg.addr = vcm->attr->addr;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	*val = 0;
	switch (vcm_type) {
	case DW9714:
		ret = i2c_transfer(adapter, &msg, 1);
		*val = (*val >> DM9714L_DATA_SHIFT) & 0xffff;
		break;
	case DW9804:
		reg = (u8)vcm->pos_reg_lsb;
		ret = twsi_read_i2c_bb(msg.addr, reg, (u8 *)val);
		*val = *val & 0xff;
		reg = (u8)vcm->pos_reg_msb;
		ret = twsi_read_i2c_bb(msg.addr, reg, &val1);
		*val = ((val1 & 0x3) << 8) | *val;
		break;
	case DW9718:
		reg = (u8)vcm->pos_reg_lsb;
		ret = twsi_read_i2c_bb(msg.addr, reg, (u8 *)val);
		*val = *val & 0xff;
		reg = (u8)vcm->pos_reg_msb;
		ret = twsi_read_i2c_bb(msg.addr, reg, &val1);
		*val = ((val1 & 0x3) << 8) | *val;
		break;
	default:
		ret = -EIO;
		pr_err("not support current vcm type\n");
	}


	return ret;
}
static int b52_sensor_s_focus_twsi(struct v4l2_subdev *sd, u16 val)
{
	int ret = 0;
	struct i2c_adapter *adapter;
	struct b52_sensor_vcm *vcm;
	struct reg_tab_bb buf;
	enum b52_sensor_vcm_type vcm_type;
	u8 val_buf[2];
	struct i2c_msg msg;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (sensor->drvdata->num_module == 0)
		return 0;

	vcm = sensor->drvdata->module->vcm;
	vcm_type =  vcm->type;
	msg.addr = vcm->attr->addr;
	msg.flags = 0;
	msg.len = 2;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;

	switch (vcm_type) {
	case DW9714:
		msg.buf = val_buf;
		val = (val << 4) | 0x0f;
		msg.buf[0] = (val>>8) & 0x3f;
		msg.buf[1] = val & 0xff;
		ret = i2c_transfer(adapter, &msg, 1);
		break;
	case DW9804:
		msg.buf = (u8 *)&buf;
		buf.reg = (u8)vcm->pos_reg_msb;
		buf.val = (u8)((val >> 8) & 0x3);
		ret = i2c_transfer(adapter, &msg, 1);
		buf.reg = (u8)vcm->pos_reg_lsb;
		buf.val = (u8)(val & 0xff);
		ret |= i2c_transfer(adapter, &msg, 1);
		break;
	case DW9718:
		msg.buf = (u8 *)&buf;
		buf.reg = (u8)vcm->pos_reg_msb;
		buf.val = (u8)((val >> 8) & 0x3);
		ret = i2c_transfer(adapter, &msg, 1);
		buf.reg = (u8)vcm->pos_reg_lsb;
		buf.val = (u8)(val & 0xff);
		ret |= i2c_transfer(adapter, &msg, 1);
		break;
	default:
		ret = 0;
		pr_err("not support current vcm type\n");
	}
	return ret;
}

static int b52_sensor_s_expo(struct v4l2_subdev *sd, u32 expo, u16 vts)
{
	const struct b52_sensor_regs *reg;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (sensor->drvdata->type == SONY_SENSOR)
		expo = (expo >> 4) & 0xFFFF;

	reg = &sensor->drvdata->vts_reg;
	b52_sensor_call(sensor, i2c_write, reg->tab[0].reg, vts, reg->num);

	reg = &sensor->drvdata->expo_reg;
	b52_sensor_call(sensor, i2c_write, reg->tab[0].reg, expo, reg->num);

	return 0;
}

static int b52_sensor_s_gain(struct v4l2_subdev *sd, u32 gain)
{
	int max;
	u32 ag, dg;
	const struct b52_sensor_regs *reg;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	reg = &sensor->drvdata->gain_reg[B52_SENSOR_AG];
	max = sensor->drvdata->gain_range[B52_SENSOR_AG].max;

	ag = (gain <= max) ? gain : max;
	ag <<= sensor->drvdata->gain_shift;
	if (sensor->drvdata->type == SONY_SENSOR)
		ag = (256*16 - 256*256/ag + 8)/16;
	b52_sensor_call(sensor, i2c_write, reg->tab[0].reg, ag, reg->num);

	reg = &sensor->drvdata->gain_reg[B52_SENSOR_DG];
	if (reg->tab == NULL) {
		if (gain > max)
			pr_err("gain value is incorrect: %d\n", gain);
		return 0;
	}

	if (gain <= max)
		dg = B52_GAIN_UNIT << 4;
	else
		dg = (gain << 8) / max;
	dg <<= sensor->drvdata->gain_shift;
	if (sensor->drvdata->type != SONY_SENSOR)
		dg = (dg + 8) >> 4;
	b52_sensor_call(sensor, i2c_write, reg->tab[0].reg, dg, reg->num);

	return 0;
}

static int b52_sensor_cmd_read_twsi(struct v4l2_subdev *sd, u16 addr,
		u32 *val, u8 num)
{
	int i;
	int shift = 0;
	struct regval_tab tab[3];
	struct b52_cmd_i2c_data data;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!sensor || !val || (num == 0)) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	if (num > 3) {
		pr_err("%s, read num too long\n", __func__);
		return -EINVAL;
	}

	data.attr = &sensor->drvdata->i2c_attr;
	data.tab = tab;
	data.num = num;
	data.pos = sensor->pos;
	for (i = 0; i < data.num; i++)
		twsi_read_i2c(data.attr->addr, addr + i, &data.tab[i].val);

	if (num == 1) {
		*val = tab[0].val;
	} else if (num == 2) {
		if (data.attr->val_len == I2C_8BIT)
			shift = 8;
		else if (data.attr->val_len == I2C_16BIT)
			shift = 16;
		*val = (tab[0].val << shift) | tab[1].val;
	} else if (num == 3) {
		if (data.attr->val_len == I2C_8BIT)
			shift = 8;
		else {
			pr_err("wrong i2c len for num %d\n", num);
			return -EINVAL;
		}

		*val = (tab[0].val << shift * 2) |
			   (tab[1].val << shift * 1) |
			   (tab[2].val << shift * 0);
	}

	return 0;
}

static int b52_sensor_cmd_write_twsi(struct v4l2_subdev *sd, u16 addr,
		u32 val, u8 num)
{
	int shift = 0, i;
	struct regval_tab tab[3];
	const struct b52_sensor_i2c_attr *attr;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!sensor || num == 0) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	attr = &sensor->drvdata->i2c_attr;

	switch (num) {
	case 1:
		tab[0].reg = addr;
		tab[0].val = val;
		break;
	case 2:
		if (attr->val_len == I2C_8BIT)
			shift = 8;
		else if (attr->val_len == I2C_16BIT)
			shift = 16;

		tab[0].reg = addr;
		tab[0].val = (val >> shift) & ((1 << shift) - 1);

		tab[1].reg = addr + 1;
		tab[1].val = val & ((1 << shift) - 1);
		break;
	case 3:
		if (attr->val_len == I2C_8BIT)
			shift = 8;
		else {
			pr_err("wrong i2c len for num %d\n", num);
			return -EINVAL;
		}

		tab[0].reg = addr;
		tab[0].val = (val >> shift * 2) & ((1 << shift) - 1);

		tab[1].reg = addr + 1;
		tab[1].val = (val >> shift * 1) & ((1 << shift) - 1);

		tab[2].reg = addr + 2;
		tab[2].val = (val >> shift * 0) & ((1 << shift) - 1);
		break;
	default:
		pr_err("%s, write num no correct\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < num; i++)
		twsi_write_i2c(attr->addr, tab[i].reg, tab[i].val);

	return 0;
}
static int b52_sensor_g_cur_fmt_twsi(struct v4l2_subdev *sd,
	struct b52_cmd_i2c_data *data)
{
	int i;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	if (!data) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sensor->lock);

	data->attr = &sensor->drvdata->i2c_attr;
	data->tab  = sensor->mf_regs.tab;
/*not write sensor setting in set_format command*/
/*	data->num  = sensor->mf_regs.num; */
	data->num  = 0;
	data->pos  = sensor->pos;
	for (i = 0; i < data->num; i++)
		twsi_write_i2c(data->attr->addr,
				data->tab[i].reg,
				data->tab[i].val);
	mutex_unlock(&sensor->lock);

	return 0;
}

static int __b52_sensor_cmd_write(const struct b52_sensor_i2c_attr
		*i2c_attr, const struct b52_sensor_regs *regs, u8 pos)
{
	struct b52_cmd_i2c_data data;
	int i;

	if (!i2c_attr || !regs) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	if (regs->num == 0)
		return 0;

	data.attr = i2c_attr;
	data.tab  = regs->tab;
	data.num  = regs->num;
	data.pos  = pos;
	for (i = 0; i < data.num; i++)
		twsi_write_i2c(data.attr->addr,
				data.tab[i].reg,
				data.tab[i].val);
	/*FIXME:how to get completion*/
	return 0;
}
#else
static int __b52_sensor_cmd_write(const struct b52_sensor_i2c_attr
		*i2c_attr, const struct b52_sensor_regs *regs, u8 pos)
{
	struct b52_cmd_i2c_data data;

	if (!i2c_attr || !regs) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	if (regs->num == 0)
		return 0;

	data.attr = i2c_attr;
	data.tab  = regs->tab;
	data.num  = regs->num;
	data.pos  = pos;

	/*FIXME:how to get completion*/
	return b52_cmd_write_i2c(&data);
}
static int b52_sensor_cmd_write(struct v4l2_subdev *sd, u16 addr,
		u32 val, u8 num)
{
	int shift = 0;
	struct regval_tab tab[3];
	struct b52_sensor_regs regs;
	const struct b52_sensor_i2c_attr *attr;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!sensor || num == 0) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	attr = &sensor->drvdata->i2c_attr;
	if (num > 3 || (num == 3 && attr->val_len == I2C_16BIT)) {
		pr_err("%s, write num %d too long\n", __func__, num);
		return -EINVAL;
	}

	if (attr->val_len == I2C_8BIT)
		shift = 8;
	else if (attr->val_len == I2C_16BIT)
		shift = 16;

	switch (num) {
	case 1:
		tab[0].reg = addr;
		tab[0].val = val;
		break;
	case 2:
		tab[0].reg = addr;
		tab[0].val = (val >> shift) & ((1 << shift) - 1);

		tab[1].reg = addr + 1;
		tab[1].val = val & ((1 << shift) - 1);
		break;
	case 3:
		tab[0].reg = addr;
		tab[0].val = (val >> shift * 2) & ((1 << shift) - 1);

		tab[1].reg = addr + 1;
		tab[1].val = (val >> shift * 1) & ((1 << shift) - 1);

		tab[2].reg = addr + 2;
		tab[2].val = (val >> shift * 0) & ((1 << shift) - 1);
		break;
	default:
		pr_err("%s, write num no correct\n", __func__);
		return -EINVAL;
	}

	regs.tab = tab;
	regs.num = num;

	return __b52_sensor_cmd_write(attr, &regs, sensor->pos);
}

static int b52_sensor_cmd_read(struct v4l2_subdev *sd, u16 addr,
		u32 *val, u8 num)
{
	int ret;
	int shift = 0;
	int i;
	struct regval_tab tab[3];
	const struct b52_sensor_i2c_attr *attr;
	struct b52_cmd_i2c_data data;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!sensor || !val || (num == 0)) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	attr = &sensor->drvdata->i2c_attr;
	if (num > 3 || (num == 3 && attr->val_len == I2C_16BIT)) {
		pr_err("%s, read num %d too long\n", __func__, num);
		return -EINVAL;
	}

	for (i = 0; i < num; i++)
		tab[i].reg = addr + i;
	data.attr = &sensor->drvdata->i2c_attr;
	data.tab = tab;
	data.num = num;
	data.pos = sensor->pos;

	ret = b52_cmd_read_i2c(&data);
	if (ret)
		return ret;

	if (data.attr->val_len == I2C_8BIT)
		shift = 8;
	else if (data.attr->val_len == I2C_16BIT)
		shift = 16;

	switch (num) {
	case 1:
		*val = tab[0].val;
		break;
	case 2:
		*val = (tab[0].val << shift) | tab[1].val;
		break;
	case 3:
		*val =	(tab[0].val << shift * 2) |
			(tab[1].val << shift * 1) |
			(tab[2].val << shift * 0);
		break;
	default:
		pr_err("%s, read num no correct\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int b52_sensor_g_cur_fmt(struct v4l2_subdev *sd,
	struct b52_cmd_i2c_data *data)
{
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!data) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sensor->lock);

	data->attr = &sensor->drvdata->i2c_attr;
	data->tab  = sensor->mf_regs.tab;
	data->num  = sensor->mf_regs.num;
	data->pos  = sensor->pos;

	mutex_unlock(&sensor->lock);

	return 0;
}


#endif
struct b52_sensor *b52_get_sensor(struct media_entity *entity)
{
	struct v4l2_subdev *sd;
	struct media_device *mdev = entity->parent;
	struct media_entity_graph graph;
	struct media_entity *sensor_entity = NULL;

	mutex_lock(&mdev->graph_mutex);
	media_entity_graph_walk_start(&graph, entity);
	while ((entity = media_entity_graph_walk_next(&graph)))
		if (entity->type == MEDIA_ENT_T_V4L2_SUBDEV_SENSOR) {
			sensor_entity = entity;
			break;
		}
	mutex_unlock(&mdev->graph_mutex);

	if (!sensor_entity) {
		pr_err("sensor entity not found\n");
		return NULL;
	}

	sd = container_of(sensor_entity, struct v4l2_subdev, entity);
	return to_b52_sensor(sd);
}
EXPORT_SYMBOL(b52_get_sensor);



/*only used for detect sensor, not download the FW*/
static int b52_sensor_isp_read(const struct b52_sensor_i2c_attr *attr,
		u16 reg, u16 *val, u8 pos)
{
	if (!attr || !val) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}
#ifdef CONFIG_ISP_USE_TWSI3
	return twsi_read_i2c(attr->addr, reg, val);
#else
	return b52_isp_read_i2c(attr, reg, val, pos);
#endif
}

#ifdef CONFIG_ISP_USE_TWSI3
static int b52_vcm_init(struct b52_sensor_vcm *vcm)
{
	int ret = 0;
	int written_num = 0;
	struct i2c_adapter *adapter;
	struct reg_tab_bb buf;
	enum b52_sensor_vcm_type vcm_type;
	struct i2c_msg msg;

	if (!vcm)
		return 0;

	vcm_type =  vcm->type;
	msg.addr = vcm->attr->addr;
	msg.flags = 0;
	msg.len = 2;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL) {
		pr_err("%s: Unable to get adapter\n", __func__);
		return -1;
	}

	switch (vcm_type) {
	case DW9714:
		break;
	case DW9804:
		msg.buf = (u8 *)&buf;
		while (written_num < vcm->init.num) {
			buf.reg = (u8)vcm->init.tab[written_num].reg;
			buf.val = (u8)vcm->init.tab[written_num].val;
			ret = i2c_transfer(adapter, &msg, 1);
			if (ret < 0)
				return ret;
			written_num++;
		}
		break;
	case DW9718:
		msg.buf = (u8 *)&buf;
		while (written_num < vcm->init.num) {
			buf.reg = (u8)vcm->init.tab[written_num].reg;
			buf.val = (u8)vcm->init.tab[written_num].val;
			ret |= i2c_transfer(adapter, &msg, 1);
			written_num++;
		}
		break;
	default:
		pr_err("not support current vcm type\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}
#endif

static int b52_sensor_init(struct v4l2_subdev *sd)
{
	int ret = 0;
	int num = 0;
	int written_num = 0;
	struct b52_sensor_regs regs;
	const struct b52_sensor_i2c_attr *attr;
	struct b52_sensor_module *module;
#ifdef CONFIG_ISP_USE_TWSI3
	struct b52_sensor_vcm *vcm;
#endif
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!sensor) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	attr = &sensor->drvdata->i2c_attr;
	regs.tab = sensor->drvdata->global_setting.tab;

	while (written_num < sensor->drvdata->global_setting.num) {
		if (likely(regs.tab[num].reg != SENSOR_MDELAY ||
				regs.tab[num].val != SENSOR_MDELAY)) {
			num++;
			if (likely(written_num + num <
					sensor->drvdata->global_setting.num))
				continue;
		}

		regs.num = num;
		ret = __b52_sensor_cmd_write(attr, &regs, sensor->pos);
		if (ret)
			return ret;

		if (unlikely(regs.tab[num].reg == SENSOR_MDELAY &&
				regs.tab[num].val == SENSOR_MDELAY)) {
			msleep(regs.tab[num].mask);
			num++;
		}

		written_num += num;
		regs.tab += written_num;
		num = 0;
	}

	b52_sensor_call(sensor, get_pixel_rate,
			&sensor->pixel_rate, sensor->mclk);
	pr_debug("sensor pxiel rate %d\n", sensor->pixel_rate);

	if (sensor->csi.calc_dphy)
		b52_sensor_call(sensor, get_dphy_desc,
				&sensor->csi.dphy_desc, sensor->mclk);

	b52_sensor_call(sensor, update_otp,
			&sensor->opt);

	module = sensor->drvdata->module;
	for (num = 0; num < sensor->drvdata->num_module; num++)
		if (sensor->opt.module_id == module->id)
			break;

	if (num < sensor->drvdata->num_module)
		sensor->cur_mod_id = num;
	else
		sensor->cur_mod_id = -ENODEV;

#ifdef CONFIG_ISP_USE_TWSI3
	if (sensor->drvdata->num_module != 0) {
		vcm = sensor->drvdata->module->vcm;
		/* init vcm setting */
		ret = b52_vcm_init(vcm);
		if (ret < 0)
			pr_err("%s, error param\n", __func__);
	}
#endif
	ret = v4l2_ctrl_handler_setup(&sensor->ctrl_hdl);
	if (ret < 0)
		pr_err("%s: setup hadnler failed\n", __func__);
	return ret;
}
#if 0
/* FIXME add detect vcm in future */
static int b52_sensor_detect_vcm(struct v4l2_subdev *sd)
{
	int i;
	u16 val;
	int ret;
	const struct b52_sensor_vcm *vcm;
	const struct b52_sensor_regs *id;
	const struct b52_sensor_i2c_attr *attr;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!sensor) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	if (!sensor->drvdata->module) {
		pr_err("sensor support internal vcm\n");
		return 0;
	}

	vcm = sensor->drvdata->module->vcm;
	id = &vcm->id;

	attr = vcm->attr;

	for (i = 0; i < id->num; i++) {
		ret = b52_sensor_isp_read(attr, id->tab[i].reg,
				&val, sensor->pos);

		if (ret || val != id->tab[i].val) {
			pr_err("detect %s failed\n", vcm->name);
			pr_err("val: got %x, req %x\n", val, id->tab[i].val);
			return -ENODEV;
		}
	}

	pr_info("sensor external vcm: %s detected\n", vcm->name);
	return 0;
}
#endif
static int b52_sensor_detect_sensor(struct v4l2_subdev *sd)
{
	int i;
	u16 val;
	int ret;
	const struct b52_sensor_regs *id;
	const struct b52_sensor_i2c_attr *attr;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	id = &sensor->drvdata->id;

	if (!sensor) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	attr = &sensor->drvdata->i2c_attr;

	for (i = 0; i < id->num; i++) {
		ret = b52_sensor_isp_read(attr, id->tab[i].reg,
				&val, sensor->pos);

		if (ret || val != id->tab[i].val) {
			pr_err("detect %s failed\n", sensor->drvdata->name);
			pr_err("val: got %x, req %x\n", val, id->tab[i].val);
			return -ENODEV;
		}
	}

	pr_info("sensor %s detected\n", sensor->drvdata->name);
	return 0;
}

static int b52_sensor_get_power(struct v4l2_subdev *sd)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct b52_sensor *sensor = to_b52_sensor(sd)
	struct device_node *pdata_np;

	pdata_np = (struct device_node *)client->dev.of_node;

	sensor->power.af_2v8 = devm_regulator_get(&client->dev, "af_2v8");
	if (IS_ERR(sensor->power.af_2v8)) {
		dev_warn(&client->dev, "Failed to get regulator af_2v8\n");
		sensor->power.af_2v8 = NULL;
	}
	sensor->power.avdd_2v8 = devm_regulator_get(&client->dev, "avdd_2v8");
	if (IS_ERR(sensor->power.avdd_2v8)) {
		dev_warn(&client->dev, "Failed to get regulator avdd_2v8\n");
		sensor->power.avdd_2v8 = NULL;
	}
	sensor->power.dovdd_1v8 = devm_regulator_get(&client->dev, "dovdd_1v8");
	if (IS_ERR(sensor->power.dovdd_1v8)) {
		dev_warn(&client->dev, "Failed to get regulator dovdd_1v8\n");
		sensor->power.dovdd_1v8 = NULL;
	}
	sensor->power.dvdd_1v2 = devm_regulator_get(&client->dev, "dvdd_1v2");
	if (IS_ERR(sensor->power.dvdd_1v2)) {
		dev_warn(&client->dev, "Failed to get regulator dvdd_1v2\n");
		sensor->power.dvdd_1v2 = NULL;
	}

	return 0;
}

static int b52_sensor_put_power(struct v4l2_subdev *sd)
{
	struct b52_sensor *sensor = to_b52_sensor(sd)
	if (sensor->power.avdd_2v8)
		devm_regulator_put(sensor->power.avdd_2v8);
	if (sensor->power.dvdd_1v2)
		devm_regulator_put(sensor->power.dvdd_1v2);
	if (sensor->power.af_2v8)
		devm_regulator_put(sensor->power.af_2v8);
	if (sensor->power.dovdd_1v8)
		devm_regulator_put(sensor->power.dovdd_1v8);
	return 0;
}

static int b52_sensor_g_vcm_info(struct v4l2_subdev *sd,
		struct b52_sensor_vcm *vcm)
{
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (sensor->cur_mod_id < 0) {
		pr_err("%s: no module, no vcm\n", __func__);
		return -ENODEV;
	}
	if (sensor->drvdata->num_module == 0)
		return 1;
	else
		*vcm = *sensor->drvdata->module[sensor->cur_mod_id].vcm;

	return 0;
}

static int b52_sensor_gain_to_iso(struct v4l2_subdev *sd,
		u32 gain, u32 *iso)
{
	const struct v4l2_fract *f;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	f = &sensor->drvdata->gain2iso_ratio;

	if (!f->denominator) {
		pr_err("%s: f->denominator is zero\n", __func__);
		return -EINVAL;
	}

	*iso = gain * f->numerator / f->denominator;
	return 0;
}

static int b52_sensor_iso_to_gain(struct v4l2_subdev *sd,
		u32 iso, u32 *gain)
{
	const struct v4l2_fract *f;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	f = &sensor->drvdata->gain2iso_ratio;

	if (!f->numerator) {
		pr_err("%s: f->numerator is zero\n", __func__);
		return -EINVAL;
	}

	*gain = iso * f->denominator / f->numerator;
	return 0;
}

static int b52_sensor_to_expo_line(struct v4l2_subdev *sd,
		u32 time, u32 *lines)
{
	/*
	 * time unit: 100 us according to v4l2
	 */
	u32 us = time * 100;
	u8 i;
	u64 temp;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	i = sensor->cur_res_idx;

	if (!lines) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	temp = us * (u64)(sensor->pixel_rate);
	temp = div_u64(temp, sensor->drvdata->res[i].hts);
	*lines = (u32)div_u64(temp + 500000, 1000000);

	pr_debug("%u us, line %u, hts %u, pixle rate %u\n", us, *lines,
		sensor->drvdata->res[i].hts, sensor->pixel_rate);
	return 0;
}

static int b52_sensor_to_expo_time(struct v4l2_subdev *sd,
		u32 *time, u32 lines)
{
	/*time unit: 100 us according to v4l2*/
	u32 line_time;
	u8 i;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	i = sensor->cur_res_idx;

	if (!time) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	line_time = sensor->drvdata->res[i].hts * 10000 /
		(sensor->pixel_rate / 1000);

	*time = lines * line_time / 10;

	pr_debug("%s: %d 100us, %d line\n", __func__, *time, lines);
	return 0;
}

static int b52_sensor_g_cur_fps(struct v4l2_subdev *sd,
	struct v4l2_fract *fps)
{
	u32 i;
	int ret;
	u32 vts;
	u32 size;
	const struct b52_sensor_regs *reg;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!fps) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	i = sensor->cur_res_idx;
	reg = &sensor->drvdata->vts_reg;

	ret = b52_sensor_call(sensor, i2c_read,
			reg->tab->reg, &vts, reg->num);
	if (ret) {
		pr_err("%s: read vts failed\n", __func__);
		return ret;
	}

	size = vts * sensor->drvdata->res[i].hts;

	fps->numerator = (sensor->pixel_rate * 10) / (size / 10);
	fps->denominator = 100;

	return 0;
}

#define BANDING_STEP_50HZ	0
#define BANDING_STEP_60HZ	1
static u32 __cal_band_step(int hz, u32 pixel_rate, u32 hts)
{
	u32 banding_step;

	if (hz == BANDING_STEP_50HZ)
		banding_step = pixel_rate/100/hts;
	else if (hz == BANDING_STEP_60HZ)
		banding_step = pixel_rate/120/hts;
	else
		return 0;

	return banding_step;
}
static int b52_sensor_g_band_step(struct v4l2_subdev *sd,
		u16 *band_50hz, u16 *band_60hz)
{
	int i;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	i = sensor->cur_res_idx;

	*band_50hz = __cal_band_step(BANDING_STEP_50HZ,
		sensor->pixel_rate,
		sensor->drvdata->res[i].hts);
	*band_60hz = __cal_band_step(BANDING_STEP_60HZ,
		sensor->pixel_rate,
		sensor->drvdata->res[i].hts);

	return 0;
}

static int b52_sensor_s_flip(struct v4l2_subdev *sd,
		int hflip, int on)
{
	u32 val;
	int ret;
	const struct b52_sensor_regs *reg;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (hflip)
		reg = &sensor->drvdata->hflip;
	else
		reg = &sensor->drvdata->vflip;

	ret = b52_sensor_call(sensor, i2c_read,
			reg->tab->reg, &val, reg->num);
	if (ret)
		return ret;

	val &= ~(reg->tab->mask);

	if (on)
		val &= ~(reg->tab->val);
	else
		val |= reg->tab->val;

	ret = b52_sensor_call(sensor, i2c_write,
			reg->tab->reg, val, reg->num);

	return ret;
}

static int b52_sensor_g_param_range(struct v4l2_subdev *sd,
		int type, u16 *min, u16 *max)
{
	const struct b52_sensor_data *data;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	data = sensor->drvdata;

	if (!min || !max) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	switch (type) {
	case B52_SENSOR_GAIN:
		*min = data->gain_range[B52_SENSOR_AG].min;
		*max = data->gain_range[B52_SENSOR_AG].max;
		if (data->gain_range[B52_SENSOR_DG].min != 0) {
			*min = *min * data->gain_range[B52_SENSOR_DG].min / B52_GAIN_UNIT;
			*max = *max * data->gain_range[B52_SENSOR_DG].max / B52_GAIN_UNIT;
		}
		break;

	case B52_SENSOR_AGAIN:
		*min = data->gain_range[B52_SENSOR_AG].min;
		*max = data->gain_range[B52_SENSOR_AG].max;
		break;

	case B52_SENSOR_DGAIN:
		*min = data->gain_range[B52_SENSOR_DG].min;
		*max = data->gain_range[B52_SENSOR_DG].max;
		break;

	case B52_SENSOR_EXPO:
		*min = data->expo_range.min;
		*max = data->expo_range.max;
		break;

	case B52_SENSOR_VTS:
		*min = data->vts_range.min;
		*max = data->vts_range.max;
		break;

	case B52_SENSOR_REQ_VTS:
		*min = data->res[sensor->cur_res_idx].min_vts;
		*max = data->res[sensor->cur_res_idx].min_vts;
		break;

	case B52_SENSOR_FOCUS:
		*min = data->focus_range.min;
		*max = data->focus_range.max;
		break;

	default:
		pr_err("%s: wrong type\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int b52_sensor_g_sensor_attr(struct v4l2_subdev *sd,
		struct b52_sensor_i2c_attr *attr)
{
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (!attr) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	*attr = sensor->drvdata->i2c_attr;

	return 0;
}

static int b52_sensor_g_aecagc_reg(struct v4l2_subdev *sd,
		int type, struct b52_sensor_regs *reg)
{
	const struct b52_sensor_data *data;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	data = sensor->drvdata;

	switch (type) {
	case B52_SENSOR_AGAIN:
		*reg = data->gain_reg[B52_SENSOR_AG];
		break;

	case B52_SENSOR_DGAIN:
		*reg = data->gain_reg[B52_SENSOR_DG];
		break;

	case B52_SENSOR_EXPO:
		*reg = data->expo_reg;
		break;

	case B52_SENSOR_VTS:
		*reg = data->vts_reg;
		break;

	default:
		pr_err("%s: wrong type: %d\n", __func__, type);
		return -EINVAL;
	}

	if (!reg || !reg->tab || !reg->num) {
		pr_err("%s: reg is null\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int b52_sensor_g_csi(struct v4l2_subdev *sd,
		struct mipi_csi2 *csi)
{
	struct b52_sensor *sensor = to_b52_sensor(sd);

	*csi = sensor->csi;

	return  0;
}
static struct b52_sensor_ops b52_sensor_def_ops = {
	.init          = b52_sensor_init,
#ifdef CONFIG_ISP_USE_TWSI3
	.i2c_read      = b52_sensor_cmd_read_twsi,
	.i2c_write     = b52_sensor_cmd_write_twsi,
	.g_cur_fmt     = b52_sensor_g_cur_fmt_twsi,
	.s_focus       = b52_sensor_s_focus_twsi,
	.g_focus       = b52_sensor_g_focus_twsi,
	.s_expo        = b52_sensor_s_expo,
	.s_gain        = b52_sensor_s_gain,
#else
	.i2c_read      = b52_sensor_cmd_read,
	.i2c_write     = b52_sensor_cmd_write,
	.g_cur_fmt     = b52_sensor_g_cur_fmt,
#endif
	.get_power     = b52_sensor_get_power,
	.put_power     = b52_sensor_put_power,
	.detect_sensor = b52_sensor_detect_sensor,
	/* .detect_vcm    = b52_sensor_detect_vcm,*/
	.g_vcm_info    = b52_sensor_g_vcm_info,
	.g_cur_fps     = b52_sensor_g_cur_fps,
	.g_param_range = b52_sensor_g_param_range,
	.g_aecagc_reg  = b52_sensor_g_aecagc_reg,
	.g_sensor_attr = b52_sensor_g_sensor_attr,
	.g_band_step   = b52_sensor_g_band_step,
	.g_csi         = b52_sensor_g_csi,
	.s_flip        = b52_sensor_s_flip,
	.gain_to_iso   = b52_sensor_gain_to_iso,
	.iso_to_gain   = b52_sensor_iso_to_gain,
	.to_expo_line  = b52_sensor_to_expo_line,
	.to_expo_time  = b52_sensor_to_expo_time,
};

static int b52_sensor_set_defalut(struct b52_sensor *sensor)
{
	if (!sensor) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	sensor->ops = b52_sensor_def_ops;

	if (!sensor->drvdata->ops->get_pixel_rate) {
		pr_err("error: get_pixel_rate not defined\n");
		return -EINVAL;
	} else {
		sensor->ops.get_pixel_rate =
			sensor->drvdata->ops->get_pixel_rate;
	}

	if (sensor->csi.calc_dphy) {
		if (!sensor->drvdata->ops->get_dphy_desc) {
			pr_err("error: get_dphy not defined\n");
			return -EINVAL;
		} else
			sensor->ops.get_dphy_desc =
				sensor->drvdata->ops->get_dphy_desc;
	}

	if (!sensor->drvdata->ops->update_otp) {
		pr_err("error: update_otp not defined\n");
		return -EINVAL;
	} else
		sensor->ops.update_otp =
			sensor->drvdata->ops->update_otp;

	return 0;
}

static int b52_sensor_s_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;
	struct sensor_power *power;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct b52_sensor *sensor = to_b52_sensor(sd);
	power = (struct sensor_power *) &(sensor->power);

	if (on) {
		if (power->ref_cnt++ > 0)
			return 0;

		power->pwdn = devm_gpiod_get(&client->dev, "pwdn");
		if (IS_ERR(power->pwdn)) {
			dev_warn(&client->dev, "Failed to get gpio pwdn\n");
			power->pwdn = NULL;
		} else {
			ret = gpiod_direction_output(power->pwdn, 0);
			if (ret < 0) {
				dev_err(&client->dev, "Failed to set gpio pwdn\n");
				return ret;
			}
		}

		power->rst = devm_gpiod_get(&client->dev, "reset");
		if (IS_ERR(power->rst)) {
			dev_warn(&client->dev, "Failed to get gpio reset\n");
			power->rst = NULL;
		} else {
			ret = gpiod_direction_output(power->rst, 1);
			if (ret < 0) {
				dev_err(&client->dev, "Failed to set gpio rst\n");
				goto rst_err;
			}
		}

		if (power->avdd_2v8) {
			regulator_set_voltage(power->avdd_2v8,
						2800000, 2800000);
			ret = regulator_enable(power->avdd_2v8);
			if (ret < 0)
				goto avdd_err;
		}

		if (power->pwdn)
			gpiod_set_value_cansleep(power->pwdn, 0);

		if (power->dovdd_1v8) {
			regulator_set_voltage(power->dovdd_1v8,
						1800000, 1800000);
			ret = regulator_enable(power->dovdd_1v8);
			if (ret < 0)
				goto dovdd_err;
		}
		if (power->dvdd_1v2) {
			regulator_set_voltage(power->dvdd_1v2,
						1200000, 1200000);
			ret = regulator_enable(power->dvdd_1v2);
			if (ret < 0)
				goto dvdd_err;
		}
		if (power->af_2v8) {
			regulator_set_voltage(power->af_2v8,
						2800000, 2800000);
			ret = regulator_enable(power->af_2v8);
			if (ret < 0)
				goto af_err;
		}

		if (power->rst) {
			gpiod_set_value_cansleep(power->rst, 1);
			usleep_range(100, 120);
			gpiod_set_value_cansleep(power->rst, 0);
		}

	} else {
		if (WARN_ON(power->ref_cnt == 0))
			return -EINVAL;

		if (--power->ref_cnt > 0)
			return 0;

		if (power->rst)
			gpiod_set_value_cansleep(power->rst, 1);
		if (power->dvdd_1v2)
			regulator_disable(power->dvdd_1v2);
		if (power->avdd_2v8)
			regulator_disable(power->avdd_2v8);
		if (power->pwdn)
			gpiod_set_value_cansleep(power->pwdn, 1);
		if (power->dovdd_1v8)
			regulator_disable(power->dovdd_1v8);
		if (power->af_2v8)
			regulator_disable(power->af_2v8);

		if (sensor->power.rst)
			devm_gpiod_put(&client->dev, sensor->power.rst);
		if (sensor->power.pwdn)
			devm_gpiod_put(&client->dev, sensor->power.pwdn);
	}

	return ret;

af_err:
	if (power->dvdd_1v2)
		regulator_disable(power->dvdd_1v2);
dvdd_err:
	if (power->dovdd_1v8)
		regulator_disable(power->dovdd_1v8);
dovdd_err:
	if (power->avdd_2v8)
		regulator_disable(power->af_2v8);
avdd_err:
	if (sensor->power.rst)
		devm_gpiod_put(&client->dev, sensor->power.rst);
rst_err:
	if (sensor->power.pwdn)
		devm_gpiod_put(&client->dev, sensor->power.pwdn);

	return ret;
}

static int b52_sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	const struct b52_sensor_regs *regs;
	struct b52_sensor *sensor = to_b52_sensor(sd)

	if (enable)
		regs = &sensor->drvdata->streamon;
	else
		regs = &sensor->drvdata->streamoff;

	return __b52_sensor_cmd_write(
			&sensor->drvdata->i2c_attr,
			regs, sensor->pos);
}

static int b52_sensor_enum_mbus_code(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh,
		struct v4l2_subdev_mbus_code_enum *code)
{
	const struct b52_sensor_data *data;
	struct b52_sensor *sensor = to_b52_sensor(sd)

	data = sensor->drvdata;

	if (code->pad > 0 || code->index > data->num_mbus_fmt) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	code->code = data->mbus_fmt[code->index].mbus_code;

	return 0;
}

static int b52_sensor_enum_frame_size(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh,
		struct v4l2_subdev_frame_size_enum *fse)
{
	int i;
	const struct b52_sensor_data *data;
	struct b52_sensor *sensor = to_b52_sensor(sd)
	data = sensor->drvdata;

	if (fse->pad > 0 || fse->index > data->num_res) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < data->num_mbus_fmt; i++)
		if (fse->code == data->mbus_fmt[i].mbus_code)
			break;

	if (i >= data->num_mbus_fmt) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	fse->min_width = data->res[fse->index].width;
	fse->max_width = data->res[fse->index].width;
	fse->min_height = data->res[fse->index].height;
	fse->max_height = data->res[fse->index].height;

	return 0;
}

static int b52_sensor_enum_frame_interval(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh,
		struct v4l2_subdev_frame_interval_enum *fie)
{

	int i;
	u32 size;
	const struct b52_sensor_data *data;
	struct b52_sensor *sensor = to_b52_sensor(sd)
	data = sensor->drvdata;

	if (fie->pad > 0 || fie->index > data->num_res) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < data->num_mbus_fmt; i++)
		if (fie->code == data->mbus_fmt[i].mbus_code)
			break;

	if (i >= data->num_mbus_fmt) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < data->num_res; i++)
		if (fie->width == data->res[i].width &&
			fie->height == data->res[i].height)
			break;

	if (i >= data->num_res) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	size = data->res[i].min_vts * data->res[i].hts;

	fie->interval.numerator = (size * 10) /
			(sensor->pixel_rate / 1000);
	fie->interval.denominator = 10000;

	return 0;
}

static int b52_sensor_get_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh,
		struct v4l2_subdev_format *format)
{
	int ret = 0;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (format->pad > 0) {
		pr_err("%s, error pad num\n", __func__);
		return -EINVAL;
	}

	mutex_lock(&sensor->lock);

	switch (format->which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		/*FIXME*/
		format->format = *v4l2_subdev_get_try_format(fh, 0);
		break;
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		format->format = sensor->mf;
		break;
	default:
		ret = -EINVAL;
		pr_err("%s, error format->which\n", __func__);
		goto error;
	}

error:
	mutex_unlock(&sensor->lock);
	return ret;
}

static int b52_sensor_set_fmt(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh,
		struct v4l2_subdev_format *format)
{
	int i;
	int j;
	struct b52_sensor_regs *mf_regs;
	const struct b52_sensor_data *data;
	struct v4l2_mbus_framefmt *mf = &format->format;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	data = sensor->drvdata;
	mf_regs = &sensor->mf_regs;

	if (format->pad > 0) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	for (i = 0; i < data->num_mbus_fmt; i++)
		if (mf->code == data->mbus_fmt[i].mbus_code)
			break;

	if (i >= data->num_mbus_fmt) {
		pr_info("%s: mbus code not match\n", __func__);
		i = 0;
	};

	mf->code = data->mbus_fmt[i].mbus_code;
	mf->colorspace = data->mbus_fmt[i].colorspace;

	for (j = 0; j < data->num_res; j++)
		if (mf->width == data->res[j].width &&
			mf->height == data->res[j].height)
			break;

	if (j >= data->num_res) {
		pr_info("%s: frame size not match\n", __func__);
		j = 0;
	}

	mf->width  = data->res[j].width;
	mf->height = data->res[j].height;
	mf->field  = V4L2_FIELD_NONE;


	if (format->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		int size = sizeof(*mf_regs->tab);

		mutex_lock(&sensor->lock);

		if (mf->code != sensor->mf.code ||
			mf->width != sensor->mf.width ||
			mf->height != sensor->mf.height) {
			memcpy(mf_regs->tab,
					data->mbus_fmt[i].regs.tab,
					size * data->mbus_fmt[i].regs.num);
			mf_regs->num = data->mbus_fmt[i].regs.num;

			memcpy(mf_regs->tab + mf_regs->num,
					data->res[j].regs.tab,
					size * data->res[j].regs.num);
			mf_regs->num += data->res[j].regs.num;
		}

		sensor->mf = *mf;
		sensor->cur_mbus_idx = i;
		sensor->cur_res_idx = j;

		mutex_unlock(&sensor->lock);
	}

	return 0;
}

static int b52_sensor_g_skip_top_lines(struct v4l2_subdev *sd, u32 *lines)
{
	struct b52_sensor *sensor = to_b52_sensor(sd);
	*lines = sensor->drvdata->skip_top_lines;

	return 0;
}

static int b52_sensor_g_skip_frames(struct v4l2_subdev *sd, u32 *frames)
{
	struct b52_sensor *sensor = to_b52_sensor(sd);

	*frames = sensor->drvdata->skip_frames;

	return 0;
}

static int b52_sensor_sd_open(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	int ret;
	/*FIXME: not need put power on here*/
	ret = v4l2_subdev_call(sd, core, s_power, 1);

	return ret;
}

static int b52_sensor_sd_close(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh)
{
	int ret;
	/*FIXME: not need put power off here*/
	ret = v4l2_subdev_call(sd, core, s_power, 0);

	return ret;
}

static int b52_sensor_link_setup(struct media_entity *entity,
			  const struct media_pad *local,
			  const struct media_pad *remote, u32 flags)
{
	return 0;
}

static struct v4l2_subdev_video_ops b52_sensor_video_ops = {
	.s_stream = b52_sensor_s_stream,
};

static const struct v4l2_subdev_pad_ops b52_sensor_pad_ops = {
	.enum_mbus_code      = b52_sensor_enum_mbus_code,
	.enum_frame_size     = b52_sensor_enum_frame_size,
	.enum_frame_interval = b52_sensor_enum_frame_interval,
	.get_fmt             = b52_sensor_get_fmt,
	.set_fmt             = b52_sensor_set_fmt,
};

struct v4l2_subdev_sensor_ops b52_sensor_sensor_ops = {
	.g_skip_top_lines = b52_sensor_g_skip_top_lines,
	.g_skip_frames    = b52_sensor_g_skip_frames,
};

static struct v4l2_subdev_core_ops b52_sensor_core_ops = {
	.s_power = b52_sensor_s_power,
};

static struct v4l2_subdev_ops b52_sensor_sd_ops = {
	.core   = &b52_sensor_core_ops,
	.pad    = &b52_sensor_pad_ops,
	.video  = &b52_sensor_video_ops,
	.sensor = &b52_sensor_sensor_ops,
};

static const struct v4l2_subdev_internal_ops b52_sensor_sd_internal_ops = {
	.open  = b52_sensor_sd_open,
	.close = b52_sensor_sd_close,
};

static const struct media_entity_operations sensor_media_ops = {
	.link_setup = b52_sensor_link_setup,
};

static int b52_detect_sensor(struct b52_sensor *sensor)
{
	int ret;

	if (!sensor) {
		pr_err("%s, error param\n", __func__);
		return -EINVAL;
	}

	ret = b52_sensor_call(sensor, get_power);
	if (ret)
		return ret;

	ret = v4l2_subdev_call(&sensor->sd, core, s_power, 1);
	if (ret) {
		pr_err("%s, sensor power up error\n", __func__);
		goto error;
	}

	ret = b52_sensor_call(sensor, detect_sensor);

#if 0
	/* FIXME support detect sensor success while VCM detect error */
	ret |= b52_sensor_call(sensor, detect_vcm);
#endif
	v4l2_subdev_call(&sensor->sd, core, s_power, 0);
	if (ret)
		goto error;

	return 0;

error:
	b52_sensor_call(sensor, put_power);
	return ret;
}

/*FIXME: refine the min/max*/
#define FLASH_TIMEOUT_MIN		100000	/* us */
#define FLASH_TIMEOUT_MAX		100000
#define FLASH_TIMEOUT_STEP		50000

#define FLASH_INTENSITY_MIN         100 /* mA */
#define FLASH_INTENSITY_STEP		20
#define FLASH_INTENSITY_MAX         100 /* mA */

#define TORCH_INTENSITY_MIN         100  /* mA */
#define TORCH_INTENSITY_MAX         100
#define TORCH_INTENSITY_STEP        20

static int b52_sensor_config_flash(
		struct b52_sensor_flash *flash)
{
	pr_debug("%s\n", __func__);
	return 0;
}

static int b52_sensor_set_flash(
		struct b52_sensor_flash *flash, int on)
{
	if (on)
		ledtrig_flash_ctrl(1);
	else
		ledtrig_flash_ctrl(0);
	flash->flash_status = on;

	return 0;
}

static int b52_sensor_set_torch(
		struct b52_sensor_flash *flash, int on)
{
	if (on)
		ledtrig_torch_ctrl(1);
	else
		ledtrig_torch_ctrl(0);
	flash->flash_status = on;

	return 0;
}

static int b52_sensor_g_ctrl(struct v4l2_ctrl *ctrl)
{
	int i;
	struct b52_sensor_flash *flash;
	struct b52_sensor *sensor = container_of(
			ctrl->handler, struct b52_sensor, ctrl_hdl);
	flash = &sensor->flash;

	switch (ctrl->id) {
	case V4L2_CID_HBLANK:
		i = sensor->cur_res_idx;
		ctrl->val = sensor->drvdata->res[i].hts -
			sensor->drvdata->res[i].width;
		break;

	case V4L2_CID_PIXEL_RATE:
		ctrl->val64 = sensor->pixel_rate;
		break;

	case V4L2_CID_FLASH_FAULT:
		break;

	case V4L2_CID_FLASH_STROBE_STATUS:
		ctrl->val = flash->flash_status;
		break;

	default:
		pr_err("%s: ctrl not support\n", __func__);
		return -EINVAL;
	}

	pr_debug("G_CTRL %08x:%d\n", ctrl->id, ctrl->val);

	return 0;
}

static int b52_sensor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct b52_sensor_flash *flash;
	struct b52_sensor *sensor = container_of(
			ctrl->handler, struct b52_sensor, ctrl_hdl);
	flash = &sensor->flash;

	/*FIXME: implement flash config and set function*/
	switch (ctrl->id) {
	case V4L2_CID_VFLIP:
		b52_sensor_call(sensor, s_flip, 0, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
		b52_sensor_call(sensor, s_flip, 1, ctrl->val);
		break;

	case V4L2_CID_FLASH_LED_MODE:
		flash->led_mode = ctrl->val;
		b52_sensor_config_flash(flash);
		break;

	case V4L2_CID_FLASH_STROBE_SOURCE:
		flash->strobe_source = ctrl->val;
		b52_sensor_config_flash(flash);
		break;

	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH)
			b52_sensor_set_flash(flash, 1);
		else if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH)
			b52_sensor_set_torch(flash, 1);
		else
			return -EBUSY;
		break;

	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH)
			b52_sensor_set_flash(flash, 0);
		else if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH)
			b52_sensor_set_torch(flash, 0);
		else
			return -EBUSY;
		break;

	case V4L2_CID_FLASH_TIMEOUT:
		flash->timeout = ctrl->val;

		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;
		break;

	case V4L2_CID_FLASH_INTENSITY:
		flash->flash_current = (ctrl->val - FLASH_INTENSITY_MIN)
				     / FLASH_INTENSITY_STEP;

		if (flash->led_mode != V4L2_FLASH_LED_MODE_FLASH)
			break;

		b52_sensor_config_flash(flash);
		break;

	case V4L2_CID_FLASH_TORCH_INTENSITY:
		/*FIXME*/
		flash->torch_current = (ctrl->val - TORCH_INTENSITY_MIN)
			/ TORCH_INTENSITY_STEP;

		if (flash->led_mode != V4L2_FLASH_LED_MODE_TORCH)
			break;

		b52_sensor_config_flash(flash);
		break;

	default:
		pr_err("%s: ctrl %x not support\n", __func__, ctrl->id);
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_ctrl_ops b52_sensor_ctrl_ops = {
	.g_volatile_ctrl = b52_sensor_g_ctrl,
	.s_ctrl          = b52_sensor_s_ctrl,
};

static int b52_sensor_init_ctrls(struct b52_sensor *sensor)
{
	int i;
	u32 min = 0;
	u32 max = 0;
	struct v4l2_ctrl *ctrl;
	struct b52_sensor_flash *flash = &sensor->flash;
	const struct b52_sensor_data *data = sensor->drvdata;

	v4l2_ctrl_handler_init(&sensor->ctrl_hdl, 18);

	v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_VFLIP, 0, 1, 1, 0);

	ctrl = v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_ANALOGUE_GAIN,
			data->gain_range[B52_SENSOR_AG].min,
			data->gain_range[B52_SENSOR_AG].max, 1,
			data->gain_range[B52_SENSOR_AG].min);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

/*FIXME: use vts not vb*/
	ctrl = v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_VBLANK, data->vts_range.min,
			data->vts_range.max, 1, data->vts_range.min);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FOCUS_ABSOLUTE, data->focus_range.min,
			data->focus_range.max, 1, data->focus_range.min);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	for (i = 0; i < sensor->drvdata->num_res; i++) {
		min = min_t(u32, min, sensor->drvdata->res[i].hts
				- sensor->drvdata->res[i].width);
		max = max_t(u32, max, sensor->drvdata->res[i].hts
				- sensor->drvdata->res[i].width);
	}
	ctrl = v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_HBLANK, min, max, 1, min);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	ctrl = v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_PIXEL_RATE, 0, 0, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std_menu(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_LED_MODE, 2, ~7,
			V4L2_FLASH_LED_MODE_NONE);

	flash->strobe_source = V4L2_FLASH_STROBE_SOURCE_SOFTWARE;
	v4l2_ctrl_new_std_menu(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_STROBE_SOURCE, 0, ~1,
			V4L2_FLASH_STROBE_SOURCE_SOFTWARE);

	v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_STROBE, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_STROBE_STOP, 0, 1, 1, 0);

	ctrl = v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_STROBE_STATUS, 0, 1, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	flash->timeout = FLASH_TIMEOUT_MIN;
	v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_TIMEOUT, FLASH_TIMEOUT_MIN,
			FLASH_TIMEOUT_MAX, FLASH_TIMEOUT_STEP,
			FLASH_TIMEOUT_MIN);

	flash->flash_current = FLASH_INTENSITY_MIN;
	v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_INTENSITY, FLASH_INTENSITY_MIN,
			FLASH_INTENSITY_MAX, FLASH_INTENSITY_STEP,
			FLASH_INTENSITY_MIN);

	v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_TORCH_INTENSITY,
			TORCH_INTENSITY_MIN, TORCH_INTENSITY_MAX,
			TORCH_INTENSITY_STEP,
			TORCH_INTENSITY_MIN);

	ctrl = v4l2_ctrl_new_std(&sensor->ctrl_hdl,
			&b52_sensor_ctrl_ops,
			V4L2_CID_FLASH_FAULT, 0,
			V4L2_FLASH_FAULT_OVER_VOLTAGE |
			V4L2_FLASH_FAULT_TIMEOUT |
			V4L2_FLASH_FAULT_OVER_TEMPERATURE |
			V4L2_FLASH_FAULT_SHORT_CIRCUIT, 0, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	sensor->sd.ctrl_handler = &sensor->ctrl_hdl;

	return sensor->ctrl_hdl.error;
}

static int b52_sensor_alloc_fmt_regs(struct b52_sensor *sensor)
{
	int i;
	u32 reg_num = 0;
	u32 total_reg_num = 0;

	for (i = 0; i < sensor->drvdata->num_res; i++)
		reg_num = max_t(u32, reg_num,
			sensor->drvdata->res[i].regs.num);

	total_reg_num = reg_num;
	reg_num = 0;

	for (i = 0; i < sensor->drvdata->num_mbus_fmt; i++)
		reg_num = max_t(u32,
			reg_num, sensor->drvdata->mbus_fmt[i].regs.num);

	total_reg_num += reg_num;

	sensor->mf_regs.tab = devm_kzalloc(sensor->dev,
		total_reg_num * sizeof(struct regval_tab), GFP_KERNEL);

	if (!sensor->mf_regs.tab) {
		pr_err("%s failed\n", __func__);
		return -ENOMEM;
	}

	return 0;
}

static const struct of_device_id fimc_is_sensor_of_match[];
static const struct of_device_id b52_sensor_of_match[];

static int b52_sensor_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int ret = 0;
	struct device *dev = &client->dev;
	struct b52_sensor *sensor;
	const struct of_device_id *of_id;
	struct v4l2_subdev *sd;
	struct device_node *np = dev->of_node;

	of_id = of_match_node(b52_sensor_of_match, dev->of_node);
	if (!of_id)
		return -ENODEV;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->drvdata = of_id->data;
	sensor->dev = dev;

	ret = of_property_read_u32(np, "sensor-pos", (u32 *)&sensor->pos);
	if (ret < 0) {
		dev_err(dev, "failed to get sensor position, errno %d\n", ret);
		return ret;
	}

	ret = of_property_read_u32(np, "mclk", &sensor->mclk);
	if (ret < 0) {
		dev_err(dev, "failed to get mclk, errno %d\n", ret);
		return ret;
	}

	sensor->csi.dphy_desc.nr_lane = sensor->drvdata->nr_lane;
	if (!sensor->csi.dphy_desc.nr_lane) {
		dev_err(dev, "the csi lane number is zero\n");
		return -EINVAL;
	}
	sensor->csi.calc_dphy = sensor->drvdata->calc_dphy;
	if (!sensor->csi.calc_dphy) {
		ret = of_property_read_u32(np, "dphy3", &sensor->csi.dphy[0]);
		if (ret < 0) {
			dev_err(dev, "failed to dphy3, errno %d\n", ret);
			return ret;
		}
		ret = of_property_read_u32(np, "dphy5", &sensor->csi.dphy[1]);
		if (ret < 0) {
			dev_err(dev, "failed to dphy5, errno %d\n", ret);
			return ret;
		}
		ret = of_property_read_u32(np, "dphy6", &sensor->csi.dphy[2]);
		if (ret < 0) {
			dev_err(dev, "failed to dphy6, errno %d\n", ret);
			return ret;
		}
	}

	sd = &sensor->sd;
	v4l2_i2c_subdev_init(sd, client, &b52_sensor_sd_ops);
	sd->internal_ops = &b52_sensor_sd_internal_ops;
	snprintf(sd->name, sizeof(sd->name), sensor->drvdata->name);
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	b52_sensor_set_defalut(sensor);

	ret = b52_detect_sensor(sensor);
	if (ret)
		return ret;

	ret = b52_sensor_alloc_fmt_regs(sensor);
	if (ret)
		return ret;

	ret = b52_sensor_init_ctrls(sensor);
	if (ret)
		goto error;

	mutex_init(&sensor->lock);

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.ops = &sensor_media_ops;
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&sd->entity, 1, &sensor->pad, 0);
	if (ret)
		goto error;
#ifdef CONFIG_ISP_USE_TWSI3
	b52_init_workqueue((void *)sensor);
#endif
	return ret;
#if 0
	sensor->mf.code = data->mbus_fmt[0].mbus_code;
	sensor->mf.colorspace = data->mbus_fmt[0].colorspace;
	sensor->mf.width = data->res[0].width;
	sensor->mf.height = data->res[0].height;
	sensor->mf.filed = V4L2_FIELD_NONE;
#endif
error:
	v4l2_ctrl_handler_free(&sensor->ctrl_hdl);

	return ret;
}

static int b52_sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct b52_sensor *sensor = to_b52_sensor(sd);

	b52_sensor_call(sensor, put_power);
	v4l2_ctrl_handler_free(&sensor->ctrl_hdl);
	media_entity_cleanup(&sd->entity);
	v4l2_device_unregister_subdev(sd);
	devm_kfree(sensor->dev, sensor);

	return 0;
}

#define DRIVER_NAME "b52-sensor"
static const struct i2c_device_id b52_sensor_ids[] = {
	{ }
};

static const struct of_device_id b52_sensor_of_match[] = {
#ifdef CONFIG_B52_CAMERA_IMX219
	{
		.compatible	= "sony,imx219",
		.data       = &b52_imx219,
	},
#endif
#ifdef CONFIG_B52_CAMERA_OV5642
	{
		.compatible	= "ovt,ov5642",
		.data       = &b52_ov5642,
	},
#endif
#ifdef CONFIG_B52_CAMERA_OV13850
	{
		.compatible	= "ovt,ov13850",
		.data       = &b52_ov13850,
	},
#endif
#ifdef CONFIG_B52_CAMERA_OV8858
	{
		.compatible = "ovt,ov8858",
		.data = &b52_ov8858,
	},
#endif

#ifdef CONFIG_B52_CAMERA_OV5648
	{
		.compatible = "ovt,ov5648",
		.data = &b52_ov5648,
	},
#endif
#ifdef CONFIG_B52_CAMERA_OV2680
	{
		.compatible = "ovt,ov2680",
		.data = &b52_ov2680,
	},
#endif
	{  }
};

const struct b52_sensor_data *memory_sensor_match(char *sensor_name)
{
	const struct of_device_id *p = b52_sensor_of_match;
	while (p != NULL) {
		if (!strcmp(sensor_name, p->compatible))
			return p->data;
		p++;
	}
	return NULL;
}
EXPORT_SYMBOL(memory_sensor_match);

static struct i2c_driver b52_sensor_driver = {
	.driver = {
		.of_match_table	= b52_sensor_of_match,
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
	},
	.probe		= b52_sensor_probe,
	.remove		= b52_sensor_remove,
	.id_table	= b52_sensor_ids,
};

module_i2c_driver(b52_sensor_driver);

MODULE_DESCRIPTION("A common b52 sensor driver");
MODULE_AUTHOR("Jianle Wang <wangjl@marvell.com>");
MODULE_LICENSE("GPL");
