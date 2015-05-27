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
#include <linux/mfd/88pm88x.h>
#include <linux/mfd/88pm886.h>
#include <linux/regulator/machine.h>

#include "88pm88x.h"

#define PM88X_POWER_UP_LOG		(0x17)
#define PM88X_POWER_DOWN_LOG1		(0xe5)
#define PM88X_POWER_DOWN_LOG2		(0xe6)
#define PM88X_SW_PDOWN			(1 << 5)
#define PM88X_BK_OSE_CTRL3		(0x52)
#define PM88X_CHGBK_CONFIG6		(0x50)

/* don't export it at present */
static struct pm88x_chip *pm88x_chip_priv;

extern struct regmap *get_88pm860_codec_regmap(void);
struct regmap *companion_base_page;

struct regmap *get_companion(void)
{
	return companion_base_page;
}
EXPORT_SYMBOL(get_companion);

struct regmap *get_codec_companion(void)
{
	return get_88pm860_codec_regmap();
}
EXPORT_SYMBOL(get_codec_companion);

static const struct resource onkey_resources[] = {
	CELL_IRQ_RESOURCE(PM88X_ONKEY_NAME, PM88X_IRQ_ONKEY),
};

static const struct resource rtc_resources[] = {
	CELL_IRQ_RESOURCE(PM88X_RTC_NAME, PM88X_IRQ_RTC),
};

static const struct resource charger_resources[] = {
	CELL_IRQ_RESOURCE("88pm88x-chg-fail", PM88X_IRQ_CHG_FAIL),
	CELL_IRQ_RESOURCE("88pm88x-chg-done", PM88X_IRQ_CHG_DONE),
	CELL_IRQ_RESOURCE("88pm88x-chg-good", PM88X_IRQ_CHG_GOOD),
};

static const struct resource battery_resources[] = {
	CELL_IRQ_RESOURCE("88pm88x-bat-cc", PM88X_IRQ_CC),
	CELL_IRQ_RESOURCE("88pm88x-bat-volt", PM88X_IRQ_VBAT),
};

static const struct resource headset_resources[] = {
	CELL_IRQ_RESOURCE("88pm88x-headset-det", PM88X_IRQ_HS_DET),
	CELL_IRQ_RESOURCE("88pm88x-mic-det", PM88X_IRQ_MIC_DET),
};

static const struct resource vbus_resources[] = {
	CELL_IRQ_RESOURCE("88pm88x-vbus-det", PM88X_IRQ_VBUS),
	CELL_IRQ_RESOURCE("88pm88x-gpadc0", PM88X_IRQ_GPADC0),
	CELL_IRQ_RESOURCE("88pm88x-gpadc1", PM88X_IRQ_GPADC1),
	CELL_IRQ_RESOURCE("88pm88x-gpadc2", PM88X_IRQ_GPADC2),
	CELL_IRQ_RESOURCE("88pm88x-gpadc3", PM88X_IRQ_GPADC3),
	CELL_IRQ_RESOURCE("88pm88x-otg-fail", PM88X_IRQ_OTG_FAIL),
};

static const struct resource leds_resources[] = {
	CELL_IRQ_RESOURCE("88pm88x-cfd-fail", PM88X_IRQ_CFD_FAIL),
};

static const struct resource dvc_resources[] = {
	{
	.name = PM88X_DVC_NAME,
	},
};

static const struct resource rgb_resources[] = {
	{
	.name = PM88X_RGB_NAME,
	},
};

static const struct resource debugfs_resources[] = {
	{
	.name = PM88X_DEBUGFS_NAME,
	},
};

static const struct resource gpadc_resources[] = {
	{
	.name = PM88X_GPADC_NAME,
	},
};

static const struct resource vr_resources[] = {
	{
	.name = PM88X_VIRTUAL_REGULATOR_NAME,
	},
};

