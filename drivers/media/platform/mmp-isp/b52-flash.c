#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/b52-flash.h>
#include <linux/leds.h>
#include <uapi/media/b52_api.h>
#include <linux/mfd/88pm88x.h>

#define SUBDEV_DRV_NAME	"flash-pdrv"

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
static struct v4l2_queryctrl flash_qctrl[] = {
	{
		.id = V4L2_CID_ENUM_FLASH,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "enum flash",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0x0001,
		.flags = 0,
	}, {
	}
};
static int flash_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	int i;
	int ret = -EINVAL;
	for (i = 0; i < ARRAY_SIZE(flash_qctrl); i++)
		if (qc->id && qc->id == flash_qctrl[i].id) {
			*qc = flash_qctrl[i];
			ret = 0;
			break;
		}
	return ret;
}

int flash_subdev_create(struct device *parent,
					const char *name, int id, void *pdata)
{
	struct platform_device *flash = devm_kzalloc(parent,
						sizeof(struct platform_device),
						GFP_KERNEL);
	int ret = 0;

	if (flash == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	flash->name = SUBDEV_DRV_NAME;
	flash->id = id;
	flash->dev.platform_data = pdata;
	ret = platform_device_register(flash);
	if (ret < 0) {
		pr_err("unable to create flash subdev: %d\n", ret);
		goto err;
	}

	return 0;
err:
	return -EINVAL;
}
EXPORT_SYMBOL(flash_subdev_create);
static int flash_core_init(struct v4l2_subdev *flash, u32 val)
{
	int ret;
	ret = v4l2_ctrl_handler_setup(flash->ctrl_handler);
	if (ret < 0)
		pr_err("%s: setup hadnler failed\n", __func__);
	return 0;
}

static long flash_core_ioctl(struct v4l2_subdev *flash,
				unsigned int cmd, void *arg)
{
	return 0;
}

int flash_core_s_power(struct v4l2_subdev *sd, int on)
{
	return 0;
}
/* TODO: Add more hsd_OPS_FN here */

static const struct v4l2_subdev_core_ops flash_core_ops = {
	.s_power = &flash_core_s_power,
	.init		= &flash_core_init,
	.ioctl		= &flash_core_ioctl,
	.queryctrl = &flash_queryctrl,
};
static const struct v4l2_subdev_video_ops flash_video_ops;
static const struct v4l2_subdev_sensor_ops flash_sensor_ops;
static const struct v4l2_subdev_pad_ops flash_pad_ops;
/* default version of host subdev just dispatch every subdev call to guests */
static const struct v4l2_subdev_ops flash_subdev_ops = {
	.core	= &flash_core_ops,
	.video	= &flash_video_ops,
	.sensor	= &flash_sensor_ops,
	.pad	= &flash_pad_ops,
};

/************************* host subdev implementation *************************/

static int flash_subdev_open(struct v4l2_subdev *hsd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
}

static int flash_subdev_close(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops flash_subdev_internal_ops = {
	.open	= flash_subdev_open,
	.close	= flash_subdev_close,
};

static int flash_subdev_remove(struct platform_device *pdev)
{
	struct flash_subdev *flash = platform_get_drvdata(pdev);

	if (unlikely(flash == NULL))
		return -EINVAL;

	media_entity_cleanup(&flash->subdev.entity);
	devm_kfree(flash->dev, flash);
	return 0;
}
static int b52_config_flash(
		struct flash_subdev *flash)
{
	pr_debug("%s\n", __func__);
	return 0;
}

static int b52_set_flash(
		struct flash_subdev *flash, int on)
{
	if (on)
		ledtrig_flash_ctrl(1);
	else
		ledtrig_flash_ctrl(0);
	flash->flash_status = on;

	return 0;
}

static int b52_set_torch(
		struct flash_subdev *flash, int on)
{
	if (on)
		ledtrig_torch_ctrl(1);
	else
		ledtrig_torch_ctrl(0);
	flash->flash_status = on;

	return 0;
}


static int b52_get_flash_duration(
		struct flash_subdev *flash, int *value)
{
	return get_flash_duration(value);
}

static int flash_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct flash_subdev *flash = container_of(
			ctrl->handler, struct flash_subdev,
			flash_ctrl.ctrl_hdl);
	switch (ctrl->id) {
	case V4L2_CID_FLASH_FAULT:
		break;

	case V4L2_CID_FLASH_STROBE_STATUS:
		ctrl->val = flash->flash_status;
		break;

	case V4L2_CID_FLASH_SELECT_TYPE:
		break;

	case V4L2_CID_PRIVATE_FLASH_DURATION:
		return b52_get_flash_duration(flash, &ctrl->val);

	default:
		pr_err("%s: ctrl not support\n", __func__);
		return -EINVAL;
	}

	pr_debug("G_CTRL %08x:%d\n", ctrl->id, ctrl->val);

	return 0;
}

static int flash_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct flash_subdev *flash = container_of(
			ctrl->handler, struct flash_subdev,
			flash_ctrl.ctrl_hdl);

	/*FIXME: implement flash config and set function*/
	switch (ctrl->id) {

	case V4L2_CID_FLASH_LED_MODE:
		flash->led_mode = ctrl->val;
		b52_config_flash(flash);
		break;

	case V4L2_CID_FLASH_STROBE_SOURCE:
		flash->strobe_source = ctrl->val;
		b52_config_flash(flash);
		break;

	case V4L2_CID_FLASH_STROBE:
		if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH)
			b52_set_flash(flash, 1);
		else if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH)
			b52_set_torch(flash, 1);
		else
			return -EBUSY;
		break;

	case V4L2_CID_FLASH_STROBE_STOP:
		if (flash->led_mode == V4L2_FLASH_LED_MODE_FLASH)
			b52_set_flash(flash, 0);
		else if (flash->led_mode == V4L2_FLASH_LED_MODE_TORCH)
			b52_set_torch(flash, 0);
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

		b52_config_flash(flash);
		break;

	case V4L2_CID_FLASH_TORCH_INTENSITY:
		/*FIXME*/
		flash->torch_current = (ctrl->val - TORCH_INTENSITY_MIN)
			/ TORCH_INTENSITY_STEP;

		if (flash->led_mode != V4L2_FLASH_LED_MODE_TORCH)
			break;

		b52_config_flash(flash);
		break;
	case V4L2_CID_FLASH_SELECT_TYPE:
		break;
	default:
		pr_err("%s: ctrl %x not support\n", __func__, ctrl->id);
		return -EINVAL;
	}

	return 0;
}

