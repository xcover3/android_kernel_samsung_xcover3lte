/*
 * Buck driver for Marvell 88PM88X
 *
 * Copyright (C) 2014 Marvell International Ltd.
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

/* max current in sleep */
#define MAX_SLEEP_CURRENT	5000

/* ------------- 88pm886 buck registers --------------- */

/* buck voltage */
#define PM886_BUCK2_VOUT	(0xb3)
#define PM886_BUCK3_VOUT	(0xc1)
#define PM886_BUCK4_VOUT	(0xcf)
#define PM886_BUCK5_VOUT	(0xdd)

/* set buck sleep voltage */
#define PM886_BUCK1_SET_SLP	(0xa3)
#define PM886_BUCK2_SET_SLP	(0xb1)
#define PM886_BUCK3_SET_SLP	(0xbf)
#define PM886_BUCK4_SET_SLP	(0xcd)
#define PM886_BUCK5_SET_SLP	(0xdb)

/* control section */
#define PM886_BUCK_EN		(0x08)

/* ------------- 88pm880 buck registers --------------- */
/* buck voltage */
#define PM880_BUCK2_VOUT	(0x58)
#define PM880_BUCK3_VOUT	(0x70)
#define PM880_BUCK4_VOUT	(0x88)
#define PM880_BUCK5_VOUT	(0x98)
#define PM880_BUCK6_VOUT	(0xa8)

/* set buck sleep voltage */
#define PM880_BUCK1_SET_SLP	(0x26)
#define PM880_BUCK1A_SET_SLP	(0x26)
#define PM880_BUCK1B_SET_SLP	(0x3c)

#define PM880_BUCK2_SET_SLP	(0x56)
#define PM880_BUCK3_SET_SLP	(0x6e)
#define PM880_BUCK4_SET_SLP	(0x86)
#define PM880_BUCK5_SET_SLP	(0x96)
#define PM880_BUCK6_SET_SLP	(0xa6)
#define PM880_BUCK7_SET_SLP	(0xb6)

/* control section */
#define PM880_BUCK_EN		(0x08)

/*
 * vreg - the buck regs string.
 * ebit - the bit number in the enable register.
 * amax - the current
 * Buck has 2 kinds of voltage steps. It is easy to find voltage by ranges,
 * not the constant voltage table.
 */
#define PM88X_BUCK(_pmic, vreg, ebit, amax, volt_ranges, n_volt)		\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm88x_volt_buck_ops,				\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= _pmic##_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.n_voltages		= n_volt,			\
		.linear_ranges		= volt_ranges,			\
		.n_linear_ranges	= ARRAY_SIZE(volt_ranges),	\
		.vsel_reg	= _pmic##_##vreg##_VOUT,		\
		.vsel_mask	= 0x7f,					\
		.enable_reg	= _pmic##_BUCK_EN,			\
		.enable_mask	= 1 << (ebit),				\
	},								\
	.max_ua			= (amax),				\
	.sleep_vsel_reg		= _pmic##_##vreg##_SET_SLP,		\
	.sleep_vsel_mask	= 0x7f,					\
	.sleep_enable_reg	= _pmic##_##vreg##_SLP_CTRL,		\
	.sleep_enable_mask	= (0x3 << 4),				\
}

#define PM886_BUCK(vreg, ebit, amax, volt_ranges, n_volt)		\
	PM88X_BUCK(PM886, vreg, ebit, amax, volt_ranges, n_volt)

#define PM880_BUCK(vreg, ebit, amax, volt_ranges, n_volt)		\
	PM88X_BUCK(PM880, vreg, ebit, amax, volt_ranges, n_volt)

/* buck1 has dvc function */
static const struct regulator_linear_range buck_volt_range1[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0x4f, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 0x50, 0x54, 50000),
};

/* 88pm886 buck 2, 3, 4, 5 */
static const struct regulator_linear_range buck_volt_range2[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0x4f, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 0x50, 0x72, 50000),
};
/* 88pm880 buck 1, 2, 3, 4, 5, 6, 7 */
static const struct regulator_linear_range buck_volt_range3[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0x19, 12500),
};

struct pm88x_buck_info {
	struct regulator_desc desc;
	int max_ua;
	u8 sleep_enable_mask;
	u8 sleep_enable_reg;
	u8 sleep_vsel_reg;
	u8 sleep_vsel_mask;
};

