/*
 * 88pm886 interrupt support
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/mfd/88pm886.h>

/* interrupt status registers */
#define PM886_INT_STATUS1		(0x05)

#define PM886_INT_ENA_1			(0x0a)
#define PM886_ONKEY_INT_ENA1		(1 << 0)
#define PM886_EXTON_INT_ENA1		(1 << 1)
#define PM886_CHG_INT_ENA1		(1 << 2)
#define PM886_BAT_INT_ENA1		(1 << 3)
#define PM886_RTC_INT_ENA1		(1 << 4)
#define PM886_CLASSD_INT_ENA1		(1 << 5)
#define PM886_XO_INT_ENA1		(1 << 6)
#define PM886_GPIO_INT_ENA1		(1 << 7)

#define PM886_INT_ENA_2			(0x0b)
#define PM886_VBAT_INT_ENA2		(1 << 0)
#define PM886_RSVED1_INT_ENA2		(1 << 1)
#define PM886_VBUS_INT_ENA2		(1 << 2)
#define PM886_ITEMP_INT_ENA2		(1 << 3)
#define PM886_BUCK_PGOOD_INT_ENA2	(1 << 4)
#define PM886_LDO_PGOOD_INT_ENA2	(1 << 5)
#define PM886_RSVED6_INT_ENA2		(1 << 6)
#define PM886_RSVED7_INT_ENA2		(1 << 7)

#define PM886_INT_ENA_3			(0x0c)
#define PM886_GPADC0_INT_ENA3		(1 << 0)
#define PM886_GPADC1_INT_ENA3		(1 << 1)
#define PM886_GPADC2_INT_ENA3		(1 << 2)
#define PM886_GPADC3_INT_ENA3		(1 << 3)
#define PM886_MIC_INT_ENA3		(1 << 4)
#define PM886_HS_INT_ENA3		(1 << 5)
#define PM886_GND_INT_ENA3		(1 << 6)
#define PM886_RSVED7_INT_ENA3		(1 << 7)

#define PM886_INT_ENA_4			(0x0d)
#define PM886_CHG_FAIL_INT_ENA4		(1 << 0)
#define PM886_CHG_DONE_INT_ENA4		(1 << 1)
#define PM886_RSVED2_INT_ENA4		(1 << 2)
#define PM886_OTG_FAIL_INT_ENA4		(1 << 3)
#define PM886_RSVED4_INT_ENA4		(1 << 4)
#define PM886_CHG_ILIM_INT_ENA4		(1 << 5)
#define PM886_CC_INT_ENA4		(1 << 6)
#define PM886_RSVED7_INT_ENA4		(1 << 7)

#define PM886_MISC_CONFIG2		(0x15)
#define PM886_INV_INT			(1 << 0)
#define PM886_INT_CLEAR			(1 << 1)
#define PM886_INT_RC			(0 << 1)
#define PM886_INT_WC			(1 << 1)
#define PM886_INT_MASK_MODE		(1 << 2)

