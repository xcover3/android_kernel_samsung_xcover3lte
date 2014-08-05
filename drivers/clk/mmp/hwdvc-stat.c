/*
 * common function for clock framework source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 * Zhoujie Wu <zjwu@marvell.com>
 * Lu Cao <lucao@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/clk/mmpdcstat.h>
#include <mach/pxa988_lowpower.h>
#include <linux/clk/dvfs-dvc.h>
#include <linux/debugfs-pxa.h>
#include <linux/gpio.h>
#include <linux/miscled-rgb.h>

struct vol_op_dcstat_info {
	u64 time;	/*ms*/
	u32 vol;	/* mV */
};

struct vol_dc_stat_info {
	bool stat_start;
	struct vol_op_dcstat_info ops_dcstat[MAX_PMIC_LEVEL];
	u32 vc_count[MAX_PMIC_LEVEL][MAX_PMIC_LEVEL];	/* [from][to] */
	u32 vc_total_count;
	ktime_t breakdown_start;
	ktime_t prev_ts;
	u32 cur_lvl;
	u64 total_time;	/* ms */
};

static DEFINE_SPINLOCK(vol_lock);
static struct vol_dc_stat_info vol_dcstat;
static int vol_ledstatus_start;

static u32 voltage_lvl[AP_COMP_MAX];

static void vol_ledstatus_show(u32 lpm)
{
	u32 hwlvl;

	switch (lpm) {
	case POWER_MODE_MAX:
		/* Active or exit from lpm */
		hwlvl = voltage_lvl[AP_ACTIVE];
		break;
	case POWER_MODE_CORE_POWERDOWN:
		/* M2 */
		hwlvl = voltage_lvl[AP_LPM];
		break;
	case POWER_MODE_APPS_IDLE:
		/* D1P */
		hwlvl = voltage_lvl[APSUB_IDLE];
		break;
	case POWER_MODE_SYS_SLEEP:
		/* D1 */
		hwlvl = voltage_lvl[APSUB_SLEEP];
		break;
	case POWER_MODE_UDR:
		/* D2 */
		hwlvl = -1UL;
	default:
		hwlvl = voltage_lvl[AP_ACTIVE];
	}

	switch  (hwlvl) {
	case VL3:
		/* VL3 : LED RG */
		led_rgb_output(LED_R, 1);
		led_rgb_output(LED_G, 1);
		led_rgb_output(LED_B, 0);
		break;
	case VL2:
		/* VL2 : LED R */
		led_rgb_output(LED_R, 1);
		led_rgb_output(LED_G, 0);
		led_rgb_output(LED_B, 0);
		break;
	case VL1:
		/* VL1 : LED G */
		led_rgb_output(LED_R, 0);
		led_rgb_output(LED_G, 1);
		led_rgb_output(LED_B, 0);
		break;
	case VL0:
		/* VL0 : LED B */
		led_rgb_output(LED_R, 0);
		led_rgb_output(LED_G, 0);
		led_rgb_output(LED_B, 1);
		break;
	case -1UL:
		/* lpm : LED -- */
		led_rgb_output(LED_R, 0);
		led_rgb_output(LED_G, 0);
		led_rgb_output(LED_B, 0);
		break;
	}
}

void vol_ledstatus_event(u32 lpm)
{
	spin_lock(&vol_lock);
	if (!vol_ledstatus_start)
		goto out;
	vol_ledstatus_show(lpm);
out:
	spin_unlock(&vol_lock);
}

static ssize_t vol_led_read(struct file *filp, char __user *buffer,
			   size_t count, loff_t *ppos)
{
	char *buf;
	ssize_t ret, size = 2 * PAGE_SIZE - 1;
	u32 len = 0;

	buf = (char *)__get_free_pages(GFP_NOIO, get_order(size));
	if (!buf)
		return -ENOMEM;

	len += snprintf(buf + len, size - len,
			"Help information :\n");
	len += snprintf(buf + len, size - len,
			"echo 1 to start voltage led status:\n");
	len += snprintf(buf + len, size - len,
			"echo 0 to stop voltage led status:\n");
	len += snprintf(buf + len, size - len,
			"VLevel3: R, G\n");
	len += snprintf(buf + len, size - len,
			"VLevel2: R\n");
	len += snprintf(buf + len, size - len,
			"VLevel1: G\n");
	len += snprintf(buf + len, size - len,
			"VLevel0: B\n");
	len += snprintf(buf + len, size - len,
			"    lpm: off\n");

	ret = simple_read_from_buffer(buffer, count, ppos, buf, len);
	free_pages((unsigned long)buf, get_order(size));
	return ret;
}

