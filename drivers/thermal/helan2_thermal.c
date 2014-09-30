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
#include <linux/cooling_dev_mrvl.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/pm_qos.h>
#include <linux/cputype.h>
#include "helan2_thermal.h"

#define APB_CLK_BASE (0xd4015000)
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
#define TSEN_INT2_DIRECTION (1 << 7)
#define TSEN_INT2_ENABLE (1 << 6)
#define TSEN_INT1_DIRECTION (1 << 5)
#define TSEN_INT1_ENABLE (1 << 4)
#define TSEN_INT0_DIRECTION (1 << 3)
#define TSEN_INT0_ENABLE (1 << 2)
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
/* TSEN_THD01 */
#define TSEN_THD0_OFF (0)
#define TSEN_THD0_MASK (0xfff)
#define TSEN_THD1_OFF (12)
#define TSEN_THD1_MASK (0xfff000)
/* TSEN_THD23 */
#define TSEN_THD2_OFF (0)
#define TSEN_THD2_MASK (0xfff)
#define TSEN_WDT_THD_OFF (12)
#define TSEN_WDT_THD_MASK (0xfff000)

#define reg_read(off) readl(thermal_dev.base + (off))
#define reg_write(val, off) writel((val), thermal_dev.base + (off))
#define reg_clr_set(off, clr, set) \
	reg_write(((reg_read(off) & ~(clr)) | (set)), off)

enum trip_points {
	TRIP_POINT_0,
	TRIP_POINT_1,
	TRIP_POINT_2,
	TRIP_POINT_3,
	TRIP_POINT_4,
	TRIP_POINT_5,
	TRIP_POINTS_NUM,
	TRIP_POINTS_ACTIVE_NUM = TRIP_POINTS_NUM - 1,
};

struct uevent_msg_priv {
	int cur_s;
	int last_s;
};

struct cooling_device {
	struct thermal_cooling_device *combile_cool;
	int max_state, cur_state;
	struct thermal_cooling_device *cool_cpufreq;
	unsigned long cpufreq_cstate[THERMAL_MAX_TRIPS];
	struct thermal_cooling_device *cool_cpuhotplug;
	unsigned long hotplug_cstate[THERMAL_MAX_TRIPS];
};

struct pxa28nm_thermal_device {
	struct thermal_zone_device *therm_cpu;
	int trip_range;
	struct resource *mem;
	void __iomem *base;
	struct clk *therm_clk;
	struct cooling_device cdev;
	int hit_trip_cnt[TRIP_POINTS_NUM];
	int irq;
	int ttemp_table[3/*2.0G,1.8G,1.5G*/][4];
};

static struct pxa28nm_thermal_device thermal_dev;
static int trips_temp[TRIP_POINTS_NUM] = {
	70000, /* TRIP_POINT_0 */
	80000, /* TRIP_POINT_1 */
	90000, /* TRIP_POINT_2 */
	95000, /* TRIP_POINT_3 */
	100000, /* TRIP_POINT_4 */
	105000, /* TRIP_POINT_5 */
};

static int trips_hyst[TRIP_POINTS_NUM] = {
	65000, /* TRIP_POINT_0_D */
	75000, /* TRIP_POINT_1_D */
	85000, /* TRIP_POINT_2_D */
	90000, /* TRIP_POINT_3_D */
	95000, /* TRIP_POINT_4_D */
	105000, /* TRIP_POINT_5_D */
};

#define THERMAL_SAFE_TEMP 80000

#define THSEN_GAIN      3874
#define THSEN_OFFSET    2821

static int pxa28nm_set_threshold(int range);
#ifdef CONFIG_PXA_DVFS
unsigned int get_chipprofile(void);
unsigned int get_iddq_105(void);
#else
static inline unsigned int get_chipprofile(void) {return 0xff; }
static inline unsigned int get_iddq_105(void) {return 0x0; }
#endif

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

