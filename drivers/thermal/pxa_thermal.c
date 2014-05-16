/*
 * pxa_thermal.c - Marvell TMU (Thermal Management Unit)
 *
 * Author:      Liang Chen <chl@marvell.com>
 * Copyright:   (C) 2013 Marvell International Ltd.
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
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/platform_data/pxa_thermal.h>
#include <linux/thermal.h>
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#include <linux/of.h>
#include <mach/addr-map.h>
#include <asm/cputype.h>
#include <linux/clk/dvfs-dvc.h>
#include <linux/cooling_dev_mrvl.h>
#include <linux/cpu.h>
#include <linux/pm_qos.h>

/* debug: Use for sysfs set temp */
/* #define DEBUG_TEMPERATURE */
#define TRIP_POINTS_NUM	4
#define TRIP_POINTS_ACTIVE_NUM (TRIP_POINTS_NUM - 1)

#define THERMAL_VIRT_BASE (APB_VIRT_BASE + 0x13200)
#define THERMAL_REG(x) (THERMAL_VIRT_BASE + (x))
#define THERMAL_TS_CTRL (0x20)
#define THERMAL_TS_READ (0x24)
#define THERMAL_TS_CLR (0x28)
#define THERMAL_TS_THD (0x2c)
#define THERMAL_TS_DUR (0x30)
#define THERMAL_TS_CNT (0x34)

/* TS_CTRL 0x20 */
#define TS_MODE (1 << 31)
#define TS_TIMEOUT_INT_MASK (1 << 30)
#define TS_HW_AUTO_ENABLE (1 << 29)
#define TS_OVER_RANGE_RST_ENABLE (1 << 28)
#define TS_OVER_HIGH_RST_ENABLE (1 << 27)
#define TS_HIGH_INT_MASK (1 << 26)
#define TS_WARNING_MASK (1 << 25)
#define TS_OVER_TEMP_INT_MASK (1 << 19)
#define TS_ON_INT_MASK (1 << 18)
#define TS_CTRL_TSEN_TEMP_ON (1 << 3)
#define TS_CTRL_RST_N_TSEN (1 << 2)
#define TS_CTRL_TSEN_LOW_RANGE (1 << 1)
#define TS_CTRL_TSEN_CHOP_EN (1 << 0)
/* TS_READ 0x24 */
#define TS_RST_DATA (0xf << 12)
#define TS_RST_FLAG (1 << 11)
#define TS_OVER_RANGE (1 << 10)
#define TS_HIGH (1 << 9)
#define TS_WRAINGING (1 << 8)
#define TS_TIMEOUT (1 << 7)
#define TS_READ_TS_ON_STATUS (1 << 6)
#define TS_READ_TS_ON (1 << 4)
#define TS_READ_OUT_DATA (0xf << 0)
/* TS_CLR 0x28 */
#define TS_ON_INT_CLR (1 << 4)
#define TS_OVER_RANGE_INT_CLR (1 << 3)
#define TS_HIGH_INT_CLR (1 << 2)
#define TS_WAINING_INT_CLR (1 << 1)
#define TS_RST_FLAG_CLR (1 << 0)
/* TS_THD 0x2c */
#define TS_HIGH_THD (0xf << 4)
#define TS_WARNING_THD (0xf << 0)
/* TS_CNT 0x30 */
#define TS_RST_CNT (0xff << 8)
#define TS_ON_CNT (0xff << 0)

/* In-kernel thermal framework related macros & definations */
#define SENSOR_NAME_LEN	16
#define MAX_TRIP_COUNT	8
#define MAX_COOLING_DEVICE 4

#define MCELSIUS	1000

/* CPU Zone information */
#define PANIC_ZONE      3
#define WARN_ZONE2       2
#define WARN_ZONE1       1
#define WARN_ZONE0       0

#define PXA_ZONE_COUNT	4

#define AUTO_MODE_1 1
#define AUTO_MODE_2 2

static int cpu_thermal_trips_temp[TRIP_POINTS_NUM] = {
	90,			/* bind to active type */
	100,			/* bind to active type */
	107,			/* bind to active type */
	112,			/* bind to critical type */
};

static int trips_temp[TRIP_POINTS_NUM] = {
	56000, /* TRIP_POINT_0 */
	85000, /* TRIP_POINT_1 */
	96000, /* TRIP_POINT_2 */
	100000, /* TRIP_POINT_3 */
};

