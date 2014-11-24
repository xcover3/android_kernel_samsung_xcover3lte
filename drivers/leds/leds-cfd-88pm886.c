/*
 * 88PM886 camera flash/torch LED driver
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/mfd/88pm886.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#define PM886_CFD_CONFIG1		(0x60)
#define PM886_CFD_CONFIG2		(0x61)
#define PM886_CFD_CONFIG3		(0x62)
#define PM886_CFD_CONFIG4		(0x63)
#define PM886_CFD_LOG1			(0x65)
#define PM886_CLS_CONFIG1		(0x71)

#define PM886_BST_CONFIG1		(0x6B)
#define PM886_BST_CONFIG2		(0x6C)
#define PM886_BST_CONFIG3		(0x6D)
#define PM886_BST_CONFIG4		(0x6E)
#define PM886_BST_CONFIG5		(0x6F)
#define PM886_BST_CONFIG6		(0x70)

/* 88PM886 bitmask definitions */
#define PM886_CFD_CLK_EN		(1 << 1)
#define PM886_FLASH_TIMER_OFFSET	(0)
#define PM886_FLASH_TIMER_MASK		(0x1F << PM886_FLASH_TIMER_OFFSET)
#define PM886_CF_CFD_PLS_ON		(1 << 0)
#define PM886_CF_BITMASK_TRCH_RESET	(1 << 1)
#define PM886_CF_BITMASK_MODE		(1 << 0)
#define PM886_SELECT_FLASH_MODE	(1 << 0)
#define PM886_SELECT_TORCH_MODE	(0 << 0)
#define PM886_CF_EN_HIGH		(1 << 3)
#define PM886_CFD_ENABLED		(1 << 2)
#define PM886_CFD_MASKED		(0 << 2)
#define PM886_BOOST_MODE		(0x1 << 1)
#define PM886_OV_SET_OFFSET	(4)
#define PM886_OV_SET_MASK		(0x3 << PM886_OV_SET_OFFSET)
#define PM886_UV_SET_OFFSET	(1)
#define PM886_UV_SET_MASK		(0x3 << PM886_UV_SET_OFFSET)

#define PM886_BST_HSOC_OFFSET		(5)
#define PM886_BST_HSOC_MASK		(0x7 << PM886_BST_HSOC_OFFSET)
#define PM886_BST_LSOC_OFFSET		(1)
#define PM886_BST_LSOC_MASK		(0x7 << PM886_BST_LSOC_OFFSET)
#define PM886_BST_HSZC_OFFSET		(5)
#define PM886_BST_HSZC_MASK		(0x3 << PM886_BST_HSZC_OFFSET)
#define PM886_BST_VSET_OFFSET		(4)
#define PM886_BST_VSET_MASK		(0x7 << PM886_BST_VSET_OFFSET)
#define PM886_BST_UVVBAT_OFFSET	(0)
#define PM886_BST_UVVBAT_MASK		(0x7 << PM886_BST_UVVBAT_OFFSET)
#define PM886_BST_UVVBAT_EN_MASK	(0x1 << 3)
#define PM886_BST_ILIM_DIS		(0x1 << 7)

#define PM886_LED_ISET(x)		(((x) - 50) / 50)
#define PM886_FLASH_ISET_OFFSET	(0)
#define PM886_FLASH_ISET_MASK		(0x1f << PM886_FLASH_ISET_OFFSET)
#define PM886_TORCH_ISET_OFFSET	(5)
#define PM886_TORCH_ISET_MASK		(0x7 << PM886_TORCH_ISET_OFFSET)

#define PM886_MIN_CURRENT		(50)		/* mA */
#define PM886_MAX_FLASH_CURRENT		(1600)		/* mA */
#define PM886_MAX_SAFE_FLASH_CURRENT	(PM886_MAX_FLASH_CURRENT - 600)		/* mA */
#define PM886_MAX_TORCH_CURRENT		(400)		/* mA */
#define PM886_MAX_SAFE_TORCH_CURRENT	(PM886_MAX_TORCH_CURRENT - 200)		/* mA */

#define PM886_CURRENT_DIV(x)		DIV_ROUND_UP(256, (x / 50))

static unsigned gpio_en;
static unsigned int dev_num;