static ssize_t hit_trip_status_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	int ret = 0;
	u32 tmp;
	ret += sprintf(buf + ret, "Register dump:\n");
	ret += sprintf(buf + ret, "TSEN_PCTRL=0x%x\n", reg_read(TSEN_PCTRL));
	tmp = reg_read(TSEN_LCTRL);
	ret += sprintf(buf + ret, "TSEN_LCTRL=0x%x(int0_en:%d, int1_en:%d)\n",
		tmp, !!(tmp & TSEN_INT0_ENABLE), !!(tmp & TSEN_INT1_ENABLE));
	ret += sprintf(buf + ret, "TSEN_PSTATUS=0x%x\n",
			reg_read(TSEN_PSTATUS));
	tmp = reg_read(TSEN_LSTATUS);
	ret += sprintf(buf + ret, "TSEN_LSTATUS=0x%x(temp:%dmC)\n", tmp,
		millicelsius_decode((tmp & TSEN_DATA_LATCHED_MASK) >>
			TSEN_DATA_LATCHED_OFF));
	ret += sprintf(buf + ret, "TSEN_RSTATUS=0x%x\n",
			reg_read(TSEN_RSTATUS));
	tmp = reg_read(TSEN_THD01);
	ret += sprintf(buf + ret, "TSEN_THD01=0x%x(th0_u:%dmC, th1_d:%dmC)\n",
	tmp, millicelsius_decode((tmp & TSEN_THD0_MASK) >> TSEN_THD0_OFF),
		millicelsius_decode((tmp & TSEN_THD1_MASK) >> TSEN_THD1_OFF));
	tmp = reg_read(TSEN_THD23);
	ret += sprintf(buf + ret, "TSEN_THD23=0x%x(hw_r:%dmC, sw_r:%dmC)\n",
			tmp, millicelsius_decode((tmp & TSEN_WDT_THD_MASK) >>
			TSEN_WDT_THD_OFF), millicelsius_decode((tmp &
			TSEN_THD2_MASK) >> TSEN_THD2_OFF));
	for (i = 0; i < TRIP_POINTS_NUM; i++) {
		ret += sprintf(buf + ret, "trip %d: %d hits\n",
				trips_temp[i],
				thermal_dev.hit_trip_cnt[i]);
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
	u32 tmp;
	if (!(reg_read(TSEN_PCTRL) & TSEN_RESET)) {
		tmp = reg_read(TSEN_LSTATUS);
		*temp = millicelsius_decode((tmp & TSEN_DATA_LATCHED_MASK) >>
				TSEN_DATA_LATCHED_OFF);
		/* unreasonable temperature */
		if (*temp > 120000)
			*temp = 0;
	} else
		*temp = 0;

	return 0;
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
		*temp = trips_temp[trip];
	else
		*temp = -1;
	return 0;
}

static int cpu_sys_get_trip_hyst(struct thermal_zone_device *thermal,
		int trip, unsigned long *temp)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		*temp = trips_hyst[trip];
	else
		*temp = -1;
	return 0;
}

static int cpu_sys_set_trip_temp(struct thermal_zone_device *thermal, int trip,
		unsigned long temp)
{
	u32 tmp;
	struct pxa28nm_thermal_device *cpu_thermal = thermal->devdata;
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		trips_temp[trip] = temp;
	if ((TRIP_POINTS_NUM - 1) == trip) {
		tmp = (millicelsius_encode(trips_temp[TRIP_POINTS_NUM - 1]) <<
					TSEN_THD2_OFF) & TSEN_THD2_MASK;
		reg_clr_set(TSEN_THD23, TSEN_THD2_MASK, tmp);
	} else
		pxa28nm_set_threshold(cpu_thermal->trip_range);
	return 0;
}

static int cpu_sys_set_trip_hyst(struct thermal_zone_device *thermal,
		int trip, unsigned long temp)
{
	struct pxa28nm_thermal_device *cpu_thermal = thermal->devdata;
	if ((trip >= 0) && (trip < TRIP_POINTS_ACTIVE_NUM))
		trips_hyst[trip] = temp;
	if ((TRIP_POINTS_NUM - 1) == trip)
		pr_warn("critical down doesn't used\n");
	else
		pxa28nm_set_threshold(cpu_thermal->trip_range);
	return 0;
}

static int cpu_sys_get_crit_temp(struct thermal_zone_device *thermal,
		unsigned long *temp)
{
	return trips_temp[TRIP_POINTS_NUM - 1];
}

