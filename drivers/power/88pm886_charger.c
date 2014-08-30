/*
 * 88PM886 PMIC Charger driver
 *
 * Copyright (c) 2014 Marvell Technology Ltd.
 * Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/platform_device.h>
#include <linux/regulator/machine.h>
#include <linux/jiffies.h>
#include <linux/notifier.h>
#include <linux/err.h>
#include <linux/mfd/88pm886.h>
#include <linux/of_device.h>
#include <linux/platform_data/mv_usb.h>

#define PM886_CHG_CONFIG1		(0x28)
#define PM886_CHG_ENABLE		(1 << 0)
#define PM886_CHG_WDT_EN		(1 << 1)

#define PM886_CHG_STATUS2		(0x43)

#define PM886_CHG_LOG1			(0x45)
#define PM886_BATT_REMOVAL		(1 << 0)
#define PM886_CHG_REMOVAL		(1 << 1)
#define PM886_BATT_TEMP			(1 << 2)
#define PM886_CHG_WDT			(1 << 3)
#define PM886_BAT_VOLTAGE		(1 << 5)
#define PM886_CHG_TIMEOUT		(1 << 6)
#define PM886_INTERNAL_TEMP		(1 << 7)

struct pm886_charger_info {
	struct device *dev;
	struct power_supply ac_chg;
	struct power_supply usb_chg;
	struct notifier_block nb;
	int ac_chg_online;
	int usb_chg_online;

	struct mutex lock;

	unsigned int prechg_cur;	/* precharge current limit */
	unsigned int prechg_vol;	/* precharge voltage limit */

	unsigned int fastchg_eoc;	/* fastcharge end current */
	unsigned int fastchg_cur;	/* fastcharge current */
	unsigned int fastchg_vol;	/* fastcharge voltage */

	unsigned int recharge_vol;	/* gap between VBAT_FAST_SET and VBAT */

	unsigned int limit_cur;

	unsigned int allow_basic_charge;
	int irq_nums;
	int irq[7];

	struct pm886_chip *chip;
	int pm886_charger_status;
};

static enum power_supply_property pm886_props[] = {
	POWER_SUPPLY_PROP_STATUS, /* Charger status output */
	POWER_SUPPLY_PROP_ONLINE, /* External power source */
};

static inline int get_prechg_cur(struct pm886_charger_info *info)
{
	static int ret;
	ret = (info->prechg_cur - 300) / 150;
	ret = (ret < 0) ? 0 : ((ret > 0x7) ? 0x7 : ret);
	dev_dbg(info->dev, "%s: precharge current = 0x%x\n", __func__, ret);
	return ret;
}

static inline int get_prechg_vol(struct pm886_charger_info *info)
{
	static int ret;
	ret = (info->prechg_vol - 2300) / 100;
	ret = (ret < 0) ? 0 : ((ret > 0x7) ? 0x7 : ret);
	dev_dbg(info->dev, "%s: precharge voltage = 0x%x\n", __func__, ret);
	return ret;
}

static inline int get_fastchg_eoc(struct pm886_charger_info *info)
{
	static int ret;
	ret = info->fastchg_eoc * 100 / 1172;
	ret = (ret < 0) ? 0 : ((ret > 0x3f) ? 0x3f : ret);
	dev_dbg(info->dev, "%s: fastcharge eoc = 0x%x\n", __func__, ret);
	return (ret < 0) ? 0 : ret;
}

static inline int get_fastchg_cur(struct pm886_charger_info *info)
{
	static int ret;
	ret = (info->fastchg_cur - 300) / 150;
	ret = (ret < 0) ? 0 : ((ret > 0x16) ? 0x16 : ret);
	dev_dbg(info->dev, "%s: fastcharge current = 0x%x\n", __func__, ret);
	return ret;
}

static inline int get_fastchg_vol(struct pm886_charger_info *info)
{
	static int ret;
	ret = (info->fastchg_vol - 3600) / 125;
	ret = (ret < 0) ? 0 : ((ret > 0x48) ? 0x48 : ret);
	dev_dbg(info->dev, "%s: fastcharge voltage = 0x%x\n", __func__, ret);
	return (ret < 0) ? 0 : ret;
}

static inline int get_recharge_vol(struct pm886_charger_info *info)
{
	static int ret;
	ret = (info->recharge_vol - 50) / 50;
	ret = (ret < 0) ? 0 : ((ret > 0x3) ? 0x3 : ret);
	dev_dbg(info->dev, "%s: recharge voltage = 0x%x\n", __func__, ret);
	return (ret < 0) ? 0 : ret;
}

static char *supply_interface[] = {
	"battery",
};

