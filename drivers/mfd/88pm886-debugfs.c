/*
 * Copyright (C) Marvell 2014
 *
 * Author: Yi Zhang <yizhang@marvell.com>
 * License Terms: GNU General Public License v2
 */
/*
 * 88pm886 register access
 *
 * read:
 * # echo -0x[page] 0x[reg] > <debugfs>/pm886/compact_addr
 * # cat <debugfs>/pm886/page-address   ---> get page address
 * # cat <debugfs>/pm886/register-value ---> get register address
 * or:
 * # echo page > <debugfs>/pm886/page-address
 * # echo addr > <debugfs>/pm886/register-address
 * # cat <debugfs>/pm886/register-value
 *
 * write:
 * # echo -0x[page] 0x[reg] > <debugfs>/pm886/compact_addr
 * # echo value > <debugfs>/pm886/register-value ---> set value
 * or:
 * # echo page > <debugfs>/pm886/page-address
 * # echo addr > <debugfs>/pm886/register-address
 * # cat <debugfs>/pm886/register-value
 *
 */

#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/kobject.h>
#include <linux/slab.h>
#include <linux/irq.h>

#include <linux/mfd/88pm886.h>

#ifdef CONFIG_DEBUG_FS
#include <linux/string.h>
#include <linux/ctype.h>
#endif

#define PM886_NAME		"88pm886"
#define PM886_GPADC_NAME	"88pm886-gpadc"
#define PM886_PAGES_NUM		(0x8)

static struct dentry *pm886_dir;
static struct dentry *pm886_gpadc_dir;
static u8 debug_page_addr, debug_reg_addr, debug_reg_val;

static int pm886_reg_addr_print(struct seq_file *s, void *p)
{
	return seq_printf(s, "0x%02x\n", debug_reg_addr);
}

static int pm886_reg_addr_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm886_reg_addr_print, inode->i_private);
}

static ssize_t pm886_reg_addr_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct pm886_chip *chip = file->private_data;
	unsigned long user_reg;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_reg);
	if (err)
		return err;

	/* FIXME: suppose it's 0xff first */
	if (user_reg >= 0xff) {
		dev_err(chip->dev, "debugfs error input > number of pages\n");
		return -EINVAL;
	}
	debug_reg_addr = user_reg;

	return count;
}

static const struct file_operations pm886_reg_addr_fops = {
	.open = pm886_reg_addr_open,
	.write = pm886_reg_addr_write,
	.read = seq_read,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int pm886_page_addr_print(struct seq_file *s, void *p)
{
	return seq_printf(s, "0x%02x\n", debug_page_addr);
}

static int pm886_page_addr_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm886_page_addr_print, inode->i_private);
}

static ssize_t pm886_page_addr_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct pm886_chip *chip =
		((struct seq_file *)(file->private_data))->private;
	u8 user_page;
	int err;

	err = kstrtou8_from_user(user_buf, count, 0, &user_page);
	if (err)
		return err;

	if (user_page >= PM886_PAGES_NUM) {
		dev_err(chip->dev, "debugfs error input > number of pages\n");
		return -EINVAL;
	}
	switch (user_page) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 7:
		pr_info("set page number as: 0x%x\n", user_page);
		break;
	default:
		pr_info("wrong page number: 0x%x\n", user_page);
		return -EINVAL;
	}

	debug_page_addr = user_page;

	return count;
}

static const struct file_operations pm886_page_addr_fops = {
	.open = pm886_page_addr_open,
	.write = pm886_page_addr_write,
	.read = seq_read,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int pm886_reg_val_get(struct seq_file *s, void *p)
{
	struct pm886_chip *chip = s->private;
	int err;
	struct regmap *map;
	unsigned int user_val;

	dev_info(chip->dev, "--->page: 0x%02x, reg: 0x%02x\n",
		 debug_page_addr, debug_reg_addr);
	switch (debug_page_addr) {
	case 0:
		map = chip->base_regmap;
		break;
	case 1:
		map = chip->power_regmap;
		break;
	case 2:
		map = chip->gpadc_regmap;
		break;
	case 3:
		map = chip->battery_regmap;
		break;
	case 7:
		map = chip->test_regmap;
		break;
	default:
		return -EINVAL;
	}

	err = regmap_read(map, debug_reg_addr, &user_val);
	if (err < 0) {
		dev_err(chip->dev, "read register fail.\n");
		return -EINVAL;
	}
	debug_reg_val = user_val;

	seq_printf(s, "0x%02x\n", debug_reg_val);
	return 0;
}

static int pm886_reg_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm886_reg_val_get, inode->i_private);
}