static struct thermal_zone_device_ops cpu_thermal_ops = {
	.get_temp = cpu_sys_get_temp,
	.get_trip_type = cpu_sys_get_trip_type,
	.get_trip_temp = cpu_sys_get_trip_temp,
	.get_trip_hyst = cpu_sys_get_trip_hyst,
	.set_trip_temp = cpu_sys_set_trip_temp,
	.set_trip_hyst = cpu_sys_set_trip_hyst,
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

static int combile_get_max_state(struct thermal_cooling_device *cdev,
		unsigned long *state)
{
	struct pxa28nm_thermal_device *cpu_thermal = cdev->devdata;
	*state = cpu_thermal->cdev.max_state;
	return 0;
}

static int combile_get_cur_state(struct thermal_cooling_device *cdev,
		unsigned long *state)
{
	struct pxa28nm_thermal_device *cpu_thermal = cdev->devdata;
	*state = cpu_thermal->cdev.cur_state;
	return 0;
}

static int combile_set_cur_state(struct thermal_cooling_device *cdev,
		unsigned long state)
{
	struct pxa28nm_thermal_device *cpu_thermal = cdev->devdata;
	struct thermal_cooling_device *c_freq = cpu_thermal->cdev.cool_cpufreq;
	struct thermal_cooling_device *c_plug =
		cpu_thermal->cdev.cool_cpuhotplug;
	unsigned long freq_state = 0, plug_state = 0;
	unsigned long temp = 0;

	if (state > cpu_thermal->cdev.max_state)
		return -EINVAL;
	cpu_thermal->cdev.cur_state = state;

	freq_state = thermal_dev.cdev.cpufreq_cstate[state];
	plug_state = thermal_dev.cdev.hotplug_cstate[state];

	cpu_sys_get_temp(thermal_dev.therm_cpu, &temp);
	pr_info("Thermal cpu temp %ldC, state %lu, cpufreq qos %lu, core_num qos %lu\n",
		temp / 1000, state, freq_state, plug_state);

	if (c_freq)
		c_freq->ops->set_cur_state(c_freq, freq_state);
	if (c_plug)
		c_plug->ops->set_cur_state(c_plug, plug_state);
	return 0;
}

static struct thermal_cooling_device_ops const combile_cooling_ops = {
	.get_max_state = combile_get_max_state,
	.get_cur_state = combile_get_cur_state,
	.set_cur_state = combile_set_cur_state,
};


static void pxa28nm_register_thermal(void)
{
	int i, trip_w_mask = 0;

	thermal_dev.cdev.cool_cpufreq = cpufreq_cool_register();
	thermal_dev.cdev.cool_cpuhotplug = cpuhotplug_cool_register();

	thermal_dev.cdev.combile_cool = thermal_cooling_device_register(
			"cpu-combile-cool", &thermal_dev, &combile_cooling_ops);
	thermal_dev.cdev.max_state = TRIP_POINTS_ACTIVE_NUM;
	thermal_dev.cdev.cur_state = 0;

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		trip_w_mask |= (1 << i);
	thermal_dev.therm_cpu = thermal_zone_device_register(
			"thsens_cpu", TRIP_POINTS_NUM, trip_w_mask,
			&thermal_dev, &cpu_thermal_ops, NULL, 0, 0);
	/*
	 * enable bi_direction state machine, then it didn't care
	 * whether up/down trip points are crossed or not
	 */
	thermal_dev.therm_cpu->tzdctrl.state_ctrl = true;
	/* bind combile cooling */
	thermal_zone_bind_cooling_device(thermal_dev.therm_cpu,
			TRIP_POINT_0,  thermal_dev.cdev.combile_cool,
			THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);

	i = sysfs_create_group(&((thermal_dev.therm_cpu->device).kobj),
			&thermal_attr_grp);
	if (i < 0)
		pr_err("Failed to register private thermal interface\n");
}

static int pxa28nm_set_threshold(int range)
{
	u32 tmp;

	if (range < 0 || range > TRIP_POINTS_ACTIVE_NUM) {
		pr_err("soc thermal: invalid threshold %d\n", range);
		return -1;
	}

	if (0 == range) {
		tmp = (millicelsius_encode(trips_temp[0]) << TSEN_THD0_OFF) &
							TSEN_THD0_MASK;
		reg_clr_set(TSEN_THD01, TSEN_THD0_MASK, tmp);
		tmp = (millicelsius_encode(trips_hyst[0]) << TSEN_THD1_OFF) &
							TSEN_THD1_MASK;
		reg_clr_set(TSEN_THD01, TSEN_THD1_MASK, tmp);
		reg_clr_set(TSEN_LCTRL, 0, TSEN_INT0_ENABLE);
		reg_clr_set(TSEN_LCTRL, TSEN_INT1_ENABLE, 0);

	} else if (TRIP_POINTS_ACTIVE_NUM == range) {
		tmp = (millicelsius_encode(trips_temp[range - 1]) <<
						TSEN_THD0_OFF) & TSEN_THD0_MASK;
		reg_clr_set(TSEN_THD01, TSEN_THD0_MASK, tmp);
		tmp = (millicelsius_encode(trips_hyst[range - 1]) <<
						TSEN_THD1_OFF) & TSEN_THD1_MASK;
		reg_clr_set(TSEN_THD01, TSEN_THD1_MASK, tmp);
		reg_clr_set(TSEN_LCTRL, TSEN_INT0_ENABLE, 0);
		reg_clr_set(TSEN_LCTRL, 0, TSEN_INT1_ENABLE);
	} else {
		tmp = (millicelsius_encode(trips_temp[range]) <<
						TSEN_THD0_OFF) & TSEN_THD0_MASK;
		reg_clr_set(TSEN_THD01, TSEN_THD0_MASK, tmp);
		tmp = (millicelsius_encode(trips_hyst[range - 1]) <<
						TSEN_THD1_OFF) & TSEN_THD1_MASK;
		reg_clr_set(TSEN_THD01, TSEN_THD1_MASK, tmp);
		reg_clr_set(TSEN_LCTRL, 0, TSEN_INT0_ENABLE);
		reg_clr_set(TSEN_LCTRL, 0, TSEN_INT1_ENABLE);
	}
	return 0;
}

static void pxa28nm_set_interval(int ms)
{
	/* 500k clock, high 16bit */
	int interval_val = ms * 500 / 256;
	reg_clr_set(TSEN_LCTRL, TSEN_AUTO_INTERVAL_MASK,
	(interval_val << TSEN_AUTO_INTERVAL_OFF) & TSEN_AUTO_INTERVAL_MASK);
}

#define PP_CARE_NUM 4
static void pxa28nm_mapping_cooling_dev(struct device_node *np)
{
	unsigned int profile = 0, iddq105 = 0;
	int i, j;
	u32 cur_t = 0, old_t = 0, x = 0, y = 0;
	int fake_t = 0;
	char dt_name[20];
	u32 ctable[PP_CARE_NUM][4] = {{0} };
	struct cpufreq_frequency_table *table =
		cpufreq_frequency_get_table(0);
	unsigned int freq_max = 0;
	u32 tsafe_freq, tsafe_plug;

	profile = get_chipprofile();
	iddq105 = get_iddq_105();

	if (10 == profile || 12 == profile || 14 == profile)
		profile++;

	if (0 != iddq105 && profile < 16) {
		getThermalLimitsPerPP(profile, iddq105, (int **)thermal_dev.ttemp_table);

		sprintf(dt_name, "core-p%d", profile);
		if (!of_property_read_bool(np, dt_name))
			sprintf(dt_name, "core-p0");

		of_property_read_u32_array(np, dt_name, (u32 *)ctable, PP_CARE_NUM * 4);
		old_t = 0;
		cur_t = 1;
		thermal_dev.cdev.cpufreq_cstate[0] = 0;
		thermal_dev.cdev.hotplug_cstate[0] = 0;
		while (1) {
f_next:
			if (cur_t == old_t || cur_t > TRIP_POINTS_ACTIVE_NUM) {
				/* keep the remaining cooling as the last one */
				for (i = (cur_t + 1); i < THERMAL_MAX_TRIPS; i++) {
					thermal_dev.cdev.cpufreq_cstate[i] =
						thermal_dev.cdev.cpufreq_cstate[cur_t];
					thermal_dev.cdev.hotplug_cstate[i] =
						thermal_dev.cdev.hotplug_cstate[cur_t];
				}
				break;
			}
			old_t = cur_t;
			for (i = 0; i < PP_CARE_NUM; i++) {
				for (j = 0; j < 4; j++) {
					if (ctable[i][j] == cur_t) {
						thermal_dev.cdev.cpufreq_cstate[cur_t] = i;
						thermal_dev.cdev.hotplug_cstate[cur_t] = j;
						x = thermal_dev.cdev.cpufreq_cstate[cur_t - 1];
						y = thermal_dev.cdev.hotplug_cstate[cur_t - 1];
						trips_temp[cur_t-1] = thermal_dev.ttemp_table[x][y]
							* 1000;
						/*
						 * get to 1.2G row, we can't find a temperture
						 * to go out from SV table anymore, so get out
						 */
						if (i == PP_CARE_NUM - 1)
							goto f_next;
						cur_t++;
						goto f_next;
					}
				}
			}
		}
		pr_info("|----raw mapping----|\n");
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			pr_info("%dmC==>%ld %ld\n", trips_temp[i],
					thermal_dev.cdev.cpufreq_cstate[i+1],
					thermal_dev.cdev.hotplug_cstate[i+1]);
		}
		/*
		 * for -1000mC, set fake temp from 15C; for same temperature,
		 * sub a 10C for the former one, thus to keep trip point different and satisfy
		 * thermal framework
		 */
		pr_info("|----refine temp----|\n");
		fake_t = 15000;
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			if (-1000 == trips_temp[i]) {
				trips_temp[i] = fake_t;
				fake_t += 5000;
			} else
				break;
		}
		for (i = TRIP_POINTS_ACTIVE_NUM - 1; i > 0; i--) {
			if (trips_temp[i] <= trips_temp[i - 1])
				trips_temp[i - 1] = trips_temp[i] - 10000;
		}
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			pr_info("%dmC==>%ld %ld\n", trips_temp[i],
					thermal_dev.cdev.cpufreq_cstate[i+1],
					thermal_dev.cdev.hotplug_cstate[i+1]);
		}
		/* Apply THERMAL_SAFE_TEMP */
		pr_info("|----apply thermal safe----|\n");
		sprintf(dt_name, "thermal_safe-p%d", profile);
		if (!of_property_read_bool(np, dt_name))
			sprintf(dt_name, "thermal_safe-p0");
		of_property_read_u32_index(np, dt_name, 0, &tsafe_freq);
		of_property_read_u32_index(np, dt_name, 1, &tsafe_plug);

		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			if (THERMAL_SAFE_TEMP == trips_temp[i]) {
				j = i;
				goto find_tsen_safe;
			}
		}
		/* if don't find, we need assign one */
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			if (trips_temp[i] > THERMAL_SAFE_TEMP) {
				trips_temp[i] = THERMAL_SAFE_TEMP;
				j = i;
				break;
			}
		}

