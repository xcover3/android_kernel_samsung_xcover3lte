/*
 * Base driver for Marvell 88PM800
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Haojian Zhuang <haojian.zhuang@marvell.com>
 * Joseph(Yossi) Hanin <yhanin@marvell.com>
 * Qiao Zhou <zhouqiao@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/switch.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm80x.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/reboot.h>

/* Interrupt Registers */
#define PM800_INT_STATUS1		(0x05)
#define PM800_ONKEY_INT_STS1		(1 << 0)
#define PM800_EXTON_INT_STS1		(1 << 1)
#define PM800_CHG_INT_STS1			(1 << 2)
#define PM800_BAT_INT_STS1			(1 << 3)
#define PM800_RTC_INT_STS1			(1 << 4)
#define PM800_CLASSD_OC_INT_STS1	(1 << 5)

#define PM800_INT_STATUS2		(0x06)
#define PM800_VBAT_INT_STS2		(1 << 0)
#define PM800_VSYS_INT_STS2		(1 << 1)
#define PM800_VCHG_INT_STS2		(1 << 2)
#define PM800_TINT_INT_STS2		(1 << 3)
#define PM800_GPADC0_INT_STS2	(1 << 4)
#define PM800_TBAT_INT_STS2		(1 << 5)
#define PM800_GPADC2_INT_STS2	(1 << 6)
#define PM800_GPADC3_INT_STS2	(1 << 7)

#define PM800_INT_STATUS3		(0x07)

#define PM800_INT_STATUS4		(0x08)
#define PM800_GPIO0_INT_STS4		(1 << 0)
#define PM800_GPIO1_INT_STS4		(1 << 1)
#define PM800_GPIO2_INT_STS4		(1 << 2)
#define PM800_GPIO3_INT_STS4		(1 << 3)
#define PM800_GPIO4_INT_STS4		(1 << 4)

#define PM800_INT_ENA_1		(0x09)
#define PM800_ONKEY_INT_ENA1		(1 << 0)
#define PM800_EXTON_INT_ENA1		(1 << 1)
#define PM800_CHG_INT_ENA1			(1 << 2)
#define PM800_BAT_INT_ENA1			(1 << 3)
#define PM800_RTC_INT_ENA1			(1 << 4)
#define PM800_CLASSD_OC_INT_ENA1	(1 << 5)

#define PM800_INT_ENA_2		(0x0A)
#define PM800_VBAT_INT_ENA2		(1 << 0)
#define PM800_VSYS_INT_ENA2		(1 << 1)
#define PM800_VCHG_INT_ENA2		(1 << 2)
#define PM800_TINT_INT_ENA2		(1 << 3)
#define PM822_IRQ_LDO_PGOOD_EN		(1 << 4)
#define PM822_IRQ_BUCK_PGOOD_EN		(1 << 5)

#define PM800_INT_ENA_3		(0x0B)
#define PM800_GPADC0_INT_ENA3		(1 << 0)
#define PM800_GPADC1_INT_ENA3		(1 << 1)
#define PM800_GPADC2_INT_ENA3		(1 << 2)
#define PM800_GPADC3_INT_ENA3		(1 << 3)
#define PM800_GPADC4_INT_ENA3		(1 << 4)

#define PM800_INT_ENA_4		(0x0C)
#define PM800_GPIO0_INT_ENA4		(1 << 0)
#define PM800_GPIO1_INT_ENA4		(1 << 1)
#define PM800_GPIO2_INT_ENA4		(1 << 2)
#define PM800_GPIO3_INT_ENA4		(1 << 3)
#define PM800_GPIO4_INT_ENA4		(1 << 4)

/* number of INT_ENA & INT_STATUS regs */
#define PM800_INT_REG_NUM			(4)

/* Interrupt Number in 88PM800 */
enum {
	PM800_IRQ_ONKEY,	/*EN1b0 *//*0 */
	PM800_IRQ_EXTON,	/*EN1b1 */
	PM800_IRQ_CHG,		/*EN1b2 */
	PM800_IRQ_BAT,		/*EN1b3 */
	PM800_IRQ_RTC,		/*EN1b4 */
	PM800_IRQ_CLASSD,	/*EN1b5 *//*5 */
	PM800_IRQ_VBAT,		/*EN2b0 */
	PM800_IRQ_VSYS,		/*EN2b1 */
	PM800_IRQ_VCHG,		/*EN2b2 */
	PM800_IRQ_TINT,		/*EN2b3 */
	PM822_IRQ_LDO_PGOOD,	/*EN2b4 *//*10 */
	PM822_IRQ_BUCK_PGOOD,	/*EN2b5 */
	PM800_IRQ_GPADC0,	/*EN3b0 */
	PM800_IRQ_GPADC1,	/*EN3b1 */
	PM800_IRQ_GPADC2,	/*EN3b2 */
	PM800_IRQ_GPADC3,	/*EN3b3 *//*15 */
	PM800_IRQ_GPADC4 = 16,	/*EN3b4 */
	PM822_IRQ_MIC_DET = 16,	/*EN3b4 */
	PM822_IRQ_HS_DET = 17,	/*EN3b4 */
	PM800_IRQ_GPIO0,	/*EN4b0 */
	PM800_IRQ_GPIO1,	/*EN4b1 */
	PM800_IRQ_GPIO2,	/*EN4b2 *//*20 */
	PM800_IRQ_GPIO3,	/*EN4b3 */
	PM800_IRQ_GPIO4,	/*EN4b4 */
	PM800_MAX_IRQ,
};

