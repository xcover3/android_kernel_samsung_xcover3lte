/*
 * Base driver for Marvell 88PM886
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm886.h>
#include <linux/regulator/machine.h>

#define CELL_IRQ_RESOURCE(_name, _irq) { \
	.name = _name, \
	.start = _irq, .end = _irq, \
	.flags = IORESOURCE_IRQ, \
	}
#define CELL_DEV(_name, _r, _compatible, _id) { \
	.name = _name, \
	.of_compatible = _compatible, \
	.num_resources = ARRAY_SIZE(_r), \
	.resources = _r, \
	.id = _id, \
	}

static const struct resource onkey_resources[] = {
	CELL_IRQ_RESOURCE(PM886_ONKEY_NAME, PM886_IRQ_ONKEY),
};

static const struct resource rtc_resources[] = {
	CELL_IRQ_RESOURCE(PM886_RTC_NAME, PM886_IRQ_RTC),
};

static const struct resource charger_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-chg-fail", PM886_IRQ_CHG_FAIL),
	CELL_IRQ_RESOURCE("88pm886-chg-done", PM886_IRQ_CHG_DONE),
	CELL_IRQ_RESOURCE("88pm886-chg-ilim", PM886_IRQ_CHG_ILIM),
};

static const struct resource battery_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-bat-cc", PM886_IRQ_CC),
	CELL_IRQ_RESOURCE("88pm886-bat-volt", PM886_IRQ_VBAT),
};

static const struct resource headset_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-headset-det", PM886_IRQ_HS_DET),
	CELL_IRQ_RESOURCE("88pm886-mic-det", PM886_IRQ_MIC_DET),
};

static const struct resource vbus_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-vbus-det", PM886_IRQ_VBUS),
	CELL_IRQ_RESOURCE("88pm886-otg-fail", PM886_IRQ_OTG_FAIL),
};

static const struct resource leds_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-cfd-fail", PM886_IRQ_CFD_FAIL),
};

static const struct resource regulator_resources[] = {
	{
	.name = PM886_REGULATOR_NAME,
	},
};

static const struct mfd_cell pm886_cell_devs[] = {
	CELL_DEV(PM886_RTC_NAME, rtc_resources, "marvell,88pm886-rtc", -1),
	CELL_DEV(PM886_ONKEY_NAME, onkey_resources, "marvell,88pm886-onkey", -1),
	CELL_DEV(PM886_CHARGER_NAME, charger_resources, "marvell,88pm886-charger", -1),
	CELL_DEV(PM886_BATTERY_NAME, battery_resources, "marvell,88pm886-battery", -1),
	CELL_DEV(PM886_HEADSET_NAME, headset_resources, "marvell,88pm886-headset", -1),
	CELL_DEV(PM886_VBUS_NAME, vbus_resources, "marvell,88pm886-vbus", -1),
	CELL_DEV(PM886_CFD_NAME, leds_resources, "marvell,88pm886-leds", -1),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-regulators", -1),
};

const struct of_device_id pm886_of_match[] = {
	{ .compatible = "marverll,88pm886", .data = (void *)PM886 },
	{},
};
EXPORT_SYMBOL_GPL(pm886_of_match);

const struct regmap_config pm886_base_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PM886_BASE_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_base_i2c_regmap);

const struct regmap_config pm886_power_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PM886_POWER_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_power_i2c_regmap);

const struct regmap_config pm886_gpadc_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PM886_GPADC_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_gpadc_i2c_regmap);

const struct regmap_config pm886_battery_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PM886_BATTERY_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_battery_i2c_regmap);

const struct regmap_config pm886_test_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = PM886_TEST_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_test_i2c_regmap);

struct pm886_chip *pm886_init_chip(struct i2c_client *client)
{
	struct pm886_chip *chip;

	chip = devm_kzalloc(&client->dev, sizeof(struct pm886_chip), GFP_KERNEL);
	if (!chip)
		return ERR_PTR(-ENOMEM);

	chip->client = client;
	chip->irq = client->irq;
	chip->dev = &client->dev;
	chip->power_page_addr = client->addr + 1;
	chip->gpadc_page_addr = client->addr + 2;
	chip->battery_page_addr = client->addr + 3;
	chip->test_page_addr = client->addr + 7;

	dev_set_drvdata(chip->dev, chip);
	i2c_set_clientdata(chip->client, chip);

	device_init_wakeup(&client->dev, 1);

	return chip;
}

int pm886_parse_dt(struct device_node *np, struct pm886_chip *chip)
{
	if (!chip)
		return -EINVAL;

	chip->irq_mode =
		!of_property_read_bool(np, "marvell,88pm886-irq-write-clear");

	return 0;
}

int pm886_post_init_chip(struct pm886_chip *chip)
{
	int ret;
	unsigned int val;

	if (!chip || !chip->base_regmap || !chip->power_regmap ||
	    !chip->gpadc_regmap || !chip->battery_regmap)
		return -EINVAL;

	/* read before alarm wake up bit before initialize interrupt */
	ret = regmap_read(chip->base_regmap, PM886_RTC_ALARM_CTRL1, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read RTC register: %d\n", ret);
		return ret;
	}
	chip->rtc_wakeup = !!(val & PM886_ALARM_WAKEUP);

	return 0;
}