find_tsen_safe:
		thermal_dev.cdev.cpufreq_cstate[j + 1] = tsafe_freq;
		thermal_dev.cdev.hotplug_cstate[j + 1] = tsafe_plug;
		for (i = j + 1; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			thermal_dev.cdev.cpufreq_cstate[i + 1] =
				thermal_dev.cdev.cpufreq_cstate[i];
			thermal_dev.cdev.hotplug_cstate[i + 1] =
				thermal_dev.cdev.hotplug_cstate[i];
		}
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			pr_info("%dmC==>%ld %ld\n", trips_temp[i],
				thermal_dev.cdev.cpufreq_cstate[i+1],
				thermal_dev.cdev.hotplug_cstate[i+1]);
		}

		/* The table from SV is from 2.0G, we have to tune index */
		for (i = 0; table[i].frequency != CPUFREQ_TABLE_END; i++) {
			if (table[i].frequency > freq_max)
				freq_max = table[i].frequency;
		}
		pr_info("|----tune for %dkHz chip----|\n", freq_max);
		if (1700 * 1000 < freq_max && freq_max < 1900 * 1000) {
			for (i = 0; i < THERMAL_MAX_TRIPS; i++)
				if (thermal_dev.cdev.cpufreq_cstate[i])
					thermal_dev.cdev.cpufreq_cstate[i]--;
		} else if (1400 * 1000 < freq_max && freq_max < 1600 * 1000) {
			for (i = 0; i < THERMAL_MAX_TRIPS; i++) {
				if (1 == thermal_dev.cdev.cpufreq_cstate[i])
					thermal_dev.cdev.cpufreq_cstate[i] = 0;
				else if (thermal_dev.cdev.cpufreq_cstate[i] >= 2)
					thermal_dev.cdev.cpufreq_cstate[i] -= 2;
			}
		} else if (freq_max < 1400 * 1000) {
			for (i = 0; i < THERMAL_MAX_TRIPS; i++)
				thermal_dev.cdev.cpufreq_cstate[i] = 0;
		}
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			pr_info("%dmC==>%ld %ld(state %d)\n", trips_temp[i],
					thermal_dev.cdev.cpufreq_cstate[i+1],
					thermal_dev.cdev.hotplug_cstate[i+1], i + 1);
		}
		/* set to down threshold */
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++)
			trips_hyst[i] = trips_temp[i] - 10000;
	} else {
		thermal_dev.cdev.cpufreq_cstate[0] = 0;
		thermal_dev.cdev.hotplug_cstate[0] = 0;
		for (i = 1; i <= TRIP_POINTS_ACTIVE_NUM; i++) {
			thermal_dev.cdev.cpufreq_cstate[i] = i;
			thermal_dev.cdev.hotplug_cstate[i] = 0;
		}
		pr_warn("thermal iddq unfused chip, use default policy for profile(%d) iddq(%d)\n",
				profile, iddq105);
		for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
			pr_info("%dmC==>%ld %ld(state %d)\n", trips_temp[i],
					thermal_dev.cdev.cpufreq_cstate[i+1],
					thermal_dev.cdev.hotplug_cstate[i+1], i + 1);
		}
	}
}