static ssize_t vol_led_write(struct file *filp, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	unsigned int start;
	char buf[10] = { 0 };

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	if (sscanf(buf, "%d", &start) != 1)
		return -EFAULT;
	start = !!start;

	if (vol_ledstatus_start == start) {
		pr_err("[WARNING]Voltage led status is already %s\n",
		       vol_dcstat.stat_start ? "started" : "stopped");
		return -EINVAL;
	}

	spin_lock(&vol_lock);
	vol_ledstatus_start = start;
	if (vol_ledstatus_start)
		vol_ledstatus_show(MAX_LPM_INDEX);
	else {
		led_rgb_output(LED_R, 0);
		led_rgb_output(LED_G, 0);
		led_rgb_output(LED_B, 0);
	}
	spin_unlock(&vol_lock);

	return count;
}

static const struct file_operations vol_led_ops = {
	.owner = THIS_MODULE,
	.read = vol_led_read,
	.write = vol_led_write,
};

static void vol_dcstat_update(u32 lpm)
{
	ktime_t cur_ts;
	u32 hwlvl;
	u64 time_us;

	switch (lpm) {
	case POWER_MODE_MAX:
		/* Active or exit from lpm */
		hwlvl = voltage_lvl[AP_ACTIVE];
		break;
	case POWER_MODE_CORE_POWERDOWN:
		/* M2 */
		hwlvl = voltage_lvl[AP_LPM];
		break;
	case POWER_MODE_APPS_IDLE:
		/* D1P */
		hwlvl = voltage_lvl[APSUB_IDLE];
		break;
	case POWER_MODE_SYS_SLEEP:
		/* D1 */
		hwlvl = voltage_lvl[APSUB_SLEEP];
		break;
	default:
		hwlvl = voltage_lvl[AP_ACTIVE];
		break;
	}

	if (vol_dcstat.cur_lvl == hwlvl)
		return;

	/* update voltage change times */
	vol_dcstat.vc_count[vol_dcstat.cur_lvl][hwlvl]++;

	/* update voltage dc statistics */
	cur_ts = ktime_get();
	time_us = ktime_to_us(ktime_sub(cur_ts, vol_dcstat.prev_ts));
	vol_dcstat.ops_dcstat[vol_dcstat.cur_lvl].time += time_us;
	vol_dcstat.prev_ts = cur_ts;
	vol_dcstat.cur_lvl = hwlvl;
}

void vol_dcstat_event(u32 lpm)
{
	spin_lock(&vol_lock);
	if (!vol_dcstat.stat_start)
		goto out;
	vol_dcstat_update(lpm);
out:
	spin_unlock(&vol_lock);
}

static ssize_t vol_dc_read(struct file *filp, char __user *buffer,
			   size_t count, loff_t *ppos)
{
	char *buf;
	ssize_t ret, size = 2 * PAGE_SIZE - 1;
	u32 i, j, dc_int = 0, dc_fra = 0, len = 0;

	buf = (char *)__get_free_pages(GFP_NOIO, get_order(size));
	if (!buf)
		return -ENOMEM;

	if (!vol_dcstat.total_time) {
		len += snprintf(buf + len, size - len,
				"No stat information! ");
		len += snprintf(buf + len, size - len,
				"Help information :\n");
		len += snprintf(buf + len, size - len,
				"1. echo 1 to start duty cycle stat:\n");
		len += snprintf(buf + len, size - len,
				"2. echo 0 to stop duty cycle stat:\n");
		len += snprintf(buf + len, size - len,
				"3. cat to check duty cycle info from start to stop:\n\n");
		goto out;
	}

	if (vol_dcstat.stat_start) {
		len += snprintf(buf + len, size - len,
				"Please stop the vol duty cycle stats at first\n");
		goto out;
	}

	len += snprintf(buf + len, size - len,
			"Total time:%8llums (%6llus)\n",
			div64_u64(vol_dcstat.total_time, (u64)(1000)),
			div64_u64(vol_dcstat.total_time, (u64)(1000000)));
	len += snprintf(buf + len, size - len,
			"|Level|Vol(mV)|Time(ms)|      %%|\n");
	for (i = VL0; i < MAX_PMIC_LEVEL; i++) {
		dc_int = calculate_dc(vol_dcstat.ops_dcstat[i].time,
				vol_dcstat.total_time, &dc_fra);
		len += snprintf(buf + len, size - len,
				"| VL_%1d|%7u|%8llu|%3u.%02u%%|\n",
				i, vol_dcstat.ops_dcstat[i].vol,
				div64_u64(vol_dcstat.ops_dcstat[i].time,
					(u64)(1000)),
				dc_int, dc_fra);
	}

	/* show voltage-change times */
	len += snprintf(buf + len, size - len,
			"\nTotal voltage-change times:%8u",
			vol_dcstat.vc_total_count);
	len += snprintf(buf + len, size - len, "\n|from\\to|");
	for (j = VL0; j < MAX_PMIC_LEVEL; j++)
		len += snprintf(buf + len, size - len, " Level%1d|", j);
	for (i = VL0; i < MAX_PMIC_LEVEL; i++) {
		len += snprintf(buf + len, size - len, "\n| Level%1d|", i);
		for (j = VL0; j < MAX_PMIC_LEVEL; j++)
			if (i == j)
				len += snprintf(buf + len, size - len,
						"  ---  |");
			else
				/* [from][to] */
				len += snprintf(buf + len, size - len,
						"%7u|",
						vol_dcstat.vc_count[i][j]);
	}
	len += snprintf(buf + len, size - len, "\n");
out:
	ret = simple_read_from_buffer(buffer, count, ppos, buf, len);
	free_pages((unsigned long)buf, get_order(size));
	return ret;
}