static ssize_t pm886_reg_val_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct pm886_chip *chip = file->private_data;
	unsigned long user_val;
	static struct regmap *map;
	int err;

	err = kstrtoul_from_user(user_buf, count, 0, &user_val);
	if (err)
		return err;

	if (user_val > 0xff) {
		dev_err(chip->dev, "debugfs error input > 0xff\n");
		return -EINVAL;
	}

	switch (debug_page_addr) {
	case 0:
		map = chip->base_regmap;
		break;
	case 1:
		map = chip->power_regmap;
		break;
	case 2:
		map = chip->gpadc_regmap;
		break;
	case 3:
		map = chip->battery_regmap;
		break;
	case 7:
		map = chip->test_regmap;
		break;
	default:
		pr_err("unsported pages.\n");
		return -EINVAL;
	}

	err = regmap_write(map, debug_reg_addr, user_val);
	if (err < 0) {
		dev_err(chip->dev, "write register fail.\n");
		return -EINVAL;
	}

	debug_reg_val = user_val;
	return count;
}

static const struct file_operations pm886_reg_val_fops = {
	.open = pm886_reg_val_open,
	.read = seq_read,
	.write = pm886_reg_val_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t pm886_compact_addr_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct pm886_chip *chip = file->private_data;
	int err;

	static char msg[20], index[20];
	err = copy_from_user(msg, user_buf, count);
	if (err)
		return err;
	if (msg[0] != '-') {
		dev_err(chip->dev,
			"right format: -0x[page_addr] 0x[reg_addr]\n");
		return count;
	}
	memcpy(index, msg + 1, 3);
	err = kstrtou8(index, 16, &debug_page_addr);
	if (err)
		return err;

	memcpy(index, msg + 5, 3);
	err = kstrtou8(index, 16, &debug_reg_addr);
	if (err)
		return err;

	return count;
}

static const struct file_operations pm886_compact_addr_fops = {
	.open = simple_open,
	.write = pm886_compact_addr_write,
	.owner = THIS_MODULE,
};

static int pm886_registers_print(struct pm886_chip *chip, u8 page,
			     struct seq_file *s)
{
	static u8 reg;
	static struct regmap *map;
	static int value;

	switch (page) {
	case 0:
		map = chip->base_regmap;
		break;
	case 1:
		map = chip->power_regmap;
		break;
	case 2:
		map = chip->gpadc_regmap;
		break;
	case 3:
		map = chip->battery_regmap;
		break;
	case 7:
		map = chip->test_regmap;
		break;
	default:
		pr_err("unsported pages.\n");
		return -EINVAL;
	}

	for (reg = 0; reg < 0xff; reg++) {
		regmap_read(map, reg, &value);
		seq_printf(s, "[0x%x:0x%2x] = 0x%02x\n", page, reg, value);
	}

	return 0;
}

static int pm886_print_whole_page(struct seq_file *s, void *p)
{
	struct pm886_chip *chip = s->private;
	u8 page_addr = debug_page_addr;

	seq_puts(s, "88pm886 register values:\n");
	seq_printf(s, " page 0x%02x:\n", page_addr);

	pm886_registers_print(chip, debug_page_addr, s);
	return 0;
}

static int pm886_whole_page_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm886_print_whole_page, inode->i_private);
}