static const struct mfd_cell common_cell_devs[] = {
	CELL_DEV(PM88X_RTC_NAME, rtc_resources, "marvell,88pm88x-rtc", -1),
	CELL_DEV(PM88X_ONKEY_NAME, onkey_resources, "marvell,88pm88x-onkey", -1),
	CELL_DEV(PM88X_CHARGER_NAME, charger_resources, "marvell,88pm88x-charger", -1),
	CELL_DEV(PM88X_BATTERY_NAME, battery_resources, "marvell,88pm88x-battery", -1),
	CELL_DEV(PM88X_HEADSET_NAME, headset_resources, "marvell,88pm88x-headset", -1),
	CELL_DEV(PM88X_VBUS_NAME, vbus_resources, "marvell,88pm88x-vbus", -1),
	CELL_DEV(PM88X_CFD_NAME, leds_resources, "marvell,88pm88x-leds", PM88X_FLASH_LED),
	CELL_DEV(PM88X_CFD_NAME, leds_resources, "marvell,88pm88x-leds", PM88X_TORCH_LED),
	CELL_DEV(PM88X_DVC_NAME, dvc_resources, "marvell,88pm88x-dvc", -1),
	CELL_DEV(PM88X_RGB_NAME, rgb_resources, "marvell,88pm88x-rgb0", PM88X_RGB_LED0),
	CELL_DEV(PM88X_RGB_NAME, rgb_resources, "marvell,88pm88x-rgb1", PM88X_RGB_LED1),
	CELL_DEV(PM88X_RGB_NAME, rgb_resources, "marvell,88pm88x-rgb2", PM88X_RGB_LED2),
	CELL_DEV(PM88X_DEBUGFS_NAME, debugfs_resources, "marvell,88pm88x-debugfs", -1),
	CELL_DEV(PM88X_GPADC_NAME, gpadc_resources, "marvell,88pm88x-gpadc", -1),
	CELL_DEV(PM88X_VIRTUAL_REGULATOR_NAME, vr_resources, "marvell,88pm88x-votg", -1),
};

const struct of_device_id pm88x_of_match[] = {
	{ .compatible = "marvell,88pm886", .data = (void *)PM886 },
	{ .compatible = "marvell,88pm880", .data = (void *)PM880 },
	{},
};
EXPORT_SYMBOL_GPL(pm88x_of_match);

struct pm88x_chip *pm88x_init_chip(struct i2c_client *client)
{
	struct pm88x_chip *chip;

	chip = devm_kzalloc(&client->dev, sizeof(struct pm88x_chip), GFP_KERNEL);
	if (!chip)
		return ERR_PTR(-ENOMEM);

	chip->client = client;
	chip->irq = client->irq;
	chip->dev = &client->dev;
	chip->ldo_page_addr = client->addr + 1;
	chip->power_page_addr = client->addr + 1;
	chip->gpadc_page_addr = client->addr + 2;
	chip->battery_page_addr = client->addr + 3;
	chip->buck_page_addr = client->addr + 4;
	chip->test_page_addr = client->addr + 7;

	dev_set_drvdata(chip->dev, chip);
	i2c_set_clientdata(chip->client, chip);

	device_init_wakeup(&client->dev, 1);

	return chip;
}

int pm88x_parse_dt(struct device_node *np, struct pm88x_chip *chip)
{
	if (!chip)
		return -EINVAL;

	chip->irq_mode =
		!of_property_read_bool(np, "marvell,88pm88x-irq-write-clear");

	return 0;
}