/* PM800: generation identification number */
#define PM800_CHIP_GEN_ID_NUM	0x3

/* globle device pointer */
static struct pm80x_chip *chip_g;

enum pm8xx_parent {
	PM822 = 0x822,
	PM800 = 0x800,
};

static const struct i2c_device_id pm80x_id_table[] = {
	/* TODO: add meaningful data */
	{"88PM800", PM800},
	{"88pm822", PM822},
	{} /* NULL terminated */
};
MODULE_DEVICE_TABLE(i2c, pm80x_id_table);

static const struct of_device_id pm80x_dt_ids[] = {
	{ .compatible = "marvell,88pm800", },
	{},
};
MODULE_DEVICE_TABLE(of, pm80x_dt_ids);

static struct resource rtc_resources[] = {
	{
	 .name = "88pm80x-rtc",
	 .start = PM800_IRQ_RTC,
	 .end = PM800_IRQ_RTC,
	 .flags = IORESOURCE_IRQ,
	 },
};

static struct mfd_cell rtc_devs[] = {
	{
	 .name = "88pm80x-rtc",
	 .of_compatible = "marvell,88pm80x-rtc",
	 .num_resources = ARRAY_SIZE(rtc_resources),
	 .resources = &rtc_resources[0],
	 .id = -1,
	 },
};

static struct resource onkey_resources[] = {
	{
	 .name = "88pm80x-onkey",
	 .start = PM800_IRQ_ONKEY,
	 .end = PM800_IRQ_ONKEY,
	 .flags = IORESOURCE_IRQ,
	 },
};

static const struct mfd_cell onkey_devs[] = {
	{
	 .name = "88pm80x-onkey",
	 .of_compatible = "marvell,88pm80x-onkey",
	 .num_resources = 1,
	 .resources = &onkey_resources[0],
	 .id = -1,
	 },
};

static const struct mfd_cell regulator_devs[] = {
	{
	 .name = "88pm80x-regulator",
	 .of_compatible = "marvell,88pm80x-regulator",
	 .id = -1,
	},
};

