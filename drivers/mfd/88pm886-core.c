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

#define PM886_POWER_UP_LOG		(0x17)
#define PM886_LOWPOWER1			(0x20)
#define PM886_LOWPOWER2			(0x21)
#define PM886_LOWPOWER3			(0x22)
#define PM886_LOWPOWER4			(0x23)
#define PM886_BK_OSC_CTRL1		(0x50)
#define PM886_BK_OSC_CTRL6		(0x55)
#define PM886_POWER_DOWN_LOG1		(0xe5)
#define PM886_POWER_DOWN_LOG2		(0xe6)

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
	CELL_IRQ_RESOURCE("88pm886-chg-ilimit", PM886_IRQ_CHG_ILIM),
};

static const struct resource battery_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-bat-cc", PM886_IRQ_CC),
	CELL_IRQ_RESOURCE("88pm886-bat-volt", PM886_IRQ_VBAT),
	CELL_IRQ_RESOURCE("88pm886-bat-detect", PM886_IRQ_BAT_DET),
};

static const struct resource headset_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-headset-det", PM886_IRQ_HS_DET),
	CELL_IRQ_RESOURCE("88pm886-mic-det", PM886_IRQ_MIC_DET),
};

static const struct resource vbus_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-vbus-det", PM886_IRQ_VBUS),
	CELL_IRQ_RESOURCE("88pm886-gpadc0", PM886_IRQ_GPADC0),
	CELL_IRQ_RESOURCE("88pm886-gpadc1", PM886_IRQ_GPADC1),
	CELL_IRQ_RESOURCE("88pm886-gpadc2", PM886_IRQ_GPADC2),
	CELL_IRQ_RESOURCE("88pm886-gpadc3", PM886_IRQ_GPADC3),
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

static const struct resource dvc_resources[] = {
	{
	.name = PM886_DVC_NAME,
	},
};

static const struct resource debugfs_resources[] = {
	{
	.name = PM886_DEBUGFS_NAME,
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
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-buck1", 0),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-buck2", 1),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-buck3", 2),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-buck4", 3),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-buck5", 4),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo1", 5),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo2", 6),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo3", 7),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo4", 8),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo5", 9),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo6", 10),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo7", 11),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo8", 12),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo9", 13),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo10", 14),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo11", 15),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo12", 16),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo13", 17),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo14", 18),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo15", 19),
	CELL_DEV(PM886_REGULATOR_NAME, regulator_resources, "marvell,88pm886-ldo16", 20),
	CELL_DEV(PM886_DVC_NAME, dvc_resources, "marvell,88pm886-dvc", -1),
	CELL_DEV(PM886_DEBUGFS_NAME, debugfs_resources, "marvell,88pm886-debugfs", -1),
};

const struct of_device_id pm886_of_match[] = {
	{ .compatible = "marverll,88pm886", .data = (void *)PM886 },
	{},
};
EXPORT_SYMBOL_GPL(pm886_of_match);

static bool pm886_base_readable_reg(struct device *dev, unsigned int reg)
{
	bool ret = false;

	switch (reg) {
	case 0x00:
	case 0x01:
	case 0x14:
	case 0x15:
	case 0x17:
	case 0x18:
	case 0x19:
	case 0x1d:
	case 0x25:
	case 0x36:
	case 0x6f:
		ret = true;
		break;
	default:
		break;
	}
	if ((reg >= 0x05) && (reg <= 0x08))
		ret = true;
	if ((reg >= 0x0a) && (reg <= 0x0d))
		ret = true;
	if ((reg >= 0x1f) && (reg <= 0x23))
		ret = true;
	if ((reg >= 0x30) && (reg <= 0x33))
		ret = true;
	if ((reg >= 0x38) && (reg <= 0x3b))
		ret = true;
	if ((reg >= 0x40) && (reg <= 0x48))
		ret = true;
	if ((reg >= 0x50) && (reg <= 0x5c))
		ret = true;
	if ((reg >= 0x61) && (reg <= 0x6d))
		ret = true;
	if ((reg >= 0xce) && (reg <= 0xef))
		ret = true;

	return ret;
}