struct pm886_led {
	struct led_classdev cdev;
	struct work_struct work;
	struct delayed_work delayed_work;
	struct pm886_chip *chip;
	struct mutex lock;
#define MFD_NAME_SIZE		(40)
	char name[MFD_NAME_SIZE];

	unsigned int brightness;
	unsigned int current_brightness;
	unsigned int max_current_div;

	int id;
	/* for external CF_EN and CF_TXMSK */
	int gpio_en;
	int cf_en;
	int cf_txmsk;
};

static void pm886_led_bright_set(struct led_classdev *cdev, enum led_brightness value);

static unsigned int clear_errors(struct pm886_led *led)
{
	struct pm886_chip *chip;
	unsigned int reg;

	/*!!! mutex must be locked upon entering this function */

	chip = led->chip;
	regmap_read(chip->battery_regmap, PM886_CFD_LOG1, &reg);

	if (reg) {
		dev_err(led->cdev.dev, "flash/torch fail. error log=0x%x\n", reg);
		/* clear all errors, write 1 to clear */
		regmap_write(chip->battery_regmap, PM886_CFD_LOG1, reg);
	}

	return reg;
}

static void pm886_led_delayed_work(struct work_struct *work)
{
	struct pm886_led *led;
	unsigned long jiffies;
	unsigned int ret;

	led = container_of(work, struct pm886_led,
			   delayed_work.work);
	mutex_lock(&led->lock);
	ret = clear_errors(led);
	/* set TRCH_TIMER_RESET to reset torch timer so
	 * led won't turn off */
	regmap_update_bits(led->chip->battery_regmap, PM886_CFD_CONFIG4,
			PM886_CF_BITMASK_TRCH_RESET,
			PM886_CF_BITMASK_TRCH_RESET);
	mutex_unlock(&led->lock);
	if (!ret) {
		/* reschedule torch timer reset */
		jiffies = msecs_to_jiffies(5000);
		schedule_delayed_work(&led->delayed_work, jiffies);
	} else
		pm886_led_bright_set(&led->cdev, 0);
}

static void torch_set_current(struct pm886_led *led)
{
	struct pm886_chip *chip;
	chip = led->chip;
	/* set torch current */
	regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG1,
			   PM886_TORCH_ISET_MASK,
			   ((PM886_LED_ISET(led->brightness)) << PM886_TORCH_ISET_OFFSET));
}


static void torch_on(struct pm886_led *led)
{
	unsigned long jiffies;
	struct pm886_chip *chip;
	int ret;

	chip = led->chip;
	mutex_lock(&led->lock);
	if (!gpio_en) {
		/* clear CFD_PLS_ON to disable */
		regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG4,
				   PM886_CF_CFD_PLS_ON, 0);
	} else {
		ret = devm_gpio_request(led->cdev.dev, led->cf_en, "cf-gpio");
		if (ret) {
			dev_err(led->cdev.dev,
				"request cf-gpio error!\n");
			return;
		}
		gpio_direction_output(led->cf_en, 0);
		devm_gpio_free(led->cdev.dev, led->cf_en);
	}
	/* set TRCH_TIMER_RESET to reset torch timer so
	 * led won't turn off */
	regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG4,
			PM886_CF_BITMASK_TRCH_RESET,
			PM886_CF_BITMASK_TRCH_RESET);
	clear_errors(led);
	/* set torch mode & boost voltage mode */
	regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG3,
			(PM886_CF_BITMASK_MODE | PM886_BOOST_MODE), (PM886_BOOST_MODE));
	/* disable booster HS current limit */
	regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG6,
			PM886_BST_ILIM_DIS, PM886_BST_ILIM_DIS);
	/* automatic booster mode */
	torch_set_current(led);
	if (!gpio_en) {
		/* set CFD_PLS_ON to enable */
		regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG4,
				   PM886_CF_CFD_PLS_ON, PM886_CF_CFD_PLS_ON);
	} else {
		ret = devm_gpio_request(led->cdev.dev, led->cf_en, "cf-gpio");
		if (ret) {
			dev_err(led->cdev.dev,
				"request cf-gpio error!\n");
			return;
		}
		gpio_direction_output(led->cf_en, 1);
		devm_gpio_free(led->cdev.dev, led->cf_en);
	}
	mutex_unlock(&led->lock);

	/* Once on, torch will only remain on for 10 seconds so we
	 * schedule work every 5 seconds (5000 msecs) to poke the
	 * chip register so it can remain on */
	jiffies = msecs_to_jiffies(5000);
	schedule_delayed_work(&led->delayed_work, jiffies);
}

