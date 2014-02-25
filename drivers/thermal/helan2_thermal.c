/*
 * pxa28nm_thermal.c - Marvell 28nm TMU (Thermal Management Unit)
 *
 * Author:      Feng Hong <hongfeng@marvell.com>
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
#include <linux/of.h>
#ifdef CONFIG_CPU_FREQ
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#endif

#define TSEN_PCTRL (0x0)
#define TSEN_LCTRL (0x4)
#define TSEN_PSTATUS (0x8)
#define TSEN_LSTATUS (0xC)
#define TSEN_RSTATUS (0x10)
#define TSEN_THD01 (0x14)
#define TSEN_THD23 (0x18)

/* TSEN_PCTRL */
#define TSEN_ISO_EN (1 << 3)
#define TSEN_EN (1 << 2)
#define TSEN_START (1 << 1)
#define TSEN_RESET (1 << 0)
/* TSEN_LCTRL */
#define TSEN_AUTO_INTERVAL_OFF (16)
#define TSEN_AUTO_INTERVAL_MASK (0xffff0000)
#define TSEN_RDY_INT_ENABLE (1 << 11)
#define TSEN_WDT_DIRECTION (1 << 9)
#define TSEN_WDT_ENABLE (1 << 8)
#define TSEN_AUTO_MODE_OFF (0)
#define TSEN_AUTO_MODE_MASK (0x3)
/* TSEN_LSTATUS */
#define TSEN_INT2 (1 << 15)
#define TSEN_INT1 (1 << 14)
#define TSEN_INT0 (1 << 13)
#define TSEN_RDY_INT (1 << 12)
#define TSEN_DATA_LATCHED_OFF (0)
#define TSEN_DATA_LATCHED_MASK (0xfff)
/* TSEN_RSTATUS */
#define TSEN_WDT_FLAG (1 << 12)
#define TSEN_DATA_WDT_OFF (0)
#define TSEN_DATA_WDT_MASK (0xfff)
/* TSEN_THD23 */
#define TSEN_WDT_THD_OFF (12)
#define TSEN_WDT_THD_MASK (0xfff000)

#define reg_read(off) readl(pxa28nm_thermal_dev.base + (off))
#define reg_write(val, off) writel((val), pxa28nm_thermal_dev.base + (off))
#define reg_clr_set(off, clr, set) \
	reg_write(((reg_read(off) & ~(clr)) | (set)), off)

enum trip_points {
	APP_WARNING,
	KERNEL_WARNING,
	APP_REBOOT,
	KERNEL_REBOOT,
	TRIP_POINTS_NUM,
	TRIP_POINTS_ACTIVE_NUM = TRIP_POINTS_NUM - 1,
};

struct uevent_msg_priv {
	int cur_s;
	int last_s;
};

struct pxa28nm_thermal_device {
	struct thermal_zone_device *therm_cpu;
	int temp_cpu;
	struct resource *mem;
	void __iomem *base;
	struct clk *therm_clk;
	struct thermal_cooling_device *cool_cpufreq;
	struct uevent_msg_priv msg_s[TRIP_POINTS_ACTIVE_NUM];
	int hit_trip_cnt[TRIP_POINTS_NUM];
	int irq;
};

static struct pxa28nm_thermal_device pxa28nm_thermal_dev;
static int cpu_thermal_trips_temp[TRIP_POINTS_NUM] = {
	90000, /* APP_WARNING */
	100000, /* KERNEL_WARNING */
	107000, /* APP_REBOOT */
	112000, /* KERNEL_REBOOT */
};

#define THSEN_GAIN      3874
#define THSEN_OFFSET    2821

static int millicelsius_decode(u32 tcode)
{
	int cels;
	cels = (tcode * THSEN_GAIN - THSEN_OFFSET * 1000) / 10000 + 1;
	return cels * 1000;
}

static int millicelsius_encode(int mcels)
{
	u32 tcode;
	mcels /= 1000;
	tcode = (mcels * 10 + THSEN_OFFSET) * 1000 / (THSEN_GAIN);
	return tcode;
}

