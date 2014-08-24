/*
 * Regulators driver for Marvell 88PM886
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
#include <linux/mfd/88pm886.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regulator/of_regulator.h>

/* control section */
#define PM886_BUCK_EN		(0x08)
#define PM886_LDO_EN1		(0x09)
#define PM886_LDO_EN2		(0x0a)

/* max current in sleep */
#define MAX_SLEEP_CURRENT	5000

/*
 * ldo voltage:
 * ldox_set_slp[3: 0] ldox_set [3: 0]
 */
#define PM886_LDO1_VOUT		(0x20)
#define PM886_LDO2_VOUT		(0x26)
#define PM886_LDO3_VOUT		(0x2c)
#define PM886_LDO4_VOUT		(0x32)
#define PM886_LDO5_VOUT		(0x38)
#define PM886_LDO6_VOUT		(0x3e)
#define PM886_LDO7_VOUT		(0x44)
#define PM886_LDO8_VOUT		(0x4a)
#define PM886_LDO9_VOUT		(0x50)
#define PM886_LDO10_VOUT	(0x56)
#define PM886_LDO11_VOUT	(0x5c)
#define PM886_LDO12_VOUT	(0x62)
#define PM886_LDO13_VOUT	(0x68)
#define PM886_LDO14_VOUT	(0x6e)
#define PM886_LDO15_VOUT	(0x74)
#define PM886_LDO16_VOUT	(0x7a)

/* buck voltage */
#define PM886_BUCK1_VOUT	(0xa5)
#define PM886_BUCK1_1_VOUT	(0xa6)
#define PM886_BUCK1_2_VOUT	(0xa7)
#define PM886_BUCK1_3_VOUT	(0xa8)

#define PM886_BUCK2_VOUT	(0xb3)
#define PM886_BUCK3_VOUT	(0xc1)
#define PM886_BUCK4_VOUT	(0xcf)
#define PM886_BUCK5_VOUT	(0xdd)

/*
 * buck sleep mode control registers:
 * 00-disable,
 * 01/10-sleep voltage,
 * 11-active voltage
 */
#define PM886_BUCK1_SLP_CTRL	(0xa2)
#define PM886_BUCK2_SLP_CTRL	(0xb0)
#define PM886_BUCK3_SLP_CTRL	(0xbe)
#define PM886_BUCK4_SLP_CTRL	(0xcc)
#define PM886_BUCK5_SLP_CTRL	(0xda)

/*
 * ldo sleep mode control registers:
 * 00-disable,
 * 01/10-sleep voltage,
 * 11-active voltage
 */
#define PM886_LDO1_SLP_CTRL	(0x21)
#define PM886_LDO2_SLP_CTRL	(0x27)
#define PM886_LDO3_SLP_CTRL	(0x2d)
#define PM886_LDO4_SLP_CTRL	(0x33)
#define PM886_LDO5_SLP_CTRL	(0x39)
#define PM886_LDO6_SLP_CTRL	(0x3f)
#define PM886_LDO7_SLP_CTRL	(0x45)
#define PM886_LDO8_SLP_CTRL	(0x4b)
#define PM886_LDO9_SLP_CTRL	(0x51)
#define PM886_LDO10_SLP_CTRL	(0x57)
#define PM886_LDO11_SLP_CTRL	(0x5d)
#define PM886_LDO12_SLP_CTRL	(0x63)
#define PM886_LDO13_SLP_CTRL	(0x69)
#define PM886_LDO14_SLP_CTRL	(0x6f)
#define PM886_LDO15_SLP_CTRL	(0x75)
#define PM886_LDO16_SLP_CTRL	(0x7b)

/* set buck sleep voltage */
#define PM886_BUCK1_SET_SLP	(0xa3)
#define PM886_BUCK2_SET_SLP	(0xb1)
#define PM886_BUCK3_SET_SLP	(0xbf)
#define PM886_BUCK4_SET_SLP	(0xcd)
#define PM886_BUCK5_SET_SLP	(0xdb)

/*
 * set ldo sleep voltage register is the same as the active registers;
 * only the mask is not the same:
 * bit [7 : 4] --> to set sleep voltage
 * bit [3 : 0] --> to set active voltage
 * no need to give definition here
 */