static void torch_off(struct pm886_led *led)
{
	struct pm886_chip *chip;
	int ret;

	chip = led->chip;
	cancel_delayed_work_sync(&led->delayed_work);
	mutex_lock(&led->lock);
	if (!gpio_en)
		/* clear CFD_PLS_ON to disable */
		regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG4,
				   PM886_CF_CFD_PLS_ON, 0);
	else {
		ret = devm_gpio_request(led->cdev.dev, led->cf_en, "cf-gpio");
		if (ret) {
			dev_err(led->cdev.dev,
				"request cf-gpio error!\n");
			return;
		}
		gpio_direction_output(led->cf_en, 0);
		devm_gpio_free(led->cdev.dev, led->cf_en);
	}
	mutex_unlock(&led->lock);
}

static void strobe_flash(struct pm886_led *led)
{
	struct pm886_chip *chip;
	int ret;

	chip = led->chip;

	mutex_lock(&led->lock);
	if (!gpio_en)
		/* clear CFD_PLS_ON to disable */
		regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG4,
				   PM886_CF_CFD_PLS_ON, 0);
	else {
		ret = devm_gpio_request(led->cdev.dev, led->cf_en, "cf-gpio");
		if (ret) {
			dev_err(led->cdev.dev,
				"request cf-gpio error!\n");
			return;
		}
		gpio_direction_output(led->cf_en, 0);
		devm_gpio_free(led->cdev.dev, led->cf_en);
	}
	clear_errors(led);
	/*
	 * set flash mode
	 * if flash current is above 400mA, set booster mode to current regulation mode and enable
	 * booster HS current limit
	 * otherwise set booster mode to voltage regulation mode and disable booster HS current
	 * limit
	 */
	if (led->brightness > 400) {
		regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG3,
				(PM886_CF_BITMASK_MODE | PM886_BOOST_MODE), PM886_CF_BITMASK_MODE);
		regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG6,
				PM886_BST_ILIM_DIS, 0);
	} else {
		regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG3,
				(PM886_CF_BITMASK_MODE | PM886_BOOST_MODE),
				(PM886_CF_BITMASK_MODE | PM886_BOOST_MODE));
		regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG6,
				PM886_BST_ILIM_DIS, PM886_BST_ILIM_DIS);
	}
	/* automatic booster enable mode*/
	/* set flash current */
	regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG1,
			   PM886_FLASH_ISET_MASK,
			   ((PM886_LED_ISET(led->brightness)) << PM886_FLASH_ISET_OFFSET));
	/* trigger flash */
	if (!gpio_en)
		regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG4,
				   PM886_CF_CFD_PLS_ON, PM886_CF_CFD_PLS_ON);
	else {
		ret = devm_gpio_request(led->cdev.dev, led->cf_en, "cf-gpio");
		if (ret) {
			dev_err(led->cdev.dev,
				"request cf-gpio error!\n");
			return;
		}
		gpio_direction_output(led->cf_en, 1);
		devm_gpio_free(led->cdev.dev, led->cf_en);
	}
	mutex_unlock(&led->lock);
}

static void pm886_led_work(struct work_struct *work)
{
	struct pm886_led *led;

	led = container_of(work, struct pm886_led, work);

	if (led->id == PM886_FLASH_LED) {
		if (led->brightness != 0)
			strobe_flash(led);
	} else if (led->id == PM886_TORCH_LED) {
		if ((led->current_brightness == 0) && led->brightness)
			torch_on(led);
		if (led->brightness && (led->brightness != led->current_brightness))
			torch_set_current(led);
		if (led->brightness == 0)
			torch_off(led);

		led->current_brightness = led->brightness;
	}
}

static void pm886_led_bright_set(struct led_classdev *cdev,
			   enum led_brightness value)
{
	struct pm886_led *led = container_of(cdev, struct pm886_led, cdev);

