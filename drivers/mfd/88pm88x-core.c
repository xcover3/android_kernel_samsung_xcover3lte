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

#include "88pm88x.h"

#define PM886_POWER_UP_LOG		(0x17)
#define PM886_POWER_DOWN_LOG1		(0xe5)
#define PM886_POWER_DOWN_LOG2		(0xe6)
#define PM886_SW_PDOWN			(1 << 5)
#define PM886_CHGBK_CONFIG6		(0x50)

/* don't export it at present */
static struct pm886_chip *pm886_chip_priv;

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
	CELL_IRQ_RESOURCE(PM886_ONKEY_NAME, PM886_IRQ_ONKEY),
};

static const struct resource rtc_resources[] = {
	CELL_IRQ_RESOURCE(PM886_RTC_NAME, PM886_IRQ_RTC),
};

static const struct resource charger_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-chg-fail", PM886_IRQ_CHG_FAIL),
	CELL_IRQ_RESOURCE("88pm886-chg-done", PM886_IRQ_CHG_DONE),
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
	CELL_IRQ_RESOURCE("88pm886-chg-det", PM886_IRQ_CHG_GOOD),
	CELL_IRQ_RESOURCE("88pm886-gpadc0", PM886_IRQ_GPADC0),
	CELL_IRQ_RESOURCE("88pm886-gpadc1", PM886_IRQ_GPADC1),
	CELL_IRQ_RESOURCE("88pm886-gpadc2", PM886_IRQ_GPADC2),
	CELL_IRQ_RESOURCE("88pm886-gpadc3", PM886_IRQ_GPADC3),
	CELL_IRQ_RESOURCE("88pm886-otg-fail", PM886_IRQ_OTG_FAIL),
};

static const struct resource leds_resources[] = {
	CELL_IRQ_RESOURCE("88pm886-cfd-fail", PM886_IRQ_CFD_FAIL),
};

static const struct resource dvc_resources[] = {
	{
	.name = PM886_DVC_NAME,
	},
};

static const struct resource rgb_resources[] = {
	{
	.name = PM886_RGB_NAME,
	},
};

static const struct resource debugfs_resources[] = {
	{
	.name = PM886_DEBUGFS_NAME,
	},
};

static const struct resource gpadc_resources[] = {
	{
	.name = PM886_GPADC_NAME,
	},
};

static const struct mfd_cell common_cell_devs[] = {
	CELL_DEV(PM886_RTC_NAME, rtc_resources, "marvell,88pm886-rtc", -1),
	CELL_DEV(PM886_ONKEY_NAME, onkey_resources, "marvell,88pm886-onkey", -1),
	CELL_DEV(PM886_CHARGER_NAME, charger_resources, "marvell,88pm886-charger", -1),
	CELL_DEV(PM886_BATTERY_NAME, battery_resources, "marvell,88pm886-battery", -1),
	CELL_DEV(PM886_HEADSET_NAME, headset_resources, "marvell,88pm886-headset", -1),
	CELL_DEV(PM886_VBUS_NAME, vbus_resources, "marvell,88pm886-vbus", -1),
	CELL_DEV(PM886_CFD_NAME, leds_resources, "marvell,88pm886-leds", PM886_FLASH_LED),
	CELL_DEV(PM886_CFD_NAME, leds_resources, "marvell,88pm886-leds", PM886_TORCH_LED),
	CELL_DEV(PM886_DVC_NAME, dvc_resources, "marvell,88pm886-dvc", -1),
	CELL_DEV(PM886_RGB_NAME, rgb_resources, "marvell,88pm886-rgb0", PM886_RGB_LED0),
	CELL_DEV(PM886_RGB_NAME, rgb_resources, "marvell,88pm886-rgb1", PM886_RGB_LED1),
	CELL_DEV(PM886_RGB_NAME, rgb_resources, "marvell,88pm886-rgb2", PM886_RGB_LED2),
	CELL_DEV(PM886_DEBUGFS_NAME, debugfs_resources, "marvell,88pm886-debugfs", -1),
	CELL_DEV(PM886_GPADC_NAME, gpadc_resources, "marvell,88pm886-gpadc", -1),
};

const struct of_device_id pm886_of_match[] = {
	{ .compatible = "marvell,88pm886", .data = (void *)PM886 },
	{},
};
EXPORT_SYMBOL_GPL(pm886_of_match);

struct pm886_chip *pm886_init_chip(struct i2c_client *client)
{
	struct pm886_chip *chip;

	chip = devm_kzalloc(&client->dev, sizeof(struct pm886_chip), GFP_KERNEL);
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