static void parse_powerup_down_log(struct pm88x_chip *chip)
{
	int powerup, powerdown1, powerdown2, bit;
	int powerup_bits, powerdown1_bits, powerdown2_bits;
	static const char * const powerup_name[] = {
		"ONKEY_WAKEUP	",
		"CHG_WAKEUP	",
		"EXTON_WAKEUP	",
		"SMPL_WAKEUP	",
		"ALARM_WAKEUP	",
		"FAULT_WAKEUP	",
		"BAT_WAKEUP	",
		"WLCHG_WAKEUP	",
	};
	static const char * const powerdown1_name[] = {
		"OVER_TEMP ",
		"UV_VANA5  ",
		"SW_PDOWN  ",
		"FL_ALARM  ",
		"WD        ",
		"LONG_ONKEY",
		"OV_VSYS   ",
		"RTC_RESET "
	};
	static const char * const powerdown2_name[] = {
		"HYB_DONE   ",
		"UV_VBAT    ",
		"HW_RESET2  ",
		"PGOOD_PDOWN",
		"LONKEY_RTC ",
		"HW_RESET1  ",
	};

	regmap_read(chip->base_regmap, PM88X_POWER_UP_LOG, &powerup);
	regmap_read(chip->base_regmap, PM88X_POWER_DOWN_LOG1, &powerdown1);
	regmap_read(chip->base_regmap, PM88X_POWER_DOWN_LOG2, &powerdown2);

	/*
	 * mask reserved bits
	 *
	 * note: HYB_DONE and HW_RESET1 are kept,
	 *       but should not be considered as power down events
	 */
	switch (chip->type) {
	case PM886:
		powerup &= 0x7f;
		powerdown2 &= 0x1f;
		powerup_bits = 7;
		powerdown1_bits = 8;
		powerdown2_bits = 5;
		break;
	case PM880:
		powerdown2 &= 0x3f;
		powerup_bits = 8;
		powerdown1_bits = 8;
		powerdown2_bits = 6;
		break;
	default:
		return;
	}

	/* keep globals for external usage */
	chip->powerup = powerup;
	chip->powerdown1 = powerdown1;
	chip->powerdown2 = powerdown2;

	/* power up log */
	dev_info(chip->dev, "powerup log 0x%x: 0x%x\n",
		 PM88X_POWER_UP_LOG, powerup);
	dev_info(chip->dev, " ----------------------------\n");
	dev_info(chip->dev, "|  name(power up) |  status  |\n");
	dev_info(chip->dev, "|-----------------|----------|\n");
	for (bit = 0; bit < powerup_bits; bit++)
		dev_info(chip->dev, "|  %s  |    %x     |\n",
			powerup_name[bit], (powerup >> bit) & 1);
	dev_info(chip->dev, " ----------------------------\n");

	/* power down log1 */
	dev_info(chip->dev, "PowerDW Log1 0x%x: 0x%x\n",
		PM88X_POWER_DOWN_LOG1, powerdown1);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "| name(power down1)  |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < powerdown1_bits; bit++)
		dev_info(chip->dev, "|    %s      |    %x     |\n",
			powerdown1_name[bit], (powerdown1 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");

	/* power down log2 */
	dev_info(chip->dev, "PowerDW Log2 0x%x: 0x%x\n",
		PM88X_POWER_DOWN_LOG2, powerdown2);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|  name(power down2) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < powerdown2_bits; bit++)
		dev_info(chip->dev, "|    %s     |    %x     |\n",
			powerdown2_name[bit], (powerdown2 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");

	/* write to clear power down log */
	regmap_write(chip->base_regmap, PM88X_POWER_DOWN_LOG1, 0xff);
	regmap_write(chip->base_regmap, PM88X_POWER_DOWN_LOG2, 0xff);
}

static const char *chip_stepping_to_string(unsigned int id)
{
	switch (id) {
	case 0x00:
		return "88pm886 A0";
	case 0xa1:
		return "88pm886 A1";
	case 0xb0:
		return "88pm880 A0";
	case 0xb1:
		return "88pm880 A1";
	default:
		break;
	}

	return "Unknown";
}

static void pm88x_verify_is_trimmed(struct pm88x_chip *chip)
{
	int val, trimming_reg, trimming_mask;

	switch (chip->type) {
	case PM886:
		/* bit [7] of reg 0x8A indicates trimming */
		trimming_reg = PM886_TEST_OTP5COPY7;
		trimming_mask = PM886_OTP_OOL_TEMP_DIS;
		break;
	case PM880:
		/* bits [4:0] of reg 0x92 indicates trimming */
		trimming_reg = PM880_TEST_OTP6COPY7;
		trimming_mask = PM880_VSYS_CHECK;
		break;
	default:
		return;
	}

	/* unlock test page */
	regmap_write(chip->base_regmap, 0x1f, 0x1);

	/* read trimming status */
	regmap_read(chip->test_regmap, trimming_reg, &val);

	/* lock test page */
	regmap_write(chip->base_regmap, 0x1f, 0x0);

	/* trimming value should not be zero */
	if ((val & trimming_mask) == 0) {
		dev_err(chip->dev, "WARNING!!! PMIC is not trimmed, reg[0x%x]=0x%x\n",
			trimming_reg, val);
		chip->trimming_status = false;
	} else {
		dev_info(chip->dev, "PMIC is trimmed, reg[0x%x]=0x%x\n", trimming_reg, val);
		chip->trimming_status = true;
	}
}

int pm88x_post_init_chip(struct pm88x_chip *chip)
{
	int ret;
	unsigned int val;

	if (!chip || !chip->base_regmap || !chip->power_regmap ||
	    !chip->gpadc_regmap || !chip->battery_regmap)
		return -EINVAL;

	/* save chip stepping */
	ret = regmap_read(chip->base_regmap, PM88X_ID_REG, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read chip ID: %d\n", ret);
		return ret;
	}
	chip->chip_id = val;

	dev_info(chip->dev, "PM88X chip ID = 0x%x(%s)\n", val,
			chip_stepping_to_string(chip->chip_id));

	/* verify chip is trimmed */
	pm88x_verify_is_trimmed(chip);

	/* read before alarm wake up bit before initialize interrupt */
	ret = regmap_read(chip->base_regmap, PM88X_RTC_ALARM_CTRL1, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read RTC register: %d\n", ret);
		return ret;
	}
	chip->rtc_wakeup = !!(val & PM88X_ALARM_WAKEUP);

	parse_powerup_down_log(chip);

	return 0;
}

int pm88x_init_pages(struct pm88x_chip *chip)
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

	chip->type = pm88x_of_get_type(&client->dev);
	switch (chip->type) {
	case PM886:
		base_regmap_config = &pm886_base_i2c_regmap;
		power_regmap_config = &pm886_power_i2c_regmap;
		gpadc_regmap_config = &pm886_gpadc_i2c_regmap;
		battery_regmap_config = &pm886_battery_i2c_regmap;
		test_regmap_config = &pm886_test_i2c_regmap;
		break;
	case PM880:
		base_regmap_config = &pm880_base_i2c_regmap;
		power_regmap_config = &pm880_power_i2c_regmap;
		gpadc_regmap_config = &pm880_gpadc_i2c_regmap;
		battery_regmap_config = &pm880_battery_i2c_regmap;
		test_regmap_config = &pm880_test_i2c_regmap;
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

	companion_base_page = chip->base_regmap;
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

	chip->type = pm88x_of_get_type(&client->dev);
	switch (chip->type) {
	case PM886:
		/* ldo page */
		chip->ldo_page = chip->power_page;
		chip->ldo_regmap = chip->power_regmap;
		/* buck page */
		chip->buck_regmap = chip->power_regmap;
		break;
	case PM880:
		/* ldo page */
		chip->ldo_page = chip->power_page;
		chip->ldo_regmap = chip->power_regmap;

		/* buck page */
		chip->buck_page = i2c_new_dummy(client->adapter,
						chip->buck_page_addr);
		if (!chip->buck_page) {
			dev_err(chip->dev, "Failed to new buck_page: %d\n", ret);
			ret = -ENODEV;
			goto out;
		}
		chip->buck_regmap = devm_regmap_init_i2c(chip->buck_page,
							 power_regmap_config);
		if (IS_ERR(chip->buck_regmap)) {
			dev_err(chip->dev, "Failed to init buck_regmap: %d\n", ret);
			ret = PTR_ERR(chip->buck_regmap);
			goto out;
		}

		break;
	default:
		ret = -EINVAL;
		break;
	}
out:
	return ret;
}

void pm800_exit_pages(struct pm88x_chip *chip)
{
	if (!chip)
		return;

	if (chip->ldo_page)
		i2c_unregister_device(chip->ldo_page);
	if (chip->gpadc_page)
		i2c_unregister_device(chip->gpadc_page);
	if (chip->test_page)
		i2c_unregister_device(chip->test_page);
	/* no need to unregister ldo_page */
	switch (chip->type) {
	case PM886:
		break;
	case PM880:
		if (chip->buck_page)
			i2c_unregister_device(chip->buck_page);
		break;
	default:
		break;
	}
}

int pm88x_init_subdev(struct pm88x_chip *chip)
{
	int ret;
	if (!chip)
		return -EINVAL;

	ret = mfd_add_devices(chip->dev, 0, common_cell_devs,
			      ARRAY_SIZE(common_cell_devs), NULL, 0,
			      regmap_irq_get_domain(chip->irq_data));
	if (ret < 0)
		return ret;

	switch (chip->type) {
	case PM886:
		ret = mfd_add_devices(chip->dev, 0, pm886_cell_info.cells,
				      pm886_cell_info.cell_nr, NULL, 0,
				      regmap_irq_get_domain(chip->irq_data));
		break;
	case PM880:
		ret = mfd_add_devices(chip->dev, 0, pm880_cell_info.cells,
				      pm880_cell_info.cell_nr, NULL, 0,
				      regmap_irq_get_domain(chip->irq_data));
		break;
	default:
		break;
	}
	return ret;
}

static int (*apply_to_chip)(struct pm88x_chip *chip);
/* PMIC chip itself related */
int pm88x_apply_patch(struct pm88x_chip *chip)
{
	if (!chip || !chip->client)
		return -EINVAL;

	chip->type = pm88x_of_get_type(&chip->client->dev);
	switch (chip->type) {
	case PM886:
		apply_to_chip = pm886_apply_patch;
		break;
	case PM880:
		apply_to_chip = pm880_apply_patch;
		break;
	default:
		break;
	}
	if (apply_to_chip)
		apply_to_chip(chip);
	return 0;
}

int pm88x_stepping_fixup(struct pm88x_chip *chip)
{
	if (!chip || !chip->client)
		return -EINVAL;

	chip->type = pm88x_of_get_type(&chip->client->dev);
	switch (chip->type) {
	case PM886:
		if (chip->chip_id == PM886_A1) {
			/* set HPFM bit for buck1 */
			regmap_update_bits(chip->power_regmap, 0xa0, 1 << 7, 1 << 7);
			/* clear LPFM bit for buck1 */
			regmap_update_bits(chip->power_regmap, 0x9f, 1 << 3, 0 << 3);
		}
		/* set USE_XO */
		regmap_update_bits(chip->base_regmap, PM88X_RTC_ALARM_CTRL1,
			PM88X_USE_XO, PM88X_USE_XO);
		/* to fix fault charger remove detection */
		regmap_write(chip->base_regmap, PM88X_BK_OSE_CTRL3, 0xc0);
		regmap_write(chip->battery_regmap, PM88X_CHGBK_CONFIG6, 0xe1);
		break;
	case PM880:
		/* to fix fault charger remove detection */
		regmap_write(chip->base_regmap, PM88X_BK_OSE_CTRL3, 0xc0);
		regmap_write(chip->battery_regmap, PM88X_CHGBK_CONFIG6, 0xe1);
		break;
	default:
		break;
	}

	return 0;
}

int pm88x_apply_bd_patch(struct pm88x_chip *chip, struct device_node *np)
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
		case PM88X_BASE_PAGE:
			regmap_update_bits(chip->base_regmap, reg, mask, data);
			break;
		case PM88X_LDO_PAGE:
			regmap_update_bits(chip->ldo_regmap, reg, mask, data);
			break;
		case PM88X_GPADC_PAGE:
			regmap_update_bits(chip->gpadc_regmap, reg, mask, data);
			break;
		case PM88X_BATTERY_PAGE:
			regmap_update_bits(chip->battery_regmap, reg, mask, data);
			break;
		case PM88X_BUCK_PAGE:
			regmap_update_bits(chip->buck_regmap, reg, mask, data);
			break;
		case PM88X_TEST_PAGE:
			regmap_update_bits(chip->test_regmap, reg, mask, data);
			break;
		default:
			dev_err(chip->dev, "wrong page index for %d\n", page);
			break;
		}
	}
	return 0;
}

