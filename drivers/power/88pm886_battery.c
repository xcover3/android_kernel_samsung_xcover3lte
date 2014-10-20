/*
 * Battery driver for Marvell 88PM886 fuel-gauge chip
 *
 * Copyright (c) 2014 Marvell International Ltd.
 * Author:	Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/power_supply.h>
#include <linux/mfd/88pm886.h>
#include <linux/delay.h>
#include <linux/math64.h>
#include <linux/of_device.h>
#include <linux/iio/iio.h>
#include <linux/iio/consumer.h>

#define PM886_VBAT_MEAS_EN		(1 << 1)
#define PM886_GPADC_BD_PREBIAS		(1 << 4)
#define PM886_GPADC_BD_EN		(1 << 5)
#define PM886_BD_GP_SEL			(1 << 6)

#define PM886_CC_CONFIG1		(0x01)
#define PM886_CC_EN			(1 << 0)
#define PM886_CC_CLR_ON_RD		(1 << 2)
#define PM886_SD_PWRUP			(1 << 3)

#define PM886_CC_CONFIG2		(0x02)
#define PM886_CC_READ_REQ		(1 << 0)
#define PM886_OFFCOMP_EN		(1 << 1)

#define PM886_CC_VAL1			(0x03)
#define PM886_CC_VAL2			(0x04)
#define PM886_CC_VAL3			(0x05)
#define PM886_CC_VAL4			(0x06)
#define PM886_CC_VAL5			(0x07)

#define PM886_IBAT_VAL1			(0x08)		/* LSB */
#define PM886_IBAT_VAL2			(0x09)
#define PM886_IBAT_VAL3			(0x0a)

#define PM886_IBAT_EOC_CONFIG		(0x0f)
#define PM886_IBAT_MEAS_EN		(1 << 0)

#define PM886_IBAT_EOC_MEAS1		(0x10)		/* bit [7 : 0] */
#define PM886_IBAT_EOC_MEAS2		(0x11)		/* bit [15: 8] */

#define PM886_CC_LOW_TH1		(0x12)		/* bit [7 : 0] */
#define PM886_CC_LOW_TH2		(0x13)		/* bit [15 : 8] */

#define PM886_VBAT_AVG_MSB		(0xa0)
#define PM886_VBAT_AVG_LSB		(0xa1)

#define PM886_VBAT_SLP_MSB		(0xb0)
#define PM886_VBAT_SLP_LSB		(0xb1)

#define PM886_SLP_CNT1			(0xce)

#define PM886_SLP_CNT2			(0xcf)
#define PM886_SLP_CNT_HOLD		(1 << 6)
#define PM886_SLP_CNT_RST		(1 << 7)

#define MONITOR_INTERVAL		(HZ * 30)
#define LOW_BAT_INTERVAL		(HZ * 5)
#define LOW_BAT_CAP			(15)

/* this flag is used to decide whether the ocv_flag needs update */
static atomic_t in_resume = ATOMIC_INIT(0);

enum {
	ALL_SAVED_DATA,
	SLEEP_COUNT,
};

enum {
	VBATT_CHAN,
	VBATT_SLP_CHAN,
	TEMP_CHAN,
	MAX_CHAN = 3,
};

struct pm886_battery_params {
	int status;
	int present;
	int volt;	/* µV */
	int ibat;	/* mA */
	int soc;	/* percents: 0~100% */
	int health;
	int tech;
	int temp;
};

struct temp_vs_ohm {
	unsigned int ohm;
	int temp;
};

struct pm886_battery_info {
	struct pm886_chip	*chip;
	struct device	*dev;
	struct pm886_battery_params	bat_params;

	struct power_supply	battery;
	struct delayed_work	monitor_work;
	struct delayed_work	charged_work;
	struct delayed_work	cc_work; /* sigma-delta offset compensation */
	struct workqueue_struct *bat_wqueue;

	int			total_capacity;

	bool			use_ntc;
	int			gpadc_no;

	bool			ocv_is_realiable;
	int			range_low_th;
	int			range_high_th;
	int			sleep_counter_th;

	int			power_off_th;
	int			safe_power_off_th;

	int			alart_percent;

	int			irq_nums;
	int			irq[7];

	struct iio_channel	*chan[MAX_CHAN];
	struct temp_vs_ohm	*temp_ohm_table;
	int			temp_ohm_table_size;
	int			zero_degree_ohm;

	int			abs_lowest_temp;
	int			t1;
	int			t2;
	int			t3;
	int			t4;
};

static int ocv_table[100];

static char *supply_interface[] = {
	"ac",
	"usb",
};

struct ccnt {
	int soc;	/* mC, 1mAH = 1mA * 3600 = 3600 mC */
	int max_cc;
	int last_cc;
	int cc_offs;
	int alart_cc;
};
static struct ccnt ccnt_data;

/*
 * the save_buffer mapping:
 *
 * | o o o o   o o | o o ||         o      | o o o   o o o o |
 * |<--- temp ---> |     ||ocv_is_realiable|      SoC        |
 * |---RTC_SPARE6(0xef)--||-----------RTC_SPARE5(0xee)-------|
 */
struct save_buffer {
	int soc;
	bool ocv_is_realiable;
	int temp;
};
static struct save_buffer extern_data;

static int pm886_get_batt_vol(struct pm886_battery_info *info, int active);
/*
 * - saved SoC
 * - ocv_is_realiable flag
 */
static int get_extern_data(struct pm886_battery_info *info, int flag)
{
	u8 buf[2];
	unsigned int val;
	int ret;

	if (!info) {
		pr_err("%s: 88pm886 device info is empty!\n", __func__);
		return 0;
	}

	switch (flag) {
	case ALL_SAVED_DATA:
		ret = regmap_bulk_read(info->chip->base_regmap,
				       PM886_RTC_SPARE5, buf, 2);
		if (ret < 0) {
			val = 0;
			break;
		}

		val = (buf[0] & 0xff) | ((buf[1] & 0xff) << 8);
		break;
	default:
		val = 0;
		dev_err(info->dev, "%s: unexpected case %d.\n", __func__, flag);
		break;
	}

	dev_dbg(info->dev, "%s: val = 0x%x\n", __func__, val);
	return val;
}
static void set_extern_data(struct pm886_battery_info *info, int flag, int data)
{
	u8 buf[2];
	unsigned int val;
	int ret;

	if (!info) {
		pr_err("88pm886 device info is empty!\n");
		return;
	}

	switch (flag) {
	case ALL_SAVED_DATA:
		buf[0] = data & 0xff;
		ret = regmap_read(info->chip->base_regmap,
				  PM886_RTC_SPARE6, &val);
		if (ret < 0)
			return;
		dev_dbg(info->dev, "%s: buf[0] = 0x%x\n", __func__, buf[0]);

		buf[1] = ((data >> 8) & 0xfc) | (val & 0x3);
		ret = regmap_bulk_write(info->chip->base_regmap,
					PM886_RTC_SPARE5, buf, 2);
		if (ret < 0)
			return;
		dev_dbg(info->dev, "%s: buf[1] = 0x%x\n", __func__, buf[1]);

		break;
	default:
		dev_err(info->dev, "%s: unexpected case %d.\n", __func__, flag);
		break;
	}

	return;
}