/*
 * vreg - the buck regs string.
 * ebit - the bit number in the enable register.
 * amax - the current
 * Buck has 2 kinds of voltage steps. It is easy to find voltage by ranges,
 * not the constant voltage table.
 */
#define PM886_BUCK(vreg, ebit, amax, volt_ranges, n_volt)		\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm886_volt_buck_ops,				\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM886_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.n_voltages		= n_volt,			\
		.linear_ranges		= volt_ranges,			\
		.n_linear_ranges	= ARRAY_SIZE(volt_ranges),	\
		.vsel_reg	= PM886_##vreg##_VOUT,			\
		.vsel_mask	= 0x7f,					\
		.enable_reg	= PM886_BUCK_EN,			\
		.enable_mask	= 1 << (ebit),				\
	},								\
	.max_ua			= (amax),				\
	.sleep_vsel_reg		= PM886_##vreg##_SET_SLP,		\
	.sleep_vsel_mask	= 0x7f,					\
	.sleep_enable_reg	= PM886_##vreg##_SLP_CTRL,		\
	.sleep_enable_mask	= (0x3 << 4),				\
}

/*
 * vreg - the LDO regs string
 * ebit - the bit number in the enable register.
 * ereg - the enable register
 * amax - the current
 * volt_table - the LDO voltage table
 * For all the LDOes, there are too many ranges. Using volt_table will be
 * simpler and faster.
 */
#define PM886_LDO(vreg, ereg, ebit, amax, ldo_volt_table)					\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm886_volt_ldo_ops,				\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM886_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_table),		\
		.vsel_reg	= PM886_##vreg##_VOUT,			\
		.vsel_mask	= 0xf,					\
		.enable_reg	= PM886_LDO_##ereg,			\
		.enable_mask	= 1 << (ebit),				\
		.volt_table	= ldo_volt_table,			\
	},								\
	.max_ua			= (amax),				\
	.sleep_vsel_reg		= PM886_##vreg##_VOUT,			\
	.sleep_vsel_mask	= (0xf << 4),				\
	.sleep_enable_reg	= PM886_##vreg##_SLP_CTRL,		\
	.sleep_enable_mask	= (0x3 << 4),				\
}

/* buck1 */
static const struct regulator_linear_range buck_volt_range1[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0x4f, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 0x50, 0x54, 50000),
};
/* buck 2, 3, 4, 5 */
static const struct regulator_linear_range buck_volt_range2[] = {
	REGULATOR_LINEAR_RANGE(600000, 0, 0x4f, 12500),
	REGULATOR_LINEAR_RANGE(1600000, 0x50, 0x72, 50000),
};

/* ldo */
static const unsigned int ldo_volt_table1[] = {
};

struct pm886_regulator_info {
	struct regulator_desc desc;
	int max_ua;
	u8 sleep_enable_mask;
	u8 sleep_enable_reg;
	u8 sleep_vsel_reg;
	u8 sleep_vsel_mask;
};

struct pm886_regulators {
	struct regulator_dev *rdev;
	struct pm886_chip *chip;
	struct regmap *map;
};

#define USE_SLP_VOLT		(0x2 << 4)
#define USE_ACTIVE_VOLT		(0x3 << 4)
int pm886_set_suspend_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct pm886_regulator_info *info = rdev_get_drvdata(rdev);
	u8 val;
	int ret;

	if (!info)
		return -EINVAL;

	val = (mode == REGULATOR_MODE_IDLE) ? USE_SLP_VOLT : USE_ACTIVE_VOLT;
	ret = regmap_update_bits(rdev->regmap, info->sleep_enable_reg,
				 info->sleep_enable_mask, val);
	return ret;
}

static unsigned int pm886_get_optimum_mode(struct regulator_dev *rdev,
					   int input_uV, int output_uV,
					   int output_uA)
{
	struct pm886_regulator_info *info = rdev_get_drvdata(rdev);
	if (!info)
		return REGULATOR_MODE_IDLE;

	if (output_uA < 0) {
		dev_err(rdev_get_dev(rdev), "current needs to be > 0.\n");
		return REGULATOR_MODE_IDLE;
	}

	return (output_uA < MAX_SLEEP_CURRENT) ?
		REGULATOR_MODE_IDLE : REGULATOR_MODE_NORMAL;
}