	/* skip the lowest level */
	if (value == 0)
		led->brightness = 0;
	else
		led->brightness = ((value / led->max_current_div) + 1) * 50;

	dev_dbg(led->cdev.dev, "value = %d, brightness = %d\n",
		 value, led->brightness);
	schedule_work(&led->work);
}

static ssize_t gpio_ctrl_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct pm886_led *led;
	struct led_classdev *cdev;

	int s = 0;
	cdev = dev_get_drvdata(dev);
	led = container_of(cdev, struct pm886_led, cdev);

	s += sprintf(buf, "gpio control: %d\n", gpio_en);
	return s;
}

static ssize_t gpio_ctrl_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct pm886_led *led;
	struct led_classdev *cdev;
	int ret;
	unsigned int data;

	cdev = dev_get_drvdata(dev);
	led = container_of(cdev, struct pm886_led, cdev);

	ret = sscanf(buf, "%d", &gpio_en);
	if (ret < 0) {
		dev_dbg(dev, "%s: get gpio_en error, set to 0 as default\n", __func__);
		gpio_en = 0;
	}

	dev_info(dev, "%s: gpio control %s\n",
		 __func__, (gpio_en ? "enabled" : "disabled"));

	cancel_delayed_work_sync(&led->delayed_work);
	mutex_lock(&led->lock);
	led->gpio_en = gpio_en;

	/* clear CF_EN and CFD_PLS_ON before switching modes */
	ret = devm_gpio_request(led->cdev.dev, led->cf_en, "cf-gpio");
	if (ret)
		dev_err(led->cdev.dev, "request cf-gpio error!\n");
	gpio_direction_output(led->cf_en, 0);
	devm_gpio_free(led->cdev.dev, led->cf_en);
	/* clear CFD_PLS_ON to disable */
	regmap_update_bits(led->chip->battery_regmap, PM886_CFD_CONFIG4,
				PM886_CF_CFD_PLS_ON, 0);

	if (led->id == PM886_FLASH_LED)
		regmap_update_bits(led->chip->battery_regmap, PM886_CFD_CONFIG3,
				PM886_CF_BITMASK_MODE, PM886_SELECT_FLASH_MODE);
	else
		regmap_update_bits(led->chip->battery_regmap, PM886_CFD_CONFIG3,
				PM886_CF_BITMASK_MODE, PM886_SELECT_TORCH_MODE);
	if (gpio_en)
		/* high active */
		ret = regmap_update_bits(led->chip->battery_regmap, PM886_CFD_CONFIG3,
					 (PM886_CF_EN_HIGH | PM886_CFD_ENABLED),
					 (PM886_CF_EN_HIGH | PM886_CFD_ENABLED));
	else
		ret = regmap_update_bits(led->chip->battery_regmap, PM886_CFD_CONFIG3,
					(PM886_CF_EN_HIGH | PM886_CFD_ENABLED),
					PM886_CFD_MASKED);
	regmap_read(led->chip->battery_regmap, PM886_CFD_CONFIG3, &data);
	dev_dbg(dev, "%s: [0x62]= 0x%x\n", __func__, data);

	if (ret)
		dev_err(dev, "gpio configure fail: ret = %d\n", ret);

	mutex_unlock(&led->lock);

	pm886_led_bright_set(&led->cdev, 0);

	return size;
}

static DEVICE_ATTR(gpio_ctrl, S_IRUGO | S_IWUSR, gpio_ctrl_show, gpio_ctrl_store);