static int trips_temp_d[TRIP_POINTS_NUM] = {
	46000, /* TRIP_POINT_0_D */
	56000, /* TRIP_POINT_1_D */
	80000, /* TRIP_POINT_2_D */
	85000, /* TRIP_POINT_3_D */
};

struct pxa_tmu_data {
	struct pxa_tmu_platform_data *pdata;
	struct resource *mem;
	void __iomem *base;
	int irq;
	enum soc_type soc;
	struct work_struct irq_work;
	struct mutex lock;
	struct clk *clk;
	u8 temp_error1, temp_error2;
	int mode;
	int state;
	int old_state;
	int high;
	int temp_cpu;
	int start;
	int level;
};

struct	thermal_trip_point_conf {
	int trip_val[MAX_TRIP_COUNT];
	int trip_count;
	u8 trigger_falling;
};

struct	thermal_cooling_conf {
	struct freq_clip_table freq_data[MAX_TRIP_COUNT];
	int freq_clip_count;
	int bind_trip;
};

struct thermal_sensor_conf {
	char name[SENSOR_NAME_LEN];
	int (*read_temperature)(void *data);
	struct thermal_trip_point_conf trip_data;
	struct thermal_cooling_conf cooling_data;
	void *private_data;
};

struct pxa_thermal_zone {
	enum thermal_device_mode mode;
	struct thermal_zone_device *therm_dev;
	struct thermal_cooling_device *cool_dev[MAX_COOLING_DEVICE];
	unsigned int cool_dev_size;
	struct platform_device *pxa_dev;
	struct thermal_sensor_conf *sensor_conf;
	bool bind;
};

struct cooling_device {
	struct thermal_cooling_device *combile_cool;
	int max_state, cur_state;
	struct thermal_cooling_device *cool_cpufreq;
	struct thermal_cooling_device *cool_cpuhotplug;
};

#define hole_up 80000
#define hole_down 64000

static struct pxa_thermal_zone *th_zone;
static void pxa_unregister_thermal(void);
static int pxa_register_thermal(struct thermal_sensor_conf *sensor_conf);
struct pxa_tmu_data *print_data;
static int gray_decode(unsigned int gray);

/* Get mode callback functions for thermal zone */
static int pxa_get_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode *mode)
{
	if (th_zone)
		*mode = th_zone->mode;
	return 0;
}

/* Set mode callback functions for thermal zone */
static int pxa_set_mode(struct thermal_zone_device *thermal,
			enum thermal_device_mode mode)
{
	if (!th_zone->therm_dev) {
		pr_notice("thermal zone not registered\n");
		return 0;
	}

	th_zone->mode = mode;
	thermal_zone_device_update(th_zone->therm_dev);

	return 0;
}


/* Get trip type callback functions for thermal zone */
static int pxa_get_trip_type(struct thermal_zone_device *thermal, int trip,
				 enum thermal_trip_type *type)
{
	switch (trip) {
	case WARN_ZONE0:
	case WARN_ZONE1:
	case WARN_ZONE2:
		*type = THERMAL_TRIP_ACTIVE;
		break;
	case PANIC_ZONE:
		*type = THERMAL_TRIP_CRITICAL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Get trip temperature callback functions for thermal zone */
static int pxa_get_trip_temp(struct thermal_zone_device *thermal, int trip,
				unsigned long *temp)
{
	if ((trip < 0) || (trip > TRIP_POINTS_NUM))
		return -EINVAL;

	*temp = th_zone->sensor_conf->trip_data.trip_val[trip];
	/* convert the temperature into millicelsius */
	*temp = *temp * MCELSIUS;

	return 0;
}

static int cpu_sys_get_trip_temp_d(struct thermal_zone_device *thermal,
		int trip, unsigned long *temp)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		*temp = trips_temp_d[trip];
	else
		*temp = -1;
	return 0;
}


/* Get critical temperature callback functions for thermal zone */
static int pxa_get_crit_temp(struct thermal_zone_device *thermal,
				unsigned long *temp)
{
	int ret;
	/* Panic zone */
	ret = pxa_get_trip_temp(thermal, PANIC_ZONE, temp);
	return ret;
}

/* Bind callback functions for thermal zone */
static int pxa_bind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, bind_trip;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;