static struct v4l2_ctrl_ops flash_ctrl_ops = {
	.g_volatile_ctrl = flash_g_ctrl,
	.s_ctrl          = flash_s_ctrl,
};
/* TODO: Add more hsd_OPS_FN here */
static struct v4l2_ctrl_config flash_select_type_ctrl_cfg = {
	.ops = &flash_ctrl_ops,
	.id = V4L2_CID_FLASH_SELECT_TYPE,
	.name = "Select subdev type",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0
};

static struct v4l2_ctrl_config b52_flash_duration_cfg = {
	.ops = &flash_ctrl_ops,
	.id = V4L2_CID_PRIVATE_FLASH_DURATION,
	.name = "B52 Flash durtation",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = 0xffff,
	.step = 1,
	.def = 1,
};

static int flash_init_ctrls(struct flash_subdev *flash)
{
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(&flash->flash_ctrl.ctrl_hdl, 10);
	v4l2_ctrl_new_std_menu(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_LED_MODE, 2, ~7,
			V4L2_FLASH_LED_MODE_NONE);

	flash->strobe_source = V4L2_FLASH_STROBE_SOURCE_SOFTWARE;
	v4l2_ctrl_new_std_menu(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_STROBE_SOURCE, 0, ~1,
			V4L2_FLASH_STROBE_SOURCE_SOFTWARE);

	v4l2_ctrl_new_std(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_STROBE, 0, 1, 1, 0);

	v4l2_ctrl_new_std(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_STROBE_STOP, 0, 1, 1, 0);

	ctrl = v4l2_ctrl_new_std(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_STROBE_STATUS, 0, 1, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	flash->timeout = FLASH_TIMEOUT_MIN;
	v4l2_ctrl_new_std(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_TIMEOUT, FLASH_TIMEOUT_MIN,
			FLASH_TIMEOUT_MAX, FLASH_TIMEOUT_STEP,
			FLASH_TIMEOUT_MIN);

	flash->flash_current = FLASH_INTENSITY_MIN;
	v4l2_ctrl_new_std(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_INTENSITY, FLASH_INTENSITY_MIN,
			FLASH_INTENSITY_MAX, FLASH_INTENSITY_STEP,
			FLASH_INTENSITY_MIN);

	v4l2_ctrl_new_std(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_TORCH_INTENSITY,
			TORCH_INTENSITY_MIN, TORCH_INTENSITY_MAX,
			TORCH_INTENSITY_STEP,
			TORCH_INTENSITY_MIN);

	ctrl = v4l2_ctrl_new_std(&flash->flash_ctrl.ctrl_hdl,
			&flash_ctrl_ops,
			V4L2_CID_FLASH_FAULT, 0,
			V4L2_FLASH_FAULT_OVER_VOLTAGE |
			V4L2_FLASH_FAULT_TIMEOUT |
			V4L2_FLASH_FAULT_OVER_TEMPERATURE |
			V4L2_FLASH_FAULT_SHORT_CIRCUIT, 0, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE |
			V4L2_CTRL_FLAG_READ_ONLY;

	flash->flash_ctrl.select_type = v4l2_ctrl_new_custom(
				&flash->flash_ctrl.ctrl_hdl,
				&flash_select_type_ctrl_cfg, NULL);

	ctrl = v4l2_ctrl_new_custom(
				&flash->flash_ctrl.ctrl_hdl,
				&b52_flash_duration_cfg, NULL);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE | V4L2_CTRL_FLAG_READ_ONLY;

	return flash->flash_ctrl.ctrl_hdl.error;
}
static int flash_subdev_probe(struct platform_device *pdev)
{
	/* pdev->dev.platform_data */

	struct v4l2_subdev *sd, *host_sd;
	int ret = 0;
	struct flash_data *fdata;
	struct flash_subdev *flash = devm_kzalloc(&pdev->dev,
					sizeof(*flash), GFP_KERNEL);
	if (unlikely(flash == NULL))
		return -ENOMEM;

	platform_set_drvdata(pdev, flash);
	flash->dev = &pdev->dev;
	ret = flash_init_ctrls(flash);
	if (ret < 0)
			goto err_ctrl;
	flash->subdev.ctrl_handler = &flash->flash_ctrl.ctrl_hdl;

	sd = &flash->subdev;
	ret = media_entity_init(&sd->entity, 1, &flash->pad, 0);
	if (ret < 0)
		goto err;
	v4l2_subdev_init(sd, &flash_subdev_ops);
	sd->internal_ops = &flash_subdev_internal_ops;
	v4l2_set_subdevdata(sd, flash);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strcpy(sd->name, pdev->name);
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	fdata = (struct flash_data *)flash->dev->platform_data;
	ret = v4l2_device_register_subdev(fdata->v4l2_dev,
						&flash->subdev);
	if (ret < 0) {
		pr_err("register flash subdev ret:%d\n", ret);
		goto err;
	}
#ifdef CONFIG_HOST_SUBDEV
	host_subdev_add_guest(fdata->hsd, &flash->subdev);
	host_sd =  &fdata->hsd->isd.subdev;
	v4l2_ctrl_add_handler(host_sd->ctrl_handler, sd->ctrl_handler, NULL);
#endif
	dev_info(flash->dev, "flash subdev created\n");
	return ret;
err:
	flash_subdev_remove(pdev);
err_ctrl:
	devm_kfree(flash->dev, flash);
	return ret;
}

static struct platform_driver __refdata flash_subdev_pdrv = {
	.probe	= flash_subdev_probe,
	.remove	= flash_subdev_remove,
	.driver	= {
		.name   = SUBDEV_DRV_NAME,
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(flash_subdev_pdrv);

MODULE_DESCRIPTION("flash v4l2 subdev bus driver");
MODULE_AUTHOR("Chunlin Hu <huchl@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:flash-subdev-pdrv");