	/* save chip stepping */
	ret = regmap_read(chip->base_regmap, PM886_ID_REG, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read chip ID: %d\n", ret);
		return ret;
	}
	chip->chip_id = val;

	dev_info(chip->dev, "PM886 chip ID = 0x%x\n", val);

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

	chip->type = pm886_of_get_type(&client->dev);
	switch (chip->type) {
	case PM886:
		/* ldo page */
		chip->ldo_page = chip->power_page;
		chip->ldo_regmap = chip->power_regmap;
		/* buck page */
		chip->buck_regmap = chip->power_regmap;
		break;
	default:
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
	}
out:
	return ret;
}

void pm800_exit_pages(struct pm886_chip *chip)
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
	default:
		if (chip->buck_page)
			i2c_unregister_device(chip->buck_page);
	}
}


int pm886_init_subdev(struct pm886_chip *chip)
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
	default:
		break;
	}
	return ret;
}

static int (*apply_to_chip)(struct pm886_chip *chip);
/* PMIC chip itself related */
int pm88x_apply_patch(struct pm886_chip *chip)
{
	if (!chip || !chip->client)
		return -EINVAL;

	chip->type = pm886_of_get_type(&chip->client->dev);
	switch (chip->type) {
	case PM886:
		apply_to_chip = pm886_apply_patch;
		break;
	default:
		break;
	}
	if (!apply_to_chip)
		apply_to_chip(chip);
	return 0;
}

int pm886_stepping_fixup(struct pm886_chip *chip)
{
	if (!chip || !chip->client)
		return -EINVAL;

	chip->type = pm886_of_get_type(&chip->client->dev);
	switch (chip->type) {
	case PM886:
		if (chip->chip_id == PM886_A1) {
			/* set HPFM bit for buck1 */
			regmap_update_bits(chip->power_regmap, 0xa0, 1 << 7, 1 << 7);
			/* clear LPFM bit for buck1 */
			regmap_update_bits(chip->power_regmap, 0x9f, 1 << 3, 0 << 3);
		}
		/* set USE_XO */
		regmap_update_bits(chip->base_regmap, PM886_RTC_ALARM_CTRL1,
			PM886_USE_XO, PM886_USE_XO);
		break;
	default:
		break;
	}

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
		case PM886_LDO_PAGE:
			regmap_update_bits(chip->ldo_regmap, reg, mask, data);
			break;
		case PM886_GPADC_PAGE:
			regmap_update_bits(chip->gpadc_regmap, reg, mask, data);
			break;
		case PM886_BATTERY_PAGE:
			regmap_update_bits(chip->battery_regmap, reg, mask, data);
			break;
		case PM886_BUCK_PAGE:
			regmap_update_bits(chip->buck_regmap, reg, mask, data);
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

void pm886_set_chip(struct pm886_chip *chip)
{
	pm886_chip_priv = chip;
}

struct pm886_chip *pm886_get_chip(void)
{
	return pm886_chip_priv;
}

static int i2c_raw_update_bits(u8 reg, u8 value)
{
	int ret;
	u8 data, buf[2];

	/* only for base page */
	struct i2c_client *client = pm886_get_chip()->client;
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
	if (ret < 0) {
		pr_err("%s read register fails...\n", __func__);
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
	if (ret < 0) {
		pr_err("%s write data fails: ret = %d\n", __func__, ret);
		WARN_ON(1);
	}
out:
	return ret;
}

void pm886_power_off(void)
{
	int ret;

	pr_info("begin to power off system.");
	ret = i2c_raw_update_bits(PM886_MISC_CONFIG1, PM886_SW_PDOWN);
	if (ret < 0)
		pr_err("%s power off fails", __func__);
	pr_info("finish powering off system: this line shouldn't appear.");

	/* wait for power off */
	for (;;)
		cpu_relax();
}

int pm886_reboot_notifier_callback(struct notifier_block *this,
				   unsigned long code, void *cmd)
{
	struct pm886_chip *chip;

	pr_info("%s: code = %ld, cmd = '%s'\n", __func__, code, (char *)cmd);

	chip = container_of(this, struct pm886_chip, reboot_notifier);
	if (cmd && (0 == strncmp(cmd, "recovery", 8))) {
		pr_info("%s: --> handle recovery mode\n", __func__);
		regmap_update_bits(chip->base_regmap, PM886_RTC_SPARE6,
				   1 << 0, 1 << 0);

	} else {
		/* clear the recovery indication bit */
		regmap_update_bits(chip->base_regmap,
				   PM886_RTC_SPARE6, 1 << 0, 0);
	}
	/*
	 * the uboot recognize the "reboot" case via power down log,
	 * which is 0 in this case
	 */
	return 0;
}