	bind_trip = data->cooling_data.bind_trip;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device.
	 * in pxa1L88 we only bind the 100C as the cooling device.
	 * 100C is trip 1.
	 */
	for (i = 1; i <= bind_trip; i++) {
		switch (i) {
		case WARN_ZONE1:
			if (thermal_zone_bind_cooling_device(thermal, i,
				cdev, THERMAL_NO_LIMIT, THERMAL_NO_LIMIT)) {
				pr_err("error binding cdev inst %d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = true;
			break;
		default:
			ret = -EINVAL;
		}
	}

	return ret;
}

/* Unbind callback functions for thermal zone */
static int pxa_unbind(struct thermal_zone_device *thermal,
			struct thermal_cooling_device *cdev)
{
	int ret = 0, i, bind_trip;
	struct thermal_sensor_conf *data = th_zone->sensor_conf;

	if (!th_zone->bind)
		return 0;

	bind_trip = data->cooling_data.bind_trip;

	/* find the cooling device registered*/
	for (i = 0; i < th_zone->cool_dev_size; i++)
		if (cdev == th_zone->cool_dev[i])
			break;

	/* No matching cooling device */
	if (i == th_zone->cool_dev_size)
		return 0;

	/* Bind the thermal zone to the cpufreq cooling device */
	for (i = 1; i <= bind_trip; i++) {
		switch (i) {
		case WARN_ZONE1:
			if (thermal_zone_unbind_cooling_device(thermal, i,
								cdev)) {
				pr_err("error unbinding cdev inst=%d\n", i);
				ret = -EINVAL;
			}
			th_zone->bind = false;
			break;
		default:
			ret = -EINVAL;
		}
	}
	return ret;
}

void set_high_temperature(void)
{
	unsigned long ts_ctrl, ts_thd, temp;
	/* we only care greater than 80 */
	ts_ctrl = readl(print_data->base + THERMAL_TS_CTRL);
	ts_ctrl &= ~TS_CTRL_TSEN_LOW_RANGE;
	ts_ctrl |= TS_OVER_RANGE_RST_ENABLE;
	/*
	 * init, only set warning interrupt, if temp hit warning, then
	 * enable data ready interrupt, then if temp drops to 80 degree,
	 * disable data ready interrupt
	 */
	ts_ctrl |= TS_WARNING_MASK;

	/* set high threshold 110 degree */
	ts_thd = 0x0c;
	temp = readl(print_data->base + THERMAL_TS_THD);
	temp &= ~TS_HIGH_THD;
	temp |= (0x0c << 4);
	writel(temp, print_data->base + THERMAL_TS_THD);

	ts_ctrl |= (TS_CTRL_RST_N_TSEN | TS_CTRL_TSEN_TEMP_ON);
	writel(ts_ctrl, print_data->base + THERMAL_TS_CTRL);
	print_data->high = 1;
	print_data->temp_cpu = 80 * 1000;

}

void set_low_temperature(void)
{
	unsigned long ts_ctrl;
	ts_ctrl = readl(print_data->base + THERMAL_TS_CTRL);

	ts_ctrl |= TS_CTRL_TSEN_LOW_RANGE;
	ts_ctrl |= (TS_CTRL_RST_N_TSEN | TS_CTRL_TSEN_TEMP_ON);

	writel(ts_ctrl, print_data->base + THERMAL_TS_CTRL);

	print_data->high = 0;
	print_data->temp_cpu = 64 * 1000;

}

void thermal_stop(void)
{
	unsigned long ts_ctrl;
	ts_ctrl = readl(print_data->base + THERMAL_TS_CTRL);

	ts_ctrl &= ~TS_CTRL_RST_N_TSEN;
	ts_ctrl &= ~TS_CTRL_TSEN_TEMP_ON;
	writel(ts_ctrl, print_data->base + THERMAL_TS_CTRL);

}

int thermal_level(int level)
{
	int temp = print_data->temp_cpu;
	switch (level) {
	case 0:
		if (temp >= trips_temp[level])
			level++;
		break;
	case 1:
		if (temp >= trips_temp[level])
			level++;
		else if (temp <= trips_temp_d[level-1])
			level--;
		break;
	case 2:
		if (temp >= trips_temp[level])
			level++;
		else if (temp <= trips_temp_d[level-1])
			level--;
		break;
	case 3:
		if (temp >= trips_temp[level])
			level++;
		else if (temp <= trips_temp_d[level-1])
			level--;
		break;
	case 4:
		if (temp <= trips_temp_d[level-1])
			level--;
		break;
	}

	if (th_zone->therm_dev != NULL) {
		if (level == 0)
			th_zone->therm_dev->polling_delay = 2000;
		else if (level == 1)
			th_zone->therm_dev->polling_delay = 1000;
		else if (level > 1)
			th_zone->therm_dev->polling_delay = 500;
	}

	return level;
}

void thermal_cooling_handle(int level)
{
	char *temp_info[3] = { "TYPE=thsens_cpu", "LEVEL=100000", NULL };
	if (level != print_data->level) {
		if (th_zone->therm_dev != NULL) {
				sprintf(temp_info[1], "LEVEL=%d", level);
				kobject_uevent_env
					(&th_zone->therm_dev->device.kobj,
					KOBJ_CHANGE, temp_info);
		}
	}
	print_data->level = level;
}

#define RETRY_TIMES (2)
int count;
/* Get temperature callback functions for thermal zone */
static int pxa_get_temp(struct thermal_zone_device *thermal,
			unsigned long *temp)
{
	if (!is_1p5G_chip) {
		void *data;

		if (!th_zone->sensor_conf) {
			pr_info("Temperature sensor not initialised\n");
			return -EINVAL;
		}
		data = th_zone->sensor_conf->private_data;
		*temp = th_zone->sensor_conf->read_temperature(data);
		/* convert the temperature into millicelsius */
		*temp = *temp * MCELSIUS;
	} else if (is_1p5G_chip) {
		unsigned long ts_read;
		int gray_code = 0, interval_temp = 0;
		int threshold = 80;
		int level;

		ts_read = readl(print_data->base + THERMAL_TS_READ);
		gray_code = ts_read & TS_READ_OUT_DATA;
		if (print_data->high == 1) {
			threshold = 80;
			print_data->temp_cpu =
			(gray_decode(gray_code) * 5 / 2 + threshold) * 1000;
		} else {
			threshold = 29;
			print_data->temp_cpu =
			(gray_decode(gray_code) * 5 / 2 + threshold) * 1000;
		}

		interval_temp = print_data->temp_cpu;
		if (print_data->high && (interval_temp <= hole_up))
			count++;
		else if ((!print_data->high) && (interval_temp >= hole_down))
			count++;
		else
			count = 0;

		if (count > RETRY_TIMES) {
			if (print_data->high) {
				thermal_stop();
				set_low_temperature();
			} else {
				thermal_stop();
				set_high_temperature();
			}
			count = 0;
		}

		if (print_data->start)
			level = thermal_level(print_data->level);

		*temp = print_data->temp_cpu;
	}

	return 0;
}

void thermal_control_1p5G(void)
{
	long temp, i;

	print_data->start = 0;

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		pxa_get_temp(th_zone->therm_dev, &temp);

	temp = print_data->temp_cpu;

	for (i = 0; i < TRIP_POINTS_NUM;)
		if (temp >= trips_temp[i])
			i++;
		else
			break;
	if (trips_temp[i] >= hole_up) {
		thermal_stop();
		set_high_temperature();
	} else if (trips_temp[i] <= hole_down) {
		thermal_stop();
		set_low_temperature();
	}

	print_data->level = i;
	print_data->start = 1;

}


/* Operation callback functions for thermal zone */
static struct thermal_zone_device_ops const pxa_dev_ops = {
	.bind = pxa_bind,
	.unbind = pxa_unbind,
	.get_temp = pxa_get_temp,
	.get_mode = pxa_get_mode,
	.set_mode = pxa_set_mode,
	.get_trip_temp_d = cpu_sys_get_trip_temp_d,
	.get_trip_type = pxa_get_trip_type,
	.get_trip_temp = pxa_get_trip_temp,
	.get_crit_temp = pxa_get_crit_temp,
};

#ifdef DEBUG_TEMPERATURE
static int g_test_temp = 92;

static int thermal_temp_debug_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", g_test_temp);
}

static int thermal_temp_debug_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long ts_ctrl;
	if (1 != sscanf(buf, "%d\n", &g_test_temp))
		pr_err("get debug temp err\n");

