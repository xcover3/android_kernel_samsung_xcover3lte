/*
 * Copyright 2013 Marvell Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/* PXA1928 Thermal Implementation */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/io.h>
#include <linux/syscalls.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/of.h>

#define TRIP_POINTS_NUM	4
#define THSENS_NUM	3
#define TRIP_POINTS_ACTIVE_NUM (TRIP_POINTS_NUM - 1)
#define THSEN_GAIN	3874
#define THSEN_OFFSET	2821
#define INVALID_TEMP	40000
#define PXA1928_THERMAL_POLLING_FREQUENCY_MS 500

#define TSEN_CFG_REG_0			(0x00)
#define TSEN_DEBUG_REG_0		(0x04)
#define TSEN_INT0_WDOG_THLD_REG_0	(0x1c)
#define TSEN_INT1_INT2_THLD_REG_0	(0x20)
#define TSEN_DATA_REG_0			(0x24)
#define TSEN_DATA_RAW_REG_0		(0x28)
#define TSEN_AUTO_READ_VALUE_REG_0	(0x2C)

#define TSEN_CFG_REG_1			(0x30)
#define TSEN_DEBUG_REG_1		(0x34)
#define TSEN_INT0_WDOG_THLD_REG_1	(0x3c)
#define TSEN_INT1_INT2_THLD_REG_1	(0x40)
#define TSEN_DATA_REG_1			(0x44)
#define TSEN_DATA_RAW_REG_1		(0x48)
#define TSEN_AUTO_READ_VALUE_REG_1	(0x4C)

#define TSEN_CFG_REG_2			(0x50)
#define TSEN_DEBUG_REG_2		(0x54)
#define TSEN_INT0_WDOG_THLD_REG_2	(0x5c)
#define TSEN_INT1_INT2_THLD_REG_2	(0x60)
#define TSEN_DATA_REG_2			(0x64)
#define TSEN_DATA_RAW_REG_2		(0x68)
#define TSEN_AUTO_READ_VALUE_REG_2	(0x6c)
/*bit defines of configure reg*/
#define TSEN_ENABLE (1 << 31)
#define TSEN_DIGITAL_RST (1 << 30)
#define TSEN_AUTO_READ (1 << 28)
#define TSEN_DATA_READY (1 << 24)
#define TSEN_WDOG_DIRECTION (1 << 11)
#define TSEN_WDOG_ENABLE (1 << 10)
#define TSEN_INT2_STATUS (1 << 9)
#define TSEN_INT2_DIRECTION (1 << 8)
#define TSEN_INT2_ENABLE (1 << 7)
#define TSEN_INT1_STATUS (1 << 6)
#define TSEN_INT1_DIRECTION (1 << 5)
#define TSEN_INT1_ENABLE (1 << 4)
#define TSEN_INT0_STATUS (1 << 3)
#define TSEN_INT0_DIRECTION (1 << 2)
#define TSEN_INT0_ENABLE (1 << 1)
#define BALANCE 10000

static struct mutex con_lock;

/*
 * 80000, bind to active type
 * 95000, bind to active type
 * 110000,bind to active type
 * 125000,bind to critical type
*/
static int thsens_trips_temp[THSENS_NUM][TRIP_POINTS_NUM] = {
	{80000, 95000, 110000, 125000},
	{80000, 95000, 110000, 125000},
	{80000, 95000, 110000, 125000},
};
static struct thermal_zone_device *thsens_vpu;
static struct thermal_zone_device *thsens_cpu;
static struct thermal_zone_device *thsens_gc;
static struct clk *therclk_g, *therclk_vpu;
static struct clk *therclk_cpu, *therclk_gc;
static int pdev_irq;
/*
 * 1: don't have read data feature.
 * 2: have the read data feature, but don't have the valid data check
 * 3: have the read data feature and valid data check
*/
static unsigned int g_flag;

static int celsius_decode(u32 tcode)
{
	int cels;
	cels = (tcode * THSEN_GAIN - THSEN_OFFSET * 1000) / 10000 + 1;
	return cels;
}

static int celsius_encode(int cels)
{
	u32 tcode;
	cels /= 1000;
	tcode = (cels * 10 + THSEN_OFFSET) * 1000 / (THSEN_GAIN);
	return tcode;
}