static int pm886_battery_write_buffer(struct pm886_battery_info *info,
				      struct save_buffer *value)
{
	int data;
	/* save values in RTC registers */
	data = (value->soc & 0x7f) | (value->ocv_is_realiable << 7);

	/* bits 0,1 are used for other purpose, so give up them * */
	data |= (((value->temp / 10) + 50) & 0xfc) << 8;
	set_extern_data(info, ALL_SAVED_DATA, data);

	return 0;
}

static int pm886_battery_read_buffer(struct pm886_battery_info *info,
				     struct save_buffer *value)
{
	int data;

	/* read values from RTC registers */
	data = get_extern_data(info, ALL_SAVED_DATA);
	value->soc = data & 0x7f;
	value->ocv_is_realiable = (data & 0x80) >> 7;
	value->temp = (((data >> 8) & 0xfc) - 50) * 10;
	if (value->temp < 0)
		value->temp = 0;

	/*
	 * if register is 0, then stored values are invalid,
	 * it may be casused by backup battery totally discharged
	 */
	if (data == 0) {
		dev_err(info->dev, "attention: saved value isn't realiable!\n");
		return -EINVAL;
	}

	return 0;
}

static int is_charger_online(struct pm886_battery_info *info,
			     int *who_is_charging)
{
	struct power_supply *psy;
	union power_supply_propval val;
	int i, ret = 0;

	for (i = 0; i < info->battery.num_supplicants; i++) {
		psy = power_supply_get_by_name(info->battery.supplied_to[i]);
		if (!psy || !psy->get_property)
			continue;
		ret = psy->get_property(psy, POWER_SUPPLY_PROP_ONLINE, &val);
		if (ret == 0) {
			if (val.intval) {
				*who_is_charging = i;
				return val.intval;
			}
		}
	}
	*who_is_charging = 0;
	return 0;
}

static int pm886_battery_get_charger_status(struct pm886_battery_info *info)
{
	int who, ret;
	struct power_supply *psy;
	union power_supply_propval val;
	int status = POWER_SUPPLY_STATUS_UNKNOWN;

	/* report charger online timely */
	if (!is_charger_online(info, &who))
		status = POWER_SUPPLY_STATUS_DISCHARGING;
	else {
		psy = power_supply_get_by_name(info->battery.supplied_to[who]);
		if (!psy || !psy->get_property) {
			pr_info("%s: get power supply failed.\n", __func__);
			return POWER_SUPPLY_STATUS_UNKNOWN;
		}
		ret = psy->get_property(psy, POWER_SUPPLY_PROP_STATUS, &val);
		if (ret) {
			dev_err(info->dev, "get charger property failed.\n");
			status = POWER_SUPPLY_STATUS_DISCHARGING;
		} else {
			/* update battery status */
			status = val.intval;
		}
	}
	return status;
}

static int pm886_enable_bat_detect(struct pm886_battery_info *info, bool enable)
{
	int data, mask, ret = 0;

	/*
	 * 0. gpadc is in non-stop mode
	 *    enable at least another measurement
	 *    done in the mfd driver
	 */

	/* 1. choose gpadc 1/3 to detect battery */
	switch (info->gpadc_no) {
	case 1:
		data = 0;
		mask = PM886_BD_GP_SEL;
		break;
	case 3:
		data = mask = PM886_BD_GP_SEL;
		break;
	default:
		dev_err(info->dev,
			"wrong gpadc number: %d\n", info->gpadc_no);
		return -EINVAL;
	}

	if (enable) {
		data |= (PM886_GPADC_BD_EN | PM886_GPADC_BD_PREBIAS);
		mask |= (PM886_GPADC_BD_EN | PM886_GPADC_BD_PREBIAS);
	} else {
		data = 0;
		mask = PM886_GPADC_BD_EN | PM886_GPADC_BD_PREBIAS;
	}

	ret = regmap_update_bits(info->chip->gpadc_regmap,
				 PM886_GPADC_CONFIG8, mask, data);
	return ret;
}

static bool pm886_check_battery_present(struct pm886_battery_info *info)
{
	static bool present;
	int data, ret = 0;

	if (!info) {
		pr_err("%s: empty battery info.\n", __func__);
		return true;
	}

	if (info->use_ntc) {
		ret = pm886_enable_bat_detect(info, true);
		if (ret < 0) {
			present = true;
			goto out;
		}
		regmap_read(info->chip->base_regmap, PM886_STATUS1, &data);
		present = !!(data & PM886_BAT_DET);
	} else {
		present = true;
	}
out:
	if (ret < 0)
		present = true;

	/* disable the battery detection to measure the battery temperature*/
	pm886_enable_bat_detect(info, false);

	return present;
}

static bool system_is_reboot(struct pm886_battery_info *info)
{
	if (!info || !info->chip)
		return true;
	return !((info->chip->powerdown1) || (info->chip->powerdown2));
}

static bool check_battery_change(struct pm886_battery_info *info,
				 int new_soc, int saved_soc)
{
	int status, remove_th;
	if (!info) {
		pr_err("%s: empty battery info!\n", __func__);
		return true;
	}

	info->bat_params.status = pm886_battery_get_charger_status(info);
	status = info->bat_params.status;
	switch (status) {
	case POWER_SUPPLY_STATUS_DISCHARGING:
	case POWER_SUPPLY_STATUS_UNKNOWN:
		if (saved_soc < 5)
			remove_th = 5;
		else if (saved_soc < 15)
			remove_th = 10;
		else if (saved_soc < 80)
			remove_th = 15;
		else
			remove_th = 5;
		break;
	default:
		remove_th = 60;
		break;
	}
	dev_info(info->dev, "new_soc = %d, saved_soc = %d, remove = %d\n",
		 new_soc, saved_soc, remove_th);

	return !!(abs(new_soc - saved_soc) > remove_th);
}

static bool check_soc_range(struct pm886_battery_info *info, int soc)
{
	if (!info) {
		pr_err("%s: empty battery info!\n", __func__);
		return true;
	}

	if ((soc > info->range_low_th) && (soc < info->range_high_th))
		return false;
	else
		return true;
}