static irqreturn_t pxa28nm_thread_irq(int irq, void *devid)
{
	if (thermal_dev.therm_cpu) {
		/*
		 * trigger framework cooling, the real cooling behavior
		 * rely on governor, if it's user_space, then only uevent
		 * will be sent by framework, other wise, related governor
		 * will do real cooling
		 */
		thermal_zone_device_update(thermal_dev.therm_cpu);
	}
	return IRQ_HANDLED;
}

static irqreturn_t pxa28nm_irq(int irq, void *devid)
{
	u32 tmp, tmp_lc;
	unsigned long temp;
	tmp = reg_read(TSEN_LSTATUS);
	reg_clr_set(TSEN_LSTATUS, 0, tmp);

	tmp_lc = reg_read(TSEN_LCTRL);
	if ((tmp_lc & TSEN_RDY_INT_ENABLE) && (tmp & TSEN_RDY_INT)) {
		cpu_sys_get_temp(thermal_dev.therm_cpu, &temp);
		pr_info("in irq temp = %ld\n", temp);
	}
	if ((tmp_lc & TSEN_INT0_ENABLE) && (tmp & TSEN_INT0)) {
		thermal_dev.hit_trip_cnt[thermal_dev.trip_range]++;
		thermal_dev.trip_range++;
		if (thermal_dev.trip_range > TRIP_POINTS_ACTIVE_NUM)
			thermal_dev.trip_range = TRIP_POINTS_ACTIVE_NUM;
		pxa28nm_set_threshold(thermal_dev.trip_range);

	}
	if ((tmp_lc & TSEN_INT1_ENABLE) && (tmp & TSEN_INT1)) {
		thermal_dev.trip_range--;
		if (thermal_dev.trip_range < 0)
			thermal_dev.trip_range = 0;
		pxa28nm_set_threshold(thermal_dev.trip_range);
	}
	if ((tmp_lc & TSEN_INT2_ENABLE) && (tmp & TSEN_INT2)) {
		/* wait framework shutdown */
		cpu_sys_get_temp(thermal_dev.therm_cpu, &temp);
		pr_info("critical temp = %ld\n", temp);
	}
	return IRQ_WAKE_THREAD;
}

