/*
 * Copyright (C) Marvell 2014
 *
 * Author: Yi Zhang <yizhang@marvell.com>
 * License Terms: GNU General Public License v2
 */
/*
 * 88pm88x register access
 *
 * read:
 * # echo -0x[page] 0x[reg] > <debugfs>/pm88x/compact_addr
 * # cat <debugfs>/pm88x/page-address   ---> get page address
 * # cat <debugfs>/pm88x/register-value ---> get register address
 * or:
 * # echo page > <debugfs>/pm88x/page-address
 * # echo addr > <debugfs>/pm88x/register-address
 * # cat <debugfs>/pm88x/register-value
 *
 * write:
 * # echo -0x[page] 0x[reg] > <debugfs>/pm88x/compact_addr
 * # echo value > <debugfs>/pm88x/register-value ---> set value
 * or:
 * # echo page > <debugfs>/pm88x/page-address
 * # echo addr > <debugfs>/pm88x/register-address
 * # cat <debugfs>/pm88x/register-value
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

#include <linux/mfd/88pm88x.h>
#include <linux/mfd/88pm886.h>

#ifdef CONFIG_DEBUG_FS
#include <linux/string.h>
#include <linux/ctype.h>
#endif

#define PM88X_NAME		"88pm88x"
#define PM88X_PAGES_NUM		(0x8)

enum pm88x_debug_module {
	DEBUG_INVALID = -1,
	DEBUG_BUCK = 1,
	DEBUG_LDO = 2,
	DEBUG_VR = 3,
	DEBUG_DVC = 4,
	DEBUG_GPADC = 5,
};

static struct dentry *pm88x_dir;
static u8 debug_page_addr, debug_reg_addr, debug_reg_val;

static int pm88x_reg_addr_print(struct seq_file *s, void *p)
{
	return seq_printf(s, "0x%02x\n", debug_reg_addr);
}

static int pm88x_reg_addr_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm88x_reg_addr_print, inode->i_private);
}

static ssize_t pm88x_reg_addr_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
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

static const struct file_operations pm88x_reg_addr_fops = {
	.open = pm88x_reg_addr_open,
	.write = pm88x_reg_addr_write,
	.read = seq_read,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int pm88x_page_addr_print(struct seq_file *s, void *p)
{
	return seq_printf(s, "0x%02x\n", debug_page_addr);
}

static int pm88x_page_addr_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm88x_page_addr_print, inode->i_private);
}

static ssize_t pm88x_page_addr_write(struct file *file,
				     const char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip =
		((struct seq_file *)(file->private_data))->private;
	u8 user_page;
	int err;

	err = kstrtou8_from_user(user_buf, count, 0, &user_page);
	if (err)
		return err;

	if (user_page >= PM88X_PAGES_NUM) {
		dev_err(chip->dev, "debugfs error input > number of pages\n");
		return -EINVAL;
	}
	switch (user_page) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
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

static const struct file_operations pm88x_page_addr_fops = {
	.open = pm88x_page_addr_open,
	.write = pm88x_page_addr_write,
	.read = seq_read,
	.release = single_release,
	.owner = THIS_MODULE,
};

static int pm88x_reg_val_get(struct seq_file *s, void *p)
{
	struct pm88x_chip *chip = s->private;
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
		map = chip->ldo_regmap;
		break;
	case 2:
		map = chip->gpadc_regmap;
		break;
	case 3:
		map = chip->battery_regmap;
		break;
	case 4:
		map = chip->buck_regmap;
		break;
	case 7:
		map = chip->test_regmap;
		break;
	default:
		pr_err("unsupported pages.\n");
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

static int pm88x_reg_val_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm88x_reg_val_get, inode->i_private);
}

static ssize_t pm88x_reg_val_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = ((struct seq_file *)(file->private_data))->private;
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
		map = chip->ldo_regmap;
		break;
	case 2:
		map = chip->gpadc_regmap;
		break;
	case 3:
		map = chip->battery_regmap;
		break;
	case 4:
		map = chip->buck_regmap;
		break;
	case 7:
		map = chip->test_regmap;
		break;
	default:
		pr_err("unsupported pages.\n");
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

static const struct file_operations pm88x_reg_val_fops = {
	.open = pm88x_reg_val_open,
	.read = seq_read,
	.write = pm88x_reg_val_write,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t pm88x_compact_addr_write(struct file *file,
				const char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
	int err, index1, index2;

	char msg[20] = { 0 };
	err = copy_from_user(msg, user_buf, count);
	if (err)
		return err;
	err = sscanf(msg, "-0x%x 0x%x", &index1, &index2);
	if (err != 2) {
		dev_err(chip->dev,
			"right format: -0x[page_addr] 0x[reg_addr]\n");
		return count;
	}
	debug_page_addr = (u8)index1;
	debug_reg_addr = (u8)index2;

	return count;
}

static const struct file_operations pm88x_compact_addr_fops = {
	.open = simple_open,
	.write = pm88x_compact_addr_write,
	.owner = THIS_MODULE,
};

static int pm88x_registers_print(struct pm88x_chip *chip, u8 page,
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
	case 4:
		map = chip->buck_regmap;
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

static int pm88x_print_whole_page(struct seq_file *s, void *p)
{
	struct pm88x_chip *chip = s->private;
	u8 page_addr = debug_page_addr;

	seq_puts(s, "88pm88x register values:\n");
	seq_printf(s, " page 0x%02x:\n", page_addr);

	pm88x_registers_print(chip, debug_page_addr, s);
	return 0;
}

static int pm88x_whole_page_open(struct inode *inode, struct file *file)
{
	return single_open(file, pm88x_print_whole_page, inode->i_private);
}

static const struct file_operations pm88x_whole_page_fops = {
	.open = pm88x_whole_page_open,
	.read = seq_read,
	.release = single_release,
	.owner = THIS_MODULE,
};

static ssize_t pm88x_read_power_down(struct file *file, char __user *user_buf,
				     size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
	unsigned int i;
	int len = 0;
	char buf[100];
	char *powerdown1_name[] = {
		"OVER_TEMP",
		"UV_VANA5",
		"SW_PDOWN",
		"FL_ALARM",
		"WD",
		"LONG_ONKEY",
		"OV_VSYS",
		"RTC_RESET"
	};
	char *powerdown2_name[] = {
		"HYB_DONE",
		"UV_VBAT",
		"HW_RESET2",
		"PGOOD_PDOWN",
		"LONKEY_RTC",
		"HW_RESET1",
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

static const struct file_operations pm88x_power_down_fops = {
	.open = simple_open,
	.read = pm88x_read_power_down,
	.owner = THIS_MODULE,
};

static ssize_t pm88x_read_power_up(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
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
		"WLCHG_WAKEUP",
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

static const struct file_operations pm88x_power_up_fops = {
	.open = simple_open,
	.read = pm88x_read_power_up,
	.owner = THIS_MODULE,
};

static ssize_t pm88x_buck1_info_read(struct file *file, char __user *user_buf,
				   size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
	struct pm88x_dvc *dvc = chip->dvc;
	int uv, i, len = 0;
	char str[1000];

	for (i = 0; i < dvc->desc.max_level; i++) {
		uv = pm88x_dvc_get_volt(i);
		if (uv < dvc->desc.min_uV)
			dev_err(chip->dev, "get buck 1 voltage failed of level %d.\n", i);
		else
			len += snprintf(str + len, sizeof(str),
					"buck 1, level %d, voltage %duV.\n", i, uv);
	}

	return simple_read_from_buffer(user_buf, count, ppos, str, len);
}

static ssize_t pm88x_buck1_info_write(struct file *file, const char __user *user_buf,
				      size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
	struct pm88x_dvc *dvc = chip->dvc;
	int i, ret, lvl, volt, uv;
	char arg;

	ret = sscanf(user_buf, "-%c", &arg);
	if (ret < 1) {
		ret = sscanf(user_buf, "%d\n", &volt);
		if (ret < 1) {
			pr_err("Type \"echo -h > <debugfs>/88pm88x/buck1_info\" for help.\n");
		} else {
			for (i = 0; i < dvc->desc.max_level; i++) {
				pm88x_dvc_set_volt(i, volt);
				uv = pm88x_dvc_get_volt(i);
				pr_info("buck 1, level %d, voltage %d uV\n", i, uv);
			}
		}
	} else {
		switch (arg) {
		case 'l':
			ret = sscanf(user_buf, "-%c %d %d", &arg, &lvl, &volt);
			if (ret >= 2) {
				if (lvl > dvc->desc.max_level) {
					pr_err("Please check voltage level.\n");
					return count;
				}
				if (ret == 3)
					pm88x_dvc_set_volt(lvl, volt);
				uv = pm88x_dvc_get_volt(lvl);
				pr_info("buck 1, level %d, voltage %d uV\n", lvl, uv);
			} else {
				pr_err("Type \"echo -h > ");
				pr_err("<debugfs>/88pm88x/buck1_info\" for help.\n");
			}
			break;
		case 'h':
			pr_info("Usage of buck1_info:\n");
			pr_info("1: cat <debugfs>/88pm88x/buck1_info\n");
			pr_info("   dump voltages for all levels.\n");
			pr_info("2: echo [voltage-in-uV] > <debugfs>/88pm88x/buck1_info\n");
			pr_info("   set same voltages for all levels.\n");
			pr_info("3: echo -l [level] > <debugfs>/88pm88x/buck1_info\n");
			pr_info("   dump voltage of [level].\n");
			pr_info("4: echo -l [level] [voltage-in-uV] > ");
			pr_info("<debugfs>/88pm88x/buck1_info\n");
			pr_info("   set voltage of [level].\n");
			break;
		default:
			pr_err("Type \"echo -h > <debugfs>/88pm88x/buck1_info\" for help.\n");
			break;
		}
	}

	return count;
}

static const struct file_operations pm88x_buck1_info_fops = {
	.open = simple_open,
	.read = pm88x_buck1_info_read,
	.write = pm88x_buck1_info_write,
	.owner = THIS_MODULE,
};

static ssize_t pm88x_debug_read(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
	int len = 0;
	ssize_t ret = -EINVAL;
	char *buf;

	if (!chip) {
		pr_err("Cannot find chip!\n");
		return -EINVAL;
	}

	buf = kzalloc(8000, GFP_KERNEL);
	if (!buf) {
		pr_err("Cannot allocate buffer!\n");
		return -ENOMEM;
	}

	ret = pm88x_display_buck(chip, buf);
	if (ret < 0) {
		pr_err("Error in printing the buck list!\n");
		goto out_print;
	}
	len += ret;

	ret = pm88x_display_vr(chip, buf + len);
	if (ret < 0) {
		pr_err("Error in printing the virtual regulator list!\n");
		goto out_print;
	}
	len += ret;

	ret = pm88x_display_ldo(chip, buf + len);
	if (ret < 0) {
		pr_err("Error in printing the ldo list!\n");
		goto out_print;
	}
	len += ret;

	ret = pm88x_display_dvc(chip, buf + len);
	if (ret < 0) {
		pr_err("Error in printing the dvc!\n");
		goto out_print;
	}
	len += ret;

	ret = pm88x_display_gpadc(chip, buf + len);
	if (ret < 0) {
		pr_err("Error in printing the GPADC values!\n");
		goto out_print;
	}
	len += ret;

	ret = simple_read_from_buffer(user_buf, count, ppos, buf, len);
out_print:
	kfree(buf);
	return ret;
}

static void pm88x_debug_usage(void)
{
	pr_info("Usage of debugfs for PM88X:\n");
	pr_info("1: dump PMIC information.\n");
	pr_info("	cat <debugfs>/88pm88x/pm88x_debug\n");
	pr_info("2: configure PMIC.\n");
	pr_info("	echo -[OPTION] > <debugfs>/88pm88x/pm88x_debug\n");
	pr_info("	-b [name]	print or set [name] information for BUCK\n");
	pr_info("	-l [name]	print or set [name] information for LDO\n");
	pr_info("	-v [name]	print or set [name] information for Virtual Regulator\n");
	pr_info("	-d [name]	print or set [name] information for DVC\n");
	pr_info("	-g [name]	print or set [name] information for GPADC\n");
	pr_info("	-V [en] [voltage(uV)]		set [en] [voltage] for active mode\n");
	pr_info("					set [en] for gpadc channel\n");
	pr_info("	-s [en] [voltage(uV)]		set [en] [voltage] for sleep mode\n");
	pr_info("	-a [en] [voltage(uV)]		set [en] [voltage] for audio mode\n");
	pr_info("	-L [level] [voltage(uV)]	set [voltages] of [level] for DVC\n");
	pr_info("	-B [bias_en] [bias_current(uA)]	set [bias_en] [bias_current] for gpadc\n");
	pr_info("	-h				help\n");
}

static ssize_t pm88x_debug_write(struct file *file, const char __user *user_buf,
				 size_t count, loff_t *ppos)
{
	struct pm88x_chip *chip = file->private_data;
	struct pm88x_debug_info info;
	int i, ret, buf_len, j = 0, len = 0;
	char arg;
	char print_tmp[200];
	char *buf;

	if (!chip) {
		pr_err("Cannot find chip!\n");
		return -EINVAL;
	}

	buf = kzalloc(3000, GFP_KERNEL);
	if (!buf) {
		pr_err("Cannot allocate buffer!\n");
		return -ENOMEM;
	}

	info.en = -1;
	info.volt = -1;
	info.lvl = -1;
	info.lvl_volt = -1;
	info.slp_en = -1;
	info.slp_volt = -1;
	info.audio_en = -1;
	info.audio_volt = -1;
	info.bias_en = -1;
	info.bias_current = -1;
	info.debug_mod = DEBUG_INVALID;
	buf_len = strchr(user_buf, '\n') - user_buf;

	for (i = 0; i < 15; i++)
		info.name[i] = 0;

	if (buf_len == 0)
		goto out;

	if ((*user_buf != '-') || (*(user_buf + buf_len - 1) == ' ') ||
			(*(user_buf + buf_len - 1) == '-'))
		goto out;

	for (i = 0; i < buf_len; i++) {
		if (*(user_buf + i) == '-') {
			ret = sscanf(user_buf + i, "-%c", &arg);
			if (ret != 1)
				goto out;
			switch (arg) {
			case 'b':
				if (info.debug_mod != DEBUG_INVALID)
					goto out;
				info.debug_mod = DEBUG_BUCK;
				break;
			case 'l':
				if (info.debug_mod != DEBUG_INVALID)
					goto out;
				info.debug_mod = DEBUG_LDO;
				break;
			case 'v':
				if (info.debug_mod != DEBUG_INVALID)
					goto out;
				info.debug_mod = DEBUG_VR;
				break;
			case 'g':
				if (info.debug_mod != DEBUG_INVALID)
					goto out;
				info.debug_mod = DEBUG_GPADC;
				break;
			case 'd':
				if (info.debug_mod != DEBUG_INVALID)
					goto out;
				info.debug_mod = DEBUG_DVC;
				break;
			case 'V':
			case 's':
			case 'a':
			case 'B':
			case 'L':
				break;
			case 'h':
			default:
				goto out;
			}
		}

		if (*(user_buf + i) == ' ' && *(user_buf + i + 1) != '-') {
			switch (*(user_buf + i - 1)) {
			case 'b':
			case 'l':
			case 'd':
			case 'v':
			case 'g':
				if ((user_buf + i) == strrchr(user_buf, ' '))
					strncpy(info.name, user_buf + i + 1, buf_len - (i + 1));
				else
					strncpy(info.name, user_buf + i + 1,
						strchr(user_buf + i + 1, ' ') -
						(user_buf + i + 1));
				break;
			case 'V':
				ret = sscanf(user_buf + i + 1, "%d %d", &info.en, &info.volt);
				if (ret < 1)
					goto out;
				if ((ret == 1) && (info.en > 1)) {
					info.volt = info.en;
					info.en = -1;
				}
				break;
			case 'L':
				ret = sscanf(user_buf + i + 1, "%d %d", &info.lvl, &info.lvl_volt);
				if (ret < 1)
					goto out;
				if ((ret == 1) && (info.lvl > 16)) {
					info.lvl_volt = info.lvl;
					info.lvl = -1;
				}
				break;
			case 's':
				ret = sscanf(user_buf + i + 1, "%d %d", &info.slp_en,
					     &info.slp_volt);
				if (ret < 1)
					goto out;
				if ((ret == 1) && (info.slp_en > 3)) {
					info.slp_volt = info.slp_en;
					info.slp_en = -1;
				}
				break;
			case 'a':
				ret = sscanf(user_buf + i + 1, "%d %d", &info.audio_en,
					     &info.audio_volt);
				if (ret < 1)
					goto out;
				if ((ret == 1) && (info.audio_en > 1)) {
					info.audio_volt = info.audio_en;
					info.audio_en = -1;
				}
				break;
			case 'B':
				ret = sscanf(user_buf + i + 1, "%d %d", &info.bias_en,
					     &info.bias_current);
				if (ret < 1)
					goto out;
				if ((ret == 1) && (info.bias_en > 1)) {
					info.bias_current = info.bias_en;
					info.bias_en = -1;
				}
				break;
			case 'h':
				goto out;
			default:
				break;
			}
		}
	}

	switch (info.debug_mod) {
	case DEBUG_BUCK:
		ret = pm88x_buck_debug_write(chip, buf + len, &info);
		if (ret < 0)
			goto out;
		break;
	case DEBUG_LDO:
		ret = pm88x_ldo_debug_write(chip, buf + len, &info);
		if (ret < 0)
			goto out;
		break;
	case DEBUG_VR:
		ret = pm88x_vr_debug_write(chip, buf + len, &info);
		if (ret < 0)
			goto out;
		break;
	case DEBUG_DVC:
		ret = pm88x_dvc_debug_write(chip, buf + len, &info);
		if (ret < 0)
			goto out;
		break;
	case DEBUG_GPADC:
		ret = pm88x_gpadc_debug_write(chip, buf + len, &info);
		if (ret < 0)
			goto out;
		break;
	case DEBUG_INVALID:
	default:
		goto out;
	}

	memset(print_tmp, 0, 200);
	for (i = 0; i < ret; i++) {
		if (buf[i] == '\n') {
			pr_info("%s\n", print_tmp);
			memset(print_tmp, 0, 200);
			j = 0;
		} else {
			if (j >= 200) {
				pr_err("%s: Debug information is too long to print.", __func__);
				break;
			}
			print_tmp[j] = buf[i];
			j++;
		}
	}

	kfree(buf);
	return count;
out:
	pm88x_debug_usage();
	kfree(buf);
	return count;
}

static const struct file_operations pm88x_debug_fops = {
	.open = simple_open,
	.read = pm88x_debug_read,
	.write = pm88x_debug_write,
	.owner = THIS_MODULE,
};

static int pm88x_debug_probe(struct platform_device *pdev)
{
	struct dentry *file;
	struct pm88x_chip *chip = dev_get_drvdata(pdev->dev.parent);

	pm88x_dir = debugfs_create_dir(PM88X_NAME, NULL);
	if (!pm88x_dir)
		goto err;

	file = debugfs_create_file("register-address", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm88x_dir, chip, &pm88x_reg_addr_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("page-address", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm88x_dir, chip, &pm88x_page_addr_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("register-value", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm88x_dir, chip, &pm88x_reg_val_fops);
	if (!file)
		goto err;

	file = debugfs_create_file("compact-address", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm88x_dir, chip, &pm88x_compact_addr_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("whole-page", (S_IRUGO | S_IRUSR | S_IRGRP),
		pm88x_dir, chip, &pm88x_whole_page_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("power-down-log", (S_IRUGO | S_IRUSR | S_IRGRP),
		pm88x_dir, chip, &pm88x_power_down_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("power-up-log", (S_IRUGO | S_IRUSR | S_IRGRP),
		pm88x_dir, chip, &pm88x_power_up_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("buck1_info", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm88x_dir, chip, &pm88x_buck1_info_fops);
	if (!file)
		goto err;
	file = debugfs_create_file("pm88x_debug", (S_IRUGO | S_IWUSR | S_IWGRP),
		pm88x_dir, chip, &pm88x_debug_fops);
	if (!file)
		goto err;
	return 0;
err:
	debugfs_remove_recursive(pm88x_dir);
	dev_err(&pdev->dev, "failed to create debugfs entries.\n");
	return -ENOMEM;
}

static int pm88x_debug_remove(struct platform_device *pdev)
{
	debugfs_remove_recursive(pm88x_dir);

	return 0;
}


static struct platform_driver pm88x_debug_driver = {
	.driver = {
		.name = "88pm88x-debugfs",
		.owner = THIS_MODULE,
	},
	.probe  = pm88x_debug_probe,
	.remove = pm88x_debug_remove
};

static int pm88x_debug_init(void)
{
	return platform_driver_register(&pm88x_debug_driver);
}

static void pm88x_debug_exit(void)
{
	platform_driver_unregister(&pm88x_debug_driver);
}
subsys_initcall(pm88x_debug_init);
module_exit(pm88x_debug_exit);

MODULE_AUTHOR("Yi Zhang <yizhang@marvell.com>");
MODULE_DESCRIPTION("88pm88x debug interface");
MODULE_LICENSE("GPL v2");
