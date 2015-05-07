#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/b52-vcm.h>
#include <linux/i2c.h>
#include <linux/cputype.h>
#define SUBDEV_DRV_NAME	"vcm-pdrv"
struct reg_tab_wb {
		u16 reg;
		u8 val;
} __packed;
struct reg_tab_bb {
		u8 reg;
		u8 val;
} __packed;
static int twsi_read_i2c_bb(u16 addr, u8 reg, u8 *val)
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

static struct b52_sensor_i2c_attr vcm_attr_8BIT = {
	.reg_len = I2C_8BIT,
	.val_len = I2C_8BIT,
	.addr = 0x0c,
};
static struct b52_sensor_i2c_attr vcm_attr_16BIT = {
	.reg_len = I2C_16BIT,
	.val_len = I2C_8BIT,
	.addr = 0x0c,
};
struct regval_tab DW9804_id[] = {
	{0x00, 0xF1, 0xff},
};
struct regval_tab DW9804_init[] = {
	/* control register */
	{0x02, 0x02, 0x03},
	/* mode register */
	{0x06, 0x00, 0xC0},
	/* period register */
	{0x07, 0x7C, 0xFF},
};
#define N_DW9804_ID ARRAY_SIZE(DW9804_id)
#define N_DW9804_INIT ARRAY_SIZE(DW9804_init)
static int dw9804_s_init(struct v4l2_subdev *subdev)
{
	struct i2c_adapter *adapter;
	struct reg_tab_bb buf;
	struct i2c_msg msg;
	int ret = 0;
	int written_num = 0;
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	if (!vcm)
		return 0;
	msg.addr = current_vcm->attr->addr;
	msg.flags = 0;
	msg.len = 2;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL) {
		pr_err("%s: Unable to get adapter\n", __func__);
		return -1;
	}
	msg.buf = (u8 *)&buf;
	while (written_num < current_vcm->init.num) {
		buf.reg = (u8)current_vcm->init.tab[written_num].reg;
		buf.val = (u8)current_vcm->init.tab[written_num].val;
		ret = i2c_transfer(adapter, &msg, 1);
		if (ret < 0)
			return ret;
		written_num++;
	};
	return ret;
}
static int dw9804_g_focus_twsi(struct v4l2_subdev *subdev, u16 *val)
{
	int ret = 0;
	u8 reg;
	u8 val1;
	struct i2c_adapter *adapter;
	struct i2c_msg msg = {
			.flags	= I2C_M_RD,
			.len	= 2,
			.buf	= (u8 *)val,
	};
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	msg.addr = current_vcm->attr->addr;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	*val = 0;
	reg = (u8)current_vcm->pos_reg_lsb;
	ret = twsi_read_i2c_bb(msg.addr, reg, (u8 *)val);
	*val = *val & 0xff;
	reg = (u8)current_vcm->pos_reg_msb;
	ret = twsi_read_i2c_bb(msg.addr, reg, &val1);
	*val = ((val1 & 0x3) << 8) | *val;
	return ret;
}
static int dw9804_s_focus_twsi(struct v4l2_subdev *subdev, u16 val)
{
	int ret = 0;
	struct i2c_adapter *adapter;
	struct reg_tab_bb buf;
	struct i2c_msg msg;
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	msg.addr = current_vcm->attr->addr;
	msg.flags = 0;
	msg.len = 2;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	msg.buf = (u8 *)&buf;
	buf.reg = (u8)current_vcm->pos_reg_msb;
	buf.val = (u8)((val >> 8) & 0x3);
	ret = i2c_transfer(adapter, &msg, 1);
	buf.reg = (u8)current_vcm->pos_reg_lsb;
	buf.val = (u8)(val & 0xff);
	ret |= i2c_transfer(adapter, &msg, 1);
	return ret;
}
struct vcm_ops dw9804_ops = {
	.init = dw9804_s_init,
	.g_register = dw9804_g_focus_twsi,
	.s_register = dw9804_s_focus_twsi,
};
static struct vcm_type vcm_dw9804 = {
	.name = "dw9804",
	.type = VCM_DW9804,
	.attr = &vcm_attr_8BIT,
	.pos_reg_msb = 0x03,
	.pos_reg_lsb = 0x04,
	.id = {
		.tab = DW9804_id,
		.num = N_DW9804_ID,
	},
	.init = {
		.tab = DW9804_init,
		.num = N_DW9804_INIT,
	},
	.ops = &dw9804_ops,
};
#define DW9714L_DATA_SHIFT 0x4
static int dw9714_g_focus_twsi(struct v4l2_subdev *subdev, u16 *val)
{
	int ret = 0;
	struct i2c_adapter *adapter;
	struct i2c_msg msg = {
			.flags	= I2C_M_RD,
			.len	= 2,
			.buf	= (u8 *)val,
	};
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	msg.addr = current_vcm->attr->addr;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	*val = 0;
	ret = i2c_transfer(adapter, &msg, 1);
	*val = (*val >> DW9714L_DATA_SHIFT) & 0xffff;
	return ret;
}
static int dw9714_s_focus_twsi(struct v4l2_subdev *subdev, u16 val)
{
	int ret = 0;
	struct i2c_adapter *adapter;
	u8 val_buf[2];
	struct i2c_msg msg;
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	msg.addr = current_vcm->attr->addr;
	msg.flags = 0;
	msg.len = 2;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;

	msg.buf = val_buf;
	val = (val << 4) | 0x0f;
	msg.buf[0] = (val>>8) & 0x3f;
	msg.buf[1] = val & 0xff;
	ret = i2c_transfer(adapter, &msg, 1);
	return ret;
}
struct vcm_ops dw9714_ops = {
	.g_register = dw9714_g_focus_twsi,
	.s_register = dw9714_s_focus_twsi,
};
static struct vcm_type vcm_dw9714 = {
	.name = "dw9714",
	.type = VCM_DW9714,
	.attr = &vcm_attr_16BIT,
	.ops = &dw9714_ops,
};