static struct resource usb_resources[] = {
	{
	.name = "88pm80x-usb",
	.start = PM800_IRQ_CHG,
	.end = PM800_IRQ_CHG,
	.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell usb_devs[] = {
	{
	.name = "88pm80x-usb",
	 .of_compatible = "marvell,88pm80x-usb",
	.num_resources = 1,
	.resources = &usb_resources[0],
	.id = -1,
	},
};

static struct resource headset_resources_800[] = {
	{
		.name = "gpio-03",
		.start = PM800_IRQ_GPIO3,
		.end = PM800_IRQ_GPIO3,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "gpadc4",
		.start = PM800_IRQ_GPADC4,
		.end = PM800_IRQ_GPADC4,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell headset_devs_800[] = {
	{
	 .name = "88pm800-headset",
	 .of_compatible = "marvell,88pm80x-headset",
	 .num_resources = ARRAY_SIZE(headset_resources_800),
	 .resources = &headset_resources_800[0],
	 .id = -1,
	 },
};

static const struct regmap_irq pm800_irqs[] = {
	/* INT0 */
	[PM800_IRQ_ONKEY] = {
		.mask = PM800_ONKEY_INT_ENA1,
	},
	[PM800_IRQ_EXTON] = {
		.mask = PM800_EXTON_INT_ENA1,
	},
	[PM800_IRQ_CHG] = {
		.mask = PM800_CHG_INT_ENA1,
	},
	[PM800_IRQ_BAT] = {
		.mask = PM800_BAT_INT_ENA1,
	},
	[PM800_IRQ_RTC] = {
		.mask = PM800_RTC_INT_ENA1,
	},
	[PM800_IRQ_CLASSD] = {
		.mask = PM800_CLASSD_OC_INT_ENA1,
	},
	/* INT1 */
	[PM800_IRQ_VBAT] = {
		.reg_offset = 1,
		.mask = PM800_VBAT_INT_ENA2,
	},
	[PM800_IRQ_VSYS] = {
		.reg_offset = 1,
		.mask = PM800_VSYS_INT_ENA2,
	},
	[PM800_IRQ_VCHG] = {
		.reg_offset = 1,
		.mask = PM800_VCHG_INT_ENA2,
	},
	[PM800_IRQ_TINT] = {
		.reg_offset = 1,
		.mask = PM800_TINT_INT_ENA2,
	},
	[PM822_IRQ_LDO_PGOOD] = {
		.reg_offset = 1,
		.mask = PM822_IRQ_LDO_PGOOD_EN,
	},
	[PM822_IRQ_BUCK_PGOOD] = {
		.reg_offset = 1,
		.mask = PM822_IRQ_BUCK_PGOOD_EN,
	},
	/* INT2 */
	[PM800_IRQ_GPADC0] = {
		.reg_offset = 2,
		.mask = PM800_GPADC0_INT_ENA3,
	},
	[PM800_IRQ_GPADC1] = {
		.reg_offset = 2,
		.mask = PM800_GPADC1_INT_ENA3,
	},
	[PM800_IRQ_GPADC2] = {
		.reg_offset = 2,
		.mask = PM800_GPADC2_INT_ENA3,
	},
	[PM800_IRQ_GPADC3] = {
		.reg_offset = 2,
		.mask = PM800_GPADC3_INT_ENA3,
	},
	[PM800_IRQ_GPADC4] = {
		.reg_offset = 2,
		.mask = PM800_GPADC4_INT_ENA3,
	},
	/* INT3 */
	[PM800_IRQ_GPIO0] = {
		.reg_offset = 3,
		.mask = PM800_GPIO0_INT_ENA4,
	},
	[PM800_IRQ_GPIO1] = {
		.reg_offset = 3,
		.mask = PM800_GPIO1_INT_ENA4,
	},
	[PM800_IRQ_GPIO2] = {
		.reg_offset = 3,
		.mask = PM800_GPIO2_INT_ENA4,
	},
	[PM800_IRQ_GPIO3] = {
		.reg_offset = 3,
		.mask = PM800_GPIO3_INT_ENA4,
	},
	[PM800_IRQ_GPIO4] = {
		.reg_offset = 3,
		.mask = PM800_GPIO4_INT_ENA4,
	},
};

static int device_gpadc_init(struct pm80x_chip *chip,
				       struct pm80x_platform_data *pdata)
{
	struct pm80x_subchip *subchip = chip->subchip;
	struct regmap *map = subchip->regmap_gpadc;
	int data = 0, mask = 0, ret = 0;

	if (!map) {
		dev_warn(chip->dev,
			 "Warning: gpadc regmap is not available!\n");
		return -EINVAL;
	}
	/*
	 * initialize GPADC without activating it turn on GPADC
	 * measurments
	 */
	ret = regmap_update_bits(map,
				 PM800_GPADC_MISC_CONFIG2,
				 PM800_GPADC_MISC_GPFSM_EN,
				 PM800_GPADC_MISC_GPFSM_EN);
	if (ret < 0)
		goto out;
	/*
	 * This function configures the ADC as requires for
	 * CP implementation.CP does not "own" the ADC configuration
	 * registers and relies on AP.
	 * Reason: enable automatic ADC measurements needed
	 * for CP to get VBAT and RF temperature readings.
	 */
	ret = regmap_update_bits(map, PM800_GPADC_MEAS_EN1,
				 PM800_MEAS_EN1_VBAT, PM800_MEAS_EN1_VBAT);
	if (ret < 0)
		goto out;
	ret = regmap_update_bits(map, PM800_GPADC_MEAS_EN2,
				 (PM800_MEAS_EN2_RFTMP | PM800_MEAS_GP0_EN),
				 (PM800_MEAS_EN2_RFTMP | PM800_MEAS_GP0_EN));
	if (ret < 0)
		goto out;

	/*
	 * the defult of PM800 is GPADC operates at 100Ks/s rate
	 * and Number of GPADC slots with active current bias prior
	 * to GPADC sampling = 1 slot for all GPADCs set for
	 * Temprature mesurmants
	 */
	mask = (PM800_GPADC_GP_BIAS_EN0 | PM800_GPADC_GP_BIAS_EN1 |
		PM800_GPADC_GP_BIAS_EN2 | PM800_GPADC_GP_BIAS_EN3);

	if (pdata && (pdata->batt_det == 0))
		data = (PM800_GPADC_GP_BIAS_EN0 | PM800_GPADC_GP_BIAS_EN1 |
			PM800_GPADC_GP_BIAS_EN2 | PM800_GPADC_GP_BIAS_EN3);
	else
		data = (PM800_GPADC_GP_BIAS_EN0 | PM800_GPADC_GP_BIAS_EN2 |
			PM800_GPADC_GP_BIAS_EN3);

	ret = regmap_update_bits(map, PM800_GP_BIAS_ENA1, mask, data);
	if (ret < 0)
		goto out;

