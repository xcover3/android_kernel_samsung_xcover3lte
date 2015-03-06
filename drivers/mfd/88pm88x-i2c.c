/*
 * 88pm88x-i2c.c  --  88pm886 i2c bus interface
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm886.h>
#include <linux/mfd/88pm88x.h>
#include <linux/clk/mmpfuse.h>

static int pm88x_i2c_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct pm88x_chip *chip;
	struct device_node *node = client->dev.of_node;
	int ret = 0;

	chip = pm88x_init_chip(client);
	if (IS_ERR(chip)) {
		ret = PTR_ERR(chip);
		dev_err(chip->dev, "initialize 88pm88x chip fails!\n");
		goto err;
	}

	ret = pm88x_parse_dt(node, chip);
	if (ret < 0) {
		dev_err(chip->dev, "parse dt fails!\n");
		goto err;
	}

	ret = pm88x_init_pages(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm88x pages fails!\n");
		goto err;
	}

	ret = pm88x_post_init_chip(chip);
	if (ret) {
		dev_err(chip->dev, "post initialize 88pm88x fails!\n");
		goto err;
	}

	ret = pm88x_irq_init(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm88x interrupt fails!\n");
		goto err_init_irq;
	}

	ret = pm88x_init_subdev(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm88x sub-device fails\n");
		goto err_init_subdev;
	}

	/* patch for PMIC chip itself */
	ret = pm88x_apply_patch(chip);
	if (ret) {
		dev_err(chip->dev, "apply 88pm88x register patch fails\n");
		goto err_apply_patch;
	}

	/* fixup according PMIC stepping */
	ret = pm88x_stepping_fixup(chip);
	if (ret) {
		dev_err(chip->dev, "fixup according to chip stepping\n");
		goto err_apply_patch;
	}

	/* patch for board configuration */
	ret = pm88x_apply_bd_patch(chip, node);
	if (ret) {
		dev_err(chip->dev, "apply 88pm88x register for board fails\n");
		goto err_apply_patch;
	}

	/* get private chip handler */
	pm88x_set_chip(chip);
	pm_power_off = pm88x_power_off;

	chip->reboot_notifier.notifier_call = pm88x_reboot_notifier_callback;
	register_reboot_notifier(&(chip->reboot_notifier));

	return 0;

err_apply_patch:
	mfd_remove_devices(chip->dev);
err_init_subdev:
	regmap_del_irq_chip(chip->irq, chip->irq_data);
err_init_irq:
	pm800_exit_pages(chip);
err:
	return ret;
}

static int pm88x_i2c_remove(struct i2c_client *i2c)
{
	struct pm88x_chip *chip = dev_get_drvdata(&i2c->dev);
	pm88x_dev_exit(chip);
	return 0;
}

static const struct i2c_device_id pm88x_i2c_id[] = {
	{ "88pm886", PM886 },
	{ "88pm880", PM880 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pm88x_i2c_id);

static int get_fuse_suspend_voltage(void)
{
	unsigned int volt_fuseinfo = 0;

	volt_fuseinfo = get_skusetting();

	switch (volt_fuseinfo) {
	case 0x20: /*100000*/
	case 0x3e: /*111110*/
	case 0x38: /*111000*/
		return 700000;
	case 0x30: /*110000*/
	case 0x3f: /*111111*/
	case 0x3c: /*111100*/
	case 0x0:  /*000000*/
		return 800000;
	default:
		return 700000;
	}
}

static int get_fuse_suspend_voltage_helan3(void)
{
	unsigned int volt_fuseinfo = 0;

	volt_fuseinfo = get_skusetting();

	switch (volt_fuseinfo) {
		case 0x1:
			return 700000;
		case 0x0:
		case 0x2:
			return 800000;
		default:
			return 800000;
	}
}

static int pm88x_i2c_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct regulator *buck1slp;
	static bool first_suspend;
	int fuse_slpvolt = 0;
	struct pm88x_chip *chip = i2c_get_clientdata(client);
	if (!buck1slp_is_ever_changed) {
		pr_info("%s: buck1_sleep is not being used by other modules.\n",
			__func__);
		if (buck1slp_ever_used_by_map || !first_suspend) {
			buck1slp = regulator_get(NULL, "buck1slp");
			if (IS_ERR(buck1slp)) {
				pr_info("%s: get buck1slp fails.\n", __func__);
				return 0;
			}

			pr_info("%s: buck1_sleep is configured by AP.\n", __func__);
			switch (chip->type) {
			case PM880:
				fuse_slpvolt = get_fuse_suspend_voltage_helan3();
				regulator_set_voltage(buck1slp, fuse_slpvolt, fuse_slpvolt);
				break;
			case PM886:
			default:
				/* set according to SOC fuse */
				fuse_slpvolt = get_fuse_suspend_voltage();
				regulator_set_voltage(buck1slp, fuse_slpvolt, fuse_slpvolt);
				break;
			}
			regulator_put(buck1slp);

			buck1slp_ever_used_by_map = false;
			first_suspend = true;
		}
	} else {
		pr_info("%s: buck1_sleep is being used by other modules.\n", __func__);
	}

	return 0;
}

static int pm88x_i2c_resume(struct i2c_client *client)
{
	return 0;
}

static struct i2c_driver pm88x_i2c_driver = {
	.driver = {
		.name	= "88pm88x",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(pm88x_of_match),
	},
	.probe		= pm88x_i2c_probe,
	.remove		= pm88x_i2c_remove,
	.id_table	= pm88x_i2c_id,
	.suspend	= pm88x_i2c_suspend,
	.resume		= pm88x_i2c_resume,
};

static int pm88x_i2c_init(void)
{
	int ret;

	ret = i2c_add_driver(&pm88x_i2c_driver);
	if (ret != 0) {
		pr_err("88pm88x I2C registration failed %d\n", ret);
		return ret;
	}

	return 0;
}
subsys_initcall(pm88x_i2c_init);

static void pm88x_i2c_exit(void)
{
	i2c_del_driver(&pm88x_i2c_driver);
}
module_exit(pm88x_i2c_exit);

MODULE_DESCRIPTION("88pm88x I2C bus interface");
MODULE_AUTHOR("Yi Zhang<yizhang@marvell.com>");
MODULE_LICENSE("GPL");