struct regval_tab dw9718_id[] = {
	{0x00, 0xF1, 0xff},
};
struct regval_tab dw9718_init[] = {
	/* control register */
	{0x01, 0x00, 0x03},
	/* mode register */
	{0x02, 0x00, 0xC0},
	/* period register */
	{0x03, 0x7C, 0xFF},
};
static int dw9718_s_init(struct v4l2_subdev *subdev)
{
	int ret = 0;
	int written_num = 0;
	struct i2c_adapter *adapter;
	struct reg_tab_bb buf;
	struct i2c_msg msg;
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	if (!vcm)
		return 0;

	msg.addr = current_vcm->attr->addr;
	msg.flags = 0;
	msg.len = 2;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL) {
		pr_err("%s: Unable to get adapter\n", __func__);
		return -1;
	}
	msg.buf = (u8 *)&buf;
	while (written_num < current_vcm->init.num) {
		buf.reg = (u8)current_vcm->init.tab[written_num].reg;
		buf.val = (u8)current_vcm->init.tab[written_num].val;
		ret |= i2c_transfer(adapter, &msg, 1);
		written_num++;
	}
	return 0;
}
static int dw9718_g_focus_twsi(struct v4l2_subdev *subdev, u16 *val)
{
	int ret = 0;
	u8 reg;
	u8 val1;
	struct i2c_adapter *adapter;
	struct i2c_msg msg = {
			.flags	= I2C_M_RD,
			.len	= 2,
			.buf	= (u8 *)val,
	};
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	msg.addr = current_vcm->attr->addr;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	*val = 0;
	reg = (u8)current_vcm->pos_reg_lsb;
	ret = twsi_read_i2c_bb(msg.addr, reg, (u8 *)val);
	*val = *val & 0xff;
	reg = (u8)current_vcm->pos_reg_msb;
	ret = twsi_read_i2c_bb(msg.addr, reg, &val1);
	*val = ((val1 & 0x3) << 8) | *val;
	return ret;
}
static int dw9718_s_focus_twsi(struct v4l2_subdev *subdev, u16 val)
{
	int ret = 0;
	struct i2c_adapter *adapter;
	struct reg_tab_bb buf;
	struct i2c_msg msg;
	struct vcm_subdev *vcm;
	struct vcm_type *current_vcm;
	vcm = to_b52_vcm(subdev);
	current_vcm = vcm->current_type;
	msg.addr = current_vcm->attr->addr;
	msg.flags = 0;
	msg.len = 2;
	adapter = i2c_get_adapter(2);
	if (adapter == NULL)
		return -1;
	msg.buf = (u8 *)&buf;
	buf.reg = (u8)current_vcm->pos_reg_msb;
	buf.val = (u8)((val >> 8) & 0x3);
	ret = i2c_transfer(adapter, &msg, 1);
	buf.reg = (u8)current_vcm->pos_reg_lsb;
	buf.val = (u8)(val & 0xff);
	ret |= i2c_transfer(adapter, &msg, 1);
	return ret;
}
struct vcm_ops dw9718_ops = {
	.init = dw9718_s_init,
	.g_register = dw9718_g_focus_twsi,
	.s_register = dw9718_s_focus_twsi,
};
#define N_DW9718_ID ARRAY_SIZE(dw9718_id)
#define N_DW9718_INIT ARRAY_SIZE(dw9718_init)
static struct vcm_type vcm_dw9718 = {
	.name = "dw9718",
	.type = VCM_DW9718,
	.attr = &vcm_attr_8BIT,
	.pos_reg_msb = 0x02,
	.pos_reg_lsb = 0x03,
	.id = {
		.tab = dw9718_id,
		.num = N_DW9718_ID,
	},
	.init = {
		.tab = dw9718_init,
		.num = N_DW9718_INIT,
	},
	.ops = &dw9718_ops,
};

