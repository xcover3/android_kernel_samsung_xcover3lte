/*
 * gpadc_thermal.c - thermistor thermal management
 *
 * Author:      Feng Hong <hongfeng@marvell.com>
 *		Yi Zhang <yizhang@marvell.com>
 * Copyright:   (C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>

enum trip_points {
	TRIP_0,
	TRIP_1,
	TRIP_2,
	TRIP_3,
	TRIP_POINTS_NUM,
	TRIP_POINTS_ACTIVE_NUM = TRIP_POINTS_NUM - 1,
};

struct uevent_msg_priv {
	int cur_s;
	int last_s;
};

struct gpadc_thermal_device {
	struct thermal_zone_device *therm_adc;
	int temp_adc;
	struct uevent_msg_priv msg_s[TRIP_POINTS_ACTIVE_NUM];
	int hit_trip_cnt[TRIP_POINTS_NUM];
};

static struct gpadc_thermal_device adc_dev;
static int adc_trips_temp[TRIP_POINTS_NUM] = {
	40000, /* TRIP_0 */
	50000, /* TRIP_1 */
	55000, /* TRIP_2 */
	100000, /* TRIP_3, set critical 100C for don't enable it currently */
};

static int hit_trip_status_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	int ret = 0;
	for (i = 0; i < TRIP_POINTS_NUM; i++) {
		ret += sprintf(buf + ret, "trip %d: %d hits\n",
				adc_trips_temp[i],
				adc_dev.hit_trip_cnt[i]);
	}
	return ret;
}
static DEVICE_ATTR(hit_trip_status, 0444, hit_trip_status_get, NULL);

static struct attribute *thermal_attrs[] = {
	&dev_attr_hit_trip_status.attr,
	NULL,
};
static struct attribute_group thermal_attr_grp = {
	.attrs = thermal_attrs,
};

static void gpadc_set_interval(int ms)
{
	(adc_dev.therm_adc)->polling_delay = ms;
}

static int gpadc_sys_get_temp(struct thermal_zone_device *thermal,
		unsigned long *temp)
{
	int ret = 0;
	struct gpadc_thermal_device *t_dev = &adc_dev;
	char *temp_info[3]    = { "TYPE=thsens_adc", "TEMP=10000", NULL };
	int mon_interval;

	/* TODO get temperature */
	/* t_dev->temp_adc = */
	*temp = t_dev->temp_adc * 1000;

	if (t_dev->therm_adc) {
		if (t_dev->temp_adc >= adc_trips_temp[TRIP_2]) {
			t_dev->hit_trip_cnt[TRIP_2]++;
			t_dev->msg_s[TRIP_2].cur_s = 1;
			t_dev->msg_s[TRIP_1].cur_s = 1;
			t_dev->msg_s[TRIP_0].cur_s = 1;
			mon_interval = 2000;
		} else if ((t_dev->temp_adc >= adc_trips_temp[TRIP_1]) &&
				(t_dev->temp_adc < adc_trips_temp[TRIP_2])) {
			t_dev->hit_trip_cnt[TRIP_1]++;
			t_dev->msg_s[TRIP_2].cur_s = 0;
			t_dev->msg_s[TRIP_1].cur_s = 1;
			t_dev->msg_s[TRIP_0].cur_s = 1;
			mon_interval = 3000;
		} else if ((t_dev->temp_adc >= adc_trips_temp[TRIP_0]) &&
				(t_dev->temp_adc < adc_trips_temp[TRIP_1])) {
			t_dev->hit_trip_cnt[TRIP_0]++;
			t_dev->msg_s[TRIP_2].cur_s = 0;
			t_dev->msg_s[TRIP_1].cur_s = 0;
			t_dev->msg_s[TRIP_0].cur_s = 1;
			mon_interval = 4000;
		} else {
			t_dev->msg_s[TRIP_2].cur_s = 0;
			t_dev->msg_s[TRIP_1].cur_s = 0;
			t_dev->msg_s[TRIP_0].cur_s = 0;
			mon_interval = 5000;
		}

		if ((t_dev->msg_s[TRIP_2].cur_s !=
			t_dev->msg_s[TRIP_2].last_s) ||
			(t_dev->msg_s[TRIP_1].cur_s !=
			 t_dev->msg_s[TRIP_1].last_s) ||
			(t_dev->msg_s[TRIP_0].cur_s !=
			 t_dev->msg_s[TRIP_0].last_s)) {
			gpadc_set_interval(mon_interval);
			t_dev->msg_s[TRIP_2].last_s =
				t_dev->msg_s[TRIP_2].cur_s;
			t_dev->msg_s[TRIP_1].last_s =
				t_dev->msg_s[TRIP_1].cur_s;
			t_dev->msg_s[TRIP_0].last_s =
				t_dev->msg_s[TRIP_0].cur_s;
			pr_info("board adc thermal %dC\n",
					t_dev->temp_adc / 1000);
			sprintf(temp_info[1], "TEMP=%d", t_dev->temp_adc);
			/* TODO notify user for trip point cross */
			/*
			kobject_uevent_env(&((t_dev->therm_adc)->
				device.kobj), KOBJ_CHANGE, temp_info);
			*/
		}
	}
	return ret;
}