static void enable_thsens(void *reg)
{
	__raw_writel(readl(reg) | TSEN_ENABLE, reg);
	__raw_writel(readl(reg) & ~TSEN_DIGITAL_RST, reg);
}

static int thsens_are_enabled(void *reg)
{
	if (((readl(reg) & TSEN_ENABLE) == TSEN_ENABLE) &&
		((readl(reg) & TSEN_DIGITAL_RST) == !TSEN_DIGITAL_RST))
		return 1;

	return 0;
}

static void init_wd_int0_threshold(int id, void *reg)
{
	__raw_writel((readl(reg) & 0x000FFF) |
			(celsius_encode(thsens_trips_temp[id][0]) << 12), reg);
	__raw_writel((readl(reg) & 0xFFF000) |
			celsius_encode(thsens_trips_temp[id][3]), reg);
}

static void init_int1_int2_threshold(int id, void *reg)
{
	__raw_writel((readl(reg) & 0xFFF000) |
			celsius_encode(thsens_trips_temp[id][1]), reg);
	__raw_writel((readl(reg) & 0x000FFF) |
			(celsius_encode(thsens_trips_temp[id][2]) << 12), reg);
}

static void init_thsens_irq_dir(void *reg)
{
	__raw_writel(readl(reg) | TSEN_WDOG_DIRECTION |
		TSEN_WDOG_ENABLE | TSEN_INT2_DIRECTION | TSEN_INT2_ENABLE |
		TSEN_INT1_DIRECTION | TSEN_INT1_ENABLE | TSEN_INT0_DIRECTION |
			TSEN_INT0_ENABLE , reg);
}

static void enable_auto_read(void *reg)
{
	__raw_writel(readl(reg) | TSEN_AUTO_READ, reg);
}

static void set_auto_read_interval(void *reg)
{
	__raw_writel(readl(reg) | PXA1928_THERMAL_POLLING_FREQUENCY_MS , reg);
}

static int thsens_data_read(void *config_reg, void *data_reg)
{
	int i, j, data = 0;
	int temp = 0;
	int cnt = 10;
	if (g_flag == 3) {
		while (cnt) {
			if (((readl(config_reg) & TSEN_DATA_READY) >> 24)) {
				temp = celsius_decode(readl(data_reg)) * 1000;
				break;
			} else
				cnt--;
		}
		if (cnt == 0) {
			pr_err("Invalid temperature!\n");
			temp = 80000;
		}
	} else if ((g_flag == 2) && thsens_are_enabled(config_reg)) {
		for (i = 0, j = 0; i < 5; i++) {
			data = celsius_decode(readl(data_reg)) * 1000;
			if (data < 130000) {
				j++;
				temp += data;
			}
			usleep_range(1000, 10000);
		}
		if (j != 0)
			temp /= j;
		else
			temp = 30000;
	} else
		temp = 30000;

	return temp;
}

#ifdef DEBUG_TEMPERATURE
static int g_test_temp_vpu = 80000;
static int g_test_temp_cpu = 80000;
static int g_test_temp_gc = 80000;

static int thermal_temp_debug_get(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct thermal_zone_device *tz_dev = dev_get_drvdata(dev);
	switch (tz_dev->id - 1) {
	case 0:
		return sprintf(buf, "%d\n", g_test_temp_vpu);
		break;
	case 1:
		return sprintf(buf, "%d\n", g_test_temp_cpu);
		break;
	case 2:
		return sprintf(buf, "%d\n", g_test_temp_gc);
		break;
	default:
		pr_err("this is invalid device\n");
		return -INVALID_TEMP;
		break;
	}
}

static int thermal_temp_debug_set(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct thermal_zone_device *tz_dev = dev_get_drvdata(dev);

	switch (tz_dev->id - 1) {
	case 0:
		if (1 != sscanf(buf, "%d\n", &g_test_temp_vpu))
			pr_err("set vpu debug temp error\n");
		break;
	case 1:
		if (1 != sscanf(buf, "%d\n", &g_test_temp_cpu))
			pr_err("set vpu debug temp error\n");
		break;
	case 2:
		if (1 != sscanf(buf, "%d\n", &g_test_temp_gc))
			pr_err("set vpu debug temp error\n");
		break;
	default:
		pr_err("this is invalid device\n");
		break;
	}
	return count;
}