static int pm886_get_current_limit(struct regulator_dev *rdev)
{
	struct pm886_regulator_info *info = rdev_get_drvdata(rdev);
	if (!info)
		return 0;
	return info->max_ua;
}

static int pm886_set_suspend_voltage(struct regulator_dev *rdev, int uv)
{
	int ret, sel;
	struct pm886_regulator_info *info = rdev_get_drvdata(rdev);
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
	if (!strncmp(info->desc.name, "BUCK", 4)) {
		sel = regulator_map_voltage_linear_range(rdev, uv, uv);
		if (sel < 0)
			return -EINVAL;
	} else {
		sel = regulator_map_voltage_iterate(rdev, uv, uv);
		if (sel < 0)
			return -EINVAL;
	}

	sel <<= ffs(info->sleep_vsel_mask) - 1;

	ret = regmap_update_bits(rdev->regmap, info->sleep_vsel_reg,
				 info->sleep_vsel_mask, sel);
	if (ret < 0)
		return -EINVAL;

	/* TODO: do we need this? */
	ret = pm886_set_suspend_mode(rdev, REGULATOR_MODE_IDLE);
	return ret;
}

/*
 * about the get_optimum_mode()/set_suspend_mode()/set_suspend_voltage() interface:
 * - 88pm886 has two sets of registers to set and enable/disable regulators
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
static struct regulator_ops pm886_volt_buck_ops = {
	.list_voltage = regulator_list_voltage_linear_range,
	.map_voltage = regulator_map_voltage_linear_range,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm886_get_current_limit,
	.get_optimum_mode = pm886_get_optimum_mode,
	.set_suspend_mode = pm886_set_suspend_mode,
	.set_suspend_voltage = pm886_set_suspend_voltage,
};

static struct regulator_ops pm886_volt_ldo_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm886_get_current_limit,
	.get_optimum_mode = pm886_get_optimum_mode,
	.set_suspend_mode = pm886_set_suspend_mode,
	.set_suspend_voltage = pm886_set_suspend_voltage,
};

/* The array is indexed by id(PM886_ID_XXX) */
static struct pm886_regulator_info pm886_regulator_configs[] = {
	PM886_BUCK(BUCK1, 0, 3000000, buck_volt_range1, 0x55),
	PM886_BUCK(BUCK2, 1, 1200000, buck_volt_range2, 0x73),
	PM886_BUCK(BUCK3, 2, 1200000, buck_volt_range2, 0x73),
	PM886_BUCK(BUCK4, 3, 1200000, buck_volt_range2, 0x73),
	PM886_BUCK(BUCK5, 4, 1200000, buck_volt_range2, 0x73),

	PM886_LDO(LDO1, EN1, 0, 100000, ldo_volt_table1),
	PM886_LDO(LDO2, EN1, 1, 100000, ldo_volt_table1),
	PM886_LDO(LDO3, EN1, 2, 100000, ldo_volt_table1),
	PM886_LDO(LDO4, EN1, 3, 400000, ldo_volt_table1),
	PM886_LDO(LDO5, EN1, 4, 400000, ldo_volt_table1),
	PM886_LDO(LDO6, EN1, 5, 400000, ldo_volt_table1),
	PM886_LDO(LDO7, EN1, 6, 400000, ldo_volt_table1),
	PM886_LDO(LDO8, EN1, 7, 400000, ldo_volt_table1),
	PM886_LDO(LDO9, EN1, 0, 400000, ldo_volt_table1),
	PM886_LDO(LDO10, EN2, 1, 200000, ldo_volt_table1),
	PM886_LDO(LDO11, EN2, 2, 200000, ldo_volt_table1),
	PM886_LDO(LDO12, EN2, 3, 200000, ldo_volt_table1),
	PM886_LDO(LDO13, EN2, 4, 200000, ldo_volt_table1),
	PM886_LDO(LDO14, EN2, 5, 200000, ldo_volt_table1),
	PM886_LDO(LDO15, EN2, 6, 200000, ldo_volt_table1),
	PM886_LDO(LDO16, EN2, 7, 200000, ldo_volt_table1),
};