static bool pm886_charger_check_allowed(struct pm886_charger_info *info)
{
	union power_supply_propval val;
	struct power_supply *psy;
	int ret;

	if (!info->allow_basic_charge)
		return false;

	psy = power_supply_get_by_name(info->usb_chg.supplied_to[0]);
	if (!psy || !psy->get_property) {
		dev_err(info->dev, "get battery property failed.\n");
		return false;
	}

	/* check if there is a battery present */
	ret = psy->get_property(psy, POWER_SUPPLY_PROP_PRESENT, &val);
	if (ret) {
		dev_err(info->dev, "get battery property failed.\n");
		return false;
	}
	if (val.intval == 0) {
		dev_dbg(info->dev, "battery not present.\n");
		return false;
	}

	/* check if battery is healthy */
	ret = psy->get_property(psy, POWER_SUPPLY_PROP_HEALTH, &val);
	if (ret) {
		dev_err(info->dev, "get battery property failed.\n");
		return false;
	}
	if (val.intval != POWER_SUPPLY_HEALTH_GOOD) {
		dev_info(info->dev, "battery health is not good.\n");
		return false;
	}

	return true;
}

static int pm886_charger_get_property(struct power_supply *psy,
			      enum power_supply_property psp,
			      union power_supply_propval *val)
{
	struct pm886_charger_info *info = dev_get_drvdata(psy->dev->parent);