struct pm88x_regulators {
	struct regulator_dev *rdev;
	struct pm88x_chip *chip;
	struct regmap *map;
};

#define USE_SLP_VOLT		(0x2 << 4)
#define USE_ACTIVE_VOLT		(0x3 << 4)
int pm88x_buck_set_suspend_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct pm88x_buck_info *info = rdev_get_drvdata(rdev);
	u8 val;
	int ret;

	if (!info)
		return -EINVAL;

	val = (mode == REGULATOR_MODE_IDLE) ? USE_SLP_VOLT : USE_ACTIVE_VOLT;
	ret = regmap_update_bits(rdev->regmap, info->sleep_enable_reg,
				 info->sleep_enable_mask, val);
	return ret;
}

static unsigned int pm88x_buck_get_optimum_mode(struct regulator_dev *rdev,
					   int input_uV, int output_uV,
					   int output_uA)
{
	struct pm88x_buck_info *info = rdev_get_drvdata(rdev);
	if (!info)
		return REGULATOR_MODE_IDLE;

	if (output_uA < 0) {
		dev_err(rdev_get_dev(rdev), "current needs to be > 0.\n");
		return REGULATOR_MODE_IDLE;
	}

	return (output_uA < MAX_SLEEP_CURRENT) ?
		REGULATOR_MODE_IDLE : REGULATOR_MODE_NORMAL;
}

static int pm88x_buck_get_current_limit(struct regulator_dev *rdev)
{
	struct pm88x_buck_info *info = rdev_get_drvdata(rdev);
	if (!info)
		return 0;
	return info->max_ua;
}

static int pm88x_buck_set_suspend_voltage(struct regulator_dev *rdev, int uv)
{
	int ret, sel;
	struct pm88x_buck_info *info = rdev_get_drvdata(rdev);
	if (!info || !info->desc.ops)
		return -EINVAL;
	if (!info->desc.ops->set_suspend_mode)
		return 0;
	/*
	 * two steps:
	 * 1) set the suspend voltage to *_set_slp registers
	 * 2) set regulator mode via set_suspend_mode() interface to enable output
	 */
	/* the suspend voltage mapping is the same as active */
	sel = regulator_map_voltage_linear_range(rdev, uv, uv);
	if (sel < 0)
		return -EINVAL;

	sel <<= ffs(info->sleep_vsel_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, info->sleep_vsel_reg,
				 info->sleep_vsel_mask, sel);
	if (ret < 0)
		return -EINVAL;

	/* TODO: do we need this? */
	ret = pm88x_buck_set_suspend_mode(rdev, REGULATOR_MODE_IDLE);
	return ret;
}

/*
 * about the get_optimum_mode()/set_suspend_mode()/set_suspend_voltage() interface:
 * - 88pm88x has two sets of registers to set and enable/disable regulators
 *   in active and suspend(sleep) status:
 *   the following focues on the sleep part:
 *   - there are two control bits: 00-disable,
 *				   01/10-use sleep voltage,
 *				   11-use active voltage,
 *- in most of the scenario, these registers are configured when the whole PMIC
 *  initialized, when the system enters into suspend(sleep) mode, the regulator
 *  works according to the setting or disabled;
 *- there is also case that the device driver needs to:
 *  - set the sleep voltage;
 *  - choose to use sleep voltage or active voltage depends on the load;
 *  so:
 *  set_suspend_voltage() is used to manipulate the registers to set sleep volt;
 *  set_suspend_mode() is used to switch between sleep voltage and active voltage
 *  get_optimum_mode() is used to get right mode
 */
static struct regulator_ops pm88x_volt_buck_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm88x_buck_get_current_limit,
	.get_optimum_mode = pm88x_buck_get_optimum_mode,
	.set_suspend_mode = pm88x_buck_set_suspend_mode,
	.set_suspend_voltage = pm88x_buck_set_suspend_voltage,
};

/* The array is indexed by id(PM886_ID_BUCK*) */
static struct pm88x_buck_info pm886_buck_configs[] = {
	PM886_BUCK(BUCK1, 0, 3000000, buck_volt_range1, 0x55),
	PM886_BUCK(BUCK2, 1, 1200000, buck_volt_range2, 0x73),
	PM886_BUCK(BUCK3, 2, 1200000, buck_volt_range2, 0x73),
	PM886_BUCK(BUCK4, 3, 1200000, buck_volt_range2, 0x73),
	PM886_BUCK(BUCK5, 4, 1200000, buck_volt_range2, 0x73),
};