static const struct regmap_irq pm886_irqs[] = {
	/* INT0 */
	[PM886_IRQ_ONKEY] = {.reg_offset = 0, .mask = PM886_ONKEY_INT_ENA1,},
	[PM886_IRQ_EXTON] = {.reg_offset = 0, .mask = PM886_EXTON_INT_ENA1,},
	[PM886_IRQ_CHG] = {.reg_offset = 0, .mask = PM886_CHG_INT_ENA1,},
	[PM886_IRQ_BAT] = {.reg_offset = 0, .mask = PM886_BAT_INT_ENA1,},
	[PM886_IRQ_RTC] = {.reg_offset = 0, .mask = PM886_RTC_INT_ENA1,},
	[PM886_IRQ_CLASSD] = { .reg_offset = 0, .mask = PM886_CLASSD_INT_ENA1,},
	[PM886_IRQ_XO] = {.reg_offset = 0, .mask = PM886_XO_INT_ENA1,},
	[PM886_IRQ_GPIO] = {.reg_offset = 0, .mask = PM886_GPIO_INT_ENA1,},

	/* INT1 */
	[PM886_IRQ_VBAT] = {.reg_offset = 1, .mask = PM886_VBAT_INT_ENA2,},
	[PM886_IRQ_VBUS] = {.reg_offset = 1, .mask = PM886_VBUS_INT_ENA2,},
	[PM886_IRQ_ITEMP] = {.reg_offset = 1, .mask = PM886_ITEMP_INT_ENA2,},
	[PM886_IRQ_BUCK_PGOOD] = {
		.reg_offset = 1,
		.mask = PM886_BUCK_PGOOD_INT_ENA2,
	},
	[PM886_IRQ_LDO_PGOOD] = {
		.reg_offset = 1,
		.mask = PM886_LDO_PGOOD_INT_ENA2,
	},
	/* INT2 */
	[PM886_IRQ_GPADC0] = {.reg_offset = 2, .mask = PM886_GPADC0_INT_ENA3,},
	[PM886_IRQ_GPADC1] = {.reg_offset = 2, .mask = PM886_GPADC1_INT_ENA3,},
	[PM886_IRQ_GPADC2] = {.reg_offset = 2, .mask = PM886_GPADC2_INT_ENA3,},
	[PM886_IRQ_GPADC3] = {.reg_offset = 2, .mask = PM886_GPADC3_INT_ENA3,},
	[PM886_IRQ_MIC_DET] = {.reg_offset = 2, .mask = PM886_MIC_INT_ENA3,},
	[PM886_IRQ_HS_DET] = {.reg_offset = 2, .mask = PM886_HS_INT_ENA3,},
	[PM886_IRQ_GND_DET] = {.reg_offset = 2, .mask = PM886_GND_INT_ENA3,},

	/* INT3 */
	[PM886_IRQ_CHG_FAIL] = {
		.reg_offset = 3,
		.mask = PM886_CHG_FAIL_INT_ENA4,
	},
	[PM886_IRQ_CHG_DONE] = {
		.reg_offset = 3,
		.mask = PM886_CHG_DONE_INT_ENA4,
	},
	[PM886_IRQ_OTG_FAIL] = {
		.reg_offset = 3,
		.mask = PM886_OTG_FAIL_INT_ENA4,
	},
	[PM886_IRQ_CHG_ILIM] = {
		.reg_offset = 3,
		.mask = PM886_CHG_ILIM_INT_ENA4,
	},
	[PM886_IRQ_CC] = {.reg_offset = 3, .mask = PM886_CC_INT_ENA4,},
};

struct regmap_irq_chip pm886_irq_chip = {
	.name = "88pm886",
	.irqs = pm886_irqs,
	.num_irqs = ARRAY_SIZE(pm886_irqs),

	.num_regs = 4,
	.status_base = PM886_INT_STATUS1,
	.mask_base = PM886_INT_ENA_1,
	.ack_base = PM886_INT_STATUS1,
	.mask_invert = 1,
};

int pm886_irq_init(struct pm886_chip *chip)
{
	int mask, data, ret;
	struct regmap *map;

	if (!chip || !chip->base_regmap || !chip->irq) {
		pr_err("cannot initialize interrupt!\n");
		return -EINVAL;
	}
	map = chip->base_regmap;

	/*
	 * irq_mode defines the way of clearing interrupt.
	 * it's read-clear by default.
	 */
	mask = PM886_INV_INT | PM886_INT_CLEAR | PM886_INT_MASK_MODE;
	data = (chip->irq_mode) ? PM886_INT_WC : PM886_INT_RC;
	ret = regmap_update_bits(map, PM886_MISC_CONFIG2, mask, data);
	if (ret < 0) {
		dev_err(chip->dev, "cannot set interrupt mode!\n");
		return ret;
	}

	ret = regmap_add_irq_chip(map, chip->irq, IRQF_ONESHOT, -1,
				  &pm886_irq_chip, &chip->irq_data);
	return ret;
}

int pm886_irq_exit(struct pm886_chip *chip)
{
	regmap_del_irq_chip(chip->irq, chip->irq_data);
	return 0;
}