static bool pm886_power_readable_reg(struct device *dev, unsigned int reg)
{
	bool ret = false;

	switch (reg) {
	case 0x00:
	case 0x06:
	case 0x16:
	case 0x19:
	case 0x1a:
	case 0x20:
	case 0x21:
	case 0x23:
	case 0x2c:
	case 0x2d:
	case 0x2f:
	case 0x32:
	case 0x33:
	case 0x35:
	case 0x38:
	case 0x39:
	case 0x3b:
	case 0x3e:
	case 0x3f:
	case 0x41:
	case 0x44:
	case 0x45:
	case 0x47:
	case 0x4a:
	case 0x4b:
	case 0x4d:
	case 0x50:
	case 0x51:
	case 0x53:
	case 0x56:
	case 0x57:
	case 0x59:
	case 0x5c:
	case 0x5d:
	case 0x5f:
	case 0x62:
	case 0x63:
	case 0x65:
	case 0x68:
	case 0x69:
	case 0x6b:
	case 0x6e:
	case 0x6f:
	case 0x71:
	case 0x74:
	case 0x75:
	case 0x77:
	case 0x7a:
	case 0x7b:
	case 0x7d:
		ret = true;
		break;
	default:
		break;
	}
	if ((reg >= 0x01) && (reg <= 0x03))
		ret = true;
	if ((reg >= 0x08) && (reg <= 0x0a))
		ret = true;
	if ((reg >= 0x0e) && (reg <= 0x10))
		ret = true;
	if ((reg >= 0x0e) && (reg <= 0x10))
		ret = true;
	if ((reg >= 0x27) && (reg <= 0x29))
		ret = true;

	return ret;
}

static bool pm886_gpadc_readable_reg(struct device *dev, unsigned int reg)
{
	bool ret = false;

	switch (reg) {
	case 0x06:
	case 0x13:
	case 0x14:
	case 0x18:
	case 0x1a:
	case 0x1b:
	case 0x28:
	case 0x2a:
	case 0x2b:
	case 0x38:
	case 0x3d:
	case 0x80:
	case 0x81:
	case 0x90:
	case 0x91:
	case 0xa0:
	case 0xa1:
		ret = true;
		break;
	default:
		break;
	}

	if ((reg >= 0x00) && (reg <= 0x03))
		ret = true;
	if ((reg >= 0x05) && (reg <= 0x08))
		ret = true;
	if ((reg >= 0x0a) && (reg <= 0x0e))
		ret = true;
	if ((reg >= 0x20) && (reg <= 0x26))
		ret = true;
	if ((reg >= 0x30) && (reg <= 0x34))
		ret = true;
	if ((reg >= 0x40) && (reg <= 0x43))
		ret = true;
	if ((reg >= 0x46) && (reg <= 0x5d))
		ret = true;
	if ((reg >= 0x84) && (reg <= 0x8b))
		ret = true;
	if ((reg >= 0x94) && (reg <= 0x9b))
		ret = true;
	if ((reg >= 0xa4) && (reg <= 0xad))
		ret = true;
	if ((reg >= 0xb0) && (reg <= 0xb3))
		ret = true;
	if ((reg >= 0xc0) && (reg <= 0xc7))
		ret = true;

	return ret;
}

static bool pm886_battery_readable_reg(struct device *dev, unsigned int reg)
{
	bool ret = false;

	switch (reg) {
	case 0x47:
	case 0x53:
	case 0x54:
	case 0x58:
	case 0x5b:
	case 0x65:
		ret = true;
		break;
	default:
		break;
	}

	if ((reg >= 0x00) && (reg <= 0x15))
		ret = true;
	if ((reg >= 0x28) && (reg <= 0x31))
		ret = true;
	if ((reg >= 0x34) && (reg <= 0x36))
		ret = true;
	if ((reg >= 0x38) && (reg <= 0x3b))
		ret = true;
	if ((reg >= 0x3e) && (reg <= 0x40))
		ret = true;
	if ((reg >= 0x42) && (reg <= 0x45))
		ret = true;
	if ((reg >= 0x4a) && (reg <= 0x51))
		ret = true;
	if ((reg >= 0x60) && (reg <= 0x63))
		ret = true;
	if ((reg >= 0x6b) && (reg <= 0x71))
		ret = true;

	return ret;
}

static bool pm886_test_readable_reg(struct device *dev, unsigned int reg)
{
	return true;
}

const struct regmap_config pm886_base_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pm886_base_readable_reg,

	.max_register = PM886_BASE_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_base_i2c_regmap);

const struct regmap_config pm886_power_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pm886_power_readable_reg,

	.max_register = PM886_POWER_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_power_i2c_regmap);

const struct regmap_config pm886_gpadc_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pm886_gpadc_readable_reg,

	.max_register = PM886_GPADC_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_gpadc_i2c_regmap);

const struct regmap_config pm886_battery_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pm886_battery_readable_reg,

	.max_register = PM886_BATTERY_PAGE_NUMS,
};
EXPORT_SYMBOL_GPL(pm886_battery_i2c_regmap);

const struct regmap_config pm886_test_i2c_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.readable_reg = pm886_test_readable_reg,

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