static struct vcm_type *b52_vcm_type[10] = {
	[0] = NULL,
	[1] = &vcm_dw9714,
	[2] = NULL,
	[3] = NULL,
	[4] = &vcm_dw9804,
	[5] = &vcm_dw9718,
};

static struct v4l2_queryctrl vcm_qctrl[] = {
	{
		.id = V4L2_CID_ENUM_VCM,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "enum vcm",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0x0001,
		.flags = 0,
	}, {
	}
};

int vcm_subdev_create(struct device *parent,
					const char *name, int id, void *pdata)
{
	struct platform_device *vcm = devm_kzalloc(parent,
						sizeof(struct platform_device),
						GFP_KERNEL);
	int ret = 0;

	if (vcm == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	vcm->name = SUBDEV_DRV_NAME;
	vcm->id = id;
	vcm->dev.platform_data = pdata;
	ret = platform_device_register(vcm);
	if (ret < 0) {
		pr_err("unable to create vcm subdev: %d\n", ret);
		goto err;
	}

	return 0;
err:
	return -EINVAL;
}
EXPORT_SYMBOL(vcm_subdev_create);
/**************************** dispatching functions ***************************/

static int vcm_core_init(struct v4l2_subdev *vcm, u32 val)
{

	int ret;
	ret = v4l2_ctrl_handler_setup(vcm->ctrl_handler);
	if (ret < 0)
		pr_err("%s: setup hadnler failed\n", __func__);
	return 0;
}

static int vcm_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;
	int ret = -EINVAL;
	for (i = 0; i < ARRAY_SIZE(vcm_qctrl); i++)
		if (qc->id && qc->id == vcm_qctrl[i].id) {
			*qc = vcm_qctrl[i];
			ret = 0;
			break;
		}
	return ret;
}

static long vcm_core_ioctl(struct v4l2_subdev *vcm, unsigned int cmd, void *arg)
{
	return 0;
}
/* TODO: Add more hsd_OPS_FN here */
static int vcm_g_ctrl(struct v4l2_ctrl *ctrl)
{
	switch (ctrl->id) {
	case V4L2_CID_VCM_SELECT_TYPE:
		break;
	default:
		pr_err("%s: ctrl not support\n", __func__);
		return -EINVAL;
	}

	pr_debug("G_CTRL %08x:%d\n", ctrl->id, ctrl->val);

	return 0;
}


static int vcm_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vcm_subdev *vcm = container_of(
			ctrl->handler, struct vcm_subdev, vcm_ctrl.ctrl_hdl);
	switch (ctrl->id) {
	case V4L2_CID_VCM_SELECT_TYPE:
		if (ctrl->val < 0 || ctrl->val > VCM_NONE) {
			pr_err("%s: ctrl not support\n", __func__);
			return -EINVAL;
		}
		vcm->current_type = vcm->b52_vcm_type[ctrl->val];
		if (!pxa1928_is_a0())
			return 0;
		if (vcm->current_type->ops->init)
			vcm->current_type->ops->init(&vcm->subdev);
		break;
	default:
		pr_err("%s: ctrl not support\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_ctrl_ops vcm_ctrl_ops = {
	.g_volatile_ctrl = vcm_g_ctrl,
	.s_ctrl          = vcm_s_ctrl,
};
static struct v4l2_ctrl_config vcm_select_type_ctrl_cfg = {
	.ops = &vcm_ctrl_ops,
	.id = V4L2_CID_VCM_SELECT_TYPE,
	.name = "Select subdev type",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 0xff,
	.step = 1,
	.def = 0
};
int vcm_core_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}
static const struct v4l2_subdev_core_ops vcm_core_ops = {
	.s_power = &vcm_core_s_power,
	.init		= &vcm_core_init,
	.ioctl		= &vcm_core_ioctl,
	.queryctrl	= &vcm_queryctrl,
};
static const struct v4l2_subdev_video_ops vcm_video_ops;
static const struct v4l2_subdev_sensor_ops vcm_sensor_ops;
static const struct v4l2_subdev_pad_ops vcm_pad_ops;
/* default version of host subdev just dispatch every subdev call to guests */
static const struct v4l2_subdev_ops vcm_subdev_ops = {
	.core	= &vcm_core_ops,
	.video	= &vcm_video_ops,
	.sensor	= &vcm_sensor_ops,
	.pad	= &vcm_pad_ops,
};