static int gpadc_sys_get_trip_type(struct thermal_zone_device *thermal,
		int trip, enum thermal_trip_type *type)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_ACTIVE_NUM))
		*type = THERMAL_TRIP_ACTIVE;
	else if (TRIP_POINTS_ACTIVE_NUM == trip)
		*type = THERMAL_TRIP_CRITICAL;
	else
		*type = (enum thermal_trip_type)(-1);
	return 0;
}

static int gpadc_sys_get_trip_temp(struct thermal_zone_device *thermal,
		int trip, unsigned long *temp)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		*temp = adc_trips_temp[trip];
	else
		*temp = -1;
	return 0;
}

static int gpadc_sys_set_trip_temp(struct thermal_zone_device *thermal,
		int trip, unsigned long temp)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		adc_trips_temp[trip] = temp;
	return 0;
}

static int gpadc_sys_get_crit_temp(struct thermal_zone_device *thermal,
		unsigned long *temp)
{
	return adc_trips_temp[TRIP_POINTS_NUM - 1];
}

static struct thermal_zone_device_ops adc_thermal_ops = {
	.get_temp = gpadc_sys_get_temp,
	.get_trip_type = gpadc_sys_get_trip_type,
	.get_trip_temp = gpadc_sys_get_trip_temp,
	.set_trip_temp = gpadc_sys_set_trip_temp,
	.get_crit_temp = gpadc_sys_get_crit_temp,
};

#ifdef CONFIG_PM_SLEEP
static int thermal_suspend(struct device *dev)
{
	return 0;
}

static int thermal_resume(struct device *dev)
{
	return 0;
}

static SIMPLE_DEV_PM_OPS(thermal_pm_ops,
		thermal_suspend, thermal_resume);
#define PXA_TMU_PM      (&thermal_pm_ops)
#else
#define PXA_TMU_PM      NULL
#endif

static int gpadc_register_thermal(void)
{
	/*struct cpumask mask_val;*/
	int i, trip_w_mask = 0;
	int tmp = 0;

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		trip_w_mask |= (1 << i);
	adc_dev.therm_adc = thermal_zone_device_register("thsens_adc",
			TRIP_POINTS_NUM, trip_w_mask, NULL,
			&adc_thermal_ops, NULL, 0, 5000);
	if (IS_ERR(adc_dev.therm_adc)) {
		pr_err("Failed to register board thermal zone device\n");
		return PTR_ERR(adc_dev.therm_adc);
	}

	tmp = sysfs_create_group(&((adc_dev.therm_adc->device).kobj),
			&thermal_attr_grp);
	if (tmp < 0)
		pr_err("Failed to register private adc thermal interface\n");

	return 0;
}

static int gpadc_thermal_probe(struct platform_device *pdev)
{
	/* init thermal framework */
	return gpadc_register_thermal();
}

static int gpadc_thermal_remove(struct platform_device *pdev)
{
	if (adc_dev.therm_adc)
		thermal_zone_device_unregister(adc_dev.therm_adc);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id gpadc_tmu_match[] = {
	{ .compatible = "marvell,gpadc-thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, gpadc_tmu_match);
#endif

static struct platform_driver gpadc_thermal_driver = {
	.driver = {
		.name   = "gpadc-thermal",
		.pm     = PXA_TMU_PM,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(gpadc_tmu_match),
#endif
	},
	.probe = gpadc_thermal_probe,
	.remove = gpadc_thermal_remove,
};
module_platform_driver(gpadc_thermal_driver);

MODULE_AUTHOR("Marvell Semiconductor");
MODULE_DESCRIPTION("GPADC thermal driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpadc-thermal");