	if (!info) {
		pr_err("%s: charger chip info is empty!\n", __func__);
		return -EINVAL;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (!strncmp(psy->name, "ac", 2))
			val->intval = info->ac_chg_online;
		else
			val->intval = info->usb_chg_online;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = info->pm886_charger_status;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int pm886_start_charging(struct pm886_charger_info *info)
{
	if (!info) {
		pr_err("%s: charger chip info is empty!\n", __func__);
		return 0;
	}

	/* disable watchdog */
	regmap_update_bits(info->chip->battery_regmap, PM886_CHG_CONFIG1,
			   PM886_CHG_WDT_EN, (0 << 1));

	/* do the configuration: set parmaters */

	/* enable charger */
	regmap_update_bits(info->chip->battery_regmap, PM886_CHG_CONFIG1,
			   PM886_CHG_ENABLE, PM886_CHG_ENABLE);

	/* update the charger status */
	info->pm886_charger_status = POWER_SUPPLY_STATUS_CHARGING;
	power_supply_changed(&info->ac_chg);
	power_supply_changed(&info->usb_chg);

	return 0;
}

static int pm886_charge_stopped(struct pm886_charger_info *info)
{
	if (!info) {
		pr_err("%s: charger chip info is empty!\n", __func__);
		return 0;
	}

	/* update the charger status */
	info->pm886_charger_status = POWER_SUPPLY_STATUS_DISCHARGING;
	power_supply_changed(&info->ac_chg);
	power_supply_changed(&info->usb_chg);

	return 0;
}

static void pm886_chg_ext_power_changed(struct power_supply *psy)
{
	struct pm886_charger_info *info = dev_get_drvdata(psy->dev->parent);
	bool chg_allowed;

	if (!strncmp(psy->name, "ac", 2) && (info->ac_chg_online))
		dev_dbg(info->dev, "%s: ac charger.\n", __func__);
	else if (!strncmp(psy->name, "usb", 3) && (info->usb_chg_online))
		dev_dbg(info->dev, "%s: usb charger.\n", __func__);
	else
		return;

	chg_allowed = pm886_charger_check_allowed(info);
	if (info->pm886_charger_status == POWER_SUPPLY_STATUS_CHARGING) {
		if (!chg_allowed) {
			dev_info(info->dev, "battery is not good to charge.\n");
			pm886_charge_stopped(info);
		} else {
			dev_info(info->dev, "battery is in charging.\n");
			return;
		}
	} else {
		if (!chg_allowed) {
			dev_info(info->dev, "battery is not in charging.\n");
		} else {
			pm886_start_charging(info);
			dev_info(info->dev, "now battery begins charging.\n");
			return;
		}
	}
}

static int pm886_power_supply_register(struct pm886_charger_info *info)
{
	int ret = 0;

	/* private attribute */
	info->ac_chg.supplied_to = supply_interface;
	info->ac_chg.num_supplicants = ARRAY_SIZE(supply_interface);
	info->usb_chg.supplied_to = supply_interface;
	info->usb_chg.num_supplicants = ARRAY_SIZE(supply_interface);

	/* AC supply */
	info->ac_chg.name = "ac";
	info->ac_chg.type = POWER_SUPPLY_TYPE_MAINS;
	info->ac_chg.properties = pm886_props;
	info->ac_chg.num_properties = ARRAY_SIZE(pm886_props);
	info->ac_chg.get_property = pm886_charger_get_property;
	info->ac_chg.external_power_changed = pm886_chg_ext_power_changed;

	ret = power_supply_register(info->dev, &info->ac_chg);
	if (ret < 0)
		return ret;

	/* USB supply */
	info->usb_chg.name = "usb";
	info->usb_chg.type = POWER_SUPPLY_TYPE_USB;
	info->usb_chg.properties = pm886_props;
	info->usb_chg.num_properties = ARRAY_SIZE(pm886_props);
	info->usb_chg.get_property = pm886_charger_get_property;
	info->usb_chg.external_power_changed = pm886_chg_ext_power_changed;

	ret = power_supply_register(info->dev, &info->usb_chg);
	if (ret < 0)
		goto err_usb;

	return ret;

err_usb:
	power_supply_unregister(&info->ac_chg);
	return ret;
}

static int pm886_charger_init(struct pm886_charger_info *info)
{
	info->allow_basic_charge = 1;
	info->pm886_charger_status = POWER_SUPPLY_STATUS_UNKNOWN;
	info->ac_chg_online = 0;
	info->usb_chg_online = 0;
	return 0;
}

static int pm886_charger_notifier_call(struct notifier_block *nb,
				       unsigned long type, void *chg_event)
{
	struct pm886_charger_info *info =
		container_of(nb, struct pm886_charger_info, nb);
	static unsigned long chg_type = NULL_CHARGER;

	/* no change in charger type - nothing to do */
	if (type == chg_type)
		return 0;

	chg_type = type;

	switch (type) {
	case NULL_CHARGER:
		info->ac_chg_online = 0;
		info->usb_chg_online = 0;
		break;
	case SDP_CHARGER:
	case NONE_STANDARD_CHARGER:
	case DEFAULT_CHARGER:
		info->ac_chg_online = 0;
		info->usb_chg_online = 1;
		info->fastchg_cur = 500;
		info->limit_cur = 500;
		break;
	case CDP_CHARGER:
		info->ac_chg_online = 1;
		info->usb_chg_online = 0;
		/* the max current for CDP should be 1.5A */
		info->fastchg_cur = 1500;
		info->limit_cur = 1500;
		break;
	case DCP_CHARGER:
		info->ac_chg_online = 1;
		info->usb_chg_online = 0;
		/* the max value ac_chgcording to spec */
		info->fastchg_cur = 2000;
		info->limit_cur = 2400;
		break;
	default:
		info->ac_chg_online = 0;
		info->usb_chg_online = 1;
		info->fastchg_cur = 500;
		info->limit_cur = 500;
		break;
	}

	if (info->usb_chg_online || info->ac_chg_online)
		pm886_start_charging(info);
	else
		pm886_charge_stopped(info);

	dev_dbg(info->dev, "usb_chg inserted: ac_chg = %d, usb = %d\n",
		info->ac_chg_online, info->usb_chg_online);

	return 0;
}

static irqreturn_t pm886_ilimit_handler(int irq, void *data)
{
	struct pm886_charger_info *info = data;

	if (!info) {
		pr_err("%s: charger chip info is empty!\n", __func__);
		return IRQ_NONE;
	}

	dev_info(info->dev, "input current limitation is served:\n");

	return IRQ_HANDLED;
}

static irqreturn_t pm886_charge_fail_handler(int irq, void *data)
{
	static int value;
	struct pm886_charger_info *info = data;

	if (!info) {
		pr_err("%s: charger chip info is empty!\n", __func__);
		return IRQ_NONE;
	}

	dev_dbg(info->dev, "charge fail interrupt is served:\n");
	/* charging is stopped by HW */

	regmap_read(info->chip->battery_regmap, PM886_CHG_LOG1, &value);
	if (value & PM886_BATT_TEMP)
		dev_info(info->chip->dev, "battery temperature is abnormal.\n");

	if (value & PM886_CHG_WDT)
		dev_info(info->chip->dev, "charger wdt is triggered.\n");

	if (value & PM886_BAT_VOLTAGE)
		dev_info(info->chip->dev, "battery voltage is abnormal.\n");

	if (value & PM886_CHG_TIMEOUT)
		dev_info(info->chip->dev, "charge timeout.\n");

	if (value & PM886_INTERNAL_TEMP)
		dev_info(info->chip->dev, "internal temperature abnormal.\n");

	pm886_charge_stopped(info);
	return IRQ_HANDLED;
}

static irqreturn_t pm886_charge_stop_handler(int irq, void *data)
{
	static int value;
	struct pm886_charger_info *info = data;

	if (!info) {
		pr_err("%s: charger chip info is empty!\n", __func__);
		return IRQ_NONE;
	}

	dev_dbg(info->dev, "charge stop interrupt is served:\n");
	/* charging is stopped by HW */

	regmap_read(info->chip->battery_regmap, PM886_CHG_LOG1, &value);
	if (value & PM886_BATT_REMOVAL)
		dev_info(info->chip->dev, "battery is plugged out.\n");

	if (value & PM886_CHG_REMOVAL)
		dev_info(info->chip->dev, "charger cable is plugged out.\n");

	pm886_charge_stopped(info);
	return IRQ_HANDLED;
}

static struct pm886_irq_desc {
	const char *name;
	irqreturn_t (*handler)(int irq, void *data);
} pm886_irq_descs[] = {
	{"charge fail", pm886_charge_fail_handler},
	{"charge stop", pm886_charge_stop_handler},
	{"charge input current limitation", pm886_ilimit_handler},
};

static int pm886_charger_dt_init(struct device_node *np,
			     struct pm886_charger_info *info)
{
	int ret;

	if (!info) {
		pr_err("%s: charger chip info is empty!\n", __func__);
		return -EINVAL;
	}

	ret = of_property_read_u32(np, "prechg-current", &info->prechg_cur);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "prechg-voltage", &info->prechg_vol);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "fastchg-eoc", &info->fastchg_eoc);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "fastchg-voltage", &info->fastchg_vol);
	if (ret)
		return ret;

	return 0;
}

