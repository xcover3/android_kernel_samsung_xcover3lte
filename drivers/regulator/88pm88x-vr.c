/*
 * virtual regulator driver for Marvell 88PM88X
 *
 * Copyright (C) 2015 Marvell International Ltd.
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
#include <linux/mfd/88pm88x.h>
#include <linux/mfd/88pm886.h>
#include <linux/mfd/88pm880.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>

#define PM88X_VR_EN		(0x28)

#define PM88X_VR(vreg, ebit, nr)					\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm88x_virtual_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM88X##_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.enable_reg	= PM88X##_VR_EN,			\
		.enable_mask	= 1 << (ebit),				\
	},								\
	.page_nr = nr,							\
}

struct pm88x_vr_info {
	struct regulator_desc desc;
	unsigned int page_nr;
};

struct pm88x_regulators {
	struct regulator_dev *rdev;
	struct pm88x_chip *chip;
	struct regmap *map;
};

static struct regulator_ops pm88x_virtual_regulator_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

/* The array is indexed by id(PM886_ID_BUCK*) */
static struct pm88x_vr_info pm88x_vr_configs[] = {
	PM88X_VR(VOTG, 7, 3),
};

#define PM88X_VR_OF_MATCH(comp, label) \
	{ \
		.compatible = comp, \
		.data = &pm88x_vr_configs[PM88X_ID##_##label], \
	}
static const struct of_device_id pm88x_vrs_of_match[] = {
	PM88X_VR_OF_MATCH("marvell,88pm88x-votg", VOTG),
};

static struct regmap *nr_to_regmap(struct pm88x_chip *chip, unsigned int nr)
{
	switch (nr) {
	case 0:
		return chip->base_regmap;
	case 1:
		return chip->ldo_regmap;
	case 2:
		return chip->gpadc_regmap;
	case 3:
		return chip->battery_regmap;
	case 4:
		return chip->buck_regmap;
	case 7:
		return chip->test_regmap;
	default:
		pr_err("unsupported pages.\n");
		return NULL;
	}
}

static int pm88x_virtual_regulator_probe(struct platform_device *pdev)
{
	struct pm88x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm88x_regulators *data;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	struct regulation_constraints *c;
	const struct of_device_id *match;
	const struct pm88x_vr_info *const_info;
	struct pm88x_vr_info *info;
	int ret;

	match = of_match_device(pm88x_vrs_of_match, &pdev->dev);
	if (match) {
		const_info = match->data;
		init_data = of_get_regulator_init_data(&pdev->dev,
						       pdev->dev.of_node);
	} else {
		dev_err(&pdev->dev, "parse dts fails!\n");
		return -EINVAL;
	}

	info = kmemdup(const_info, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	data = devm_kzalloc(&pdev->dev, sizeof(struct pm88x_regulators),
			    GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "failed to allocate pm88x_regualtors");
		return -ENOMEM;
	}
	data->map = nr_to_regmap(chip, info->page_nr);
	data->chip = chip;

	/* add regulator config */
	config.dev = &pdev->dev;
	config.init_data = init_data;
	config.driver_data = info;
	config.regmap = data->map;
	config.of_node = pdev->dev.of_node;

	data->rdev = devm_regulator_register(&pdev->dev, &info->desc, &config);
	if (IS_ERR(data->rdev)) {
		dev_err(&pdev->dev, "cannot register %s\n", info->desc.name);
		ret = PTR_ERR(data->rdev);
		return ret;
	}

	c = data->rdev->constraints;
	c->valid_ops_mask |= REGULATOR_CHANGE_STATUS;

	platform_set_drvdata(pdev, data);

	return 0;
}

static int pm88x_virtual_regulator_remove(struct platform_device *pdev)
{
	struct pm88x_regulators *data = platform_get_drvdata(pdev);
	devm_kfree(&pdev->dev, data);
	return 0;
}

static struct platform_driver pm88x_virtual_regulator_driver = {
	.driver		= {
		.name	= "88pm88x-virtual-regulator",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pm88x_vrs_of_match),
	},
	.probe		= pm88x_virtual_regulator_probe,
	.remove		= pm88x_virtual_regulator_remove,
};

static int pm88x_virtual_regulator_init(void)
{
	return platform_driver_register(&pm88x_virtual_regulator_driver);
}
subsys_initcall(pm88x_virtual_regulator_init);

static void pm88x_virtual_regulator_exit(void)
{
	platform_driver_unregister(&pm88x_virtual_regulator_driver);
}
module_exit(pm88x_virtual_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yi Zhang <yizhang@marvell.com>");
MODULE_DESCRIPTION("for virtual supply in Marvell 88PM88X PMIC");
MODULE_ALIAS("platform:88pm88x-vr");