int pm886_init_pages(struct pm886_chip *chip)
{
	struct i2c_client *client = chip->client;
	const struct regmap_config *base_regmap_config;
	const struct regmap_config *power_regmap_config;
	const struct regmap_config *gpadc_regmap_config;
	const struct regmap_config *battery_regmap_config;
	const struct regmap_config *test_regmap_config;
	int ret = 0;

	if (!chip || !chip->power_page_addr ||
	    !chip->gpadc_page_addr || !chip->battery_page_addr)
		return -ENODEV;

	chip->type = pm886_of_get_type(&client->dev);
	switch (chip->type) {
	case PM886:
		base_regmap_config = &pm886_base_i2c_regmap;
		power_regmap_config = &pm886_power_i2c_regmap;
		gpadc_regmap_config = &pm886_gpadc_i2c_regmap;
		battery_regmap_config = &pm886_battery_i2c_regmap;
		test_regmap_config = &pm886_test_i2c_regmap;
		break;
	default:
		return -ENODEV;
	}

	/* base page */
	chip->base_regmap = devm_regmap_init_i2c(client, base_regmap_config);
	if (IS_ERR(chip->base_regmap)) {
		dev_err(chip->dev, "Failed to init base_regmap: %d\n", ret);
		ret = PTR_ERR(chip->base_regmap);
		goto out;
	}

	/* power page */
	chip->power_page = i2c_new_dummy(client->adapter, chip->power_page_addr);
	if (!chip->power_page) {
		dev_err(chip->dev, "Failed to new power_page: %d\n", ret);
		ret = -ENODEV;
		goto out;
	}
	chip->power_regmap = devm_regmap_init_i2c(chip->power_page,
						  power_regmap_config);
	if (IS_ERR(chip->power_regmap)) {
		dev_err(chip->dev, "Failed to init power_regmap: %d\n", ret);
		ret = PTR_ERR(chip->power_regmap);
		goto out;
	}

	/* gpadc page */
	chip->gpadc_page = i2c_new_dummy(client->adapter, chip->gpadc_page_addr);
	if (!chip->gpadc_page) {
		dev_err(chip->dev, "Failed to new gpadc_page: %d\n", ret);
		ret = -ENODEV;
		goto out;
	}
	chip->gpadc_regmap = devm_regmap_init_i2c(chip->gpadc_page,
						  gpadc_regmap_config);
	if (IS_ERR(chip->gpadc_regmap)) {
		dev_err(chip->dev, "Failed to init gpadc_regmap: %d\n", ret);
		ret = PTR_ERR(chip->gpadc_regmap);
		goto out;
	}

	/* battery page */
	chip->battery_page = i2c_new_dummy(client->adapter, chip->battery_page_addr);
	if (!chip->battery_page) {
		dev_err(chip->dev, "Failed to new gpadc_page: %d\n", ret);
		ret = -ENODEV;
		goto out;
	}
	chip->battery_regmap = devm_regmap_init_i2c(chip->battery_page,
						  battery_regmap_config);
	if (IS_ERR(chip->battery_regmap)) {
		dev_err(chip->dev, "Failed to init battery_regmap: %d\n", ret);
		ret = PTR_ERR(chip->battery_regmap);
		goto out;
	}

	/* test page */
	chip->test_page = i2c_new_dummy(client->adapter, chip->test_page_addr);
	if (!chip->test_page) {
		dev_err(chip->dev, "Failed to new test_page: %d\n", ret);
		ret = -ENODEV;
		goto out;
	}
	chip->test_regmap = devm_regmap_init_i2c(chip->test_page,
						 test_regmap_config);
	if (IS_ERR(chip->test_regmap)) {
		dev_err(chip->dev, "Failed to init test_regmap: %d\n", ret);
		ret = PTR_ERR(chip->test_regmap);
		goto out;
	}

out:
	return ret;
}