/*
 * register 1 bit[7:0] -- bit[11:4] of measured value of voltage
 * register 0 bit[3:0] -- bit[3:0] of measured value of voltage
 */
static int pm886_get_batt_vol(struct pm886_battery_info *info, int active)
{
	struct iio_channel *channel;
	int vol, ret;

	if (active)
		channel = info->chan[VBATT_CHAN];
	else
		channel = info->chan[VBATT_SLP_CHAN];

	if (!channel) {
		dev_err(info->dev, "cannot get the useable channel: %s\n",
			channel->channel->datasheet_name);
		return -EINVAL;
	}

	ret = iio_read_channel_processed(channel, &vol);
	if (ret < 0) {
		dev_err(info->dev, "read %s channel fails!\n",
			channel->channel->datasheet_name);
		return ret;
	}

	/* change to micro-voltage */
	vol /= 1000;

	dev_dbg(info->dev, "%s: active = %d, voltage = %dmV\n",
		__func__, active, vol);

	return vol;
}

/* get soc from ocv: lookup table */
static int pm886_get_soc_from_ocv(int ocv)
{
	int i, count = 100;
	int soc = 0;

	if (ocv < ocv_table[0]) {
		soc = 0;
		goto out;
	}

	count = 100;
	for (i = count - 1; i >= 0; i--) {
		if (ocv >= ocv_table[i]) {
			soc = i + 1;
			break;
		}
	}
out:
	return soc;
}

static int pm886_get_ibat_cc(struct pm886_battery_info *info)
{
	int ret, data;
	unsigned char buf[3];

	ret = regmap_bulk_read(info->chip->battery_regmap, PM886_IBAT_VAL1,
			       buf, 3);
	if (ret < 0)
		goto out;

	data = ((buf[2] & 0x3) << 16) | ((buf[1] & 0xff) << 8) | buf[0];

	/* discharging, ibat < 0; charging, ibat > 0 */
	if (data & (1 << 17))
		data = (0xff << 24) | (0x3f << 18) | data;

	/* the current LSB is 0.04578mA */
	data = (data * 458) / 10;
	dev_dbg(info->dev, "%s--> ibat_cc = %duA, %dmA\n", __func__,
		data, data / 1000);

out:
	if (ret < 0)
		data = 100;

	return data;
}

static void find_match(struct pm886_battery_info *info,
		       unsigned int ohm, int *low, int *high)
{
	int start, end, mid;
	int size = info->temp_ohm_table_size;

	/* the resistor value decends as the temperature increases */
	if (ohm >= info->temp_ohm_table[0].ohm) {
		*low = 0;
		*high = 0;
		return;
	}

	if (ohm <= info->temp_ohm_table[size - 1].ohm) {
		*low = size - 1;
		*high = size -1;
		return;
	}

	start = 0;
	end = size;
	while (start < end) {
		mid = start + (end - start) / 2;
		if (ohm > info->temp_ohm_table[mid].ohm) {
			end = mid;
		} else {
			start = mid + 1;
			if (ohm >= info->temp_ohm_table[start].ohm)
				end = start;
		}
	}

	*low = end;
	if (ohm == info->temp_ohm_table[end].ohm)
		*high = end;
	else
		*high = end -1;
}

static int pm886_get_batt_temp(struct pm886_battery_info *info)
{
	struct iio_channel *channel= info->chan[TEMP_CHAN];
	int ohm, ret;
	int temp, low, high, low_temp, high_temp, low_ohm, high_ohm;

	if (!channel) {
		dev_err(info->dev, "cannot get the useable channel: %s\n",
			channel->channel->datasheet_name);
		return -EINVAL;
	}

	ret = iio_read_channel_scale(channel, &ohm, NULL);
	if (ret < 0) {
		dev_err(info->dev, "read %s channel fails!\n",
			channel->channel->datasheet_name);
		return ret;
	}

	find_match(info, ohm, &low, &high);
	if (low == high) {
		temp = info->temp_ohm_table[low].temp;
	} else {
		low_temp = info->temp_ohm_table[low].temp;
		low_ohm = info->temp_ohm_table[low].ohm;

		high_temp = info->temp_ohm_table[high].temp;
		high_ohm = info->temp_ohm_table[high].ohm;

		temp = (ohm - low_ohm) * (high_temp - low_temp) / (high_ohm - low_ohm);
	}
	dev_dbg(info->dev, "ohm = %d, low = %d, high = %d, temp = %d\n",
		ohm, low, high, temp);

	return temp;
}

/*	Temperature ranges:
 *
 * ----------|---------------|---------------|---------------|----------
 *           |               |               |               |
 *  Shutdown |   Charging    |   Charging    |   Charging    | Shutdown
 *           |  not allowed  |    allowed    |  not allowed  |
 * ----------|---------------|---------------|---------------|----------
 *       too_cold(t1)	    cold(t2)	    hot(t3)	   too_hot(t4)
 *
 */
enum {
	COLD_NO_CHARGING,
	LOW_TEMP_RANGE,
	STD_TEMP_RANGE,
	HIGH_TEMP_RANGE,
	HOT_NO_CHARGING,

	MAX_RANGE,
};

static int pm886_get_batt_health(struct pm886_battery_info *info)
{
	int temp, health, range, old_range = MAX_RANGE;
	if (!info->bat_params.present) {
		info->bat_params.health = POWER_SUPPLY_HEALTH_UNKNOWN;
		return 0;
	}

	temp = pm886_get_batt_temp(info);
	if (temp < 0) {
		return POWER_SUPPLY_HEALTH_GOOD;
	}

	if (temp < info->t1)
		range = COLD_NO_CHARGING;
	else if (temp < info->t2)
		range = LOW_TEMP_RANGE;
	else if (temp < info->t3)
		range = STD_TEMP_RANGE;
	else if (temp < info->t4)
		range = HIGH_TEMP_RANGE;
	else
		range = HOT_NO_CHARGING;

	switch (range) {
	case COLD_NO_CHARGING:
		health = POWER_SUPPLY_HEALTH_DEAD;
		break;
	case LOW_TEMP_RANGE:
		health = POWER_SUPPLY_HEALTH_COLD;
		break;
	case STD_TEMP_RANGE:
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	case HIGH_TEMP_RANGE:
		health = POWER_SUPPLY_HEALTH_OVERHEAT;
		break;
	case HOT_NO_CHARGING:
		health = POWER_SUPPLY_HEALTH_DEAD;
		break;
	default:
		health = POWER_SUPPLY_HEALTH_GOOD;
		break;
	}
	if ((old_range != range) && (old_range != MAX_RANGE)) {
		dev_dbg(info->dev, "temperature changes: %d --> %d\n",
			old_range, range);
		power_supply_changed(&info->battery);
	}
	old_range = range;

	return health;
}