static int pxa28nm_thermal_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 tmp;

	memset(&thermal_dev, 0, sizeof(thermal_dev));
	thermal_dev.irq = platform_get_irq(pdev, 0);
	if (thermal_dev.irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq\n");
		return -EINVAL;
	}

	thermal_dev.mem =
		platform_get_resource(pdev, IORESOURCE_MEM, 0);
	thermal_dev.base =
		devm_ioremap_resource(&pdev->dev, thermal_dev.mem);
	if (IS_ERR(thermal_dev.base))
		return PTR_ERR(thermal_dev.base);

	ret = devm_request_threaded_irq(&pdev->dev, thermal_dev.irq,
			pxa28nm_irq, pxa28nm_thread_irq, IRQF_ONESHOT,
			pdev->name, NULL);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request irq: %d\n",
				thermal_dev.irq);
		return ret;
	}

	thermal_dev.therm_clk = devm_clk_get(&pdev->dev, "ts_clk");
	if (IS_ERR(thermal_dev.therm_clk)) {
		dev_err(&pdev->dev, "Could not get thermal clock\n");
		return PTR_ERR(thermal_dev.therm_clk);
	}
	clk_prepare_enable(thermal_dev.therm_clk);
	/* make sure clock stable */
	usleep_range(20, 30);

	if (reg_read(TSEN_RSTATUS) & TSEN_WDT_FLAG) {
		pr_warn("System reset by thermal watch dog (%d C)\n",
			millicelsius_decode((reg_read(TSEN_RSTATUS) &
				TSEN_DATA_WDT_MASK) >> TSEN_DATA_WDT_OFF)/1000);
		reg_clr_set(TSEN_RSTATUS, 0, TSEN_WDT_FLAG);
	}

	tmp = reg_read(TSEN_LSTATUS);
	if (tmp) {
		void *apb_base = ioremap_nocache(APB_CLK_BASE, SZ_4K);
		if (apb_base) {
			pr_warn("reinit thermal LSTATUS = 0x%x\n", tmp);
			/* delay 10us for each step to ganrantee reset suc */
			if (cpu_is_pxa1U88()) {
				/* bit1 ctl reset, and bit0 ctl enable */
				writel(0x2, (apb_base + 0x6C));
				udelay(10);
				writel(0x0, (apb_base + 0x6C));
				udelay(10);
				writel(0x1, (apb_base + 0x6C));
				udelay(10);
			} else {
				/* bit2 ctl reset, bit1&0 ctl enable */
				writel(0x4, (apb_base + 0x6C));
				udelay(10);
				writel(0x0, (apb_base + 0x6C));
				udelay(10);
				writel(0x3, (apb_base + 0x6C));
				udelay(10);
			}
			iounmap(apb_base);

			tmp = reg_read(TSEN_LSTATUS);
			reg_clr_set(TSEN_LSTATUS, 0, tmp);
			tmp = reg_read(TSEN_LSTATUS);
			if (tmp)
				WARN_ON("reinit thermal failed\n");
		}
	} else
		pr_info("thermal status fine\n");

	/* init thermal framework */
	pxa28nm_register_thermal();
	/* init threshold */
	pxa28nm_mapping_cooling_dev(pdev->dev.of_node);
	tmp = (millicelsius_encode(110000) << TSEN_WDT_THD_OFF) &
					TSEN_WDT_THD_MASK;
	reg_clr_set(TSEN_THD23, TSEN_WDT_THD_MASK, tmp);
	reg_clr_set(TSEN_LCTRL, 0, TSEN_WDT_DIRECTION | TSEN_WDT_ENABLE);
	tmp = (millicelsius_encode(trips_temp[TRIP_POINTS_NUM - 1]) <<
			TSEN_THD2_OFF) & TSEN_THD2_MASK;
	reg_clr_set(TSEN_THD23, TSEN_THD2_MASK, tmp);
	reg_clr_set(TSEN_LCTRL, 0, TSEN_INT2_ENABLE | TSEN_INT2_DIRECTION);

	reg_clr_set(TSEN_LCTRL, TSEN_INT0_ENABLE, TSEN_INT0_DIRECTION);
	reg_clr_set(TSEN_LCTRL, TSEN_INT1_ENABLE | TSEN_INT1_DIRECTION, 0);
	thermal_dev.trip_range = 0;
	pxa28nm_set_threshold(thermal_dev.trip_range);
	/* set auto interval 200ms and start auto mode 2*/
	pxa28nm_set_interval(200);
	reg_clr_set(TSEN_PCTRL, TSEN_ISO_EN | TSEN_RESET, 0);
	tmp = ((2 << TSEN_AUTO_MODE_OFF) & TSEN_AUTO_MODE_MASK);
	reg_clr_set(TSEN_LCTRL, 0, tmp);
	pr_info("helan2 thermal probed\n");
	return 0;
}

static int pxa28nm_thermal_remove(struct platform_device *pdev)
{
	reg_clr_set(TSEN_PCTRL, 0, TSEN_RESET);
	clk_disable_unprepare(thermal_dev.therm_clk);
	cpufreq_cool_unregister(thermal_dev.cdev.cool_cpufreq);
	cpuhotplug_cool_unregister(thermal_dev.cdev.cool_cpuhotplug);
	thermal_cooling_device_unregister(thermal_dev.cdev.combile_cool);
	thermal_zone_device_unregister(thermal_dev.therm_cpu);
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