	dev_info(chip->dev, "pm800 device_gpadc_init: Done\n");
	return 0;

out:
	dev_info(chip->dev, "pm800 device_gpadc_init: Failed!\n");
	return ret;
}

static int device_onkey_init(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata)
{
	int ret;

	ret = mfd_add_devices(chip->dev, 0, &onkey_devs[0],
			      ARRAY_SIZE(onkey_devs), &onkey_resources[0], 0,
			      NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add onkey subdev\n");
		return ret;
	}

	return 0;
}

static int device_rtc_init(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata)
{
	int ret;

	if (pdata) {
		rtc_devs[0].platform_data = pdata->rtc;
		rtc_devs[0].pdata_size =
				pdata->rtc ? sizeof(struct pm80x_rtc_pdata) : 0;
	}
	ret = mfd_add_devices(chip->dev, 0, &rtc_devs[0],
			      ARRAY_SIZE(rtc_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add rtc subdev\n");
		return ret;
	}

	return 0;
}

static int device_regulator_init(struct pm80x_chip *chip,
					   struct pm80x_platform_data *pdata)
{
	int ret;

	ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
			      ARRAY_SIZE(regulator_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add regulator subdev\n");
		return ret;
	}

	return 0;
}

static int device_vbus_init(struct pm80x_chip *chip,
			                   struct pm80x_platform_data *pdata)
{
	int ret;

	usb_devs[0].platform_data = pdata->usb;
	usb_devs[0].pdata_size = sizeof(struct pm80x_vbus_pdata);
	ret = mfd_add_devices(chip->dev, 0, &usb_devs[0],
			      ARRAY_SIZE(usb_devs), NULL, 0, NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add usb subdev\n");
		return ret;
	}

	return 0;
}

static int device_headset_init(struct pm80x_chip *chip,
					   struct pm80x_platform_data *pdata)
{
	int ret;

	headset_devs_800[0].platform_data = pdata->headset;
	ret = mfd_add_devices(chip->dev, 0, &headset_devs_800[0],
			      ARRAY_SIZE(headset_devs_800), NULL, 0, NULL);

	if (ret < 0) {
		dev_err(chip->dev, "Failed to add headset subdev\n");
		return ret;
	}

	return 0;
}

static int device_irq_init_800(struct pm80x_chip *chip)
{
	struct regmap *map = chip->regmap;
	unsigned long flags = IRQF_ONESHOT;
	int data, mask, ret = -EINVAL;

	if (!map || !chip->irq) {
		dev_err(chip->dev, "incorrect parameters\n");
		return -EINVAL;
	}

	/*
	 * irq_mode defines the way of clearing interrupt. it's read-clear by
	 * default.
	 */
	mask =
	    PM800_WAKEUP2_INV_INT | PM800_WAKEUP2_INT_CLEAR |
	    PM800_WAKEUP2_INT_MASK;

	data = (chip->irq_mode) ? PM800_WAKEUP2_INT_WC : PM800_WAKEUP2_INT_RC;
	ret = regmap_update_bits(map, PM800_WAKEUP2, mask, data);

	if (ret < 0)
		goto out;

	ret =
	    regmap_add_irq_chip(chip->regmap, chip->irq, flags, -1,
				chip->regmap_irq_chip, &chip->irq_data);

out:
	return ret;
}

static void device_irq_exit_800(struct pm80x_chip *chip)
{
	regmap_del_irq_chip(chip->irq, chip->irq_data);
}

static struct regmap_irq_chip pm800_irq_chip = {
	.name = "88pm800",
	.irqs = pm800_irqs,
	.num_irqs = ARRAY_SIZE(pm800_irqs),

	.num_regs = 4,
	.status_base = PM800_INT_STATUS1,
	.mask_base = PM800_INT_ENA_1,
	.ack_base = PM800_INT_STATUS1,
	.mask_invert = 1,
};

static int pm800_pages_init(struct pm80x_chip *chip)
{
	struct pm80x_subchip *subchip;
	struct i2c_client *client = chip->client;

	int ret = 0;

	subchip = chip->subchip;
	if (!subchip || !subchip->power_page_addr || !subchip->gpadc_page_addr)
		return -ENODEV;

	/* PM800 block power page */
	subchip->power_page = i2c_new_dummy(client->adapter,
					    subchip->power_page_addr);
	if (subchip->power_page == NULL) {
		ret = -ENODEV;
		goto out;
	}

	subchip->regmap_power = devm_regmap_init_i2c(subchip->power_page,
						     &pm80x_regmap_config);
	if (IS_ERR(subchip->regmap_power)) {
		ret = PTR_ERR(subchip->regmap_power);
		dev_err(chip->dev,
			"Failed to allocate regmap_power: %d\n", ret);
		goto out;
	}

	i2c_set_clientdata(subchip->power_page, chip);

	/* PM800 block GPADC */
	subchip->gpadc_page = i2c_new_dummy(client->adapter,
					    subchip->gpadc_page_addr);
	if (subchip->gpadc_page == NULL) {
		ret = -ENODEV;
		goto out;
	}

	subchip->regmap_gpadc = devm_regmap_init_i2c(subchip->gpadc_page,
						     &pm80x_regmap_config);
	if (IS_ERR(subchip->regmap_gpadc)) {
		ret = PTR_ERR(subchip->regmap_gpadc);
		dev_err(chip->dev,
			"Failed to allocate regmap_gpadc: %d\n", ret);
		goto out;
	}
	i2c_set_clientdata(subchip->gpadc_page, chip);

out:
	return ret;
}

static void pm800_pages_exit(struct pm80x_chip *chip)
{
	struct pm80x_subchip *subchip;

	subchip = chip->subchip;

	if (subchip && subchip->power_page)
		i2c_unregister_device(subchip->power_page);

	if (subchip && subchip->gpadc_page)
		i2c_unregister_device(subchip->gpadc_page);
}

static int device_800_init(struct pm80x_chip *chip,
				     struct pm80x_platform_data *pdata)
{
	int ret;
	unsigned int val;

	/*
	 * alarm wake up bit will be clear in device_irq_init(),
	 * read before that
	 */
	ret = regmap_read(chip->regmap, PM800_RTC_CONTROL, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read RTC register: %d\n", ret);
		goto out;
	}
	if (val & PM800_ALARM_WAKEUP) {
		if (pdata && pdata->rtc)
			pdata->rtc->rtc_wakeup = 1;
	}

	ret = device_gpadc_init(chip, pdata);
	if (ret < 0) {
		dev_err(chip->dev, "[%s]Failed to init gpadc\n", __func__);
		goto out;
	}

	chip->regmap_irq_chip = &pm800_irq_chip;
	if (pdata)
		chip->irq_mode = pdata->irq_mode;

	ret = device_irq_init_800(chip);
	if (ret < 0) {
		dev_err(chip->dev, "[%s]Failed to init pm800 irq\n", __func__);
		goto out;
	}

	ret = device_onkey_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add onkey subdev\n");
		goto out_dev;
	}

	ret = device_rtc_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add rtc subdev\n");
		goto out;
	}