	ts_ctrl = readl(print_data->base + THERMAL_TS_CTRL);
	ts_ctrl |= TS_ON_INT_MASK;
	writel(ts_ctrl, print_data->base + THERMAL_TS_CTRL);

	return count;
}

static DEVICE_ATTR(thermal_debug_temp, 0644, thermal_temp_debug_get,
		thermal_temp_debug_set);

static struct attribute *thermal_attrs[] = {
	&dev_attr_thermal_debug_temp.attr,
	NULL,
};

static struct attribute_group thermal_attr_grp = {
	.attrs = thermal_attrs,
};
#endif

void set_durtime(struct pxa_tmu_data *data, int durtime)
{
	/* durtime is 2, use 16k clock, ts_dur is 1s */
	unsigned long ts_dur;
	ts_dur = 16000 * durtime;
	writel(ts_dur, data->base + THERMAL_TS_DUR);

	return;
}

/*
 * This function may be called from interrupt based temperature sensor
 * when threshold is changed.
 */
static void pxa_report_trigger(struct pxa_tmu_data *data)
{
	unsigned int i;
	int max, min;
	char *temp_info[3] = { "TYPE=thsens_cpu", "TEMP=100000", NULL };
	if (!th_zone || !th_zone->therm_dev)
		return;

	thermal_zone_device_update(th_zone->therm_dev);

	mutex_lock(&th_zone->therm_dev->lock);

	/* Find the level for which trip happened */
	for (i = 0; i < th_zone->sensor_conf->trip_data.trip_count; i++) {
		if (th_zone->therm_dev->last_temperature <
			th_zone->sensor_conf->trip_data.trip_val[i] * MCELSIUS)
			break;
	}

	data->state = i;

	if (data->state == data->old_state)
		goto out;

	max = max(data->state, data->old_state);
	min = min(data->state, data->old_state);

	for (i = min; i < max; i++) {
		sprintf(temp_info[1], "TEMP=%d",
					th_zone->therm_dev->last_temperature);
		kobject_uevent_env(&th_zone->therm_dev->device.kobj,
					KOBJ_CHANGE, temp_info);
	}

	data->old_state = data->state;
	/* set durtime, >85C 2s irq occur, >90C 1s irq, >100C 0.5s irq*/
	switch (data->state) {
	case 0:
		set_durtime(data, 4);
		break;
	case 1:
		set_durtime(data, 2);
		break;
	case 2:
	case 3:
		set_durtime(data, 1);
		break;
	}
out:
	mutex_unlock(&th_zone->therm_dev->lock);
	return;
}