static int pm886_led_dt_init(struct device_node *np,
			 struct device *dev,
			 struct pm886_led_pdata *pdata)
{
	int ret;
	ret = of_property_read_u32(np,
				   "gpio-en", &pdata->gpio_en);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "flash-en-gpio", &pdata->cf_en);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "flash-txmsk-gpio", &pdata->cf_txmsk);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "flash-timer", &pdata->flash_timer);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "cls-ov-set", &pdata->cls_ov_set);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "cls-uv-set", &pdata->cls_uv_set);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "cfd-bst-vset", &pdata->cfd_bst_vset);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "bst-uvvbat-set", &pdata->bst_uvvbat_set);
	if (ret)
		return ret;
	ret = of_property_read_u32(np,
				   "max-flash-current", &pdata->max_flash_current);
	if (ret) {
		pdata->max_flash_current = PM886_MAX_SAFE_FLASH_CURRENT;
		dev_err(dev,
			"max-flash-current is not define in DTS, using default value\n");
	}

	ret = of_property_read_u32(np, "max-torch-current", &pdata->max_torch_current);
	if (ret) {
		pdata->max_torch_current = PM886_MAX_SAFE_TORCH_CURRENT;
		dev_err(dev,
			"max-torch-current is not define in DTS, using default value\n");
	}

	return 0;
}
static int pm886_setup(struct platform_device *pdev, struct pm886_led_pdata *pdata)
{
	struct pm886_led *led = platform_get_drvdata(pdev);
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	int ret;

	if (gpio_en) {
		/* high active */
		ret = regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG3,
				(PM886_CF_EN_HIGH | PM886_CFD_ENABLED),
				(PM886_CF_EN_HIGH | PM886_CFD_ENABLED));
		if (ret)
			return ret;
		ret = devm_gpio_request(&pdev->dev, led->cf_en, "cf-gpio");
		if (ret) {
			dev_err(&pdev->dev,
				"request cf-gpio error!\n");
			return ret;
		}
		gpio_direction_output(led->cf_en, 0);
		devm_gpio_free(&pdev->dev, led->cf_en);

		ret = devm_gpio_request(&pdev->dev, led->cf_txmsk,
					"cf_txmsk-gpio");
		if (ret) {
			dev_err(&pdev->dev,
				"request cf_txmsk-gpio error!\n");
			return ret;
		}
		gpio_direction_output(led->cf_txmsk, 0);
		devm_gpio_free(&pdev->dev, led->cf_txmsk);

	}

	/* set FLASH_TIMER as configured in DT */
	ret = regmap_update_bits(chip->battery_regmap, PM886_CFD_CONFIG2,
			PM886_FLASH_TIMER_MASK, (pdata->flash_timer << PM886_FLASH_TIMER_OFFSET));
	if (ret)
		return ret;
	/* set booster high-side to 3.5A and low-side to 4.5A */
	ret = regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG4,
			PM886_BST_HSOC_MASK, PM886_BST_HSOC_MASK);
	if (ret)
		return ret;
	ret = regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG4,
			PM886_BST_LSOC_MASK, PM886_BST_LSOC_MASK);
	if (ret)
		return ret;
	/* set high-side zero current limit to 130mA */
	ret = regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG5,
			PM886_BST_HSZC_MASK, 0);
	if (ret)
		return ret;
	/* set over & under voltage protection as configured in DT */
	ret = regmap_update_bits(chip->battery_regmap, PM886_CLS_CONFIG1,
			(PM886_OV_SET_MASK | PM886_UV_SET_MASK),
			((pdata->cls_ov_set << PM886_OV_SET_OFFSET) |
			(pdata->cls_uv_set << PM886_UV_SET_OFFSET)));
	if (ret)
		return ret;
	/* set booster voltage as configured in DT */
	ret = regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG1,
			PM886_BST_VSET_MASK, (pdata->cfd_bst_vset << PM886_BST_VSET_OFFSET));
	if (ret)
		return ret;
	/* set VBAT TH as configured in DT */
	ret = regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG2,
			PM886_BST_UVVBAT_MASK, (pdata->bst_uvvbat_set << PM886_BST_UVVBAT_OFFSET));
	if (ret)
		return ret;
	/* if A0, disable UVVBAT due to comparator faults in silicon */
	if (chip->chip_id == PM886_A0) {
		ret = regmap_update_bits(chip->battery_regmap, PM886_BST_CONFIG2,
				PM886_BST_UVVBAT_EN_MASK, 0);
		if (ret)
			return ret;
	}
	/* set CFD_CLK_EN to enable */
	ret = regmap_update_bits(chip->base_regmap, PM886_CLK_CTRL1,
			PM886_CFD_CLK_EN, PM886_CFD_CLK_EN);
	if (ret)
		return ret;
	else
		return 0;
}
static int pm886_led_probe(struct platform_device *pdev)
{
	struct pm886_led *led;
	struct pm886_led_pdata *pdata = pdev->dev.platform_data;
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device_node *node = pdev->dev.of_node;
	unsigned int max_current;
	int ret;

	led = devm_kzalloc(&pdev->dev, sizeof(struct pm886_led),
			    GFP_KERNEL);
	if (led == NULL)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF)) {
		if (!pdata) {
			pdata = devm_kzalloc(&pdev->dev,
					     sizeof(*pdata), GFP_KERNEL);
			if (!pdata)
				return -ENOMEM;
		}
		ret = pm886_led_dt_init(node, &pdev->dev, pdata);
		if (ret)
			return ret;
	} else if (!pdata) {
		return -EINVAL;
	}

	if (pdev->id == PM886_FLASH_LED) {
		strncpy(led->name, "flash", MFD_NAME_SIZE - 1);
		pdata->max_flash_current = (pdata->max_flash_current < PM886_MIN_CURRENT) ?
				PM886_MIN_CURRENT : pdata->max_flash_current;
		max_current = (pdata->max_flash_current > PM886_MAX_FLASH_CURRENT) ?
				PM886_MAX_FLASH_CURRENT : pdata->max_flash_current;
	} else {
		strncpy(led->name, "torch", MFD_NAME_SIZE - 1);
		pdata->max_torch_current = (pdata->max_torch_current < PM886_MIN_CURRENT) ?
				PM886_MIN_CURRENT : pdata->max_torch_current;
		max_current = (pdata->max_torch_current > PM886_MAX_TORCH_CURRENT) ?
				PM886_MAX_TORCH_CURRENT : pdata->max_torch_current;
	}

	led->chip = chip;
	led->id = pdev->id;
	led->max_current_div = PM886_CURRENT_DIV(max_current);
	led->cf_en = pdata->cf_en;
	led->cf_txmsk = pdata->cf_txmsk;
	led->gpio_en = pdata->gpio_en;
	gpio_en = pdata->gpio_en;

	led->current_brightness = 0;
	led->cdev.name = led->name;
	led->cdev.brightness_set = pm886_led_bright_set;
	/* for camera trigger */
	led->cdev.default_trigger = led->name;

	dev_set_drvdata(&pdev->dev, led);

	mutex_init(&led->lock);

	mutex_lock(&led->lock);
	if (!dev_num) {
		ret = pm886_setup(pdev, pdata);
		if (ret < 0) {
			dev_err(&pdev->dev, "device setup failed: %d\n", ret);
			return ret;
		}
		dev_num++;
	}
	mutex_unlock(&led->lock);

	INIT_WORK(&led->work, pm886_led_work);
	INIT_DELAYED_WORK(&led->delayed_work, pm886_led_delayed_work);


	ret = led_classdev_register(&pdev->dev, &led->cdev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register LED: %d\n", ret);
		return ret;
	}
	pm886_led_bright_set(&led->cdev, 0);

	ret = device_create_file(led->cdev.dev, &dev_attr_gpio_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "device attr create fail: %d\n", ret);
		return ret;
	}

	return 0;
}

static int pm886_led_remove(struct platform_device *pdev)
{
	struct pm886_led *led = platform_get_drvdata(pdev);

	mutex_lock(&led->lock);
	if (dev_num > 0)
		dev_num--;
	mutex_unlock(&led->lock);
	if (led)
		led_classdev_unregister(&led->cdev);
	return 0;
}

static const struct of_device_id pm886_led_dt_match[] = {
	{ .compatible = "marvell,88pm886-leds", },
	{ },
};
MODULE_DEVICE_TABLE(of, pm886_chg_dt_match);

static struct platform_driver pm886_led_driver = {
	.driver	= {
		.owner	= THIS_MODULE,
		.name	= "88pm886-leds",
		.of_match_table = of_match_ptr(pm886_led_dt_match),
	},
	.probe	= pm886_led_probe,
	.remove	= pm886_led_remove,
};

module_platform_driver(pm886_led_driver);

MODULE_DESCRIPTION("Camera flash/torch driver for Marvell 88PM886");
MODULE_AUTHOR("Ariel Heller <arielh@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:88PM886-leds");
