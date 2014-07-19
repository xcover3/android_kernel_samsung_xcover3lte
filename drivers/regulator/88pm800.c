/*
 * Regulators driver for Marvell 88PM800
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Joseph(Yossi) Hanin <yhanin@marvell.com>
 * Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/88pm80x.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
#include "88pm8xx-regulator.h"

static int pm800_set_voltage(struct regulator_dev *rdev,
			     int min_uv, int max_uv, unsigned *selector)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);
	int ret, old_selector = -1, best_val = 0;

	if (!info)
		return -EINVAL;

	if (info->desc.id == PM800_ID_VOUTSW)
		return 0;

	ret = regulator_map_voltage_iterate(rdev, min_uv, max_uv);
	if (ret >= 0) {
		best_val = rdev->desc->ops->list_voltage(rdev, ret);
		if (min_uv <= best_val && max_uv >= best_val) {
			*selector = ret;
			if (old_selector == *selector)
				ret = 0;
			else
				ret = regulator_set_voltage_sel_regmap(rdev, ret);
		} else {
			ret = -EINVAL;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int pm800_get_voltage(struct regulator_dev *rdev)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);
	int sel, ret;

	if (!info)
		return -EINVAL;

	if (info->desc.id == PM800_ID_VOUTSW)
		return 0;

	sel = regulator_get_voltage_sel_regmap(rdev);
	if (sel < 0)
		return sel;

	ret = rdev->desc->ops->list_voltage(rdev, sel);

	return ret;
}

static int pm800_get_current_limit(struct regulator_dev *rdev)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);

	return info->max_ua;
}

struct regulator_ops pm800_volt_range_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage = pm800_set_voltage,
	.get_voltage = pm800_get_voltage,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm800_get_current_limit,
};

struct regulator_ops pm800_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm800_get_current_limit,
};

static int pm800_regulator_dt_init(struct platform_device *pdev,
	struct of_regulator_match **regulator_matches, int *range)
{
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;

	switch (chip->type) {
		case CHIP_PM800:
			*regulator_matches = pm800_regulator_matches;
			*range = PM800_ID_RG_MAX;
			break;
		case CHIP_PM822:
			*regulator_matches = pm822_regulator_matches;
			*range = PM822_ID_RG_MAX;
			break;
		case CHIP_PM86X:
			*regulator_matches = pm86x_regulator_matches;
			*range = PM86X_ID_RG_MAX;
			break;
		default:
			return -ENODEV;
	}

	return of_regulator_match(&pdev->dev, np, *regulator_matches, *range);
}

static int pm800_regulator_probe(struct platform_device *pdev)
{
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm80x_platform_data *pdata = dev_get_platdata(pdev->dev.parent);
	struct pm800_regulators *pm800_data;
	struct pm800_regulator_info *info;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	int i, ret, range = 0;
	struct of_regulator_match *regulator_matches;

	if (!pdata || pdata->num_regulators == 0) {
		if (IS_ENABLED(CONFIG_OF)) {
			ret = pm800_regulator_dt_init(pdev, &regulator_matches,
						      &range);
			if (ret < 0)
				return ret;
		} else {
			return -ENODEV;
		}
	} else if (pdata->num_regulators) {
		unsigned int count = 0;

		/* Check whether num_regulator is valid. */
		for (i = 0; i < ARRAY_SIZE(pdata->regulators); i++) {
			if (pdata->regulators[i])
				count++;
		}
		if (count != pdata->num_regulators)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	pm800_data = devm_kzalloc(&pdev->dev, sizeof(*pm800_data),
					GFP_KERNEL);
	if (!pm800_data) {
		dev_err(&pdev->dev, "Failed to allocate pm800_regualtors");
		return -ENOMEM;
	}

	pm800_data->map = chip->subchip->regmap_power;
	pm800_data->chip = chip;

	platform_set_drvdata(pdev, pm800_data);

	for (i = 0; i < range; i++) {
		if (!pdata || pdata->num_regulators == 0)
			init_data = regulator_matches->init_data;
		else
			init_data = pdata->regulators[i];
		if (!init_data) {
			regulator_matches++;
			continue;
		}
		info = regulator_matches->driver_data;
		config.dev = &pdev->dev;
		config.init_data = init_data;
		config.driver_data = info;
		config.regmap = pm800_data->map;
		config.of_node = regulator_matches->of_node;

		pm800_data->regulators[i] =
				regulator_register(&info->desc, &config);
		if (IS_ERR(pm800_data->regulators[i])) {
			ret = PTR_ERR(pm800_data->regulators[i]);
			dev_err(&pdev->dev, "Failed to register %s\n",
				info->desc.name);

			while (--i >= 0)
				regulator_unregister(pm800_data->regulators[i]);

			return ret;
		}
		regulator_matches++;
	}

	return 0;
}

static int pm800_regulator_remove(struct platform_device *pdev)
{
	struct pm800_regulators *pm800_data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < PM800_ID_RG_MAX; i++)
		regulator_unregister(pm800_data->regulators[i]);

	return 0;
}

static struct platform_driver pm800_regulator_driver = {
	.driver		= {
		.name	= "88pm80x-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= pm800_regulator_probe,
	.remove		= pm800_regulator_remove,
};

module_platform_driver(pm800_regulator_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joseph(Yossi) Hanin <yhanin@marvell.com>");
MODULE_DESCRIPTION("Regulator Driver for Marvell 88PM800 PMIC");
MODULE_ALIAS("platform:88pm800-regulator");