static int pm886_battery_get_slp_cnt(struct pm886_battery_info *info)
{
	int buf[2], ret, cnt;
	unsigned int mask, data;

	ret = regmap_bulk_read(info->chip->base_regmap, PM886_SLP_CNT1, buf, 2);
	if (ret) {
		cnt = 0;
		goto out;
	}

	cnt = ((buf[1] & 0x07) << 8) | buf[0];
	cnt &= 0x7ff;
out:
	/* reset the sleep counter by setting SLP_CNT_RST */
	data = mask = PM886_SLP_CNT_RST;
	ret = regmap_update_bits(info->chip->base_regmap,
				 PM886_SLP_CNT2, mask, data);

	return cnt;
}

/*
 * check whether it's the right point to set ocv_is_realiable flag
 * if yes, set it;
 *   else, leave it as 0;
 */
static void check_set_ocv_flag(struct pm886_battery_info *info,
			       struct ccnt *ccnt_val)
{
	int old_soc, vol, slp_cnt, new_soc, low_th, high_th;
	bool soc_in_good_range;

	/* save old SOC in case to recover */
	old_soc = ccnt_val->soc;
	low_th = info->range_low_th;
	high_th = info->range_high_th;

	if (info->ocv_is_realiable)
		return;

	/* check if battery is relaxed enough */
	slp_cnt = pm886_battery_get_slp_cnt(info);
	dev_dbg(info->dev, "%s: slp_cnt = %d seconds\n", __func__, slp_cnt);

	if (slp_cnt < info->sleep_counter_th) {
		dev_dbg(info->dev, "battery is not relaxed.\n");
		return;
	}

	dev_dbg(info->dev, "battery has slept %d second.\n", slp_cnt);

	/* read last sleep voltage and calc new SOC */
	vol = pm886_get_batt_vol(info, 0);
	new_soc = pm886_get_soc_from_ocv(vol);

	/* check if the new SoC is in good range or not */
	soc_in_good_range = check_soc_range(info, new_soc);
	if (soc_in_good_range) {
		info->ocv_is_realiable = 1;
		ccnt_val->soc = new_soc;
		dev_info(info->dev, "good range: new SoC = %d\n", new_soc);
	} else {
		info->ocv_is_realiable = 0;
		ccnt_val->soc = old_soc;
		dev_info(info->dev, "in bad range (%d), no update\n", old_soc);
	}

	ccnt_val->last_cc =
		(ccnt_val->max_cc / 1000) * (ccnt_val->soc * 10 + 5);
}

static int pm886_battery_calc_ccnt(struct pm886_battery_info *info,
				   struct ccnt *ccnt_val)
{
	int data, ret, factor;
	u8 buf[5];
	s64 ccnt_uc = 0, ccnt_mc = 0;

	/* 1. read columb counter to get the original SoC value */
	regmap_read(info->chip->battery_regmap, PM886_CC_CONFIG2, &data);
	/*
	 * set PM886_CC_READ_REQ to read Qbat_cc,
	 * if it has been set, then it means the data not ready
	 */
	if (!(data & PM886_CC_READ_REQ))
		regmap_update_bits(info->chip->battery_regmap, PM886_CC_CONFIG2,
				   PM886_CC_READ_REQ, PM886_CC_READ_REQ);
	/* wait until Qbat_cc is ready */
	do {
		regmap_read(info->chip->battery_regmap, PM886_CC_CONFIG2,
			    &data);
	} while ((data & PM886_CC_READ_REQ));

	ret = regmap_bulk_read(info->chip->battery_regmap, PM886_CC_VAL1,
			       buf, 5);
	if (ret < 0)
		return ret;

	ccnt_uc = (s64) (((s64)(buf[4]) << 32)
			 | (u64)(buf[3] << 24) | (u64)(buf[2] << 16)
			 | (u64)(buf[1] << 8) | (u64)buf[0]);

	dev_dbg(info->dev, "buf[0 ~ 4] = 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
		 buf[0], buf[1], buf[2], buf[3], buf[4]);
	/* Factor is nC */
	factor = 715;
	ccnt_uc = ccnt_uc * factor;
	ccnt_uc = div_s64(ccnt_uc, 1000);
	ccnt_mc = div_s64(ccnt_uc, 1000);
	dev_dbg(info->dev, "%s--> ccnt_uc: %lld uC, ccnt_mc: %lld mC\n",
		__func__, ccnt_uc, ccnt_mc);

	/* 2. add the value */
	ccnt_val->last_cc += ccnt_mc;

	/* 3. clap battery SoC for sanity check */
	if (ccnt_val->last_cc > ccnt_val->max_cc) {
		ccnt_val->soc = 100;
		ccnt_val->last_cc = ccnt_val->max_cc;
	}
	if (ccnt_val->last_cc < 0) {
		ccnt_val->soc = 0;
		ccnt_val->last_cc = 0;
	}

	ccnt_val->soc = ccnt_val->last_cc * 100 / ccnt_val->max_cc;

	dev_dbg(info->dev,
		 "%s<-- ccnt_val->soc: %d, ccnt_val->last_cc: %d mC\n",
		 __func__, ccnt_val->soc, ccnt_val->last_cc);

	return 0;
}

/* correct SoC according to user scenario */
static void pm886_battery_correct_soc(struct pm886_battery_info *info,
				      struct ccnt *ccnt_val)
{
	static int chg_status;

	info->bat_params.volt = pm886_get_batt_vol(info, 1);
	if (info->bat_params.status == POWER_SUPPLY_STATUS_UNKNOWN) {
		dev_dbg(info->dev, "battery status unknown, dont update\n");
		return;
	}