/* Register with the in-kernel thermal management */
static int pxa_register_thermal(struct thermal_sensor_conf *sensor_conf)
{
	int ret;
	struct cpumask mask_val;

	if (!sensor_conf || !sensor_conf->read_temperature) {
		pr_err("Temperature sensor not initialised\n");
		return -EINVAL;
	}

	th_zone = kzalloc(sizeof(struct pxa_thermal_zone), GFP_KERNEL);
	if (!th_zone)
		return -ENOMEM;

	th_zone->sensor_conf = sensor_conf;
	if (!is_1p5G_chip) {
		cpumask_set_cpu(0, &mask_val);
		th_zone->cool_dev[0] = cpufreq_cooling_register(&mask_val);
		if (IS_ERR(th_zone->cool_dev[0])) {
			pr_err("Failed to register cpufreq cooling device\n");
			ret = -EINVAL;
			goto err_unregister;
		}
		th_zone->cool_dev_size++;

		th_zone->therm_dev = thermal_zone_device_register("thsens_cpu",
		PXA_ZONE_COUNT, 0, NULL, &pxa_dev_ops, NULL, 0, 0);
	} else {
		th_zone->therm_dev = thermal_zone_device_register("thsens_cpu",
			PXA_ZONE_COUNT, 0, NULL, &pxa_dev_ops, NULL, 0, 2000);
	}

	if (IS_ERR(th_zone->therm_dev)) {
		pr_err("Failed to register thermal zone device\n");
		ret = PTR_ERR(th_zone->therm_dev);
		goto err_unregister;
	}
	th_zone->mode = THERMAL_DEVICE_ENABLED;

	pr_info("Pxa: Kernel Thermal management registered\n");

	return 0;

err_unregister:
	pxa_unregister_thermal();
	return ret;
}

/* Un-Register with the in-kernel thermal management */
static void pxa_unregister_thermal(void)
{
	int i;

	if (!th_zone)
		return;

	if (th_zone->therm_dev)
		thermal_zone_device_unregister(th_zone->therm_dev);

	for (i = 0; i < th_zone->cool_dev_size; i++) {
		if (th_zone->cool_dev[i])
			cpufreq_cooling_unregister(th_zone->cool_dev[i]);
	}

	kfree(th_zone);
	pr_info("Pxa: Kernel Thermal management unregistered\n");
}

/* This function decode 4bit long number of gray code into original binary */
static int gray_decode(unsigned int gray)
{
	int num, i, tmp;

	if (gray >= 16)
		return 0;

	num = gray & 0x8;
	tmp = num >> 3;
	for (i = 2; i >= 0; i--) {
		tmp = ((gray & (1 << i)) >> i) ^ tmp;
		num |= tmp << i;
	}
	return num;
}