/************************* host subdev implementation *************************/

static int vcm_subdev_open(struct v4l2_subdev *hsd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int vcm_subdev_close(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops vcm_subdev_internal_ops = {
	.open	= vcm_subdev_open,
	.close	= vcm_subdev_close,
};

static int vcm_subdev_remove(struct platform_device *pdev)
{
	struct vcm_subdev *vcm = platform_get_drvdata(pdev);

	if (unlikely(vcm == NULL))
		return -EINVAL;

	media_entity_cleanup(&vcm->subdev.entity);
	devm_kfree(vcm->dev, vcm);
	return 0;
}
int b52_g_vcm_info(struct v4l2_subdev *subdev, struct vcm_type **v_info)
{
	struct vcm_subdev *vcm = to_b52_vcm(subdev);
	if (vcm->current_type == NULL) {
		pr_err("pls select vcm type\n");
		return 1;
	}
	*v_info = vcm->current_type;
	return 0;
}
static struct b52_vcm_ops b52_vcm_def_ops = {
	.g_vcm_info          = b52_g_vcm_info,
};

static int vcm_subdev_probe(struct platform_device *pdev)
{
	/* pdev->dev.platform_data */
	int i;
	struct v4l2_subdev *sd, *host_sd;
	struct vcm_ctrls *ctrls;
	int ret = 0;
	struct vcm_data *vdata;
	struct vcm_subdev *vcm = devm_kzalloc(&pdev->dev,
					sizeof(*vcm), GFP_KERNEL);
	if (unlikely(vcm == NULL))
		return -ENOMEM;

	platform_set_drvdata(pdev, vcm);
	vcm->dev = &pdev->dev;
	ctrls = &vcm->vcm_ctrl;
	ret = v4l2_ctrl_handler_init(&ctrls->ctrl_hdl, 1);
	if (ret < 0)
		goto err_ctrl;

	ctrls->select_type = v4l2_ctrl_new_custom(&ctrls->ctrl_hdl,
				&vcm_select_type_ctrl_cfg, NULL);

	vcm->subdev.ctrl_handler = &ctrls->ctrl_hdl;
	sd = &vcm->subdev;
	ret = media_entity_init(&sd->entity, 1, &vcm->pad, 0);
	if (ret < 0)
		goto err;
	v4l2_subdev_init(sd, &vcm_subdev_ops);
	sd->internal_ops = &vcm_subdev_internal_ops;
	vcm->ops = b52_vcm_def_ops;
	v4l2_set_subdevdata(sd, vcm);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strcpy(sd->name, pdev->name);
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV_VCM;
	vdata = (struct vcm_data *)vcm->dev->platform_data;
	ret = v4l2_device_register_subdev(vdata->v4l2_dev,
						&vcm->subdev);
	if (ret < 0) {
		pr_err("register vcm subdev ret:%d\n", ret);
		goto err;
	}
	for (i = 0; i < VCM_NONE; i++)
		vcm->b52_vcm_type[i] = b52_vcm_type[i];
	vcm->current_type = b52_vcm_type[3];
#ifdef CONFIG_HOST_SUBDEV
	host_subdev_add_guest(vdata->hsd,  &vcm->subdev);
	host_sd =  &vdata->hsd->isd.subdev;
	v4l2_ctrl_add_handler(host_sd->ctrl_handler, sd->ctrl_handler, NULL);
#endif
	dev_info(vcm->dev, "vcm subdev created\n");
	return ret;
err:
	vcm_subdev_remove(pdev);
err_ctrl:
	devm_kfree(vcm->dev, vcm);
	return ret;
}

static struct platform_driver __refdata vcm_subdev_pdrv = {
	.probe	= vcm_subdev_probe,
	.remove	= vcm_subdev_remove,
	.driver	= {
		.name   = SUBDEV_DRV_NAME,
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(vcm_subdev_pdrv);

MODULE_DESCRIPTION("vcm v4l2 subdev bus driver");
MODULE_AUTHOR("Chunlin Hu <huchl@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:vcm-subdev-pdrv");