	/*
	 * use ccnt_val, which is the real capacity,
	 * not use the info->bat_parmas.soc
	 */
	chg_status = pm886_battery_get_charger_status(info);
	switch (chg_status) {
	case POWER_SUPPLY_STATUS_CHARGING:
		/* TODO: add protection here? */
		dev_dbg(info->dev, "%s: before: charging-->capacity: %d%%\n",
			__func__, ccnt_val->soc);
		info->bat_params.status = POWER_SUPPLY_STATUS_CHARGING;
		dev_dbg(info->dev, "%s: after: charging-->capacity: %d%%\n",
			__func__, ccnt_val->soc);
		break;
	case POWER_SUPPLY_STATUS_FULL:
		dev_dbg(info->dev, "%s: before: full-->capacity: %d\n",
			__func__, ccnt_val->soc);

		if (ccnt_val->soc < 100) {
			dev_info(info->dev,
				 "%s: %d: capacity %d%% < 100%%, ramp up..\n",
				 __func__, __LINE__, ccnt_val->soc);
			ccnt_val->soc++;
			info->bat_params.status = POWER_SUPPLY_STATUS_CHARGING;
		}

		if (ccnt_val->soc >= 100) {
			ccnt_val->soc = 100;
			info->bat_params.status = POWER_SUPPLY_STATUS_FULL;
		}
		dev_dbg(info->dev, "%s: after: full-->capacity: %d%%\n",
			__func__, ccnt_val->soc);
		break;
	case POWER_SUPPLY_STATUS_DISCHARGING:
	case POWER_SUPPLY_STATUS_UNKNOWN:
	default:
		dev_dbg(info->dev, "%s: before: discharging-->capacity: %d%%\n",
			__func__, ccnt_val->soc);
		/*
		 * power_off_th      ===> capacity is no less than 1%
		 *	|
		 *	|
		 *	v
		 * safe_power_off_th ===> capacity is 0%
		 *	|
		 *	|
		 *	v
		 * system is dead
		 */
		if (info->bat_params.volt <= info->power_off_th) {
			if (ccnt_val->soc > 0)
				ccnt_val->soc--;
			else if (ccnt_val->soc == 0)
				ccnt_val->soc = 1;
		} else if (info->bat_params.volt < info->safe_power_off_th) {
			dev_info(info->dev, "%s: for safe: voltage = %d\n",
				 __func__, info->bat_params.volt);
			if (ccnt_val->soc >= 1)
				ccnt_val->soc--;
		}
		if (ccnt_val->soc <= 0)
			ccnt_val->soc = 0;

		dev_dbg(info->dev, "%s: after: discharging-->capacity: %d%%\n",
			__func__, ccnt_val->soc);
		break;
	}

	ccnt_val->last_cc =
		(ccnt_val->max_cc / 1000) * (ccnt_val->soc * 10 + 5);

	return;
}

static void pm886_bat_update_status(struct pm886_battery_info *info)
{
	int ibat;

	info->bat_params.volt = pm886_get_batt_vol(info, 1);

	ibat = pm886_get_ibat_cc(info);
	info->bat_params.ibat = ibat / 1000; /* change to mA */

	if (info->bat_params.present == 0) {
		info->bat_params.status = POWER_SUPPLY_STATUS_UNKNOWN;
		info->bat_params.temp = 250;
		info->bat_params.soc = 80;
		return;
	}

	/* use charger status if the battery is present */
	info->bat_params.status = pm886_battery_get_charger_status(info);

	/* measure temperature if the battery is present */
	info->bat_params.temp = pm886_get_batt_temp(info) * 10;
	info->bat_params.health = pm886_get_batt_health(info);

	pm886_battery_calc_ccnt(info, &ccnt_data);
	pm886_battery_correct_soc(info, &ccnt_data);
	info->bat_params.soc = ccnt_data.soc;
}

static void pm886_battery_monitor_work(struct work_struct *work)
{
	struct pm886_battery_info *info;
	static int prev_cap = -1;
	static int prev_volt = -1;
	static int prev_status = -1;;

	info = container_of(work, struct pm886_battery_info, monitor_work.work);

	pm886_bat_update_status(info);
	dev_dbg(info->dev, "%s is called, status update finished.\n", __func__);

	if (atomic_read(&in_resume) == 1) {
		check_set_ocv_flag(info, &ccnt_data);
		atomic_set(&in_resume, 0);
	}

	/* notify when parameters are changed */
	if ((prev_cap != info->bat_params.soc)
	    || (abs(prev_volt - info->bat_params.volt) > 100)
	    || (prev_status != info->bat_params.status)) {

		power_supply_changed(&info->battery);
		prev_cap = info->bat_params.soc;
		prev_volt = info->bat_params.volt;
		prev_status = info->bat_params.status;
		dev_info(info->dev,
			 "cap=%d, temp=%d, volt=%d, ocv_is_realiable=%d\n",
			 info->bat_params.soc, info->bat_params.temp / 10,
			 info->bat_params.volt, info->ocv_is_realiable);
	}

	/* save the recent value in non-volatile memory */
	extern_data.soc = ccnt_data.soc;
	extern_data.temp = info->bat_params.temp;
	extern_data.ocv_is_realiable = info->ocv_is_realiable;
	pm886_battery_write_buffer(info, &extern_data);

	if (info->bat_params.soc <= LOW_BAT_CAP)
		queue_delayed_work(info->bat_wqueue, &info->monitor_work,
				   LOW_BAT_INTERVAL);
	else
		queue_delayed_work(info->bat_wqueue, &info->monitor_work,
				   MONITOR_INTERVAL);
}

static void pm886_charged_work(struct work_struct *work)
{
	struct pm886_battery_info *info =
		container_of(work, struct pm886_battery_info,
			     charged_work.work);

	pm886_bat_update_status(info);
	power_supply_changed(&info->battery);
	return;
}

static int pm886_setup_fuelgauge(struct pm886_battery_info *info)
{
	int ret = 0, data, mask, tmp;
	u8 buf[2];

	if (!info) {
		pr_err("%s: empty battery info.\n", __func__);
		return true;
	}

	/* 0. set the CCNT_LOW_TH before the CC_EN is set 23.43mC/LSB */
	tmp = ccnt_data.alart_cc * 100 / 2343;
	buf[0] = (u8)(tmp & 0xff);
	buf[1] = (u8)((tmp & 0xff00) >> 8);
	regmap_bulk_write(info->chip->battery_regmap, PM886_CC_LOW_TH1, buf, 2);

	/* 1. set PM886_OFFCOMP_EN to compensate Ibat, SWOFF_EN = 0 */
	data = mask = PM886_OFFCOMP_EN;
	ret = regmap_update_bits(info->chip->battery_regmap, PM886_CC_CONFIG2,
				 mask, data);
	if (ret < 0)
		goto out;

	/* 2. set the EOC battery current as 100mA, done in charger driver */

	/* 3. use battery current to decide the EOC */
	data = mask = PM886_IBAT_MEAS_EN;
	ret = regmap_update_bits(info->chip->battery_regmap,
				PM886_IBAT_EOC_CONFIG, mask, data);
	/*
	 * 4. set SD_PWRUP to enable sigma-delta
	 *    set CC_CLR_ON_RD to clear coulomb counter on read
	 *    set CC_EN to enable coulomb counter
	 */
	data = mask = PM886_SD_PWRUP | PM886_CC_CLR_ON_RD | PM886_CC_EN;
	ret = regmap_update_bits(info->chip->battery_regmap, PM886_CC_CONFIG1,
				 mask, data);
	if (ret < 0)
		goto out;

	/* 5. enable VBAT measurement */
	data = mask = PM886_VBAT_MEAS_EN;
	ret = regmap_update_bits(info->chip->gpadc_regmap, PM886_GPADC_CONFIG1,
				 mask, data);
	if (ret < 0)
		goto out;

	/* 6. hold the sleep counter until this bit is released or be reset */
	data = mask = PM886_SLP_CNT_HOLD;
	ret = regmap_update_bits(info->chip->base_regmap,
				 PM886_SLP_CNT2, mask, data);
out:
	return ret;
}