void pm800_exit_pages(struct pm886_chip *chip)
{
	if (!chip)
		return;

	if (chip->power_page)
		i2c_unregister_device(chip->power_page);
	if (chip->gpadc_page)
		i2c_unregister_device(chip->gpadc_page);
	if (chip->test_page)
		i2c_unregister_device(chip->test_page);
}


int pm886_init_subdev(struct pm886_chip *chip)
{
	int ret;
	if (!chip)
		return -EINVAL;

	ret = mfd_add_devices(chip->dev, 0, pm886_cell_devs,
			      ARRAY_SIZE(pm886_cell_devs), NULL, 0,
			      regmap_irq_get_domain(chip->irq_data));
	return ret;
}

static const struct reg_default pm886_base_patch[] = {
};

static const struct reg_default pm886_power_patch[] = {
};

static const struct reg_default pm886_gpadc_patch[] = {
};

static const struct reg_default pm886_battery_patch[] = {
};

static const struct reg_default pm886_test_patch[] = {
};

/* PMIC chip itself related */
int pm886_apply_patch(struct pm886_chip *chip)
{
	int ret, size;

	if (!chip || !chip->base_regmap || !chip->power_regmap ||
	    !chip->gpadc_regmap || !chip->battery_regmap ||
	    !chip->test_regmap)
		return -EINVAL;

	size = ARRAY_SIZE(pm886_base_patch);
	if (size == 0)
		goto power;
	ret = regmap_register_patch(chip->base_regmap, pm886_base_patch, size);
	if (ret < 0)
		return ret;

power:
	size = ARRAY_SIZE(pm886_power_patch);
	if (size == 0)
		goto gpadc;
	ret = regmap_register_patch(chip->power_regmap, pm886_power_patch, size);
	if (ret < 0)
		return ret;

gpadc:
	size = ARRAY_SIZE(pm886_gpadc_patch);
	if (size == 0)
		goto battery;
	ret = regmap_register_patch(chip->gpadc_regmap, pm886_gpadc_patch, size);
	if (ret < 0)
		return ret;
battery:
	size = ARRAY_SIZE(pm886_battery_patch);
	if (size == 0)
		goto test;
	ret = regmap_register_patch(chip->battery_regmap, pm886_battery_patch, size);
	if (ret < 0)
		return ret;

test:
	size = ARRAY_SIZE(pm886_test_patch);
	if (size == 0)
		goto out;
	ret = regmap_register_patch(chip->test_regmap, pm886_test_patch, size);
	if (ret < 0)
		return ret;
out:
	return 0;
}

int pm886_apply_bd_patch(struct pm886_chip *chip, struct device_node *np)
{
	unsigned int page, reg, mask, data;
	const __be32 *values;
	int size, rows, index;

	if (!chip || !chip->base_regmap ||
	    !chip->power_regmap || !chip->gpadc_regmap ||
	    !chip->battery_regmap ||
	    !chip->test_regmap)
		return -EINVAL;

	values = of_get_property(np, "marvell,pmic-bd-cfg", &size);
	if (!values) {
		dev_warn(chip->dev, "No valid property for %s\n", np->name);
		/* exit SUCCESS */
		return 0;
	}

	/* number of elements in array */
	size /= sizeof(*values);
	rows = size / 4;
	dev_info(chip->dev, "pmic board specific configuration.\n");
	index = 0;

	while (rows--) {
		page = be32_to_cpup(values + index++);
		reg = be32_to_cpup(values + index++);
		mask = be32_to_cpup(values + index++);
		data = be32_to_cpup(values + index++);
		switch (page) {
		case PM886_BASE_PAGE:
			regmap_update_bits(chip->base_regmap, reg, mask, data);
			break;
		case PM886_POWER_PAGE:
			regmap_update_bits(chip->power_regmap, reg, mask, data);
			break;
		case PM886_GPADC_PAGE:
			regmap_update_bits(chip->gpadc_regmap, reg, mask, data);
			break;
		case PM886_BATTERY_PAGE:
			regmap_update_bits(chip->battery_regmap, reg, mask, data);
			break;
		case PM886_TEST_PAGE:
			regmap_update_bits(chip->test_regmap, reg, mask, data);
			break;
		default:
			dev_err(chip->dev, "wrong page index for %d\n", page);
			break;
		}
	}
	return 0;
}

long pm886_of_get_type(struct device *dev)
{
	const struct of_device_id *id = of_match_device(pm886_of_match, dev);

	if (id)
		return (long)id->data;
	else
		return 0;
}

void pm886_dev_exit(struct pm886_chip *chip)
{
	mfd_remove_devices(chip->dev);
	pm886_irq_exit(chip);
}