static ssize_t vol_dc_write(struct file *filp, const char __user *buffer,
			    size_t count, loff_t *ppos)
{
	unsigned int start, i, j;
	char buf[10] = { 0 };
	ktime_t cur_ts;
	u64 time_us;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;
	if (sscanf(buf, "%d", &start) != 1)
		return -EFAULT;
	start = !!start;

	if (vol_dcstat.stat_start == start) {
		pr_err("[WARNING]Voltage duty-cycle statistics is already %s\n",
		       vol_dcstat.stat_start ? "started" : "stopped");
		return -EINVAL;
	}
	vol_dcstat.stat_start = start;

	spin_lock(&vol_lock);
	cur_ts = ktime_get();
	if (vol_dcstat.stat_start) {
		for (i = VL0; i < MAX_PMIC_LEVEL; i++) {
			vol_dcstat.ops_dcstat[i].time = 0;
			for (j = VL0; j < MAX_PMIC_LEVEL; j++)
				vol_dcstat.vc_count[i][j] = 0;
		}

		vol_dcstat.prev_ts = cur_ts;
		vol_dcstat.breakdown_start = cur_ts;
		vol_dcstat.cur_lvl = voltage_lvl[AP_ACTIVE];
		vol_dcstat.total_time = -1UL;
		vol_dcstat.vc_total_count = 0;
	} else {
		time_us = ktime_to_us(ktime_sub(cur_ts, vol_dcstat.prev_ts));
		vol_dcstat.ops_dcstat[vol_dcstat.cur_lvl].time += time_us;
		vol_dcstat.total_time = ktime_to_us(ktime_sub(cur_ts,
					vol_dcstat.breakdown_start));
		for (i = VL0; i < MAX_PMIC_LEVEL; i++)
			for (j = VL0; j < MAX_PMIC_LEVEL; j++)
				vol_dcstat.vc_total_count +=
					vol_dcstat.vc_count[i][j];
	}
	spin_unlock(&vol_lock);
	return count;
}

static const struct file_operations vol_dc_ops = {
	.owner = THIS_MODULE,
	.read = vol_dc_read,
	.write = vol_dc_write,
};

static int hwdvc_stat_notifier_handler(struct notifier_block *nb,
		unsigned long rails, void *data)
{
	struct hwdvc_notifier_data *vl;

	if (rails >= AP_COMP_MAX)
		return NOTIFY_OK;

	spin_lock(&vol_lock);
	vl = (struct hwdvc_notifier_data *)(data);
	voltage_lvl[rails] = vl->newlv;

	if (!vol_dcstat.stat_start)
		goto out_dc;
	if (rails == AP_ACTIVE)
		vol_dcstat_update(POWER_MODE_MAX);
out_dc:
	if (!vol_ledstatus_start)
		goto out_led;
	if (rails == AP_ACTIVE)
		vol_ledstatus_show(POWER_MODE_MAX);
out_led:
	spin_unlock(&vol_lock);
	return NOTIFY_OK;
}

static struct notifier_block hwdvc_stat_ntf = {
	.notifier_call = hwdvc_stat_notifier_handler,
};

static int __init hwdvc_stat_init(void)
{
	struct dentry *dvfs_node, *volt_dc_stat, *volt_led;
	struct dvc_plat_info platinfo;
	u32 idx;

	/* record voltage lvl when init */
	if (dvfs_get_dvcplatinfo(&platinfo))
		return -ENOENT;

	for (idx = VL0; idx < MAX_PMIC_LEVEL; idx++)
		vol_dcstat.ops_dcstat[idx].vol =
			platinfo.millivolts[idx];

	hwdvc_notifier_register(&hwdvc_stat_ntf);

	dvfs_node = debugfs_create_dir("vlstat", pxa);
	if (!dvfs_node)
		return -ENOENT;

	volt_dc_stat = debugfs_create_file("vol_dc_stat", 0444,
		dvfs_node, NULL, &vol_dc_ops);
	if (!volt_dc_stat)
		goto err_1;

	volt_led = debugfs_create_file("vol_led", 0644,
		dvfs_node, NULL, &vol_led_ops);
	if (!volt_led)
		goto err_volt_led;

	return 0;

err_volt_led:
	debugfs_remove(volt_dc_stat);
err_1:
	debugfs_remove(dvfs_node);
	return -ENOENT;
}
arch_initcall(hwdvc_stat_init);
