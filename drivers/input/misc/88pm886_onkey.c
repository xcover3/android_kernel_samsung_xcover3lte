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
#include <linux/mfd/88pm886.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/of.h>

#define PM886_ONKEY_STS1		(0 << 1)

#define PM886_GPIO0_HW_RST1_N		(0x6 << 1)
#define PM886_GPIO0_HW_RST2_N		(0x7 << 1)

#define PM886_GPIO1_HW_RST1_N		(0x6 << 5)
#define PM886_GPIO1_HW_RST2_N		(0x7 << 5)

#define PM886_GPIO2_HW_RST1		(0x6 << 1)
#define PM886_GPIO2_HW_RST2		(0x7 << 1)

#define PM886_HWRST_DB_MSK		(0x1 << 7)
#define PM886_HWRST_DB_2S		(0x0 << 7)
#define PM886_HWRST_DB_7S		(0x1 << 7)

#define PM886_LONKEY_PRESS_TIME_MSK	(0xf0)
#define PM886_LONG_KEY_DELAY		(16)	/* 1 .. 16 seconds */
#define PM886_LONKEY_PRESS_TIME		((PM886_LONG_KEY_DELAY-1) << 4)
#define PM886_LONKEY_RESTOUTN_PULSE_MSK (0x3)

#define PM886_LONG_ONKEY_EN1           (0 << 1)
#define PM886_LONG_ONKEY_EN2           (1 << 1)

struct pm886_onkey_info {
	struct input_dev *idev;
	struct pm886_chip *pm886;
	struct regmap *map;
	int irq;
	int gpio_number;
};

static int pm886_config_gpio(struct pm886_onkey_info *info)
{
	if (!info || !info->map) {
		pr_err("%s: No chip information!\n", __func__);
		return -ENODEV;
	}

	/* choose HW_RST1_N for GPIO, only toggle RESETOUTN */
	switch (info->gpio_number) {
	case 0:
		regmap_update_bits(info->map, PM886_GPIO_CTRL1,
				   PM886_GPIO0_MODE_MSK, PM886_GPIO0_HW_RST1_N);
		break;
	case 1:
		regmap_update_bits(info->map, PM886_GPIO_CTRL1,
				   PM886_GPIO1_MODE_MSK, PM886_GPIO1_HW_RST1_N);
		break;
	case 2:
		regmap_update_bits(info->map, PM886_GPIO_CTRL2,
				   PM886_GPIO2_MODE_MSK, PM886_GPIO2_HW_RST1);
		break;
	default:
		dev_err(info->idev->dev.parent, "use the wrong GPIO, exit 0\n");
		return 0;
	}
	/* 0xe2: set debounce period of ONKEY as 7s, when used with GPIO */
	regmap_update_bits(info->map, PM886_AON_CTRL2,
			   PM886_HWRST_DB_MSK , PM886_HWRST_DB_7S);

	/* 0xe3: set debounce period of ONKEY as 16s */
	regmap_update_bits(info->map, PM886_AON_CTRL3,
			   PM886_LONKEY_PRESS_TIME_MSK,
			   PM886_LONKEY_PRESS_TIME);

	/* 0xe3: set duration of RESETOUTN pulse as 1s */
	regmap_update_bits(info->map, PM886_AON_CTRL3,
			   PM886_LONKEY_RESTOUTN_PULSE_MSK, 1);

	/* 0xe4: enable LONG_ONKEY_DETECT2, onkey reset system */
	regmap_update_bits(info->map, PM886_AON_CTRL4,
			   PM886_LONG_ONKEY_EN2, PM886_LONG_ONKEY_EN2);
	return 0;
}

static irqreturn_t pm886_onkey_handler(int irq, void *data)
{
	struct pm886_onkey_info *info = data;
	int ret = 0;
	unsigned int val;

	/* reset the LONGKEY reset time */
	regmap_update_bits(info->map, PM886_MISC_CONFIG1,
			   PM886_LONKEY_RST, PM886_LONKEY_RST);

	ret = regmap_read(info->map, PM886_STATUS1, &val);
	if (ret < 0) {
		dev_err(info->idev->dev.parent,
			"failed to read status: %d\n", ret);
		return IRQ_NONE;
	}
	val &= PM886_ONKEY_STS1;

	input_report_key(info->idev, KEY_POWER, val);
	input_sync(info->idev);

	return IRQ_HANDLED;
}

static SIMPLE_DEV_PM_OPS(pm886_onkey_pm_ops, NULL, NULL);

static int pm886_onkey_dt_init(struct device_node *np,
			       struct pm886_onkey_info *info)
{
	int ret;
	if (!info) {
		pr_err("%s: No chip information!\n", __func__);
		return -ENODEV;
	}

	ret = of_property_read_u32(np, "marvell,pm886-onkey-gpio-number",
				   &info->gpio_number);
	if (ret < 0) {
		dev_warn(info->idev->dev.parent, "No GPIO for long onkey.\n");
		return 0;
	}

	return 0;
}

static int pm886_onkey_probe(struct platform_device *pdev)
{

	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device_node *node = pdev->dev.of_node;
	struct pm886_onkey_info *info;
	int err;

	info = devm_kzalloc(&pdev->dev, sizeof(struct pm886_onkey_info),
			    GFP_KERNEL);
	if (!info || !chip)
		return -ENOMEM;
	info->pm886 = chip;

	/* give the gpio number as a default value */
	info->gpio_number = -1;
	err = pm886_onkey_dt_init(node, info);
	if (err < 0)
		return -ENODEV;

	info->irq = platform_get_irq(pdev, 0);
	if (info->irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource!\n");
		err = -EINVAL;
		goto out;
	}

	info->map = info->pm886->base_regmap;
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

	info->idev->name = "88pm886_on";
	info->idev->phys = "88pm886_on/input0";
	info->idev->id.bustype = BUS_I2C;
	info->idev->dev.parent = &pdev->dev;
	info->idev->evbit[0] = BIT_MASK(EV_KEY);
	__set_bit(KEY_POWER, info->idev->keybit);

	err = devm_request_threaded_irq(&pdev->dev, info->irq, NULL,
					pm886_onkey_handler,
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

	err = pm886_config_gpio(info);
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

static int pm886_onkey_remove(struct platform_device *pdev)
{
	struct pm886_onkey_info *info = platform_get_drvdata(pdev);

	device_init_wakeup(&pdev->dev, 0);
	devm_free_irq(&pdev->dev, info->irq, info);

	input_unregister_device(info->idev);

	devm_kfree(&pdev->dev, info);

	return 0;
}

static const struct of_device_id pm886_onkey_dt_match[] = {
	{ .compatible = "marvell,88pm886-onkey", },
	{ },
};
MODULE_DEVICE_TABLE(of, pm886_onkey_dt_match);

static struct platform_driver pm886_onkey_driver = {
	.driver = {
		.name = "88pm886-onkey",
		.owner = THIS_MODULE,
		.pm = &pm886_onkey_pm_ops,
		.of_match_table = of_match_ptr(pm886_onkey_dt_match),
	},
	.probe = pm886_onkey_probe,
	.remove = pm886_onkey_remove,
};
module_platform_driver(pm886_onkey_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Marvell 88PM886 ONKEY driver");
MODULE_AUTHOR("Yi Zhang <yizhang@marvell.com>");
MODULE_ALIAS("platform:88pm886-onkey");