	ret = device_regulator_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add regulators subdev\n");
		goto out;
	}

	ret = device_vbus_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add vbus detection subdev\n");
		goto out;
	}

	ret = device_headset_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add headset subdev\n");
		goto out;
	}

	return 0;
out_dev:
	mfd_remove_devices(chip->dev);
	device_irq_exit_800(chip);
out:
	return ret;
}

static int pm800_dt_init(struct device_node *np,
			 struct device *dev,
			 struct pm80x_platform_data *pdata)
{
	/* parent platform data only */
	pdata->irq_mode =
		!of_property_read_bool(np, "marvell,88pm800-irq-write-clear");

	return 0;
}
#define PM800_INVALID_PAGE 0x0
#define PM800_BASE_PAGE  0x1
#define PM800_POWER_PAGE 0x2
#define PM800_GPADC_PAGE 0x3
/*
 * board specfic configurations also parameters from customers,
 * this parameters are passed form board dts file
 */
static int pmic_board_config(struct pm80x_chip *chip, struct device_node *np)
{
	unsigned int page, reg, mask, data;
	const __be32 *values;
	int size, rows, index;

	values = of_get_property(np, "marvell,pmic-board-cfg", &size);
	if (!values) {
		dev_warn(chip->dev, "no valid property for %s\n", np->name);
		return 0;
	}
	size /= sizeof(*values);	/* Number of elements in array */
	rows = size / 4;
	dev_info(chip->dev, "Proceed PMIC board specific configuration (%d items)\n", rows);
	index = 0;

	while (rows--) {
		page = be32_to_cpup(values + index++);
		reg = be32_to_cpup(values + index++);
		mask = be32_to_cpup(values + index++);
		data = be32_to_cpup(values + index++);
		switch (page) {
		case PM800_BASE_PAGE:
			dev_info(chip->dev, "Base page 0x%02x update 0x%02x mask with value 0x%02x\n", reg, mask, data);
			regmap_update_bits(chip->regmap, reg, mask, data);
			break;
		case PM800_POWER_PAGE:
			dev_info(chip->dev, "Power page 0x%02x update 0x%02x mask with value 0x%02x\n", reg, mask, data);
			regmap_update_bits(chip->subchip->regmap_power, reg, mask, data);
			break;
		case PM800_GPADC_PAGE:
			dev_info(chip->dev, "GPADC page 0x%02x update 0x%02x mask with value 0x%02x\n", reg, mask, data);
			regmap_update_bits(chip->subchip->regmap_gpadc, reg, mask, data);
			break;
		default:
			dev_warn(chip->dev, "Unsupported page for %d\n", page);
			break;
		}
	}

	return 0;
}