/*
 * Calculate a temperature value from a temperature code.
 * The unit of the temperature is degree Celsius.
 */
static int code_to_temp(struct pxa_tmu_data *data, u8 temp_code)
{
	int temp = 0;

	/* temp_code should range between 75 and 175 */
	if (temp_code == 0) {
		temp = -ENODATA;
		goto out;
	}

	temp = (gray_decode(temp_code)) * 5 / 2 + 80;
out:
	if (temp <= 0)
		temp = 80;
	return temp;
}


static int pxa_tmu_read(struct pxa_tmu_data *data)
{
	unsigned long temp_code, ts_ctrl;
	int temp = 0, gray_code = 0;

#ifdef DEBUG_TEMPERATURE
	temp = g_test_temp;
#else
	mutex_lock(&data->lock);
	temp_code = readl(data->base + THERMAL_TS_READ);

	if (AUTO_MODE_1 == data->mode) {
		if (temp_code & TS_READ_TS_ON) {
			gray_code = temp_code & TS_READ_OUT_DATA;
			temp = code_to_temp(data, gray_code);

			if (0 == gray_code) {
				ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
				ts_ctrl &= ~TS_ON_INT_MASK;
				writel(ts_ctrl, data->base + THERMAL_TS_CTRL);
			}
		}
	} else if (AUTO_MODE_2 == data->mode) {
		gray_code = ((temp_code >> 16) & 0xf);
		temp = code_to_temp(data, gray_code);
	}
	mutex_unlock(&data->lock);
#endif
	return temp;
}

static int pxa_tmu_initialize(struct platform_device *pdev)
{
	struct pxa_tmu_data *data = platform_get_drvdata(pdev);
	int ret = 0, threshold_code;
	unsigned long ts_ctrl, ts_read, ts_clr, ts_thd, temp;
	mutex_lock(&data->lock);

	ts_read = readl(data->base + THERMAL_TS_READ);
	if (ts_read & TS_RST_FLAG) {
		ts_clr = readl(data->base + THERMAL_TS_CTRL);
		ts_clr |= TS_RST_FLAG_CLR;
		writel(ts_clr, data->base + THERMAL_TS_CTRL);
		pr_crit("system reboot with temperature 0x%x",
			(unsigned int)ts_read);
	}
	ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
	ts_ctrl |= TS_CTRL_TSEN_CHOP_EN;
	writel(ts_ctrl, data->base + THERMAL_TS_CTRL);

	if (AUTO_MODE_1 == data->mode) {
		ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
		ts_ctrl |= TS_HW_AUTO_ENABLE;
		writel(ts_ctrl, data->base + THERMAL_TS_CTRL);
	} else if (AUTO_MODE_2 == data->mode) {
		ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
		ts_ctrl |= (TS_MODE | TS_HW_AUTO_ENABLE);
		writel(ts_ctrl, data->base + THERMAL_TS_CTRL);
	}

	/* we only care greater than 80 */
	ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
	ts_ctrl &= ~TS_CTRL_TSEN_LOW_RANGE;
	ts_ctrl |= TS_OVER_RANGE_RST_ENABLE;
	/*
	 * init, only set warning interrupt, if temp hit warning, then
	 * enable data ready interrupt, then if temp drops to 80 degree,
	 * disable data ready interrupt
	 */
	ts_ctrl |= TS_WARNING_MASK;

	/* auto mode 2, must be set the TS_HIGH_INT_MASK bit*/
	if (AUTO_MODE_2 == data->mode)
		ts_ctrl |= TS_HIGH_INT_MASK;
	writel(ts_ctrl, data->base + THERMAL_TS_CTRL);

	/* 32k clock --> 2s, 16k*4 */
	set_durtime(data, 4);

	/* set warning threshold 85 degree */
	threshold_code = 0x2;
	writel(threshold_code, data->base + THERMAL_TS_THD);

	/* set high threshold 110 degree */
	ts_thd = 0x0c;
	temp = readl(data->base + THERMAL_TS_THD);
	temp &= ~TS_HIGH_THD;
	temp |= (0x0c << 4);
	writel(temp, data->base + THERMAL_TS_THD);

	/* auto mode 2, set TS_RST_CNT and TS_ON_CNT to 32 */
	if (AUTO_MODE_2 == data->mode) {
		temp = ((0x20 << 8) | 0x20);
		writel(temp, data->base + THERMAL_TS_CNT);
	}
	mutex_unlock(&data->lock);

	return ret;
}