#define PM886_OF_MATCH(comp, label) \
	{ \
		.compatible = comp, \
		.data = &pm886_regulator_configs[PM886_ID_##label], \
	}
static const struct of_device_id pm886_regulators_of_match[] = {
	PM886_OF_MATCH("marvell,88pm886-buck1", BUCK1),
	PM886_OF_MATCH("marvell,88pm886-buck2", BUCK2),
	PM886_OF_MATCH("marvell,88pm886-buck3", BUCK3),
	PM886_OF_MATCH("marvell,88pm886-buck4", BUCK4),
	PM886_OF_MATCH("marvell,88pm886-buck5", BUCK5),

	PM886_OF_MATCH("marvell,88pm886-ldo1", LDO1),
	PM886_OF_MATCH("marvell,88pm886-ldo2", LDO2),
	PM886_OF_MATCH("marvell,88pm886-ldo3", LDO3),
	PM886_OF_MATCH("marvell,88pm886-ldo4", LDO4),
	PM886_OF_MATCH("marvell,88pm886-ldo5", LDO5),
	PM886_OF_MATCH("marvell,88pm886-ldo6", LDO6),
	PM886_OF_MATCH("marvell,88pm886-ldo7", LDO7),
	PM886_OF_MATCH("marvell,88pm886-ldo8", LDO8),
	PM886_OF_MATCH("marvell,88pm886-ldo9", LDO9),
	PM886_OF_MATCH("marvell,88pm886-ldo10", LDO10),
	PM886_OF_MATCH("marvell,88pm886-ldo11", LDO11),
	PM886_OF_MATCH("marvell,88pm886-ldo12", LDO12),
	PM886_OF_MATCH("marvell,88pm886-ldo13", LDO13),
	PM886_OF_MATCH("marvell,88pm886-ldo14", LDO14),
	PM886_OF_MATCH("marvell,88pm886-ldo15", LDO15),
	PM886_OF_MATCH("marvell,88pm886-ldo16", LDO16),
};

static int pm886_regulator_probe(struct platform_device *pdev)
{
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm886_regulators *data;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	struct regulation_constraints *c;
	const struct of_device_id *match;
	const struct pm886_regulator_info *const_info;
	struct pm886_regulator_info *info;
	int ret;

	match = of_match_device(pm886_regulators_of_match, &pdev->dev);
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

	data = devm_kzalloc(&pdev->dev, sizeof(struct pm886_regulators),
			    GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "failed to allocate pm886_regualtors");
		return -ENOMEM;
	}
	data->map = chip->power_regmap;
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
	c->valid_ops_mask |= REGULATOR_CHANGE_DRMS
		| REGULATOR_CHANGE_MODE;
	c->valid_modes_mask |= REGULATOR_MODE_NORMAL
		| REGULATOR_MODE_IDLE;
	c->input_uV = 1000;

	platform_set_drvdata(pdev, data);

	return 0;
}

static int pm886_regulator_remove(struct platform_device *pdev)
{
	struct pm886_regulators *data = platform_get_drvdata(pdev);
	devm_kfree(&pdev->dev, data);
	return 0;
}

static struct platform_driver pm886_regulator_driver = {
	.driver		= {
		.name	= "88pm886-regulator",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pm886_regulators_of_match),
	},
	.probe		= pm886_regulator_probe,
	.remove		= pm886_regulator_remove,
};

static int pm886_regulator_init(void)
{
	return platform_driver_register(&pm886_regulator_driver);
}
subsys_initcall(pm886_regulator_init);

static void pm886_regulator_exit(void)
{
	platform_driver_unregister(&pm886_regulator_driver);
}
module_exit(pm886_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yi Zhang<yizhang@marvell.com>");
MODULE_DESCRIPTION("Regulator Driver for Marvell 88PM886 PMIC");
MODULE_ALIAS("platform:88pm886-regulator");