void parse_powerup_down_log(struct pm80x_chip *chip)
{
	int powerup_l, powerD_l1, powerD_l2, bit;
	char *powerup_name[7] = {
		"ONKEY_WAKEUP    ",
		"CHG_WAKEUP      ",
		"EXTON_WAKEUP    ",
		"RSVD            ",
		"RTC_ALARM_WAKEUP",
		"FAULT_WAKEUP    ",
		"BAT_WAKEUP      "
	};
	char *powerD1_name[8] = {
		"OVER_TEMP ",
		"UV_VSYS1  ",
		"SW_PDOWN  ",
		"FL_ALARM  ",
		"WD        ",
		"LONG_ONKEY",
		"OV_VSYS   ",
		"RTC_RESET "
	};
	char *powerD2_name[5] = {
		"HYB_DONE   ",
		"UV_VSYS2   ",
		"HW_RESET   ",
		"PGOOD_PDOWN",
		"LONKEY_RTC "
	};
	/*power up log*/
	regmap_read(chip->regmap, PM800_POWER_UP_LOG, &powerup_l);
	dev_info(chip->dev, "Powerup log 0x%x: 0x%x\n",
		PM800_POWER_UP_LOG, powerup_l);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|     name(power up) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 7; bit++)
		dev_info(chip->dev, "|  %s  |    %x     |\n",
			powerup_name[bit], (powerup_l >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");
	/*power down log1*/
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG1, &powerD_l1);
	dev_info(chip->dev, "PowerDW Log1 0x%x: 0x%x\n",
		PM800_POWER_DOWN_LOG1, powerD_l1);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "| name(power down1)  |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 8; bit++)
		dev_info(chip->dev, "|    %s      |    %x     |\n",
			powerD1_name[bit], (powerD_l1 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");
	/*power down log2*/
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG2, &powerD_l2);
	dev_info(chip->dev, "PowerDW Log2 0x%x: 0x%x\n",
		PM800_POWER_DOWN_LOG2, powerD_l2);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|  name(power down2) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 5; bit++)
		dev_info(chip->dev, "|    %s     |    %x     |\n",
			powerD2_name[bit], (powerD_l2 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");

}

static int pm800_init_config(struct pm80x_chip *chip, struct device_node *np)
{
	int data;
	if (!chip || !chip->regmap || !chip->subchip
	    || !chip->subchip->regmap_power) {
		pr_err("%s:chip is not availiable!\n", __func__);
		return -EINVAL;
	}

	if (np) /*have board specific parameters need to config*/
		pmic_board_config(chip, np);

	/*base page:reg 0xd0.7 = 1 32kHZ generated from XO */
	regmap_read(chip->regmap, PM800_RTC_CONTROL, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_RTC_CONTROL, data);

	/* Set internal digital sleep voltage as 1.2V */
	regmap_read(chip->regmap, PM800_LOW_POWER1, &data);
	data &= ~(0xf << 4);
	regmap_write(chip->regmap, PM800_LOW_POWER1, data);
	/* Enable 32Khz-out-3 low jitter XO_LJ = 1 in pm800
	 * Enable 32Khz-out-2 low jitter XO_LJ = 1 in pm822
	 * they are the same bit
	 */
	regmap_read(chip->regmap, PM800_LOW_POWER2, &data);
	data |= (1 << 5);
	regmap_write(chip->regmap, PM800_LOW_POWER2, data);

	switch (chip->type) {
	case CHIP_PM800:
		/* Enable 32Khz-out-from XO 1, 2, 3 all enabled */
		regmap_write(chip->regmap, PM800_RTC_MISC2, 0x2a);
		break;

	case CHIP_PM822:
		/* select 22pF internal capacitance on XTAL1 and XTAL2*/
		regmap_read(chip->regmap, PM800_RTC_MISC6, &data);
		data |= (0x7 << 4);
		regmap_write(chip->regmap, PM800_RTC_MISC6, data);

		/* Enable 32Khz-out-from XO 1, 2 all enabled */
		regmap_write(chip->regmap, PM800_RTC_MISC2, 0xa);

		/* gps use the LDO13 set the current 170mA  */
		regmap_read(chip->subchip->regmap_power,
					PM822_LDO13_CTRL, &data);
		data &= ~(0x3);
		data |= (0x2);
		regmap_write(chip->subchip->regmap_power,
					PM822_LDO13_CTRL, data);
		/* low power config
		 * 1. base_page 0x21, BK_CKSLP_DIS is gated 1ms after sleep mode entry.
		 * 2. base_page 0x23, REF_SLP_EN reference group enter low power mode.
		 *    REF_UVL_SEL set to be 5.6V
		 * 3. base_page 0x50, 0x55 OSC_CKLCK buck FLL is locked
		 * 4. gpadc_page 0x06, GP_SLEEP_MODE MEANS_OFF scale set to be 8
		 *    MEANS_EN_SLP set to 1, GPADC operates in sleep duty cycle mode.
		 */
		regmap_read(chip->regmap, PM800_LOW_POWER2, &data);
		data |= (1 << 4);
		regmap_write(chip->regmap, PM800_LOW_POWER2, data);

		regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG4, &data);
		data |= (1 << 4);
		data &= ~(0x3 < 2);
		regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG4, data);

		regmap_read(chip->regmap, PM800_OSC_CNTRL1, &data);
		data |= (1 << 0);
		regmap_write(chip->regmap, PM800_OSC_CNTRL1, data);

		regmap_read(chip->regmap, PM800_OSC_CNTRL6, &data);
		data &= ~(1 << 0);
		regmap_write(chip->regmap, PM800_OSC_CNTRL6, data);

		regmap_read(chip->subchip->regmap_gpadc, PM800_GPADC_MISC_CONFIG2, &data);
		data |= (0x7 << 4);
		regmap_write(chip->subchip->regmap_gpadc, PM800_GPADC_MISC_CONFIG2, data);

		/*
		 * Enable LDO sleep mode
		 * TODO: GPS and RF module need to test after enable
		 * ldo3 sleep mode may make emmc not work when resume, disable it
		 */
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP1, 0xba);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP2, 0xaa);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP3, 0xaa);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP4, 0x0a);

		break;

	case CHIP_PM86X:
		/* enable buck1 dual phase mode*/
		regmap_read(chip->subchip->regmap_power, PM860_BUCK1_MISC,
				&data);
		data |= BUCK1_DUAL_PHASE_SEL;
		regmap_write(chip->subchip->regmap_power, PM860_BUCK1_MISC,
				data);

		/*xo_cap sel bit(4~6)= 100 12pf register:0xe8*/
		regmap_read(chip->regmap, PM860_MISC_RTC3, &data);
		data |= (0x4 << 4);
		regmap_write(chip->regmap, PM860_MISC_RTC3, data);

		/*set gpio3 and gpio4 to be DVC mode*/
		regmap_read(chip->regmap, PM860_GPIO_2_3_CNTRL, &data);
		data |= PM860_GPIO3_GPIO_MODE(7);
		regmap_write(chip->regmap, PM860_GPIO_2_3_CNTRL, data);

		regmap_read(chip->regmap, PM860_GPIO_4_5_CNTRL, &data);
		data |= PM860_GPIO4_GPIO_MODE(7);
		regmap_write(chip->regmap, PM860_GPIO_4_5_CNTRL, data);

		break;

	default:
		dev_err(chip->dev, "Unknown device type: %d\n", chip->type);
	}

	/*
	 * Block wakeup attempts when VSYS rises above
	 * VSYS_UNDER_RISE_TH1, or power off may fail.
	 * it is set to prevent contimuous attempt to power up
	 * incase the VSYS is above the VSYS_LOW_TH threshold.
	 */
	regmap_read(chip->regmap, PM800_RTC_MISC5, &data);
	data |= 0x1;
	regmap_write(chip->regmap, PM800_RTC_MISC5, data);

	/* Enabele LDO and BUCK clock gating in lpm */
	regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG3, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG3, data);
	/*
	 * Disable reference group sleep mode
	 * - to reduce power fluctuation in suspend
	 */
	regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG4, &data);
	data &= ~(1 << 7);
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG4, data);

	/* Enable voltage change in pmic, POWER_HOLD = 1 */
	regmap_read(chip->regmap, PM800_WAKEUP1, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_WAKEUP1, data);

	/* Enable buck sleep mode */
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP1, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP2, 0x2);

	/*set buck2 and buck4 driver selection to be full.
	* this bit is now reserved and default value is 0, if want full
	* drive possibility it should be set to 1.
	* In A1 version it will be set to 1 by default.
	*/
	regmap_read(chip->subchip->regmap_power, 0x7c, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x7c, data);

	regmap_read(chip->subchip->regmap_power, 0x82, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x82, data);

	/* dump power up/down log */
	parse_powerup_down_log(chip);
	return 0;
}