static void pxa_tmu_control(struct platform_device *pdev, bool on)
{
	struct pxa_tmu_data *data = platform_get_drvdata(pdev);
	unsigned long ts_ctrl;
	mutex_lock(&data->lock);

	/* start measure */
	ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
	if (on) {
		/* start measure */
		if (AUTO_MODE_1 == data->mode) {
			ts_ctrl |= (TS_ON_INT_MASK);
			ts_ctrl |= (TS_CTRL_RST_N_TSEN | TS_CTRL_TSEN_TEMP_ON);
		} else if (AUTO_MODE_2 == data->mode)
			ts_ctrl |= (TS_MODE | TS_HW_AUTO_ENABLE);
	} else {
		if (AUTO_MODE_1 == data->mode)
			ts_ctrl &= ~TS_CTRL_RST_N_TSEN;
		else if (AUTO_MODE_2 == data->mode) {
			ts_ctrl &= ~TS_MODE;
			ts_ctrl &= ~TS_HW_AUTO_ENABLE;
		}
	}
	writel(ts_ctrl, data->base + THERMAL_TS_CTRL);

	mutex_unlock(&data->lock);
}


static void pxa_tmu_work(struct work_struct *work)
{
	struct pxa_tmu_data *data = container_of(work,
			struct pxa_tmu_data, irq_work);
	unsigned long ts_clr, ts_ctrl, ts_read;

	pxa_report_trigger(data);

	mutex_lock(&data->lock);
	/* hit 85 degree,over range, timeout, enable data ready interrput */
	ts_read = readl(data->base + THERMAL_TS_READ);
	if (ts_read & (TS_WRAINGING | TS_HIGH | TS_OVER_RANGE)) {
		ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
		ts_ctrl |= TS_ON_INT_MASK;
		writel(ts_ctrl, data->base + THERMAL_TS_CTRL);
	} else if (ts_read & (TS_TIMEOUT)) {
		ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
		ts_ctrl &= ~TS_ON_INT_MASK;
		writel(ts_ctrl, data->base + THERMAL_TS_CTRL);
	}

	ts_clr = readl(data->base + THERMAL_TS_CLR);
	ts_clr |= TS_ON_INT_CLR | TS_OVER_RANGE_INT_CLR |
		TS_HIGH_INT_CLR | TS_WAINING_INT_CLR;
	writel(ts_clr, data->base + THERMAL_TS_CLR);

#ifdef DEBUG_TEMPERATURE
	ts_ctrl = readl(data->base + THERMAL_TS_CTRL);
	ts_ctrl |= TS_ON_INT_MASK;
	writel(ts_ctrl, data->base + THERMAL_TS_CTRL);
#endif
	mutex_unlock(&data->lock);

	enable_irq(data->irq);
}

static irqreturn_t pxa_tmu_irq(int irq, void *id)
{
	struct pxa_tmu_data *data = id;

	disable_irq_nosync(irq);
	schedule_work(&data->irq_work);

	return IRQ_HANDLED;
}

static struct thermal_sensor_conf pxa_sensor_conf = {
	.name			= "pxa-thermal",
	.read_temperature	= (int (*)(void *))pxa_tmu_read,
};


static int get_thermal_platdata(struct device_node *np,
					struct pxa_tmu_platform_data *pdata)
{
	const __be32 *trigger_levels_count, *type;

	trigger_levels_count = of_get_property
				(np, "trigger_levels_count", NULL);
	if (trigger_levels_count)
		pdata->trigger_levels_count =
				be32_to_cpu(*trigger_levels_count);
	else
		pr_err(" thermal trigger_levels_count out is NULL\n");

	type = of_get_property(np, "type", NULL);
	if (type)
		pdata->type = be32_to_cpu(*type);
	else
		pr_err("thermal type out is NULL\n");

	return 0;
}


#ifdef CONFIG_OF
static const struct of_device_id pxa_tmu_match[] = {
	{
		.compatible = "marvell,pxa-thermal", .data = NULL},
	{},
};
MODULE_DEVICE_TABLE(of, pxa_tmu_match);
#endif


