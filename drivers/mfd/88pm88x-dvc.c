/*
 * DVC (dynamic voltage change) driver for Marvell 88PM886
 * - Derived from dvc driver for 88pm800/88pm822/88pm860
 *
 * Copyright (C) 2014 Marvell International Ltd.
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
#include <linux/mfd/88pm88x.h>
#include <linux/mfd/88pm886.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>

/* to enable dvc*/
#define PM88X_PWR_HOLD			(1 << 7)

#define BUCK_MIN_VOLT			(600000)
#define BUCK_MAX_VOLT			(1587500)
#define BUCK_STEP			(12500)
#define BUCK_MAX_DVC_LEVEL		(8)

/* encapsulate for future usage */
struct pm88x_dvc {
	struct pm88x_chip *chip;
};

static struct pm88x_dvc *g_dvc;

static inline int volt_to_reg(int uv)
{
	return (uv - BUCK_MIN_VOLT) / BUCK_STEP;
}

static inline int reg_to_volt(int regval)
{
	return regval * BUCK_STEP + BUCK_MIN_VOLT;
}

/*
 * Example for usage: set buck1 level1 as 1200mV
 * pm88x_dvc_set_volt(1, 1200 * 1000);
 * level begins with 0
 */
int pm88x_dvc_set_volt(u8 level, int uv)
{
	u8 buck1_volt_reg;
	int ret = 0;
	struct regmap *regmap;

	if (!g_dvc || !g_dvc->chip || !g_dvc->chip->buck_regmap) {
		pr_err("%s: NULL pointer!\n", __func__);
		return -EINVAL;
	}
	regmap = g_dvc->chip->buck_regmap;

	if (uv < BUCK_MIN_VOLT || uv > BUCK_MAX_VOLT) {
		dev_err(g_dvc->chip->dev, "the expected voltage is out of range!\n");
		return -EINVAL;
	}

	if (level < 0 || level >= BUCK_MAX_DVC_LEVEL) {
		dev_err(g_dvc->chip->dev, "DVC level is out of range!\n");
		return -EINVAL;
	}

	if (level < 4)
		buck1_volt_reg = PM88X_BUCK1_VOUT;
	else {
		buck1_volt_reg = PM88X_BUCK1_4_VOUT;
		level -= 4;
	}

	/* pay attention to only change the voltage value */
	ret = regmap_update_bits(regmap, buck1_volt_reg + level,
				 0x7f, volt_to_reg(uv));
	return ret;
};
EXPORT_SYMBOL(pm88x_dvc_set_volt);

int pm88x_dvc_get_volt(u8 level)
{
	struct regmap *regmap;
	int ret = 0, regval = 0;
	u8 buck1_volt_reg;

	if (!g_dvc || !g_dvc->chip || !g_dvc->chip->buck_regmap) {
		pr_err("%s: NULL pointer!\n", __func__);
		return -EINVAL;
	}

	regmap = g_dvc->chip->buck_regmap;
	if (level >= BUCK_MAX_DVC_LEVEL) {
		dev_err(g_dvc->chip->dev, "%s: DVC level out of range\n", __func__);
		return -EINVAL;
	}

	if (level < 4)
		buck1_volt_reg = PM88X_BUCK1_VOUT;
	else {
		buck1_volt_reg = PM88X_BUCK1_4_VOUT;
		level -= 4;
	}

	ret = regmap_read(regmap, buck1_volt_reg + level, &regval);
	if (ret < 0) {
		dev_err(g_dvc->chip->dev,
			"fail to read reg: 0x%x\n", buck1_volt_reg + level);
		return ret;
	}

	regval &= 0x7f;

	return reg_to_volt(regval);
}
EXPORT_SYMBOL(pm88x_dvc_get_volt);

static int pm88x_dvc_probe(struct platform_device *pdev)
{
	struct pm88x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	static struct pm88x_dvc *dvcdata;
	int ret;

	dvcdata = devm_kzalloc(&pdev->dev, sizeof(*g_dvc), GFP_KERNEL);
	if (!dvcdata) {
		dev_err(&pdev->dev, "Failed to allocate g_dvc");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, dvcdata);
	/* get global handler */
	g_dvc = dvcdata;
	g_dvc->chip = chip;

	/* config gpio1 as DVC3 pin */
	ret = regmap_update_bits(chip->base_regmap, PM88X_GPIO_CTRL1,
				 PM88X_GPIO1_MODE_MSK, PM88X_GPIO1_SET_DVC);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to set gpio1 as dvc pin!\n");
		return ret;
	}

	/* enable dvc feature */
	ret = regmap_update_bits(chip->base_regmap, PM88X_MISC_CONFIG1,
				 PM88X_PWR_HOLD, PM88X_PWR_HOLD);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to enable pmic dvc feature!\n");
		return ret;
	}

	return 0;
}

static int pm88x_dvc_remove(struct platform_device *pdev)
{
	struct pm88x_dvc *dvcdata = platform_get_drvdata(pdev);
	devm_kfree(&pdev->dev, dvcdata);
	return 0;
}

static struct of_device_id pm88x_dvc_of_match[] = {
	{.compatible = "marvell,88pm886-dvc"},
	{},
};

MODULE_DEVICE_TABLE(of, pm88x_dvc_of_match);

static struct platform_driver pm88x_dvc_driver = {
	.driver = {
		   .name = "88pm886-dvc",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(pm88x_dvc_of_match),
		   },
	.probe = pm88x_dvc_probe,
	.remove = pm88x_dvc_remove,
};

static int pm88x_dvc_init(void)
{
	return platform_driver_register(&pm88x_dvc_driver);
}
subsys_initcall(pm88x_dvc_init);

static void pm88x_dvc_exit(void)
{
	platform_driver_unregister(&pm88x_dvc_driver);
}
module_exit(pm88x_dvc_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("DVC Driver for Marvell 88PM886 PMIC");
MODULE_ALIAS("platform:88pm886-dvc");
