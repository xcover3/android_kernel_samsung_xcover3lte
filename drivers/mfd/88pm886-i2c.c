/*
 * 88pm886-i2c.c  --  88pm886 i2c bus interface
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

static int pm886_i2c_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct pm886_chip *chip;
	struct device_node *node = client->dev.of_node;
	int ret = 0;

	chip = pm886_init_chip(client);
	if (IS_ERR(chip)) {
		ret = PTR_ERR(chip);
		dev_err(chip->dev, "initialize 88pm886 chip fails!\n");
		goto err;
	}

	ret = pm886_parse_dt(node, chip);
	if (ret < 0) {
		dev_err(chip->dev, "parse dt fails!\n");
		goto err;
	}

	ret = pm886_init_pages(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm886 pages fails!\n");
		goto err;
	}

	ret = pm886_post_init_chip(chip);
	if (ret) {
		dev_err(chip->dev, "post initialize 88pm886 fails!\n");
		goto err;
	}

	ret = pm886_irq_init(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm886 interrupt fails!\n");
		goto err_init_irq;
	}

	ret = pm886_init_subdev(chip);
	if (ret) {
		dev_err(chip->dev, "initialize 88pm886 sub-device fails\n");
		goto err_init_subdev;
	}

	/* patch for PMIC chip itself */
	ret = pm886_apply_patch(chip);
	if (ret) {
		dev_err(chip->dev, "apply 88pm886 register patch fails\n");
		goto err_apply_patch;
	}

	/* patch for board configuration */
	ret = pm886_apply_bd_patch(chip, node);
	if (ret) {
		dev_err(chip->dev, "apply 88pm886 register for board fails\n");
		goto err_apply_patch;
	}

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

static int pm886_i2c_remove(struct i2c_client *i2c)
{
	struct pm886_chip *chip = dev_get_drvdata(&i2c->dev);
	pm886_dev_exit(chip);
	return 0;
}

static const struct i2c_device_id pm886_i2c_id[] = {
	{ "88pm886", PM886 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pm886_i2c_id);

static struct i2c_driver pm886_i2c_driver = {
	.driver = {
		.name	= "88pm886",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(pm886_of_match),
	},
	.probe		= pm886_i2c_probe,
	.remove		= pm886_i2c_remove,
	.id_table	= pm886_i2c_id,
};

module_i2c_driver(pm886_i2c_driver);

MODULE_DESCRIPTION("88pm886 I2C bus interface");
MODULE_AUTHOR("Yi Zhang<yizhang@marvell.com>");
MODULE_LICENSE("GPL");