long pm88x_of_get_type(struct device *dev)
{
	const struct of_device_id *id = of_match_device(pm88x_of_match, dev);

	if (id)
		return (long)id->data;
	else
		return 0;
}

void pm88x_dev_exit(struct pm88x_chip *chip)
{
	mfd_remove_devices(chip->dev);
	pm88x_irq_exit(chip);
}

void pm88x_set_chip(struct pm88x_chip *chip)
{
	pm88x_chip_priv = chip;
}

struct pm88x_chip *pm88x_get_chip(void)
{
	return pm88x_chip_priv;
}

static int i2c_raw_update_bits(u8 reg, u8 value)
{
	int ret;
	u8 data, buf[2];

	/* only for base page */
	struct i2c_client *client = pm88x_get_chip()->client;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &data,
		},
	};

	/*
	 * I2C pins may be in non-AP pinstate, and __i2c_transfer
	 * won't change it back to AP pinstate like i2c_transfer,
	 * so change i2c pins to AP pinstate explicitly here.
	 */
	i2c_pxa_set_pinstate(client->adapter, "default");

	/*
	 * set i2c to pio mode
	 * for in power off sequence, irq has been disabled
	 */
	i2c_set_pio_mode(client->adapter, 1);

	/* 1. read the original value */
	buf[0] = reg;
	ret = __i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2) {
		pr_err("%s read register fails, ret = %d...\n", __func__, ret);
		WARN_ON(1);
		goto out;
	}

	/* 2. update value */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf[0] = reg;
	msgs[0].buf[1] = data | value;
	ret = __i2c_transfer(client->adapter, msgs, 1);
	if (ret != 1) {
		pr_err("%s write data fails: ret = %d\n", __func__, ret);
		WARN_ON(1);
	}