#define PM800_SW_PDOWN			(1 << 5)
void sw_poweroff(void)
{
	int ret;
	u8 reg = PM800_WAKEUP1, data, buf[2];
	struct i2c_client *client = chip_g->client;
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

	pr_info("turning off power....\n");
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

	/* read original data from PM800_WAKEUP1 */
	buf[0] = reg;
	ret = __i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0) {
		pr_err("%s send reg fails...\n", __func__);
		BUG();
	}

	/* write data */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf[0] = reg;
	msgs[0].buf[1] = data | PM800_SW_PDOWN;
	ret = __i2c_transfer(client->adapter, msgs, 1);
	if (ret < 0) {
		pr_err("%s write data fails...\n", __func__);
		BUG();
	}
	/* we will not see this log if power off is sucessful */
	pr_err("power down failes!\n");
}

static int reboot_notifier_func(struct notifier_block *this,
		unsigned long code, void *cmd)
{
	int data;
	struct pm80x_chip *chip;

	pr_info("reboot notifier...\n");

	chip = container_of(this, struct pm80x_chip, reboot_notifier);
	if (cmd && (0 == strcmp(cmd, "recovery"))) {
		pr_info("Enter recovery mode\n");
		regmap_read(chip->regmap, PM800_USER_DATA6, &data);
		regmap_write(chip->regmap, PM800_USER_DATA6, data | 0x1);

	} else {
		regmap_read(chip->regmap, PM800_USER_DATA6, &data);
		regmap_write(chip->regmap, PM800_USER_DATA6, data & 0xfe);
	}

	if (code != SYS_POWER_OFF) {
		regmap_read(chip->regmap, PM800_USER_DATA6, &data);
		/* this bit is for charger server */
		regmap_write(chip->regmap, PM800_USER_DATA6, data | 0x2);
	}

	return 0;
}