/* The array is indexed by id(PM880_ID_BUCK*) */
static struct pm88x_buck_info pm880_buck_configs[] = {
	PM880_BUCK(BUCK1A, 0, 3000000, buck_volt_range3, 0x20),
	PM880_BUCK(BUCK2, 1, 1200000, buck_volt_range3, 0x20),
	PM880_BUCK(BUCK3, 2, 1200000, buck_volt_range3, 0x20),
	PM880_BUCK(BUCK4, 3, 1200000, buck_volt_range3, 0x20),
	PM880_BUCK(BUCK5, 4, 1200000, buck_volt_range3, 0x20),
	PM880_BUCK(BUCK6, 3, 1200000, buck_volt_range3, 0x20),
	PM880_BUCK(BUCK7, 4, 1200000, buck_volt_range3, 0x20),
};

#define PM88X_BUCK_OF_MATCH(_pmic, id, comp, label) \
	{ \
		.compatible = comp, \
		.data = &_pmic##_buck_configs[id##_##label], \
	}
#define PM886_BUCK_OF_MATCH(comp, label) \
	PM88X_BUCK_OF_MATCH(pm886, PM886_ID, comp, label)

#define PM880_BUCK_OF_MATCH(comp, label) \
	PM88X_BUCK_OF_MATCH(pm880, PM880_ID, comp, label)

static const struct of_device_id pm88x_bucks_of_match[] = {
	PM886_BUCK_OF_MATCH("marvell,88pm886-buck1", BUCK1),
	PM886_BUCK_OF_MATCH("marvell,88pm886-buck2", BUCK2),
	PM886_BUCK_OF_MATCH("marvell,88pm886-buck3", BUCK3),
	PM886_BUCK_OF_MATCH("marvell,88pm886-buck4", BUCK4),
	PM886_BUCK_OF_MATCH("marvell,88pm886-buck5", BUCK5),

	PM880_BUCK_OF_MATCH("marvell,88pm880-buck1a", BUCK1A),
	PM880_BUCK_OF_MATCH("marvell,88pm880-buck2", BUCK2),
	PM880_BUCK_OF_MATCH("marvell,88pm880-buck3", BUCK3),
	PM880_BUCK_OF_MATCH("marvell,88pm880-buck4", BUCK4),
	PM880_BUCK_OF_MATCH("marvell,88pm880-buck5", BUCK5),
	PM880_BUCK_OF_MATCH("marvell,88pm880-buck6", BUCK6),
	PM880_BUCK_OF_MATCH("marvell,88pm880-buck7", BUCK7),
};

static int pm88x_buck_probe(struct platform_device *pdev)
{
	struct pm88x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm88x_regulators *data;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	struct regulation_constraints *c;
	const struct of_device_id *match;
	const struct pm88x_buck_info *const_info;
	struct pm88x_buck_info *info;
	int ret;

	match = of_match_device(pm88x_bucks_of_match, &pdev->dev);
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
	data->map = chip->buck_regmap;
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
	c->valid_ops_mask |= REGULATOR_CHANGE_DRMS | REGULATOR_CHANGE_MODE
		| REGULATOR_CHANGE_VOLTAGE;
	c->valid_modes_mask |= REGULATOR_MODE_NORMAL
		| REGULATOR_MODE_IDLE;
	c->input_uV = 1000;

	platform_set_drvdata(pdev, data);

	return 0;
}

static int pm88x_buck_remove(struct platform_device *pdev)
{
	struct pm88x_regulators *data = platform_get_drvdata(pdev);
	devm_kfree(&pdev->dev, data);
	return 0;
}

static struct platform_driver pm88x_buck_driver = {
	.driver		= {
		.name	= "88pm88x-buck",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pm88x_bucks_of_match),
	},
	.probe		= pm88x_buck_probe,
	.remove		= pm88x_buck_remove,
};

static int pm88x_buck_init(void)
{
	return platform_driver_register(&pm88x_buck_driver);
}
subsys_initcall(pm88x_buck_init);

static void pm88x_buck_exit(void)
{
	platform_driver_unregister(&pm88x_buck_driver);
}
module_exit(pm88x_buck_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yi Zhang<yizhang@marvell.com>");
MODULE_DESCRIPTION("Buck for Marvell 88PM88X PMIC");
MODULE_ALIAS("platform:88pm88x-buck");