out:
	return ret;
}

static void pm88x_get_status(void)
{
	u8 data, buf[2];
	int ret;

	/* only for base page */
	struct i2c_client *client = pm88x_get_chip()->client;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &data,
		},
	};

	i2c_pxa_set_pinstate(client->adapter, "default");
	i2c_set_pio_mode(client->adapter, 1);

	buf[0] = PM88X_MISC_CONFIG1;
	ret = __i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2)
		pr_err("%s read register fails..., error code = %d\n", __func__, ret);

	pr_info("1: update value: msgs->buf[0] = 0x%x, msgs->buf[1] = 0x%x\n",
		msgs->buf[0], msgs->buf[1]);

	buf[0] = PM88X_MISC_CONFIG1;
	ret = __i2c_transfer(client->adapter, msgs, 2);
	if (ret != 2)
		pr_err("%s read register fails..., error code = %d\n", __func__, ret);

	pr_info("2: update value: msgs->buf[0] = 0x%x, msgs->buf[1] = 0x%x\n",
		msgs->buf[0], msgs->buf[1]);
}

void pm88x_power_off(void)
{
	int ret;

	pr_info("begin to power off system.");
	ret = i2c_raw_update_bits(PM88X_MISC_CONFIG1, PM88X_SW_PDOWN);
	if (ret < 0)
		pr_err("%s power off fails", __func__);
	pr_info("finish powering off system: this line shouldn't appear.");

	pm88x_get_status();

	/* wait for power off */
	for (;;)
		cpu_relax();
}

int pm88x_reboot_notifier_callback(struct notifier_block *this,
				   unsigned long code, void *cmd)
{
	struct pm88x_chip *chip;

	pr_info("%s: code = %ld, cmd = '%s'\n", __func__, code, (char *)cmd);

	chip = container_of(this, struct pm88x_chip, reboot_notifier);
	if (cmd && (0 == strncmp(cmd, "recovery", 8))) {
		pr_info("%s: --> handle recovery mode\n", __func__);
		regmap_update_bits(chip->base_regmap, PM88X_RTC_SPARE6,
				   1 << 0, 1 << 0);

	} else {
		/* clear the recovery indication bit */
		regmap_update_bits(chip->base_regmap,
				   PM88X_RTC_SPARE6, 1 << 0, 0);
	}
	/*
	 * the uboot recognize the "reboot" case via power down log,
	 * which is 0 in this case
	 */
	return 0;
}