static int pm886_calc_init_soc(struct pm886_battery_info *info,
			       struct ccnt *ccnt_val)
{
	int initial_soc = 80;
	int ret, slp_volt, soc_from_vbat_slp, soc_from_saved, slp_cnt;
	bool battery_is_changed, soc_in_good_range, realiable_from_saved;

	/*---------------- the following gets the initial_soc --------------*/
	/*
	 * 1. read vbat_sleep:
	 * - then use the vbat_sleep to calculate SoC: soc_from_vbat_slp
	 */
	slp_volt = pm886_get_batt_vol(info, 0);
	dev_info(info->dev, "---> %s: slp_volt = %dmV\n", __func__, slp_volt);
	soc_from_vbat_slp = pm886_get_soc_from_ocv(slp_volt);
	dev_info(info->dev, "---> %s: soc_from_vbat_slp = %d\n",
		 __func__, soc_from_vbat_slp);

	/* 2. read saved SoC: soc_from_saved */
	/*
	 *  if system comes here because of software reboot
	 *  and
	 *  soc_from_saved is not realiable, use soc_from_vbat_slp
	 */

	ret = pm886_battery_read_buffer(info, &extern_data);
	soc_from_saved = extern_data.soc;
	realiable_from_saved = extern_data.ocv_is_realiable;
	dev_info(info->dev,
		 "---> %s: soc_from_saved = %d, realiable_from_saved = %d\n", \
		 __func__, soc_from_saved, realiable_from_saved);

	if (system_is_reboot(info)) {
		dev_info(info->dev,
			 "---> %s: arrive here from reboot.\n", __func__);
		if (ret < 0) {
			initial_soc = soc_from_vbat_slp;
			info->ocv_is_realiable = false;
		} else {
			initial_soc = soc_from_saved;
			info->ocv_is_realiable = realiable_from_saved;
		}
		goto end;
	}
	dev_info(info->dev, "---> %s: arrive here from power on.\n", __func__);

	/*
	 * 3. compare the soc_from_vbat_slp and the soc_from_saved
	 *    to decide whether the battery is changed:
	 *    if changed, initial_soc = soc_from_vbat_slp;
	 *    else, --> battery is not changed
	 *        if sleep_counter < threshold --> battery is not relaxed
	 *                initial_soc = soc_from_saved;
	 *        else, ---> battery is relaxed
	 *            if soc_from_vbat_slp in good range
	 *                initial_soc = soc_from_vbat_slp;
	 *            else
	 *                initial_soc = soc_from_saved;
	 */
	battery_is_changed = check_battery_change(info, soc_from_vbat_slp,
						  soc_from_saved);
	dev_info(info->dev, "battery_is_changed = %d\n", battery_is_changed);
	if (battery_is_changed) {
		dev_info(info->dev, "----> %s: battery is changed\n", __func__);
		initial_soc = soc_from_vbat_slp;
		info->ocv_is_realiable = false;
		goto end;
	}

	/* battery unchanged */
	slp_cnt = pm886_battery_get_slp_cnt(info);
	dev_info(info->dev, "----> %s: battery is unchanged: \n", __func__);
	dev_info(info->dev, "\t\t slp_cnt = %d\n", slp_cnt);

	if (slp_cnt < info->sleep_counter_th) {
		initial_soc = soc_from_saved;
		info->ocv_is_realiable = realiable_from_saved;
		dev_info(info->dev,
			 "---> %s: battery is unchanged, and not relaxed:\n",
			 __func__);
		dev_info(info->dev,
			 "\t\t use soc_from_saved and realiable_from_saved.\n");
		goto end;
	}

	dev_info(info->dev,
		 "---> %s: battery is unchanged and relaxed\n", __func__);

	soc_in_good_range = check_soc_range(info, soc_from_vbat_slp);
	dev_info(info->dev, "soc_in_good_range = %d\n", soc_in_good_range);
	if (soc_in_good_range) {
		initial_soc = soc_from_vbat_slp;
		info->ocv_is_realiable = true;
		dev_info(info->dev,
			 "OCV is in good range, use soc_from_vbat_slp.\n");
	} else {
		initial_soc = soc_from_saved;
		info->ocv_is_realiable = realiable_from_saved;
		dev_info(info->dev,
			 "OCV is in bad range, use soc_from_saved.\n");
	}

end:
	/* update ccnt_data timely */
	ccnt_val->soc = initial_soc;
	ccnt_val->last_cc =
		(ccnt_val->max_cc / 1000) * (ccnt_val->soc * 10 + 5);

	dev_info(info->dev,
		 "<---- %s: initial soc = %d\n", __func__, initial_soc);

	return initial_soc;
}

static int pm886_init_fuelgauge(struct pm886_battery_info *info)
{
	int ret;
	if (!info) {
		pr_err("%s: empty battery info.\n", __func__);
		return -EINVAL;
	}

	/* configure HW registers to enable the fuelgauge */
	ret = pm886_setup_fuelgauge(info);
	if (ret < 0) {
		dev_err(info->dev, "setup fuelgauge registers fail.\n");
		return ret;
	}

	/*------------------- the following is SW related policy -------------*/
	/* 1. check whether the battery is present */
	info->bat_params.present = pm886_check_battery_present(info);
	/* 2. get initial_soc */
	info->bat_params.soc = pm886_calc_init_soc(info, &ccnt_data);
	/* 3. the following initial the software status */
	if (info->bat_params.present == 0) {
		info->bat_params.status = POWER_SUPPLY_STATUS_UNKNOWN;
		info->bat_params.temp = 250;
		info->bat_params.soc = 80;
		return 0;
	}
	info->bat_params.status = pm886_battery_get_charger_status(info);
	/* 4. hardcode type[Lion] */
	info->bat_params.tech = POWER_SUPPLY_TECHNOLOGY_LION;

	return 0;
}