static int hit_trip_status_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	int ret = 0;
	for (i = 0; i < TRIP_POINTS_NUM; i++) {
		ret += sprintf(buf + ret, "trip %d: %d hits\n",
				cpu_thermal_trips_temp[i],
				pxa28nm_thermal_dev.hit_trip_cnt[i]);
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

static int cpu_sys_get_temp(struct thermal_zone_device *thermal,
		unsigned long *temp)
{
	int ret = 0;

	*temp = pxa28nm_thermal_dev.temp_cpu;
	return ret;
}

static int cpu_sys_get_trip_type(struct thermal_zone_device *thermal, int trip,
		enum thermal_trip_type *type)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_ACTIVE_NUM))
		*type = THERMAL_TRIP_ACTIVE;
	else if (TRIP_POINTS_ACTIVE_NUM == trip)
		*type = THERMAL_TRIP_CRITICAL;
	else
		*type = (enum thermal_trip_type)(-1);
	return 0;
}

static int cpu_sys_get_trip_temp(struct thermal_zone_device *thermal, int trip,
		unsigned long *temp)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		*temp = cpu_thermal_trips_temp[trip];
	else
		*temp = -1;
	return 0;
}

static int cpu_sys_set_trip_temp(struct thermal_zone_device *thermal, int trip,
		unsigned long temp)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		cpu_thermal_trips_temp[trip] = temp;
	return 0;
}

static int cpu_sys_get_crit_temp(struct thermal_zone_device *thermal,
		unsigned long *temp)
{
	return cpu_thermal_trips_temp[TRIP_POINTS_NUM - 1];
}

static struct thermal_zone_device_ops cpu_thermal_ops = {
	.get_temp = cpu_sys_get_temp,
	.get_trip_type = cpu_sys_get_trip_type,
	.get_trip_temp = cpu_sys_get_trip_temp,
	.set_trip_temp = cpu_sys_set_trip_temp,
	.get_crit_temp = cpu_sys_get_crit_temp,
};

#ifdef CONFIG_PM_SLEEP
static int thermal_suspend(struct device *dev)
{
	/* DE confirmed, enough for avoid leakage */
	reg_clr_set(TSEN_PCTRL, 0, TSEN_RESET);
	return 0;
}