static const struct file_operations pm886_whole_page_fops = {
	.open = pm886_whole_page_open,
	.read = seq_read,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t pm886_read_power_down(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct pm886_chip *chip = file->private_data;
	unsigned int i;
	int len = 0;
	char buf[100];
	char *powerdown1_name[] = {
		"OVER_TEMP",
		"UV_VSYS1",
		"SW_PDOWN",
		"FL_ALARM",
		"WD",
		"LONG_ONKEY",
		"OV_VSYS",
		"RTC_RESET"
	};
	char *powerdown2_name[] = {
		"HYB_DONE",
		"UV_VSYS2",
		"HW_RESET",
		"PGOOD_PDOWN",
		"LONKEY_RTC"
	};

	if (!chip)
		return -EINVAL;

	len += sprintf(&buf[len], "0x%x,0x%x ",
			chip->powerdown1, chip->powerdown2);
	if (!chip->powerdown1 && !chip->powerdown2) {
		len += sprintf(&buf[len], "(NO_PMIC_RESET)\n");
		return simple_read_from_buffer(user_buf, count, ppos, buf, len);
	}

	len += sprintf(&buf[len], "(");
	for (i = 0; i < ARRAY_SIZE(powerdown1_name); i++) {
		if ((1 << i) & chip->powerdown1)
			len += sprintf(&buf[len], "%s ", powerdown1_name[i]);
	}

	for (i = 0; i < ARRAY_SIZE(powerdown2_name); i++) {
		if ((1 << i) & chip->powerdown2)
			len += sprintf(&buf[len], "%s ", powerdown2_name[i]);
	}

	len--;
	len += sprintf(&buf[len], ")\n");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations pm886_power_down_fops = {
	.open = simple_open,
	.read = pm886_read_power_down,
	.owner = THIS_MODULE,
};

static ssize_t pm886_read_power_up(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct pm886_chip *chip = file->private_data;
	unsigned int i;
	int len = 0;
	char buf[100];
	char *powerup_name[] = {
		"ONKEY_WAKEUP",
		"CHG_WAKEUP",
		"EXTON_WAKEUP",
		"SMPL_WAKEUP",
		"ALARM_WAKEUP",
		"FAULT_WAKEUP",
		"BAT_WAKEUP",
		"RESERVED",
	};

	if (!chip)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(powerup_name); i++) {
		if ((1 << i) & chip->powerup)
			len += sprintf(&buf[len], "0x%x (%s)\n",
				       chip->powerup, powerup_name[i]);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations pm886_power_up_fops = {
	.open = simple_open,
	.read = pm886_read_power_up,
	.owner = THIS_MODULE,
};

static int pm886_debug_probe(struct platform_device *pdev)
{
	struct dentry *file;
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);

	pm886_dir = debugfs_create_dir(PM886_NAME, NULL);
	if (!pm886_dir)
		goto err;

	pm886_gpadc_dir = debugfs_create_dir(PM886_GPADC_NAME, NULL);
	if (!pm886_gpadc_dir)
		goto err;

	file = debugfs_create_file("register-address", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm886_dir, chip, &pm886_reg_addr_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("page-address", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm886_dir, chip, &pm886_page_addr_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("register-value", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm886_dir, chip, &pm886_reg_val_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("compact-address", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm886_dir, chip, &pm886_compact_addr_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("whole-page", (S_IRUGO | S_IRUSR | S_IRGRP),
		pm886_dir, chip, &pm886_whole_page_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("power-down-log", (S_IRUGO | S_IRUSR | S_IRGRP),
		pm886_dir, chip, &pm886_power_down_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("power-up-log", (S_IRUGO | S_IRUSR | S_IRGRP),
		pm886_dir, chip, &pm886_power_up_fops);
	if (!file)
		goto err;
	return 0;
err:
	debugfs_remove_recursive(pm886_dir);
	dev_err(&pdev->dev, "failed to create debugfs entries.\n");
	return -ENOMEM;
}

static int pm886_debug_remove(struct platform_device *pdev)
{
	debugfs_remove_recursive(pm886_dir);

	return 0;
}


static struct platform_driver pm886_debug_driver = {
	.driver = {
		.name = "88pm886-debugfs",
		.owner = THIS_MODULE,
	},
	.probe  = pm886_debug_probe,
	.remove = pm886_debug_remove
};

static int pm886_debug_init(void)
{
	return platform_driver_register(&pm886_debug_driver);
}

static void pm886_debug_exit(void)
{
	platform_driver_unregister(&pm886_debug_driver);
}
subsys_initcall(pm886_debug_init);
module_exit(pm886_debug_exit);

MODULE_AUTHOR("Yi Zhang <yizhang@marvell.com>");
MODULE_DESCRIPTION("88pm886 debug interface");
MODULE_LICENSE("GPL v2");