static void pm886_external_power_changed(struct power_supply *psy)
{
	struct pm886_battery_info *info;

	info = container_of(psy, struct pm886_battery_info, battery);
	queue_delayed_work(info->bat_wqueue, &info->charged_work, 0);
	return;
}

static enum power_supply_property pm886_batt_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_HEALTH,
};

static int pm886_batt_get_prop(struct power_supply *psy,
			       enum power_supply_property psp,
			       union power_supply_propval *val)
{
	struct pm886_battery_info *info = dev_get_drvdata(psy->dev->parent);

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		info->bat_params.status =
			pm886_battery_get_charger_status(info);
		val->intval = info->bat_params.status;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = info->bat_params.present;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		/* report fake capacity if battery is absent */
		if (!info->bat_params.present)
			info->bat_params.soc = 80;
		val->intval = info->bat_params.soc;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = info->bat_params.tech;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		info->bat_params.volt = pm886_get_batt_vol(info, 1);
		val->intval = info->bat_params.volt;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		info->bat_params.ibat = pm886_get_ibat_cc(info);
		info->bat_params.ibat /= 1000;
		val->intval = info->bat_params.ibat;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		if (!info->bat_params.present)
			info->bat_params.temp = 250;
		else
			info->bat_params.temp = pm886_get_batt_temp(info) * 10;
		val->intval = info->bat_params.temp;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		info->bat_params.health = pm886_get_batt_health(info);
		val->intval = info->bat_params.health;
		break;
	default:
		return -ENODEV;
	}
	return 0;
}

static irqreturn_t pm886_battery_cc_handler(int irq, void *data)
{
	struct pm886_battery_info *info = data;
	if (!info) {
		pr_err("%s: empty battery info.\n", __func__);
		return IRQ_NONE;
	}
	dev_info(info->dev, "battery columb counter interrupt is served\n");

	/* update the battery status when this interrupt is triggered. */
	pm886_bat_update_status(info);
	power_supply_changed(&info->battery);

	return IRQ_HANDLED;
}

static irqreturn_t pm886_battery_vbat_handler(int irq, void *data)
{
	return IRQ_HANDLED;
}

/* this interrupt may not need to be handled */
static irqreturn_t pm886_battery_detect_handler(int irq, void *data)
{
	struct pm886_battery_info *info = data;
	if (!info) {
		pr_err("%s: empty battery info.\n", __func__);
		return IRQ_NONE;
	}
	dev_info(info->dev, "battery detection interrupt is served\n");

	/* check whether the battery is present */
	info->bat_params.present = pm886_check_battery_present(info);
	power_supply_changed(&info->battery);

	return IRQ_HANDLED;
}

static struct pm886_irq_desc {
	const char *name;
	irqreturn_t (*handler)(int irq, void *data);
} pm886_irq_descs[] = {
	{"columb counter", pm886_battery_cc_handler},
	{"battery voltage", pm886_battery_vbat_handler},
	{"battery detection", pm886_battery_detect_handler},
};

static int pm886_battery_dt_init(struct device_node *np,
				 struct pm886_battery_info *info)
{
	int ret, size, rows, i, ohm, temp,  index = 0;
	const __be32 *values;

	if (of_get_property(np, "bat-ntc-support", NULL))
		info->use_ntc = true;
	else
		info->use_ntc = false;

	if (info->use_ntc) {
		ret = of_property_read_u32(np, "bd-gpadc-no", &info->gpadc_no);
		if (ret)
			return ret;
	}
	ret = of_property_read_u32(np, "bat-capacity", &info->total_capacity);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "sleep-period", &info->sleep_counter_th);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "low-threshold", &info->range_low_th);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "high-threshold", &info->range_high_th);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "alart-percent", &info->alart_percent);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "power-off-th", &info->power_off_th);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "safe-power-off-th",
				   &info->safe_power_off_th);
	if (ret)
		return ret;

	/* initialize the ocv table */
	ret = of_property_read_u32_array(np, "ocv-table", ocv_table, 100);
	if (ret)
		return ret;

	ret = of_property_read_u32(np, "zero-degree-ohm",
				   &info->zero_degree_ohm);
	if (ret)
		return ret;

	values = of_get_property(np, "ntc-table", &size);
	if (!values) {
		pr_warn("No NTC table for %s\n", np->name);
		return 0;
	}

	size /= sizeof(*values);
	/* <ohm, temp>*/
	rows = size / 4;
	info->temp_ohm_table = kzalloc(sizeof(struct temp_vs_ohm) * rows,
				       GFP_KERNEL);
	if (!info->temp_ohm_table)
		return -ENOMEM;
	info->temp_ohm_table_size = rows;

	for (i = 0; i < rows; i++) {
		ohm = be32_to_cpup(values + index++);
		info->temp_ohm_table[i].ohm = ohm;

		temp = be32_to_cpup(values + index++);
		if (ohm > info->zero_degree_ohm)
			info->temp_ohm_table[i].temp = 0 - temp;
		else
			info->temp_ohm_table[i].temp = temp;
	}

	/* ignore if fails */
	of_property_read_u32(np, "abs-lowest-temp", &info->abs_lowest_temp);
	of_property_read_u32(np, "t1-degree", &info->t1);
	of_property_read_u32(np, "t2-degree", &info->t2);
	of_property_read_u32(np, "t3-degree", &info->t3);
	of_property_read_u32(np, "t4-degree", &info->t4);

	info->t1 -= info->abs_lowest_temp;
	info->t2 -= info->abs_lowest_temp;
	info->t3 -= info->abs_lowest_temp;
	info->t4 -= info->abs_lowest_temp;

	return 0;
}

static int pm886_battery_setup_adc(struct pm886_battery_info *info)
{
	struct iio_channel *chan;
	char s[20];

	/* active vbat voltage channel */
	chan = iio_channel_get(info->dev, "vbat");
	if (PTR_ERR(chan) == -EPROBE_DEFER) {
		dev_err(info->dev, "get vbat iio channel defers.\n");
		return -EPROBE_DEFER;
	}
	info->chan[VBATT_CHAN] = IS_ERR(chan) ? NULL : chan;

	/* sleep vbat voltage channel */
	chan = iio_channel_get(info->dev, "vbat_slp");
	if (PTR_ERR(chan) == -EPROBE_DEFER) {
		dev_err(info->dev, "get vbat_slp iio channel defers.\n");
		return -EPROBE_DEFER;
	}
	info->chan[VBATT_SLP_CHAN] = IS_ERR(chan) ? NULL : chan;

	/* temperature channel */

	sprintf(s, "gpadc%d_res", info->gpadc_no);
	chan = iio_channel_get(info->dev, s);
	if (PTR_ERR(chan) == -EPROBE_DEFER) {
		dev_err(info->dev, "get %s iio channel defers.\n", s);
		return -EPROBE_DEFER;
	}
	info->chan[TEMP_CHAN] = IS_ERR(chan) ? NULL : chan;

	return 0;
}