static int thermal_resume(struct device *dev)
{
	reg_clr_set(TSEN_PCTRL, TSEN_RESET, 0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(thermal_pm_ops,
		thermal_suspend, thermal_resume);
#define PXA_TMU_PM      (&thermal_pm_ops)
#else
#define PXA_TMU_PM      NULL
#endif

static void pxa28nm_register_thermal(void)
{
#ifdef CONFIG_CPU_FREQ
	struct cpumask mask_val;
#endif
	int i, trip_w_mask = 0;
#ifdef CONFIG_CPU_FREQ
	/* register cooling and thermal device */
	cpumask_set_cpu(0, &mask_val);
	pxa28nm_thermal_dev.cool_cpufreq = cpufreq_cooling_register(&mask_val);
#endif
	for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++)
		trip_w_mask |= (1 << i);
	pxa28nm_thermal_dev.therm_cpu = thermal_zone_device_register(
			"thsens_cpu", TRIP_POINTS_NUM, trip_w_mask, NULL,
			&cpu_thermal_ops, NULL, 0, 0);
#ifdef CONFIG_CPU_FREQ
	/* bind cpufreq cooling */
	thermal_zone_bind_cooling_device(pxa28nm_thermal_dev.therm_cpu,
			KERNEL_WARNING, pxa28nm_thermal_dev.cool_cpufreq,
			THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);
#endif

	i = sysfs_create_group(&((pxa28nm_thermal_dev.therm_cpu->device).kobj),
			&thermal_attr_grp);
	if (i < 0)
		pr_err("Failed to register private thermal interface\n");
}

static void pxa28nm_set_interval(int ms)
{
	/* 500k clock, high 16bit */
	int interval_val = ms * 500 / 256;
	reg_clr_set(TSEN_LCTRL, TSEN_AUTO_INTERVAL_MASK,
	(interval_val << TSEN_AUTO_INTERVAL_OFF) & TSEN_AUTO_INTERVAL_MASK);
}

static irqreturn_t pxa28nm_thread_irq(int irq, void *devid)
{
	struct pxa28nm_thermal_device *t_dev = &pxa28nm_thermal_dev;
	char *temp_info[3]    = { "TYPE=thsens_cpu", "TEMP=100000", NULL };
	int mon_interval;

	if (t_dev->therm_cpu) {
		if (t_dev->temp_cpu >= cpu_thermal_trips_temp[APP_REBOOT]) {
			t_dev->hit_trip_cnt[APP_REBOOT]++;
			t_dev->msg_s[APP_REBOOT].cur_s = 1;
			t_dev->msg_s[KERNEL_WARNING].cur_s = 1;
			t_dev->msg_s[APP_WARNING].cur_s = 1;
			mon_interval = 200;
		} else if ((t_dev->temp_cpu >=
				cpu_thermal_trips_temp[KERNEL_WARNING]) &&
				(t_dev->temp_cpu <
				 cpu_thermal_trips_temp[APP_REBOOT])) {
			t_dev->hit_trip_cnt[KERNEL_WARNING]++;
			t_dev->msg_s[APP_REBOOT].cur_s = 0;
			t_dev->msg_s[KERNEL_WARNING].cur_s = 1;
			t_dev->msg_s[APP_WARNING].cur_s = 1;
			mon_interval = 500;
		} else if ((t_dev->temp_cpu >=
				cpu_thermal_trips_temp[APP_WARNING]) &&
				(t_dev->temp_cpu <
				 cpu_thermal_trips_temp[KERNEL_WARNING])) {
			t_dev->hit_trip_cnt[APP_WARNING]++;
			t_dev->msg_s[APP_REBOOT].cur_s = 0;
			t_dev->msg_s[KERNEL_WARNING].cur_s = 0;
			t_dev->msg_s[APP_WARNING].cur_s = 1;
			mon_interval = 1000;
		} else {
			t_dev->msg_s[APP_REBOOT].cur_s = 0;
			t_dev->msg_s[KERNEL_WARNING].cur_s = 0;
			t_dev->msg_s[APP_WARNING].cur_s = 0;
			mon_interval = 2000;
		}

		if ((t_dev->msg_s[APP_REBOOT].cur_s !=
			t_dev->msg_s[APP_REBOOT].last_s) ||
			(t_dev->msg_s[KERNEL_WARNING].cur_s !=
			 t_dev->msg_s[KERNEL_WARNING].last_s) ||
			(t_dev->msg_s[APP_WARNING].cur_s !=
			 t_dev->msg_s[APP_WARNING].last_s)) {
			pxa28nm_set_interval(mon_interval);
			t_dev->msg_s[APP_REBOOT].last_s =
				t_dev->msg_s[APP_REBOOT].cur_s;
			t_dev->msg_s[KERNEL_WARNING].last_s =
				t_dev->msg_s[KERNEL_WARNING].cur_s;
			t_dev->msg_s[APP_WARNING].last_s =
				t_dev->msg_s[APP_WARNING].cur_s;
			pr_info("SoC thermal %dC\n", t_dev->temp_cpu);
			/* notify user for trip point cross */
			sprintf(temp_info[1], "TEMP=%d", t_dev->temp_cpu);
			kobject_uevent_env(&((t_dev->therm_cpu)->
					device.kobj), KOBJ_CHANGE, temp_info);
		}
		/* trigger framework cooling, like cpufreq */
		thermal_zone_device_update(t_dev->therm_cpu);
	}
	return IRQ_HANDLED;
}

static irqreturn_t pxa28nm_irq(int irq, void *devid)
{
	u32 tmp;
	tmp = reg_read(TSEN_LSTATUS);
	reg_clr_set(TSEN_LSTATUS, 0, tmp);
	if (tmp & TSEN_RDY_INT) {
		pxa28nm_thermal_dev.temp_cpu =
			millicelsius_decode((tmp & TSEN_DATA_LATCHED_MASK) >>
					TSEN_DATA_LATCHED_OFF);
	}
	return IRQ_WAKE_THREAD;
}

static int pxa28nm_thermal_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 tmp;

	pxa28nm_thermal_dev.irq = platform_get_irq(pdev, 0);
	if (pxa28nm_thermal_dev.irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq\n");
		return -EINVAL;
	}

	pxa28nm_thermal_dev.mem =
		platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pxa28nm_thermal_dev.base =
		devm_ioremap_resource(&pdev->dev, pxa28nm_thermal_dev.mem);
	if (IS_ERR(pxa28nm_thermal_dev.base))
		return PTR_ERR(pxa28nm_thermal_dev.base);

	ret = devm_request_threaded_irq(&pdev->dev, pxa28nm_thermal_dev.irq,
			pxa28nm_irq, pxa28nm_thread_irq, IRQF_ONESHOT,
			pdev->name, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n",
				pxa28nm_thermal_dev.irq);
		return ret;
	}

	pxa28nm_thermal_dev.therm_clk = clk_get(NULL, "ts_clk");
	if (IS_ERR(pxa28nm_thermal_dev.therm_clk)) {
		dev_err(&pdev->dev, "Could not get thermal clock\n");
		return PTR_ERR(pxa28nm_thermal_dev.therm_clk);
	}
	clk_prepare_enable(pxa28nm_thermal_dev.therm_clk);

	if (reg_read(TSEN_RSTATUS) & TSEN_WDT_FLAG) {
		pr_warn("System reset by thermal watch dog (%d C)\n",
			millicelsius_decode((reg_read(TSEN_RSTATUS) &
				TSEN_DATA_WDT_MASK) >> TSEN_DATA_WDT_OFF)/1000);
		reg_clr_set(TSEN_RSTATUS, 0, TSEN_WDT_FLAG);
	}
	/* init thermal framework */
	pxa28nm_register_thermal();
	/* set 117C as watchdog reset */
	tmp = (millicelsius_encode(117000) << TSEN_WDT_THD_OFF) &
					TSEN_WDT_THD_MASK;
	reg_clr_set(TSEN_THD23, TSEN_WDT_THD_MASK, tmp);
	reg_clr_set(TSEN_LCTRL, 0, TSEN_WDT_DIRECTION | TSEN_WDT_ENABLE);
	/* set auto interval 2000ms */
	pxa28nm_set_interval(2000);
	/* start auto test mode 2 */
	reg_clr_set(TSEN_PCTRL, TSEN_ISO_EN | TSEN_RESET, 0);
	tmp = TSEN_RDY_INT_ENABLE | ((2 << TSEN_AUTO_MODE_OFF) &
					TSEN_AUTO_MODE_MASK);
	reg_clr_set(TSEN_LCTRL, 0, tmp);
	return 0;
}

static int pxa28nm_thermal_remove(struct platform_device *pdev)
{
	reg_clr_set(TSEN_PCTRL, 0, TSEN_RESET);
	clk_disable_unprepare(pxa28nm_thermal_dev.therm_clk);
	thermal_zone_device_unregister(pxa28nm_thermal_dev.therm_cpu);
#ifdef CONFIG_CPU_FREQ
	cpufreq_cooling_unregister(pxa28nm_thermal_dev.cool_cpufreq);
#endif
	pr_info("Kernel Thermal management unregistered\n");
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id pxa28nm_tmu_match[] = {
	{ .compatible = "marvell,pxa28nm-thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, pxa28nm_tmu_match);
#endif

static struct platform_driver pxa28nm_thermal_driver = {
	.driver = {
		.name   = "pxa28nm-thermal",
		.pm     = PXA_TMU_PM,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(pxa28nm_tmu_match),
#endif
	},
	.probe = pxa28nm_thermal_probe,
	.remove = pxa28nm_thermal_remove,
};
module_platform_driver(pxa28nm_thermal_driver);

MODULE_AUTHOR("Marvell Semiconductor");
MODULE_DESCRIPTION("HELAN2 SoC thermal driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa28nm-thermal");
