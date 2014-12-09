/*
 * Marvell 88PM886 ONKEY driver
 *
 * Copyright (C) 2014 Marvell International Ltd.
 * Yi Zhang <yizhang@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/mfd/88pm88x.h>
#include <linux/mfd/88pm88x.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of.h>

#define PM88X_ONKEY_STS1		(0x1 << 0)

#define PM88X_GPIO0_HW_RST1_N		(0x6 << 1)
#define PM88X_GPIO0_HW_RST2_N		(0x7 << 1)

#define PM88X_GPIO1_HW_RST1_N		(0x6 << 5)
#define PM88X_GPIO1_HW_RST2_N		(0x7 << 5)

#define PM88X_GPIO2_HW_RST1		(0x6 << 1)
#define PM88X_GPIO2_HW_RST2		(0x7 << 1)

#define PM88X_HWRST_DB_MSK		(0x1 << 7)
#define PM88X_HWRST_DB_2S		(0x0 << 7)
#define PM88X_HWRST_DB_7S		(0x1 << 7)

#define PM88X_LONKEY_PRESS_TIME_MSK	(0xf0)
#define PM88X_LONG_KEY_DELAY		(16)	/* 1 .. 16 seconds */
#define PM88X_LONKEY_PRESS_TIME		((PM88X_LONG_KEY_DELAY-1) << 4)
#define PM88X_LONKEY_RESTOUTN_PULSE_MSK (0x3)

#define PM88X_LONG_ONKEY_EN1           (0 << 1)
#define PM88X_LONG_ONKEY_EN2           (1 << 1)

struct pm88x_onkey_info {
	struct input_dev *idev;
	struct pm88x_chip *pm88x;
	struct regmap *map;
	int irq;
	int gpio_number;
};

static int pm88x_config_gpio(struct pm88x_onkey_info *info)
{
	if (!info || !info->map) {
		pr_err("%s: No chip information!\n", __func__);
		return -ENODEV;
	}

	/* choose HW_RST1_N for GPIO, only toggle RESETOUTN */
	switch (info->gpio_number) {
	case 0:
		regmap_update_bits(info->map, PM88X_GPIO_CTRL1,
				   PM88X_GPIO0_MODE_MSK, PM88X_GPIO0_HW_RST1_N);
		break;
	case 1:
		regmap_update_bits(info->map, PM88X_GPIO_CTRL1,
				   PM88X_GPIO1_MODE_MSK, PM88X_GPIO1_HW_RST1_N);
		break;
	case 2:
		regmap_update_bits(info->map, PM88X_GPIO_CTRL2,
				   PM88X_GPIO2_MODE_MSK, PM88X_GPIO2_HW_RST1);
		break;
	default:
		dev_err(info->idev->dev.parent, "use the wrong GPIO, exit 0\n");
		return 0;
	}
	/* 0xe2: set debounce period of ONKEY as 7s, when used with GPIO */
	regmap_update_bits(info->map, PM88X_AON_CTRL2,
			   PM88X_HWRST_DB_MSK , PM88X_HWRST_DB_7S);

	/* 0xe3: set debounce period of ONKEY as 16s */
	regmap_update_bits(info->map, PM88X_AON_CTRL3,
			   PM88X_LONKEY_PRESS_TIME_MSK,
			   PM88X_LONKEY_PRESS_TIME);

	/* 0xe3: set duration of RESETOUTN pulse as 1s */
	regmap_update_bits(info->map, PM88X_AON_CTRL3,
			   PM88X_LONKEY_RESTOUTN_PULSE_MSK, 1);

	/* 0xe4: enable LONG_ONKEY_DETECT2, onkey reset system */
	regmap_update_bits(info->map, PM88X_AON_CTRL4,
			   PM88X_LONG_ONKEY_EN2, PM88X_LONG_ONKEY_EN2);
	return 0;
}

static irqreturn_t pm88x_onkey_handler(int irq, void *data)
{
	struct pm88x_onkey_info *info = data;
	int ret = 0;
	unsigned int val;

	/* reset the LONGKEY reset time */
	regmap_update_bits(info->map, PM88X_MISC_CONFIG1,
			   PM88X_LONKEY_RST, PM88X_LONKEY_RST);

	ret = regmap_read(info->map, PM88X_STATUS1, &val);
	if (ret < 0) {
		dev_err(info->idev->dev.parent,
			"failed to read status: %d\n", ret);
		return IRQ_NONE;
	}
	val &= PM88X_ONKEY_STS1;

	input_report_key(info->idev, KEY_POWER, val);
	input_sync(info->idev);

	return IRQ_HANDLED;
}