static int pm886_charger_probe(struct platform_device *pdev)
{
	struct pm886_charger_info *info;
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device_node *node = pdev->dev.of_node;
	int ret = 0;
	int i;
	int j;

	info = devm_kzalloc(&pdev->dev, sizeof(struct pm886_charger_info),
			GFP_KERNEL);
	if (!info) {
		dev_err(&pdev->dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	info->dev = &pdev->dev;
	info->chip = chip;

	ret = pm886_charger_dt_init(node, info);
	if (ret)
		return ret;

	mutex_init(&info->lock);
	platform_set_drvdata(pdev, info);

	for (i = 0, j = 0; i < pdev->num_resources; i++) {
		info->irq[j] = platform_get_irq(pdev, i);
		if (info->irq[j] < 0)
			continue;
		j++;
	}
	info->irq_nums = j;

	ret = pm886_charger_init(info);
	if (ret < 0) {
		dev_err(info->dev, "initial charger fails.\n");
		return ret;
	}

	ret = pm886_power_supply_register(info);
	if (ret) {
		dev_err(info->dev, "register power_supply fail!\n");
		goto out;
	}

	/* register charger event notifier */
	info->nb.notifier_call = pm886_charger_notifier_call;

#ifdef CONFIG_USB_MV_UDC
	ret = mv_udc_register_client(&info->nb);
	if (ret < 0) {
		dev_err(info->dev, "failed to register client!\n");
		goto err_psy;
	}
#endif

	/* interrupt should be request in the last stage */
	for (i = 0; i < info->irq_nums; i++) {
		ret = devm_request_threaded_irq(info->dev, info->irq[i], NULL,
					   pm886_irq_descs[i].handler,
					   IRQF_ONESHOT | IRQF_NO_SUSPEND,
					   pm886_irq_descs[i].name, info);
		if (ret < 0) {
			dev_err(info->dev, "failed to request IRQ: #%d: %d\n",
				info->irq[i], ret);
			goto out_irq;
		}
	}

	dev_info(info->dev, "%s is successful!\n", __func__);

	return 0;

out_irq:
	while (--i >= 0)
		devm_free_irq(info->dev, info->irq[i], info);
#ifdef CONFIG_USB_MV_UDC
err_psy:
#endif
	power_supply_unregister(&info->ac_chg);
	power_supply_unregister(&info->usb_chg);
out:
	return ret;
}

static int pm886_charger_remove(struct platform_device *pdev)
{
	int i;
	struct pm886_charger_info *info = platform_get_drvdata(pdev);
	if (!info)
		return 0;

	for (i = 0; i < info->irq_nums; i++) {
		if (pm886_irq_descs[i].handler != NULL)
			devm_free_irq(info->dev, info->irq[i], info);
	}

	pm886_charge_stopped(info);

#ifdef CONFIG_USB_MV_UDC
	mv_udc_unregister_client(&info->nb);
#endif

	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void pm886_charger_shutdown(struct platform_device *pdev)
{
	pm886_charger_remove(pdev);
	return;
}

static const struct of_device_id pm886_chg_dt_match[] = {
	{ .compatible = "marvell,88pm886-charger", },
	{ },
};
MODULE_DEVICE_TABLE(of, pm886_chg_dt_match);

static struct platform_driver pm886_charger_driver = {
	.probe = pm886_charger_probe,
	.remove = pm886_charger_remove,
	.shutdown = pm886_charger_shutdown,
	.driver = {
		.owner = THIS_MODULE,
		.name = "88pm886-charger",
		.of_match_table = of_match_ptr(pm886_chg_dt_match),
	},
};

module_platform_driver(pm886_charger_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("88pm886 Charger Driver");
MODULE_ALIAS("platform:88pm886-charger");
MODULE_AUTHOR("Yi Zhang <yizhang@marvell.com>");