static void pm886_battery_release_adc(struct pm886_battery_info *info)
{
	int i;
	for (i = 0; i < MAX_CHAN; i++) {
		if (!info->chan[i])
			continue;

		iio_channel_release(info->chan[i]);
		info->chan[i] = NULL;
	}
}

static int pm886_battery_probe(struct platform_device *pdev)
{
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm886_battery_info *info;
	struct pm886_bat_pdata *pdata;
	struct device_node *node = pdev->dev.of_node;
	int ret;
	int i, j;

	info = devm_kzalloc(&pdev->dev, sizeof(struct pm886_battery_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	pdata = pdev->dev.platform_data;
	ret = pm886_battery_dt_init(node, info);
	if (ret)
		return -EINVAL;

	/* give default total capacity */
	if (info->total_capacity)
		ccnt_data.max_cc = info->total_capacity * 3600;
	else
		ccnt_data.max_cc = 1500 * 3600;

	if (info->alart_percent > 100 || info->alart_percent < 0)
		info->alart_percent = 5;
	ccnt_data.alart_cc = ccnt_data.max_cc * info->alart_percent / 100;

	if (info->use_ntc) {
		if (info->gpadc_no != 1 && info->gpadc_no != 3)
			return -EINVAL;
	}

	info->chip = chip;
	info->dev = &pdev->dev;
	info->bat_params.status = POWER_SUPPLY_STATUS_UNKNOWN;

	platform_set_drvdata(pdev, info);

	for (i = 0, j = 0; i < pdev->num_resources; i++) {
		info->irq[j] = platform_get_irq(pdev, i);
		if (info->irq[j] < 0)
			continue;
		j++;
	}
	info->irq_nums = j;

	ret = pm886_battery_setup_adc(info);
	if (ret < 0)
		return ret;

	ret = pm886_init_fuelgauge(info);
	if (ret < 0)
		return ret;

	pm886_bat_update_status(info);

	info->battery.name = "battery";
	info->battery.type = POWER_SUPPLY_TYPE_BATTERY;
	info->battery.properties = pm886_batt_props;
	info->battery.num_properties = ARRAY_SIZE(pm886_batt_props);
	info->battery.get_property = pm886_batt_get_prop;
	info->battery.external_power_changed = pm886_external_power_changed;
	info->battery.supplied_to = supply_interface;
	info->battery.num_supplicants = ARRAY_SIZE(supply_interface);
	power_supply_register(&pdev->dev, &info->battery);
	info->battery.dev->parent = &pdev->dev;

	info->bat_wqueue = create_singlethread_workqueue("88pm886-battery-wq");
	if (!info->bat_wqueue) {
		dev_info(chip->dev, "%s: failed to create wq.\n", __func__);
		ret = -ESRCH;
		goto out;
	}

	/* interrupt should be request in the last stage */
	for (i = 0; i < info->irq_nums; i++) {
		ret = devm_request_threaded_irq(info->dev, info->irq[i], NULL,
						pm886_irq_descs[i].handler,
						IRQF_ONESHOT | IRQF_NO_SUSPEND,
						pm886_irq_descs[i].name, info);
		if (ret < 0) {
			dev_err(info->dev, "failed to request IRQ: #%d: %d\n",
				info->irq[i], ret);
			if (!pm886_irq_descs[i].handler)
				goto out_irq;
		}
	}

	INIT_DELAYED_WORK(&info->charged_work, pm886_charged_work);
	INIT_DEFERRABLE_WORK(&info->monitor_work, pm886_battery_monitor_work);

	/* update the status timely */
	queue_delayed_work(info->bat_wqueue, &info->monitor_work, 0);

	device_init_wakeup(&pdev->dev, 1);
	dev_info(info->dev, "%s is successful to be probed.\n", __func__);

	return 0;

out_irq:
	while (--i >= 0)
		devm_free_irq(info->dev, info->irq[i], info);
out:
	power_supply_unregister(&info->battery);

	return ret;
}

static int pm886_battery_remove(struct platform_device *pdev)
{
	int i;
	struct pm886_battery_info *info = platform_get_drvdata(pdev);
	if (!info)
		return 0;

	for (i = 0; i < info->irq_nums; i++) {
		if (pm886_irq_descs[i].handler != NULL)
			devm_free_irq(info->dev, info->irq[i], info);
	}

	cancel_delayed_work_sync(&info->monitor_work);
	cancel_delayed_work_sync(&info->charged_work);
	flush_workqueue(info->bat_wqueue);

	power_supply_unregister(&info->battery);
	pm886_battery_release_adc(info);

	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
static int pm886_battery_suspend(struct device *dev)
{
	struct pm886_battery_info *info = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&info->monitor_work);
	return 0;
}

static int pm886_battery_resume(struct device *dev)
{
	struct pm886_battery_info *info = dev_get_drvdata(dev);

	/*
	 * avoid to reading in short sleep case
	 * to update ocv_is_realiable flag effectively
	 */
	atomic_set(&in_resume, 1);
	queue_delayed_work(info->bat_wqueue,
			   &info->monitor_work, 300 * HZ / 1000);
	return 0;
}

static const struct dev_pm_ops pm886_battery_pm_ops = {
	.suspend	= pm886_battery_suspend,
	.resume		= pm886_battery_resume,
};
#endif

static const struct of_device_id pm886_fg_dt_match[] = {
	{ .compatible = "marvell,88pm886-battery", },
	{ },
};
MODULE_DEVICE_TABLE(of, pm886_fg_dt_match);

static struct platform_driver pm886_battery_driver = {
	.driver		= {
		.name	= "88pm886-battery",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pm886_fg_dt_match),
#ifdef CONFIG_PM
		.pm	= &pm886_battery_pm_ops,
#endif
	},
	.probe		= pm886_battery_probe,
	.remove		= pm886_battery_remove,
};

static int pm886_battery_init(void)
{
	return platform_driver_register(&pm886_battery_driver);
}
module_init(pm886_battery_init);

static void pm886_battery_exit(void)
{
	platform_driver_unregister(&pm886_battery_driver);
}
module_exit(pm886_battery_exit);

MODULE_DESCRIPTION("Marvell 88PM886 battery driver");
MODULE_AUTHOR("Yi Zhang <yizhang@marvell.com>");
MODULE_LICENSE("GPL v2");
