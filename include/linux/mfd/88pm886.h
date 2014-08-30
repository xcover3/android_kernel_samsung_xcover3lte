/*
 * Marvell 88PM886 Interface
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM886_H
#define __LINUX_MFD_88PM886_H

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/atomic.h>
#include "88pm886-reg.h"

#define PM886_RTC_NAME		"88pm886-rtc"
#define PM886_ONKEY_NAME	"88pm886-onkey"
#define PM886_CHARGER_NAME	"88pm886-charger"
#define PM886_BATTERY_NAME	"88pm886-battery"
#define PM886_HEADSET_NAME	"88pm886-headset"
#define PM886_VBUS_NAME		"88pm886-vbus"
#define PM886_CFD_NAME		"88pm886-leds"
#define PM886_REGULATOR_NAME	"88pm886-regulator"
#define PM886_DVC_NAME		"88pm886-dvc"
#define PM886_DEBUGFS_NAME	"88pm886-debugfs"
#define PM886_HWMON_NAME	"88pm886-hwmon"

/* TODO: use chip id is better */
enum pm88x_type {
	PM886 = 1,
};

/* FIXME: change according to spec */
enum pm886_reg_nums {
	PM886_BASE_PAGE_NUMS = 0xff,
	PM886_POWER_PAGE_NUMS = 0xff,
	PM886_GPADC_PAGE_NUMS = 0xff,
	PM886_BATTERY_PAGE_NUMS = 0xff,
	PM886_TEST_PAGE_NUMS = 0xff,
};

enum pm886_pages {
	PM886_BASE_PAGE = 0,
	PM886_POWER_PAGE,
	PM886_GPADC_PAGE,
	PM886_BATTERY_PAGE,
	PM886_TEST_PAGE = 7,
};

/* Interrupt Number in 88PM886 */
enum pm886_irq_number {
	PM886_IRQ_ONKEY,	/* EN1b0 *//* 0 */
	PM886_IRQ_EXTON,	/* EN1b1 */
	PM886_IRQ_CHG,		/* EN1b2 */
	PM886_IRQ_BAT,		/* EN1b3 */
	PM886_IRQ_RTC,		/* EN1b4 */
	PM886_IRQ_CLASSD,	/* EN1b5 *//* 5 */
	PM886_IRQ_XO,		/* EN1b6 */
	PM886_IRQ_GPIO,		/* EN1b7 */

	PM886_IRQ_VBAT,		/* EN2b0 *//* 8 */
				/* EN2b1 */
	PM886_IRQ_VBUS,		/* EN2b2 */
	PM886_IRQ_ITEMP,	/* EN2b3 *//* 10 */
	PM886_IRQ_BUCK_PGOOD,	/* EN2b4 */
	PM886_IRQ_LDO_PGOOD,	/* EN2b5 */

	PM886_IRQ_GPADC0,	/* EN3b0 */
	PM886_IRQ_GPADC1,	/* EN3b1 */
	PM886_IRQ_GPADC2,	/* EN3b2 *//* 15 */
	PM886_IRQ_GPADC3,	/* EN3b3 */
	PM886_IRQ_MIC_DET,	/* EN3b4 */
	PM886_IRQ_HS_DET,	/* EN3b5 */
	PM886_IRQ_GND_DET,	/* EN3b6 */

	PM886_IRQ_CHG_FAIL,	/* EN4b0 *//* 20 */
	PM886_IRQ_CHG_DONE,	/* EN4b1 */
				/* EN4b2 */
	PM886_IRQ_CFD_FAIL,	/* EN4b3 */
	PM886_IRQ_OTG_FAIL,	/* EN4b4 */
	PM886_IRQ_CHG_ILIM,	/* EN4b5 *//* 25 */
				/* EN4b6 */
	PM886_IRQ_CC,		/* EN4b7 *//* 27 */

	PM886_MAX_IRQ,			   /* 28 */
};

enum {
	PM886_ID_BUCK1 = 0,
	PM886_ID_BUCK2,
	PM886_ID_BUCK3,
	PM886_ID_BUCK4,
	PM886_ID_BUCK5,

	PM886_ID_LDO1 = 5,
	PM886_ID_LDO2,
	PM886_ID_LDO3,
	PM886_ID_LDO4,
	PM886_ID_LDO5,
	PM886_ID_LDO6,
	PM886_ID_LDO7,
	PM886_ID_LDO8,
	PM886_ID_LDO9,
	PM886_ID_LDO10,
	PM886_ID_LDO11,
	PM886_ID_LDO12,
	PM886_ID_LDO13,
	PM886_ID_LDO14 = 18,
	PM886_ID_LDO15,
	PM886_ID_LDO16 = 20,

	PM886_ID_RG_MAX = 21,
};

struct pm886_chip {
	struct i2c_client *client;
	struct device *dev;

	struct i2c_client *power_page;	/* chip client for power page */
	struct i2c_client *gpadc_page;	/* chip client for gpadc page */
	struct i2c_client *battery_page;/* chip client for battery page */
	struct i2c_client *test_page;	/* chip client for test page */

	struct regmap *base_regmap;
	struct regmap *power_regmap;
	struct regmap *gpadc_regmap;
	struct regmap *battery_regmap;
	struct regmap *test_regmap;

	unsigned short power_page_addr;	/* power page I2C address */
	unsigned short gpadc_page_addr;	/* gpadc page I2C address */
	unsigned short battery_page_addr;/* battery page I2C address */
	unsigned short test_page_addr;	/* test page I2C address */

	long type;
	int irq;

	int irq_mode;
	struct regmap_irq_chip_data *irq_data;

	bool rtc_wakeup;
};

struct pm886_chip *pm886_init_chip(struct i2c_client *client);
int pm886_parse_dt(struct device_node *np, struct pm886_chip *chip);

int pm886_init_pages(struct pm886_chip *chip);
int pm886_post_init_chip(struct pm886_chip *chip);
void pm800_exit_pages(struct pm886_chip *chip);

int pm886_init_subdev(struct pm886_chip *chip);
long pm886_of_get_type(struct device *dev);
void pm886_dev_exit(struct pm886_chip *chip);

int pm886_irq_init(struct pm886_chip *chip);
int pm886_irq_exit(struct pm886_chip *chip);
int pm886_apply_patch(struct pm886_chip *chip);
int pm886_apply_bd_patch(struct pm886_chip *chip, struct device_node *np);

extern struct regmap_irq_chip pm886_irq_chip;
extern const struct of_device_id pm886_of_match[];

/* dvc external interface */
int pm886_dvc_set_volt(u8 level, int uv);
int pm886_dvc_get_volt(u8 level);

#endif /* __LINUX_MFD_88PM886_H */