static void parse_powerup_down_log(struct pm886_chip *chip)
{
	int powerup, powerdown1, powerdown2, bit;
	static const char * const powerup_name[] = {
		"ONKEY_WAKEUP	",
		"CHG_WAKEUP	",
		"EXTON_WAKEUP	",
		"SMPL_WAKEUP	",
		"ALARM_WAKEUP	",
		"FAULT_WAKEUP	",
		"BAT_WAKEUP	",
		"RESERVED	",
	};
	static const char * const powerdown1_name[] = {
		"OVER_TEMP ",
		"UV_VSYS1  ",
		"SW_PDOWN  ",
		"FL_ALARM  ",
		"WD        ",
		"LONG_ONKEY",
		"OV_VSYS   ",
		"RTC_RESET "
	};
	static const char * const powerdown2_name[] = {
		"HYB_DONE   ",
		"UV_VSYS2   ",
		"HW_RESET   ",
		"PGOOD_PDOWN",
		"LONKEY_RTC "
	};

	/*power up log*/
	regmap_read(chip->base_regmap, PM886_POWER_UP_LOG, &powerup);
	dev_info(chip->dev, "powerup log 0x%x: 0x%x\n",
		 PM886_POWER_UP_LOG, powerup);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|     name(power up) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 7; bit++)
		dev_info(chip->dev, "|  %s  |    %x     |\n",
			powerup_name[bit], (powerup >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");

	/*power down log1*/
	regmap_read(chip->base_regmap, PM886_POWER_DOWN_LOG1, &powerdown1);
	dev_info(chip->dev, "PowerDW Log1 0x%x: 0x%x\n",
		PM886_POWER_DOWN_LOG1, powerdown1);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "| name(power down1)  |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 8; bit++)
		dev_info(chip->dev, "|    %s      |    %x     |\n",
			powerdown1_name[bit], (powerdown1 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");

	/*power down log2*/
	regmap_read(chip->base_regmap, PM886_POWER_DOWN_LOG2, &powerdown2);
	dev_info(chip->dev, "PowerDW Log2 0x%x: 0x%x\n",
		PM886_POWER_DOWN_LOG2, powerdown2);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|  name(power down2) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 5; bit++)
		dev_info(chip->dev, "|    %s     |    %x     |\n",
			powerdown2_name[bit], (powerdown2 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");

	/* write to clear */
	regmap_write(chip->base_regmap, PM886_POWER_DOWN_LOG1, 0xff);
	regmap_write(chip->base_regmap, PM886_POWER_DOWN_LOG2, 0xff);

	/* mask reserved bits and sleep indication */
	powerdown2 &= 0x1e;

	/* keep globals for external usage */
	chip->powerup = powerup;
	chip->powerdown1 = powerdown1;
	chip->powerdown2 = powerdown2;
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

	parse_powerup_down_log(chip);

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
	/* TODO: need confirmation from design team */
#if 0
	{PM886_GPIO_CTRL1, 0x40}, /* gpio1: dvc    , gpio0: input   */
	{PM886_GPIO_CTRL2, 0x00}, /*               , gpio2: input   */
	{PM886_GPIO_CTRL3, 0x44}, /* dvc2          , dvc1           */
	{PM886_GPIO_CTRL4, 0x00}, /* gpio5v_1:input, gpio5v_2: input*/
	{PM886_RTC_ALARM_CTRL1, 0x80}, /* USE_XO = 1 */
	{PM886_AON_CTRL2, 0x2a},  /* output 32kHZ from XO */
	{PM886_BK_OSC_CTRL1, 0x0c}, /* OSC_FREERUN = 1, to lock FLL */
	{PM886_BK_OSC_CTRL6, 0x0c}, /* OSCD_FREERUN = 1, to lock FLL */
	{PM886_LOWPOWER1, 0x00}, /* set internal VDD for sleep, 1.2V */
	{PM886_LOWPOWER2, 0x30}, /* XO_LJ = 1, enable low jitter for 32kHZ */
	{PM886_LOWPOWER3, 0x00},
	/* enable LPM for internal reference group in sleep */
	{PM886_LOWPOWER4, 0xc0},
#endif
};

static const struct reg_default pm886_power_patch[] = {
#if 0
	{PM886_BUCK1_SLP_CTRL, 0x30}, /*TODO: change to use sleep voltage */
	{PM886_LDO1_SLP_CTRL,  0x00}, /* disable LDO in sleep */
	{PM886_LDO2_SLP_CTRL,  0x00},
	{PM886_LDO3_SLP_CTRL,  0x00},
	{PM886_LDO4_SLP_CTRL,  0x00},
	{PM886_LDO5_SLP_CTRL,  0x00},
	{PM886_LDO6_SLP_CTRL,  0x00},
	{PM886_LDO7_SLP_CTRL,  0x00},
	{PM886_LDO8_SLP_CTRL,  0x00},
	{PM886_LDO9_SLP_CTRL,  0x00},
	{PM886_LDO10_SLP_CTRL, 0x00},
	{PM886_LDO11_SLP_CTRL, 0x00},
	{PM886_LDO12_SLP_CTRL, 0x00},
	{PM886_LDO13_SLP_CTRL, 0x00},
	{PM886_LDO14_SLP_CTRL, 0x00},
	{PM886_LDO15_SLP_CTRL, 0x00},
	{PM886_LDO16_SLP_CTRL, 0x00},
#endif
};

static const struct reg_default pm886_gpadc_patch[] = {
	/* TODO: enable GPADC? */
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