static DEVICE_ATTR(thermal_debug_temp, 0644, thermal_temp_debug_get,
		thermal_temp_debug_set);

#endif

static struct attribute *thermal_attrs[] = {
#ifdef DEBUG_TEMPERATURE
	&dev_attr_thermal_debug_temp.attr,
#endif
	NULL,
};

static struct attribute_group thermal_attr_grp = {
	.attrs = thermal_attrs,
};


static int
ts_sys_get_temp(struct thermal_zone_device *tz, unsigned long *temp)
{
	*temp = 0;
	switch (tz->id - 1) {
	case 0:
#ifdef DEBUG_TEMPERATURE
		*temp = g_test_temp_vpu;
#else
		if (tz->devdata)
			*temp = thsens_data_read(tz->devdata + TSEN_CFG_REG_0,
					tz->devdata + TSEN_DATA_REG_0);
#endif
		break;
	case 1:
#ifdef DEBUG_TEMPERATURE
		*temp = g_test_temp_cpu;
#else
		if (tz->devdata)
			*temp = thsens_data_read(tz->devdata + TSEN_CFG_REG_1,
				tz->devdata + TSEN_DATA_REG_1);
#endif
		break;
	case 2:
#ifdef DEBUG_TEMPERATURE
		*temp = g_test_temp_gc;
#else
		if (tz->devdata)
			*temp = thsens_data_read(tz->devdata + TSEN_CFG_REG_2,
				tz->devdata + TSEN_DATA_REG_2);
#endif
		break;
	default:
		*temp = -INVALID_TEMP;
		return -EINVAL;
	}

	return 0;
}

static int ts_sys_get_mode(struct thermal_zone_device *thermal,
		enum thermal_device_mode *mode)
{
	*mode = THERMAL_DEVICE_ENABLED;
	return 0;

}