static int pm800_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int ret = 0;
	struct pm80x_chip *chip;
	struct pm80x_platform_data *pdata = dev_get_platdata(&client->dev);
	struct device_node *node = client->dev.of_node;
	struct pm80x_subchip *subchip;

	if (IS_ENABLED(CONFIG_OF)) {
		if (!pdata) {
			pdata = devm_kzalloc(&client->dev,
					     sizeof(*pdata), GFP_KERNEL);
			if (!pdata)
				return -ENOMEM;
		}
		ret = pm800_dt_init(node, &client->dev, pdata);
		if (ret)
			return ret;
	} else if (!pdata) {
		return -EINVAL;
	}

	/*
	 * RTC in pmic is alive even the core is powered off, expired-alarm is
	 * a power-up event to the system; every time the system boots up,
	 * whether it's powered up by PMIC-rtc needs to be recorded and pass
	 * this information to RTC driver
	 */
	if (!pdata->rtc) {
		pdata->rtc = devm_kzalloc(&client->dev,
					  sizeof(*(pdata->rtc)), GFP_KERNEL);
		if (!pdata->rtc)
			return -ENOMEM;
	}

	ret = pm80x_init(client);
	if (ret) {
		dev_err(&client->dev, "pm800_init fail\n");
		goto out_init;
	}

	chip = i2c_get_clientdata(client);

	/* init subchip for PM800 */
	subchip =
	    devm_kzalloc(&client->dev, sizeof(struct pm80x_subchip),
			 GFP_KERNEL);
	if (!subchip) {
		ret = -ENOMEM;
		goto err_subchip_alloc;
	}

	/* pm800 has 2 addtional pages to support power and gpadc. */
	subchip->power_page_addr = client->addr + 1;
	subchip->gpadc_page_addr = client->addr + 2;
	chip->subchip = subchip;

	ret = pm800_pages_init(chip);
	if (ret) {
		dev_err(&client->dev, "pm800_pages_init failed!\n");
		goto err_page_init;
	}

	ret = device_800_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to initialize 88pm800 devices\n");
		goto err_device_init;
	}

	/*
	 * config registers for pmic.
	 * common registers is configed in pm800_init_config directly,
	 * board specfic registers and configuration is passed
	 * from board specific dts file.
	 */
	if (IS_ENABLED(CONFIG_OF))
		pm800_init_config(chip, node);
	else
		pm800_init_config(chip, NULL);

	if (pdata && pdata->plat_config)
		pdata->plat_config(chip, pdata);

	chip->reboot_notifier.notifier_call = reboot_notifier_func;

	chip_g = chip;
	pm_power_off = sw_poweroff;
	register_reboot_notifier(&(chip->reboot_notifier));

	return 0;

err_device_init:
	pm800_pages_exit(chip);
err_page_init:
err_subchip_alloc:
	pm80x_deinit();
out_init:
	return ret;
}

static int pm800_remove(struct i2c_client *client)
{
	struct pm80x_chip *chip = i2c_get_clientdata(client);

	mfd_remove_devices(chip->dev);
	device_irq_exit_800(chip);

	pm800_pages_exit(chip);
	pm80x_deinit();

	return 0;
}

static struct i2c_driver pm800_driver = {
	.driver = {
		.name = "88PM800",
		.owner = THIS_MODULE,
		.pm = &pm80x_pm_ops,
		.of_match_table	= of_match_ptr(pm80x_dt_ids),
		},
	.probe = pm800_probe,
	.remove = pm800_remove,
	.id_table = pm80x_id_table,
};

static int __init pm800_i2c_init(void)
{
	return i2c_add_driver(&pm800_driver);
}
subsys_initcall(pm800_i2c_init);

static void __exit pm800_i2c_exit(void)
{
	i2c_del_driver(&pm800_driver);
}
module_exit(pm800_i2c_exit);

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM800");
MODULE_AUTHOR("Qiao Zhou <zhouqiao@marvell.com>");
MODULE_LICENSE("GPL");