static SIMPLE_DEV_PM_OPS(pm88x_onkey_pm_ops, NULL, NULL);

static int pm88x_onkey_dt_init(struct device_node *np,
			       struct pm88x_onkey_info *info)
{
	int ret;
	if (!info) {
		pr_err("%s: No chip information!\n", __func__);
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "marvell,pm88x-onkey-gpio-number",
				   &info->gpio_number);
	if (ret < 0) {
		dev_warn(info->idev->dev.parent, "No GPIO for long onkey.\n");
		return 0;
	}

	return 0;
}

static int pm88x_onkey_probe(struct platform_device *pdev)
{

	struct pm88x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device_node *node = pdev->dev.of_node;
	struct pm88x_onkey_info *info;
	int err;

	info = devm_kzalloc(&pdev->dev, sizeof(struct pm88x_onkey_info),
			    GFP_KERNEL);
	if (!info || !chip)
		return -ENOMEM;
	info->pm88x = chip;

	/* give the gpio number as a default value */
	info->gpio_number = -1;
	err = pm88x_onkey_dt_init(node, info);
	if (err < 0)
		return -ENODEV;

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource!\n");
		err = -EINVAL;
		goto out;
	}

	info->map = info->pm88x->base_regmap;
	if (!info->map) {
		dev_err(&pdev->dev, "No regmap handler!\n");
		err = -EINVAL;
		goto out;
	}

	info->idev = input_allocate_device();
	if (!info->idev) {
		dev_err(&pdev->dev, "Failed to allocate input dev\n");
		err = -ENOMEM;
		goto out;
	}

	info->idev->name = "88pm88x_on";
	info->idev->phys = "88pm88x_on/input0";
	info->idev->id.bustype = BUS_I2C;
	info->idev->dev.parent = &pdev->dev;
	info->idev->evbit[0] = BIT_MASK(EV_KEY);
	__set_bit(KEY_POWER, info->idev->keybit);

	err = devm_request_threaded_irq(&pdev->dev, info->irq, NULL,
					pm88x_onkey_handler,
					IRQF_ONESHOT | IRQF_NO_SUSPEND,
					"onkey", info);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to request IRQ: #%d: %d\n",
			info->irq, err);
		goto out_register;
	}

	err = input_register_device(info->idev);
	if (err) {
		dev_err(&pdev->dev, "Can't register input device: %d\n", err);
		goto out_register;
	}

	platform_set_drvdata(pdev, info);

	device_init_wakeup(&pdev->dev, 1);

	err = pm88x_config_gpio(info);
	if (err < 0) {
		dev_err(&pdev->dev, "Can't configure gpio: %d\n", err);
		goto out_register;
	}
	return 0;

out_register:
	input_free_device(info->idev);
out:
	return err;
}

static int pm88x_onkey_remove(struct platform_device *pdev)
{
	struct pm88x_onkey_info *info = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	devm_free_irq(&pdev->dev, info->irq, info);

	input_unregister_device(info->idev);

	devm_kfree(&pdev->dev, info);

	return 0;
}

static const struct of_device_id pm88x_onkey_dt_match[] = {
	{ .compatible = "marvell,88pm88x-onkey", },
	{ },
};
MODULE_DEVICE_TABLE(of, pm88x_onkey_dt_match);

static struct platform_driver pm88x_onkey_driver = {
	.driver = {
		.name = "88pm88x-onkey",
		.owner = THIS_MODULE,
		.pm = &pm88x_onkey_pm_ops,
		.of_match_table = of_match_ptr(pm88x_onkey_dt_match),
	},
	.probe = pm88x_onkey_probe,
	.remove = pm88x_onkey_remove,
};
module_platform_driver(pm88x_onkey_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Marvell 88PM88x ONKEY driver");
MODULE_AUTHOR("Yi Zhang <yizhang@marvell.com>");
MODULE_ALIAS("platform:88pm88x-onkey");