static int ts_sys_get_trip_type(struct thermal_zone_device *thermal, int trip,
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

static int ts_get_trip_temp(struct thermal_zone_device *tz, int trip,
		unsigned long *temp)
{
	void *reg = NULL;
	switch (tz->id - 1) {
	case 0:
		if (0 == trip || 3 == trip)
			reg = tz->devdata + TSEN_INT0_WDOG_THLD_REG_0;
		else if (1 == trip || 2 == trip)
			reg = tz->devdata + TSEN_INT1_INT2_THLD_REG_0;
		break;
	case 1:
		if (0 == trip || 3 == trip)
			reg = tz->devdata + TSEN_INT0_WDOG_THLD_REG_1;
		else if (1 == trip || 2 == trip)
			reg = tz->devdata + TSEN_INT1_INT2_THLD_REG_1;
		break;
	case 2:
		if (0 == trip || 3 == trip)
			reg = tz->devdata + TSEN_INT0_WDOG_THLD_REG_2;
		else if (1 == trip || 2 == trip)
			reg = tz->devdata + TSEN_INT1_INT2_THLD_REG_2;
		break;
	default:
		break;
	}

	switch (trip) {
	case 0:
	case 2:
		*temp = (unsigned long)celsius_decode(__raw_readl(reg) >> 12)
			* 1000;
		break;
	case 1:
	case 3:
		*temp = (unsigned long)celsius_decode(__raw_readl(reg) &
				(0x0000FFF)) * 1000;
		break;
	default:
		*temp = -INVALID_TEMP;
		break;
	}
	return 0;
}

static int ts_sys_get_trip_temp(struct thermal_zone_device *thermal, int trip,
		unsigned long *temp)
{
	*temp = thsens_trips_temp[thermal->id - 1][trip];
	return 0;
}

void ts_set_trip_temp(void *cfg_reg, void *it1_it2, void *it0_dg,
		int trip, unsigned long temp)
{
	switch (trip) {
	case 0:
		if (!((readl(cfg_reg) & TSEN_INT0_DIRECTION) >> 2))
			__raw_writel(readl(cfg_reg) ^
					TSEN_INT0_DIRECTION, cfg_reg);
		__raw_writel((readl(it0_dg) & 0x000FFF) |
				(celsius_encode(temp) << 12), it0_dg);
		break;
	case 1:
		if (!((readl(cfg_reg) & TSEN_INT1_DIRECTION) >> 5))
			__raw_writel(readl(cfg_reg) ^
					TSEN_INT1_DIRECTION, cfg_reg);
		__raw_writel((readl(it1_it2) & 0xFFF000) |
				celsius_encode(temp), it1_it2);
		break;
	case 2:
		if (!((readl(cfg_reg) & TSEN_INT2_DIRECTION) >> 8))
			__raw_writel(readl(cfg_reg) ^
					TSEN_INT2_DIRECTION, cfg_reg);
		__raw_writel((readl(it1_it2) & 0x000FFF) |
				(celsius_encode(temp) << 12), it1_it2);
		break;
	case 3:
		__raw_writel((readl(it0_dg) & 0xFFF000) |
				celsius_encode(temp), it0_dg);
		break;
	default:
		break;
	}
}

static int ts_set_tz_trip_temp(struct thermal_zone_device *tz, int trip,
		unsigned long temp)
{
	switch (tz->id - 1) {
	case 0:
		ts_set_trip_temp(tz->devdata + TSEN_CFG_REG_0, tz->devdata +
				TSEN_INT1_INT2_THLD_REG_0, tz->devdata +
				TSEN_INT0_WDOG_THLD_REG_0, trip, temp);
		break;
	case 1:
		ts_set_trip_temp(tz->devdata + TSEN_CFG_REG_1, tz->devdata +
				TSEN_INT1_INT2_THLD_REG_1, tz->devdata +
				TSEN_INT0_WDOG_THLD_REG_1, trip, temp);
		break;
	case 2:
		ts_set_trip_temp(tz->devdata + TSEN_CFG_REG_2, tz->devdata +
				TSEN_INT1_INT2_THLD_REG_2, tz->devdata +
				TSEN_INT0_WDOG_THLD_REG_2, trip, temp);
		break;
	default:
		break;
	}
	return 0;
}
static int ts_sys_set_trip_temp(struct thermal_zone_device *tz, int trip,
		unsigned long temp)
{
	ts_set_tz_trip_temp(tz, trip, temp);
	thsens_trips_temp[tz->id - 1][trip] = temp;
	return 0;
}

static void auto_read_temp(struct thermal_zone_device *tz, const u32 reg,
		const u32 datareg)
{
	int int0, int1, int2;
	unsigned long temp = 0;
	int curt_temp = 0;
	char *temp_info[3]    = { "TYPE=thsens_cpu", "TEMP=100000", NULL };
	mutex_lock(&con_lock);
	int0 = (readl(tz->devdata + reg) & TSEN_INT0_STATUS) >> 3;
	int1 = (readl(tz->devdata + reg) & TSEN_INT1_STATUS) >> 6;
	int2 = (readl(tz->devdata + reg) & TSEN_INT2_STATUS) >> 9;

	if (int0 | int1 | int2) {
		curt_temp = thsens_data_read(tz->devdata + reg,
				tz->devdata + datareg);
		sprintf(temp_info[0], "TYPE=%s", tz->type);
		sprintf(temp_info[1], "TEMP=%d", curt_temp);
		kobject_uevent_env(&(tz->device.kobj), KOBJ_CHANGE, temp_info);
		if (int0) {
			if ((readl(tz->devdata + reg) &
						TSEN_INT0_DIRECTION) >> 2) {
				ts_get_trip_temp(tz, 0, &temp);
				ts_set_tz_trip_temp(tz, 0, temp - BALANCE);
				__raw_writel(readl(tz->devdata + reg) ^
					TSEN_INT0_DIRECTION, tz->devdata + reg);

			} else {
				ts_get_trip_temp(tz, 0, &temp);
				ts_set_tz_trip_temp(tz, 0, temp + BALANCE);
			}
		} else if (int1) {
			if ((readl(tz->devdata + reg) &
						TSEN_INT1_DIRECTION) >> 5) {
				ts_get_trip_temp(tz, 1, &temp);
				ts_set_tz_trip_temp(tz, 1, temp - BALANCE);
				__raw_writel(readl(tz->devdata + reg) ^
					TSEN_INT1_DIRECTION, tz->devdata + reg);

			} else {
				ts_get_trip_temp(tz, 1, &temp);
				ts_set_tz_trip_temp(tz, 1, temp + BALANCE);
			}
		} else {
			if ((readl(tz->devdata + reg) &
						TSEN_INT2_DIRECTION) >> 8) {
				ts_get_trip_temp(tz, 2, &temp);
				ts_set_tz_trip_temp(tz, 2, temp - BALANCE);
				__raw_writel(readl(tz->devdata + reg) ^
					TSEN_INT2_DIRECTION, tz->devdata + reg);
			} else {
				ts_get_trip_temp(tz, 2, &temp);
				ts_set_tz_trip_temp(tz, 2, temp + BALANCE);
			}
		}
	} else
		thermal_zone_device_update(tz);
	mutex_unlock(&con_lock);
}

static irqreturn_t thermal_threaded_handle_irq(int irq, void *dev_id)
{
	auto_read_temp(thsens_vpu, TSEN_CFG_REG_0, TSEN_DATA_REG_0);
	auto_read_temp(thsens_cpu, TSEN_CFG_REG_1, TSEN_DATA_REG_1);
	if (g_flag != 3)
		auto_read_temp(thsens_gc, TSEN_CFG_REG_2, TSEN_DATA_REG_2);
	return IRQ_HANDLED;
}

static int ts_sys_get_crit_temp(struct thermal_zone_device *thermal,
		unsigned long *temp)
{
	return thsens_trips_temp[thermal->id - 1][TRIP_POINTS_NUM - 1];
}

static int ts_sys_notify(struct thermal_zone_device *thermal, int count,
		enum thermal_trip_type trip_type)
{
	if (THERMAL_TRIP_CRITICAL == trip_type)
		pr_info("notify critical temp hit\n");
	else
		pr_err("unexpected temp notify\n");
	return 0;
}

static struct thermal_zone_device_ops ts_ops = {
	.get_mode = ts_sys_get_mode,
	.get_temp = ts_sys_get_temp,
	.get_trip_type = ts_sys_get_trip_type,
	.get_trip_temp = ts_sys_get_trip_temp,
	.set_trip_temp = ts_sys_set_trip_temp,
	.get_crit_temp = ts_sys_get_crit_temp,
	.notify = ts_sys_notify,
};

#ifdef CONFIG_PM
static int thermal_suspend(struct device *dev)
{
	clk_disable_unprepare(therclk_vpu);
	clk_disable_unprepare(therclk_cpu);
	clk_disable_unprepare(therclk_gc);
	clk_disable_unprepare(therclk_g);
	return 0;
}

static int thermal_resume(struct device *dev)
{
	clk_prepare_enable(therclk_g);
	clk_prepare_enable(therclk_vpu);
	clk_prepare_enable(therclk_cpu);
	clk_prepare_enable(therclk_gc);
	return 0;
}

static const struct dev_pm_ops thermal_pm_ops = {
	.suspend = thermal_suspend,
	.resume = thermal_resume,
};
#endif

#ifdef CONFIG_OF
static const struct of_device_id pxa1928_thermal_match[] = {
	{ .compatible = "mrvl,thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, pxa1928_thermal_match);
#endif

void set_flag(unsigned int flag)
{
	g_flag = flag;
}
EXPORT_SYMBOL(set_flag);

static int pxa1928_thermal_probe(struct platform_device *pdev)
{
	int ret = 0, irq;
	int i, trip_w_mask = 0;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	void *reg_base;
	/* get resources from platform data */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		pr_err("%s: no IO memory defined\n", __func__);
		ret = -ENOENT;
		goto res_fail;
	}

	/* map registers.*/
	if (!devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), "thermal")) {
		pr_err("can't request region for resource %p\n", res);
		ret = -EINVAL;
		goto res_fail;
	}

	reg_base = devm_ioremap_nocache(&pdev->dev,
			res->start, resource_size(res));

	if (reg_base == NULL) {
		pr_err("%s: res %lx - %lx map failed\n", __func__,
			(unsigned long)res->start, (unsigned long)res->end);
		ret = -ENOMEM;
		goto remap_fail;
	}

	therclk_g = clk_get(NULL, "THERMALCLK_G");
	if (IS_ERR(therclk_g)) {
		pr_err("Could not get thermal clock global\n");
		ret = PTR_ERR(therclk_g);
		goto clk_fail;
	}
	clk_prepare_enable(therclk_g);

	therclk_vpu = clk_get(NULL, "THERMALCLK_VPU");
	if (IS_ERR(therclk_vpu)) {
		pr_err("Could not get thermal vpu clock\n");
		ret = PTR_ERR(therclk_vpu);
		goto clk_disable_g;
	}
	clk_prepare_enable(therclk_vpu);

	therclk_cpu = clk_get(NULL, "THERMALCLK_CPU");
	if (IS_ERR(therclk_cpu)) {
		pr_err("Could not get thermal cpu clock\n");
		ret = PTR_ERR(therclk_cpu);
		goto clk_disable_0;
	}
	clk_prepare_enable(therclk_cpu);

	therclk_gc = clk_get(NULL, "THERMALCLK_GC");
	if (IS_ERR(therclk_gc)) {
		pr_err("Could not get thermal gc clock\n");
		ret = PTR_ERR(therclk_gc);
		goto clk_disable_1;
	}
	clk_prepare_enable(therclk_gc);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		ret = -ENXIO;
		goto clk_disable_2;
	}
	pdev_irq = irq;
	ret = request_threaded_irq(irq, NULL, thermal_threaded_handle_irq,
			IRQF_ONESHOT, pdev->name, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		ret = -ENXIO;
		goto clk_disable_2;
	}
	mutex_init(&con_lock);

	if (IS_ENABLED(CONFIG_OF)) {
		if (np)
			of_property_read_u32(np, "marvell,version-flag",
					&g_flag);
	}

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		trip_w_mask |= (1 << i);

	thsens_vpu = thermal_zone_device_register(
			"thsens_vpu", TRIP_POINTS_NUM,
			trip_w_mask, NULL, &ts_ops, NULL, 0, 0);
	if (IS_ERR(thsens_vpu)) {
		pr_err("Failed to register VPU thermal zone device\n");
		ret = -EINVAL;
		goto free_irq;
	} else {
		ret = sysfs_create_group(&((thsens_vpu->device).kobj),
				&thermal_attr_grp);
		if (ret < 0) {
			pr_err("Failed to register private vpu thermal interface\n");
			goto unregister_tz_vpu;
		}
		enable_thsens(reg_base + TSEN_CFG_REG_0);
		init_wd_int0_threshold(0, reg_base + TSEN_INT0_WDOG_THLD_REG_0);
		init_int1_int2_threshold(0, reg_base +
				TSEN_INT1_INT2_THLD_REG_0);
		init_thsens_irq_dir(reg_base + TSEN_CFG_REG_0);
		enable_auto_read(reg_base + TSEN_CFG_REG_0);
		set_auto_read_interval(reg_base + TSEN_AUTO_READ_VALUE_REG_0);
		thsens_vpu->devdata = reg_base;
		dev_set_drvdata(&(thsens_vpu->device), thsens_vpu);
	}
	thsens_cpu = thermal_zone_device_register(
			"thsens_cpu", TRIP_POINTS_NUM,
			trip_w_mask, NULL, &ts_ops, NULL, 0, 0);
	if (IS_ERR(thsens_cpu)) {
		pr_err("Failed to register CPU thermal zone device\n");
		ret = -EINVAL;
		goto unregister_tz_vpu;
	} else {
		ret = sysfs_create_group(&((thsens_cpu->device).kobj),
				&thermal_attr_grp);
		if (ret < 0) {
			pr_err("Failed to register private cpu thermal interface\n");
			goto unregister_tz_cpu;
		}
		enable_thsens(reg_base + TSEN_CFG_REG_1);
		init_wd_int0_threshold(1, reg_base +
				TSEN_INT0_WDOG_THLD_REG_1);
		init_int1_int2_threshold(1, reg_base +
				TSEN_INT1_INT2_THLD_REG_1);
		init_thsens_irq_dir(reg_base + TSEN_CFG_REG_1);
		enable_auto_read(reg_base + TSEN_CFG_REG_1);
		set_auto_read_interval(reg_base + TSEN_AUTO_READ_VALUE_REG_1);
		thsens_cpu->devdata = reg_base;
		dev_set_drvdata(&(thsens_cpu->device), thsens_cpu);
	}
	if (g_flag != 3) {
		thsens_gc = thermal_zone_device_register(
				"thsens_gc", TRIP_POINTS_NUM,
				trip_w_mask, NULL, &ts_ops, NULL, 0, 0);
		if (IS_ERR(thsens_gc)) {
			pr_err("Failed to register GC thermal zone device\n");
			ret = -EINVAL;
			goto unregister_tz_cpu;
		} else {
			ret = sysfs_create_group(&((thsens_gc->device).kobj),
					&thermal_attr_grp);
			if (ret < 0) {
				pr_err("Failed to register gc thermal\n");
				goto unregister_tz_gc;
			}
			enable_thsens(reg_base + TSEN_CFG_REG_2);
			init_wd_int0_threshold(2, reg_base +
					TSEN_INT0_WDOG_THLD_REG_2);
			init_int1_int2_threshold(2, reg_base +
					TSEN_INT1_INT2_THLD_REG_2);
			init_thsens_irq_dir(reg_base + TSEN_CFG_REG_2);
			enable_auto_read(reg_base + TSEN_CFG_REG_2);
			set_auto_read_interval(reg_base +
					TSEN_AUTO_READ_VALUE_REG_2);
			thsens_gc->devdata = reg_base;
			dev_set_drvdata(&(thsens_gc->device), thsens_gc);
		}
	}
	return ret;