static int pxa_tmu_probe(struct platform_device *pdev)
{
	struct pxa_tmu_data *data;
	struct pxa_tmu_platform_data *pdata;
	int ret, i;
	struct device_node *np = pdev->dev.of_node;
	pdata = devm_kzalloc(&pdev->dev, sizeof(struct pxa_tmu_platform_data),
					GFP_KERNEL);
	if (!pdata) {
		dev_err(&pdev->dev, "Failed to allocate pdata structure\n");
		return -ENOMEM;
	}

	if ((np != NULL) && (pdata != NULL))
		get_thermal_platdata(np, pdata);

	data = devm_kzalloc(&pdev->dev, sizeof(struct pxa_tmu_data),
					GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Failed to allocate data structure\n");
		return -ENOMEM;
	}

	data->irq = platform_get_irq(pdev, 0);
	if (data->irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq\n");
		return data->irq;
	}

	data->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data->base = devm_ioremap_resource(&pdev->dev, data->mem);
	if (IS_ERR(data->base))
		return PTR_ERR(data->base);

	INIT_WORK(&data->irq_work, pxa_tmu_work);

	/* use the IRQF_DISABLED as the irq irqflags */
	ret = devm_request_irq(&pdev->dev, data->irq, pxa_tmu_irq,
		IRQF_DISABLED, "pxa-thermal", data);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n", data->irq);
		return ret;
	}

	data->clk = clk_get(NULL, "THERMALCLK");
	if (IS_ERR(data->clk)) {
		dev_err(&pdev->dev, "Failed to get clock\n");
		return  PTR_ERR(data->clk);
	}

	data->soc = pdata->type;

	data->pdata = pdata;
	platform_set_drvdata(pdev, data);
	mutex_init(&data->lock);

	/* pxa1L88 support auto mode 1 and auto mode 2
	 * mode =1 is auto mode 1
	 * mode =2 is auto mode 2
	 * default set mode =2
	 */
	if (data->soc == SOC_ARCH_PXA1L88)
		data->mode = AUTO_MODE_2;
	print_data = data;
	clk_prepare_enable(data->clk);

	if (!is_1p5G_chip) {
		ret = pxa_tmu_initialize(pdev);
		if (ret) {
			dev_err(&pdev->dev, "Failed to initialize TMU\n");
			goto err_clk;
		}

		pxa_tmu_control(pdev, true);
	}

	/* Register the sensor with thermal management interface */
	(&pxa_sensor_conf)->private_data = data;
	/* bind 100C trip 1 as cooling device  */
	pxa_sensor_conf.cooling_data.bind_trip = 1;
	pxa_sensor_conf.trip_data.trip_count = pdata->trigger_levels_count;

	for (i = 0; i < pxa_sensor_conf.trip_data.trip_count; i++)
		pxa_sensor_conf.trip_data.trip_val[i] =
			cpu_thermal_trips_temp[i];

	ret = pxa_register_thermal(&pxa_sensor_conf);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register thermal interface\n");
		goto err_clk;
	}

	if (is_1p5G_chip) {
		thermal_stop();
		set_low_temperature();
		thermal_control_1p5G();
	}

#ifdef DEBUG_TEMPERATURE
	ret = sysfs_create_group(&th_zone->therm_dev->device.kobj,
			&thermal_attr_grp);
#endif

	return 0;

err_clk:
	platform_set_drvdata(pdev, NULL);
	clk_put(data->clk);
	return ret;
}

static int pxa_tmu_remove(struct platform_device *pdev)
{
	struct pxa_tmu_data *data = platform_get_drvdata(pdev);

	pxa_tmu_control(pdev, false);

	pxa_unregister_thermal();

	clk_put(data->clk);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int pxa_tmu_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa_tmu_data *data = platform_get_drvdata(pdev);

	pxa_tmu_control(pdev, false);
	clk_disable_unprepare(data->clk);
	return 0;
}

static int pxa_tmu_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa_tmu_data *data = platform_get_drvdata(pdev);

	clk_prepare_enable(data->clk);
	pxa_tmu_control(pdev, true);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pxa_tmu_pm,
			 pxa_tmu_suspend, pxa_tmu_resume);
#define PXA_TMU_PM	(&pxa_tmu_pm)
#else
#define PXA_TMU_PM	NULL
#endif

static struct platform_driver pxa_tmu_driver = {
	.driver = {
		.name   = "pxa-thermal",
		.pm     = PXA_TMU_PM,
		.of_match_table = of_match_ptr(pxa_tmu_match),
	},
	.probe = pxa_tmu_probe,
	.remove	= pxa_tmu_remove,
};

module_platform_driver(pxa_tmu_driver);

MODULE_DESCRIPTION("PXA TMU Driver");
MODULE_AUTHOR("Liang Chen <chl@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa-tmu");