unregister_tz_gc:
	thermal_zone_device_unregister(thsens_gc);
	sysfs_remove_group(&((thsens_gc->device).kobj), &thermal_attr_grp);
unregister_tz_cpu:
	thermal_zone_device_unregister(thsens_cpu);
	sysfs_remove_group(&((thsens_cpu->device).kobj), &thermal_attr_grp);
unregister_tz_vpu:
	thermal_zone_device_unregister(thsens_vpu);
	sysfs_remove_group(&((thsens_vpu->device).kobj), &thermal_attr_grp);
free_irq:
	free_irq(pdev_irq, NULL);
clk_disable_2:
	clk_disable_unprepare(therclk_gc);
clk_disable_1:
	clk_disable_unprepare(therclk_cpu);
clk_disable_0:
	clk_disable_unprepare(therclk_vpu);
clk_disable_g:
	clk_disable_unprepare(therclk_g);
clk_fail:
	devm_iounmap(&pdev->dev, reg_base);
remap_fail:
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
res_fail:
	pr_err("device init failed\n");

	return ret;
}

static int pxa1928_thermal_remove(struct platform_device *pdev)
{
	clk_disable_unprepare(therclk_vpu);
	clk_disable_unprepare(therclk_cpu);
	clk_disable_unprepare(therclk_gc);
	clk_disable_unprepare(therclk_g);
	thermal_zone_device_unregister(thsens_vpu);
	sysfs_remove_group(&((thsens_vpu->device).kobj), &thermal_attr_grp);
	thermal_zone_device_unregister(thsens_cpu);
	sysfs_remove_group(&((thsens_cpu->device).kobj), &thermal_attr_grp);
	thermal_zone_device_unregister(thsens_gc);
	sysfs_remove_group(&((thsens_gc->device).kobj), &thermal_attr_grp);
	free_irq(pdev_irq, NULL);
	pr_info("PXA1928: Kernel Thermal management unregistered\n");

	return 0;
}

static struct platform_driver pxa1928_thermal_driver = {
	.driver = {
		.name	= "thermal",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &thermal_pm_ops,
#endif
		.of_match_table = of_match_ptr(pxa1928_thermal_match),
	},
	.probe		= pxa1928_thermal_probe,
	.remove		= pxa1928_thermal_remove,
};

static int __init pxa1928_thermal_init(void)
{
	return platform_driver_register(&pxa1928_thermal_driver);
}

static void __exit pxa1928_thermal_exit(void)
{
	platform_driver_unregister(&pxa1928_thermal_driver);
}

module_init(pxa1928_thermal_init);
module_exit(pxa1928_thermal_exit);

MODULE_AUTHOR("Marvell Semiconductor");
MODULE_DESCRIPTION("PXA1928 SoC thermal driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa1928-thermal");
