/*
 * File:ltr_558als.c
 * Author:Yaochuan Li <yaochuan.li@spreadtrum.com>
 * Created:2013-03-18
 * Description:LTR-558ALS Driver
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
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/wakelock.h>
#include <linux/i2c/ltr_558als.h>
#include <linux/slab.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pm.h>

//#define LTR588_DBG
//#define LTR558_ADAPTIVE
#define LTR558_PROX_WAKE_LOCK
#define LTR558_ALS_POLL
//#define ALS_RATIO

#ifdef LTR588_DBG
#define ENTER pr_info("[LTR588_DBG] func: %s  line: %04d  ",\
	__func__, __LINE__)
#define PRINT_DBG(x...)  pr_info("[LTR588_DBG] " x)
#define PRINT_INFO(x...)  pr_info("[LTR588_INFO] " x)
#define PRINT_WARN(x...)  pr_info("[LTR588_WARN] " x)
#define PRINT_ERR(format, x...)\
pr_err(KERN_ERR "[LTR588_ERR] func: %s line: %04d  info: " format,\
	__func__, __LINE__, ## x)
#else
#define ENTER
#define PRINT_DBG(x...)
#define PRINT_INFO(x...)  pr_info("[LTR588_INFO] " x)
#define PRINT_WARN(x...)  pr_info("[LTR588_WARN] " x)
#define PRINT_ERR(format, x...)  \
	pr_err("[LTR588_ERR] func: %s line: %04d  info: " format, \
		__func__, __LINE__, ## x)
#endif

typedef struct tag_ltr558 {
        struct input_dev *input;
        struct input_dev *als_input;
        struct i2c_client *client;
	    bool als_enabled;
	    bool prox_enabled;
        struct work_struct work;
        struct workqueue_struct *ltr_work_queue;
#ifdef CONFIG_HAS_EARLYSUSPEND
        struct early_suspend ltr_early_suspend;
#endif
#ifdef LTR558_ALS_POLL
	struct delayed_work work_light;
#endif
#ifdef LTR558_PROX_WAKE_LOCK
    struct wake_lock prox_wake_lock;
#endif

} ltr558_t, *ltr558_p;

static int p_flag = 0;
static int l_flag = 0;
static u8 p_gainrange = PS_553_RANGE16;//PS_RANGE4;
static u8 l_gainrange = ALS_553_RANGE48_1K3;//ALS_RANGE1_320; //ALS_RANGE2_64K;//
#ifdef LTR558_ADAPTIVE
static int ps_threshold = 80; //100
static int ps_threshold_low = 60; //80
#else
static int ps_threshold = 30; //900;
static int ps_threshold_low = 10; //800;
static int val_temp = 1;
#endif
static struct i2c_client *this_client = NULL;
static int LTR_PLS_MODE = LTR_PLS_553;

#ifdef LTR558_ADAPTIVE
#define DEBOUNCE 10
#define MIN_SPACING 80 //120
#define DIVEDE 10
#endif
#ifdef ALS_RATIO
#define ALS_RATIO_VALUE 10000
#else
#define ALS_RATIO_VALUE 30000
#endif
static int ltr558_reg_init(void);

//static int ps_max_filter[5] = {0};
//static int ps_min_filter[5] = {0};
//static int max_index = 0;
//static int min_index = 0;
static int ps_min = 0;
static int ps_max = 0;
#ifdef LTR558_ALS_POLL
#define LIGHT_MEA_INTERVAL              500
#endif

static int ltr558_i2c_read_bytes(u8 index, u8 *rx_buff, u8 length)
{
	int ret = -1;
	struct i2c_msg msgs[] = {
		{
			.addr = this_client->addr,
			.flags = 0,
			.len = 1,
			.buf = &index,
		},
		{
			.addr = this_client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = rx_buff,
		},
	};

	ret = i2c_transfer(this_client->adapter, msgs, 2);
	if (ret != 2)
		PRINT_ERR("READ ERROR!ret=%d\n", ret);
	return ret;
}

static int ltr558_i2c_write_bytes(u8 *tx_buff, u8 length)
{
	int ret = -1;
	struct i2c_msg msgs[1];

	msgs[0].addr = this_client->addr;
	msgs[0].flags = 0;
	msgs[0].len = length;
	msgs[0].buf = tx_buff;

	ret = i2c_transfer(this_client->adapter, msgs, 1);
	if (ret != 1)
		PRINT_ERR("WRITE ERROR!ret=%d\n", ret);
	return ret;
}

static int ltr558_i2c_read_2_bytes(u8 reg)
{
	int ret = 0;
	u8 data[2] = {0};

	ret = ltr558_i2c_read_bytes(reg, data, 2);
	if (ret != 2) {
		PRINT_ERR("READ ERROR!ret=%d\n", ret);
		return -1;
	}

	ret = data[1];
	ret = (ret << 8) | data[0];

	return ret;
}

static int ltr558_i2c_read_1_byte(u8 reg)
{
	int ret = 0;
	u8 data[1] = {0};

	ret = ltr558_i2c_read_bytes(reg, data, 1);
	if (ret != 2) {
		PRINT_ERR("READ ERROR!ret=%d\n", ret);
		return -1;
	}

	ret = data[0];
	return ret;
}

static int ltr558_i2c_write_2_bytes(u8 reg, u16 value)
{
	int ret = 0;
	u8 data[3] = {0};

	data[0] = reg;
	data[1] = value & 0x00FF;
	data[2] = value >> 8;

	ret = ltr558_i2c_write_bytes(data, 3);
	if (ret != 1) {
		PRINT_ERR("WRITE ERROR!ret=%d\n", ret);
		return -1;
	}
	return 1;
}

static int ltr558_i2c_write_1_byte(u8 reg, u8 value)
{
	int ret = 0;
	u8 data[2] = {0};

	data[0] = reg;
	data[1] = value;

	ret = ltr558_i2c_write_bytes(data, 2);
	if (ret != 1) {
		PRINT_ERR("WRITE ERROR!ret=%d\n", ret);
		return -1;
	}
	return 1;
}

#ifdef CONFIG_OF
static int ltr558_set_power(struct device *dev, int on)
{
	static struct regulator *v_sensor;
	static int flag;

	if (!v_sensor) {
		v_sensor = regulator_get(dev, "vdd");
		if (IS_ERR(v_sensor)) {
			v_sensor = NULL;
			return -EIO;
		}
	}

	if (!flag && on) {
		regulator_set_voltage(v_sensor, 2800000, 2800000);
		flag = regulator_enable(v_sensor);
		flag = 1;
	} else if (flag && !on) {
		regulator_disable(v_sensor);
		flag = 0;
	}
	msleep(20);
	return 0;
}
#endif

static int ltr558_ps_enable(u8 gainrange)
{
	int ret = -1;
	u8 setgain;
	ltr558_t *ltr_558als = (ltr558_t *)i2c_get_clientdata(this_client);

	if (LTR_PLS_553 == LTR_PLS_MODE) {
		switch (gainrange) {
		case PS_553_RANGE16:
			setgain = MODE_PS_553_ON_Gain16;
			break;
		case PS_553_RANGE32:
			setgain = MODE_PS_553_ON_Gain32;
			break;
		case PS_553_RANGE64:
			setgain = MODE_PS_553_ON_Gain64;
			break;
		default:
			setgain = MODE_PS_553_ON_Gain16;
			break;
		}
	} else {
		switch (gainrange) {
		case PS_558_RANGE1:
			setgain = MODE_PS_558_ON_Gain1;
			break;
		case PS_558_RANGE2:
			setgain = MODE_PS_558_ON_Gain4;
			break;
		case PS_558_RANGE4:
			setgain = MODE_PS_558_ON_Gain8;
			break;
		case PS_558_RANGE8:
			setgain = MODE_PS_558_ON_Gain16;
			break;
		default:
			setgain = MODE_PS_558_ON_Gain8;
			break;
		}
	}

	ret = ltr558_i2c_write_1_byte(LTR558_PS_CONTR, setgain);
	mdelay(WAKEUP_DELAY);

	if (setgain != ltr558_i2c_read_1_byte(LTR558_PS_CONTR)) {
		ret = ltr558_i2c_write_1_byte(LTR558_PS_CONTR, setgain);
		mdelay(WAKEUP_DELAY);
	}
#ifdef LTR558_ADAPTIVE
	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, ps_threshold);//0x0000
    ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, ps_threshold_low);//0x0000
#else
	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, ps_threshold);
    ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, ps_threshold_low);
#endif

	ltr558_i2c_read_1_byte(LTR558_ALS_PS_STATUS);
	ltr558_i2c_read_2_bytes(LTR558_PS_DATA_0);
	ltr558_i2c_read_2_bytes(LTR558_ALS_DATA_CH1);
	ltr558_i2c_read_2_bytes(LTR558_ALS_DATA_CH0);

	/*input report init*/
	input_report_abs(ltr_558als->input, ABS_DISTANCE, 1);
	input_sync(ltr_558als->input);

	PRINT_INFO("ltr558_ps_enable, gainrange=%d, ret=%d\n", gainrange, ret);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

static int ltr558_ps_disable(void)
{
	int ret = -1;
	ltr558_t *ltr_558als = (ltr558_t *)i2c_get_clientdata(this_client);
	ret = ltr558_i2c_write_1_byte(LTR558_PS_CONTR, MODE_PS_STANDBY);

	/*input report init*/
	input_report_abs(ltr_558als->input, ABS_DISTANCE, 1);
	input_sync(ltr_558als->input);

	PRINT_INFO("ltr558_ps_disable, ret=%d\n", ret);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

static int ltr558_als_enable(u8 gainrange)
{
	int ret = -1;
	u8 setgain;
#ifdef LTR558_ALS_POLL
	ltr558_t *ltr_558als = (ltr558_t *)i2c_get_clientdata(this_client);
#endif
	if (LTR_PLS_553 == LTR_PLS_MODE) {
		switch (gainrange) {
		case ALS_553_RANGE1_64K:
			setgain = MODE_ALS_553_ON_Range1;
			break;
		case ALS_553_RANGE2_32K:
			setgain = MODE_ALS_553_ON_Range2;
			break;
		case ALS_553_RANGE4_16K:
			setgain = MODE_ALS_553_ON_Range4;
			break;
		case ALS_553_RANGE8_8K:
			setgain = MODE_ALS_553_ON_Range8;
			break;
		case ALS_553_RANGE48_1K3:
			setgain = MODE_ALS_553_ON_Range48;
			break;
		case ALS_553_RANGE96_600:
			setgain = MODE_ALS_553_ON_Range96;
			break;
		default:
			setgain = MODE_ALS_553_ON_Range1;
			break;
		}
	} else {
		switch (gainrange) {
		case  ALS_558_RANGE1_320:
			setgain = MODE_ALS_558_ON_Range1;
			break;
		case ALS_558_RANGE2_64K:
			setgain = MODE_ALS_558_ON_Range2;
			break;
		default:
			setgain = MODE_ALS_558_ON_Range1;
			break;
		}
	}

	ret = ltr558_i2c_write_1_byte(LTR558_ALS_CONTR, setgain);
	mdelay(WAKEUP_DELAY);

	if (setgain != ltr558_i2c_read_1_byte(LTR558_ALS_CONTR)) {
		ret = ltr558_i2c_write_1_byte(LTR558_ALS_CONTR, setgain);
		mdelay(WAKEUP_DELAY);
	}

	ltr558_i2c_read_1_byte(LTR558_ALS_PS_STATUS);
	ltr558_i2c_read_2_bytes(LTR558_PS_DATA_0);
	ltr558_i2c_read_2_bytes(LTR558_ALS_DATA_CH1);
	ltr558_i2c_read_2_bytes(LTR558_ALS_DATA_CH0);
#ifdef LTR558_ALS_POLL
	schedule_delayed_work(&ltr_558als->work_light, msecs_to_jiffies(LIGHT_MEA_INTERVAL));
#endif
	PRINT_INFO("ltr558_als_enable, gainrange=%d, ret = %d\n",
		gainrange, ret);
	if (ret >= 0)
		return 0;
	else
		return ret;
}

static int ltr558_als_disable(void)
{
	int ret = -1;
#ifdef LTR558_ALS_POLL
	ltr558_t *ltr_558als = (ltr558_t *)i2c_get_clientdata(this_client);
#endif
	ret = ltr558_i2c_write_1_byte(LTR558_ALS_CONTR, MODE_ALS_STANDBY);
#ifdef LTR558_ALS_POLL
	cancel_delayed_work_sync(&ltr_558als->work_light);
#endif
	if (ret >= 0)
		return 0;
	else
		return ret;
}

static int ltr558_als_read(int gainrange)
{
	int luxdata_int;
	int luxdata_flt, ratio;
	int ch0_coeff = 0;
	int ch1_coeff = 0;
	int ch0, ch1;

	/*IMPORTANT!CH1 MUST BE READ FIRST!*/
	ch1 = ltr558_i2c_read_2_bytes(LTR558_ALS_DATA_CH1);
	ch0 = ltr558_i2c_read_2_bytes(LTR558_ALS_DATA_CH0);

	PRINT_DBG("ch0=%d,  ch1=%d\n", ch0, ch1);
#ifdef ALS_RATIO
	if (0 == ch0)
		ratio = 100;
	else
		ratio = (ch1 * 100) / ch0;
#else
	if (0 == ch1 + ch0)
		ratio = 1000;
	else
		ratio = (1000 * ch1) / (ch1 + ch0);
#endif
#ifdef ALS_RATIO
	if (ratio < 69) {
		luxdata_flt = (13618 * ch0) - (15000 * ch1);
        luxdata_flt = luxdata_flt / ALS_RATIO_VALUE;
	} else if ((ratio >= 69) && (ratio < 100)) {
		luxdata_flt = (5700 * ch0) - (3450 * ch1);
		luxdata_flt = luxdata_flt / ALS_RATIO_VALUE;
	} else {
		luxdata_flt = 0;
	}
#else
	if (ratio < 450) {
		ch0_coeff = 17743;
		ch1_coeff = -11059;
		luxdata_flt  = ((ch0 * ch0_coeff) - (ch1 * ch1_coeff)) / ALS_RATIO_VALUE;
		luxdata_flt = luxdata_flt + 1;
	} else if ((ratio >= 450) && (ratio < 640)) {
		ch0_coeff = 42785;
		ch1_coeff = 19548;
		luxdata_flt  = ((ch0 * ch0_coeff) - (ch1 * ch1_coeff)) / ALS_RATIO_VALUE;
		luxdata_flt = luxdata_flt + 1;
	} else if ((ratio >= 640) && (ratio < 990)) {
		ch0_coeff = 5926;
		ch1_coeff = -1185;
		luxdata_flt  = ((ch0 * ch0_coeff) - (ch1 * ch1_coeff)) / ALS_RATIO_VALUE;
		luxdata_flt = luxdata_flt + 1;
	} else if (ratio >= 990) {
		ch0_coeff = 0;
		ch1_coeff = 0;
		luxdata_flt  = ((ch0 * ch0_coeff) - (ch1 * ch1_coeff)) / ALS_RATIO_VALUE;
	}
#endif

#ifdef ALS_RATIO
	luxdata_int = luxdata_flt * 50;
#else
	luxdata_int = luxdata_flt;
#endif
	return luxdata_int;
}

static int ltr558_open(struct inode *inode, struct file *file)
{
	PRINT_INFO("ltr558_open\n");
	return 0;
}

static int ltr558_release(struct inode *inode, struct file *file)
{
	PRINT_INFO("ltr558_release\n");
	return 0;
}

static long ltr558_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int flag;
#ifdef LTR558_PROX_WAKE_LOCK
	ltr558_t *pls = (ltr558_t *)i2c_get_clientdata(this_client);
#endif
	PRINT_INFO("cmd = %d, %d\n", _IOC_NR(cmd), cmd);
	switch (cmd) {
	case LTR_IOCTL_SET_PFLAG:
		if (copy_from_user(&flag, argp, sizeof(flag)))
			return -EFAULT;
		PRINT_INFO("LTR_IOCTL_SET_PFLAG = %d\n", flag);
		if (1 == flag) {
#ifdef LTR558_PROX_WAKE_LOCK
		wake_lock(&pls->prox_wake_lock);
#endif
			if (ltr558_ps_enable(p_gainrange))
				return -EIO;
		} else if (0 == flag) {
#ifdef LTR558_PROX_WAKE_LOCK
		wake_unlock(&pls->prox_wake_lock);
#endif
			if (ltr558_ps_disable())
				return -EIO;
		} else {
				return -EINVAL;
		}
		p_flag = flag;
		break;
	case LTR_IOCTL_SET_LFLAG:
		if (copy_from_user(&flag, argp, sizeof(flag)))
			return -EFAULT;
		PRINT_INFO("LTR_IOCTL_SET_LFLAG = %d\n", flag);
		if (1 == flag) {
			if (ltr558_als_enable(l_gainrange))
				return -EIO;
			} else if (0 == flag) {
				if (ltr558_als_disable())
					return -EIO;
			} else {
				return -EINVAL;
			}
		l_flag = flag;
		break;
	case LTR_IOCTL_GET_PFLAG:
		flag = p_flag;
		PRINT_INFO("LTR_IOCTL_GET_PFLAG = %d\n", flag);
		if (copy_to_user(argp, &flag, sizeof(flag)))
			return -EFAULT;
		break;
	case LTR_IOCTL_GET_LFLAG:
		flag = l_flag;
		PRINT_INFO("LTR_IOCTL_GET_LFLAG = %d\n", flag);
		if (copy_to_user(argp, &flag, sizeof(flag)))
			return -EFAULT;
		break;
	default:
		PRINT_ERR("unknown command: 0x%08X  (%d)\n", cmd, cmd);
		break;
	}
	return 0;
}

static const struct file_operations ltr558_fops = {
	.owner = THIS_MODULE,
	.open = ltr558_open,
	.release = ltr558_release,
	.unlocked_ioctl = ltr558_ioctl,
	.compat_ioctl = ltr558_ioctl,
};

static struct miscdevice ltr558_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = LTR558_I2C_NAME,
	.fops = &ltr558_fops,
};

#if 0
static int get_min_value(int *array, int size)
{
	int ret = 0;
	int i = 0;
	ret =  array[0];
	for (i = 1; i < size; i++) {
		if (ret > array[i])
			ret = array[i];
	}
	return ret;
}

static int get_max_value(int *array, int size)
{
	int ret = 0;
	int i = 0;
	ret =  array[0];
	for (i = 1; i < size; i++) {
		if (ret < array[i])
			ret = array[i];
	}
	return ret;
}
#endif
static void ltr558_work(struct work_struct *work)
{
	int status = 0;
	int value = 0;
	int val = -1;
	ltr558_t *pls = container_of(work, ltr558_t, work);

	status = ltr558_i2c_read_1_byte(LTR558_ALS_PS_STATUS);
	PRINT_DBG("LTR558_ALS_PS_STATUS = 0x%02X\n", status);

	if ((0x03 == (status & 0x03)) && (LTR_PLS_MODE == LTR_PLS_558)) {
		value = ltr558_i2c_read_2_bytes(LTR558_PS_DATA_0);
		PRINT_DBG("LTR_PLS_MODE is pls 558, LTR558_PS_DATA_0 = %d\n",
			value);
		if (value >= ps_threshold) {
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_0, 0xff);
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_1, 0x07);
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_0, (0xff & ps_threshold_low));
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_1, (ps_threshold_low >> 8) & 0x07);
			input_report_abs(pls->input, ABS_DISTANCE, 0);
			input_sync(pls->input);
		} else if (value <= ps_threshold_low) {
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_0, (0xff & ps_threshold));
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_1, (ps_threshold >> 8) & 0x07);
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_0, 0x00);
			ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_1, 0x00);
			input_report_abs(pls->input, ABS_DISTANCE, 1);
			input_sync(pls->input);
		}
	}

	if ((0x03 == (status & 0x03)) && (LTR_PLS_MODE == LTR_PLS_553)) {
		value = ltr558_i2c_read_2_bytes(LTR558_PS_DATA_0);
    PRINT_DBG("LTR_PLS_MODE is pls 553 \n");
#ifdef LTR558_ADAPTIVE
	/*threshold detect process*/
	if (0 == ps_max || 0 == ps_min) {
		ps_min = value;
		ps_max = value;
		ps_threshold = ps_min + MIN_SPACING;
	}
	if (value > ps_max) {
		ps_max_filter[max_index++] = value;
		if (ARRAY_SIZE(ps_max_filter) == max_index) {
			ps_max = get_min_value(ps_max_filter,
				ARRAY_SIZE(ps_max_filter));
			ps_threshold = ((ps_max - ps_min)  / DIVEDE) + ps_min;
			if (ps_threshold - ps_min < MIN_SPACING)
				ps_threshold = ps_min + MIN_SPACING;
			max_index = 0;
		}
		min_index = 0;
	} else if (value < ps_min) {
		ps_min_filter[min_index++] = value;
		if (ARRAY_SIZE(ps_min_filter) == min_index) {
			ps_min = get_max_value(ps_min_filter,
				ARRAY_SIZE(ps_min_filter));
			ps_threshold = ((ps_max - ps_min)  / DIVEDE) + ps_min;
			if (ps_threshold - ps_min < MIN_SPACING)
				ps_threshold = ps_min + MIN_SPACING;
			min_index = 0;
		}
		max_index = 0;
	} else {
		max_index = 0;
		min_index = 0;
	}
	/*threshold detect process end*/

	if (value > (ps_threshold + DEBOUNCE)) {
		input_report_abs(pls->input, ABS_DISTANCE, 0);
		input_sync(pls->input);
	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, 0x07FF);
	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, (ps_threshold - DEBOUNCE));
	} else if (value < (ps_threshold - DEBOUNCE)) {
		input_report_abs(pls->input, ABS_DISTANCE, 1);
		input_sync(pls->input);
    	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, (ps_threshold + DEBOUNCE));
    	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, 0x0000);
	}
    PRINT_DBG("PS INT: PS_DATA_VAL = 0x%04X(%4d)  ps_min=%4d ps_max=%4d ps_threshold=%4d\n", \
    	value, value, ps_min, ps_max, ps_threshold);
#else
	if (value >= ps_threshold) {
		ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, 0x07FF);
		ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, ps_threshold);
		val = 0;
		val_temp = 0;
		input_report_abs(pls->input, ABS_DISTANCE, val);
		input_sync(pls->input);
	} else if (value <= ps_threshold_low) {
		ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, ps_threshold);
		ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, 0x0000);
		val = 1;
		val_temp = 1;
		input_report_abs(pls->input, ABS_DISTANCE, val);
		input_sync(pls->input);
    }else{
		val = val_temp;
	input_report_abs(pls->input, ABS_DISTANCE, val);
	input_sync(pls->input);
	}
	PRINT_DBG("PS INT: PS_DATA_VAL = 0x%04X ( %d )\n", value, value);
#endif
	}
	if (0x0c == (status & 0x0c)) {/*is ALS*/
		value = ltr558_als_read(l_gainrange);
		PRINT_DBG("ALS INT: ALS_DATA_VAL = 0x%04X(%4d)\n",
			value, value);
		input_report_abs(pls->input, ABS_MISC, value);
		input_sync(pls->input);
	input_report_abs(pls->als_input, ABS_MISC, value);
	input_sync(pls->als_input);
	}

	enable_irq(pls->client->irq);
}

static irqreturn_t ltr558_irq_handler(int irq, void *dev_id)
{
	ltr558_t *pls = (ltr558_t *) dev_id;

	disable_irq_nosync(pls->client->irq);
	queue_work(pls->ltr_work_queue, &pls->work);
	return IRQ_HANDLED;
}
#if 0
static int ltr558_sw_reset(void)
{
	int ret = 0;
	if(LTR_PLS_553 == LTR_PLS_MODE){
    	ret = ltr558_i2c_write_1_byte(LTR558_ALS_CONTR, 0x02); //553
	}else{
    	ret = ltr558_i2c_write_1_byte(LTR558_ALS_CONTR, 0x04); //558
	}
	if (1 == ret)
		PRINT_INFO("ltr558_sw_reset success\n");
	else
		PRINT_ERR("ltr558_sw_reset failed! ret = %d\n", ret);
	return ret;
}
#endif
static int ltr558_reg_init(void)
{
	int ret = 0;

	if (LTR_PLS_558 == LTR_PLS_MODE) {
		ltr558_i2c_write_1_byte(LTR558_PS_LED, 0x7B);
		ltr558_i2c_write_1_byte(LTR558_PS_N_PULSES , 0x1f);
		ltr558_i2c_write_1_byte(LTR558_PS_MEAS_RATE, 0x00);
		ltr558_i2c_write_1_byte(LTR558_ALS_MEAS_RATE, 0x03);
		ltr558_i2c_write_1_byte(LTR558_INTERRUPT_PERSIST, 0x02);
		#ifdef LTR558_ALS_POLL
		ltr558_i2c_write_1_byte(LTR558_INTERRUPT, 0x09);
		#else
		ltr558_i2c_write_1_byte(LTR558_INTERRUPT, 0x0B);
        #endif
	} else {
		ltr558_i2c_write_1_byte(LTR558_PS_LED, 0x7A); //0x7B
		ltr558_i2c_write_1_byte(LTR558_PS_N_PULSES , 0x08);
		ltr558_i2c_write_1_byte(LTR558_PS_MEAS_RATE, 0x00);
		ltr558_i2c_write_1_byte(LTR558_ALS_MEAS_RATE, 0x03);
		ltr558_i2c_write_1_byte(LTR558_INTERRUPT_PERSIST, 0x12);
		#ifdef LTR558_ALS_POLL
		ltr558_i2c_write_1_byte(LTR558_INTERRUPT, 0x01);
		#else
		ltr558_i2c_write_1_byte(LTR558_INTERRUPT, 0x03);
		#endif
	}

    // ps
#ifdef LTR558_ADAPTIVE
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_0, 0x00);
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_1, 0x03);
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_0, 0xf0);
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_1, 0x02);
#else
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_0, (0xff & ps_threshold));
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_UP_1, (ps_threshold >> 8) & 0x07);
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_0, (0xff & ps_threshold_low));
	ltr558_i2c_write_1_byte(LTR558_PS_THRES_LOW_1,  (ps_threshold_low >> 8) & 0x07);
#endif
#ifdef LTR558_ALS_POLL
#else
	// als
	ltr558_i2c_write_1_byte(LTR558_ALS_THRES_UP_0, 0xff); //0x00
	ltr558_i2c_write_1_byte(LTR558_ALS_THRES_UP_1, 0x00); //0x00
	ltr558_i2c_write_1_byte(LTR558_ALS_THRES_LOW_0, 0x00); //0x01
	ltr558_i2c_write_1_byte(LTR558_ALS_THRES_LOW_1, 0x01); //0x00
#endif

	mdelay(WAKEUP_DELAY);
	//ltr558_ps_disable();
	//ltr558_als_disable();

	PRINT_INFO("ltr558_reg_init success!\n");
	return ret;
}

static int ltr558_version_check(void)
{
	int part_id = -1;
	int manufacturer_id = -1;
	part_id = ltr558_i2c_read_1_byte(LTR558_PART_NUMBER_ID);
	manufacturer_id = ltr558_i2c_read_1_byte(LTR558_MANUFACTURER_ID);
	PRINT_INFO("PART_ID: 0x%02X   MANUFACTURER_ID: 0x%02X\n",
		part_id, manufacturer_id);

	if (part_id == 0x80 && manufacturer_id == 0x05) {
		PRINT_INFO("I'm LTR558, and I'm working now\n");
		return 0;
	} else if (part_id == 0x92 && manufacturer_id == 0x05) {
		PRINT_INFO("I'm LTR553, and I'm working now\n");
		return 0;
	} else if (part_id < 0 || manufacturer_id < 0) {
		PRINT_ERR("can't read who am I\n");
		return -1;
	} else {
		PRINT_ERR("I'm working, but I'm NOT LTR558\n");
		return -1;
	}
}

static ssize_t ltr_558als_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buff)
{
	return sprintf(buff, "ps_min=%4d  ps_max=%4d  ps_threshold=%4d\n",
		ps_min, ps_max, ps_threshold);
}

#ifdef LTR588_DBG

#ifdef 0
static int str2int(char *p, char *end)
{
	int result = 0;
	int len = end - p + 1;
	int c;

	for (len > 0; len--, p++) {
		if (p > end)
			return -1;
		c = *p - '0';
		if ((unsigned int)c >= 10)
			return -1;
		result = result * 10 + c;
	}
	return result;
}

/*parse a int from the string*/
static int get_int(char *p, u8 max_len)
{
	char *start = p;
	char *end = NULL;

	if (max_len > 60) {
		PRINT_ERR("CMD ERROR!the max length should less than 60\n");
		return -1;
	}
	for (; max_len > 0; max_len--, p++) {
		if (*p >= '0' && *p <= '9') {
			start = p;
			break;
		}
	}
	if (0 == max_len)
		return -1;

	end = start;

	for (; max_len > 0; max_len--, p++) {
		if (*p >= '0' && *p <= '9') {
			end = p;
			continue;
		}
		break;
	}

	return str2int(start, end);
}

static ssize_t ltr_558als_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buff, size_t n)
{
	char *buff_temp = NULL;

	buff_temp = kmalloc(n, GFP_KERNEL);
	if (NULL == buff_temp) {
		PRINT_ERR("kmalloc err\n");
		return n;
	}

	memcpy(buff_temp, buff, n);

	ps_threshold = get_int(buff_temp, n);
	if (0x07FF < ps_threshold)
		ps_threshold = LTR558_PS_THRESHOLD;

	PRINT_INFO("set ps_threshold = 0x%04X ( %d )\n",
		ps_threshold, ps_threshold);

	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, 0x0000);
	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, 0x0000);

	if (buff_temp != NULL)
		kfree(buff_temp);

	return n;
}

#endif

static int break_loop = 0;

static ssize_t ltr_558als_val_show(struct kobject *kobj,
	struct kobj_attribute *attr, char *buff)
{
	int measured_val = 0;
	ltr558_t *pls = (ltr558_t *)i2c_get_clientdata(this_client);

	disable_irq_nosync(pls->client->irq);
	ltr558_als_enable(l_gainrange);
	ltr558_ps_enable(p_gainrange);

	break_loop = 0;
	while (1) {
		measured_val = ltr558_i2c_read_2_bytes(LTR558_PS_DATA_0);
		PRINT_DBG("PS_DATA_VAL = 0x%04X ( %d )\n",
			measured_val, measured_val);
		input_report_abs(pls->input, ABS_DISTANCE, measured_val);
		input_sync(pls->input);

		measured_val = ltr558_als_read(l_gainrange);
		PRINT_DBG("ALS_DATA_VAL = 0x%04X ( %d )\n",
			measured_val, measured_val);
		input_report_abs(pls->input, ABS_MISC, measured_val);
		input_sync(pls->input);

		if (1 == break_loop)
			break;
		msleep(200);
	}
	ltr558_ps_disable();
	ltr558_als_disable();
	enable_irq(pls->client->irq);

	return 0;
}

static ssize_t ltr_558als_val_store(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buff, size_t n)
{
	break_loop = 1;
	PRINT_INFO("stop show value\n");
	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_UP, 0x0000);
	ltr558_i2c_write_2_bytes(LTR558_PS_THRES_LOW, 0x0000);
	return n;
}
#endif

static ssize_t ltr_558als_prox_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	//struct ltr558_t *chip = dev_get_drvdata(dev);
	//ltr558_t *pls = (ltr558_t *)i2c_get_clientdata(this_client);
	return snprintf(buf, PAGE_SIZE, "ltr558 prox %d\n", p_flag);
}

static ssize_t ltr_558als_prox_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	//struct ltr558_t *chip = dev_get_drvdata(dev);
    ltr558_t *pls = (ltr558_t *)i2c_get_clientdata(this_client);
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;
    if (value) {
        p_flag = 1;
        #ifdef LTR558_PROX_WAKE_LOCK
        wake_lock(&pls->prox_wake_lock);
        #endif
        ltr558_ps_enable(p_gainrange);
    }else {
        p_flag = 0;
        #ifdef LTR558_PROX_WAKE_LOCK
        wake_unlock(&pls->prox_wake_lock);
        #endif
        ltr558_ps_disable();

    }
	return size;
}

static ssize_t ltr_558als_als_enable_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	//struct ltr558_t *chip = dev_get_drvdata(dev);
	//ltr558_t *pls = (ltr558_t *)i2c_get_clientdata(this_client);
	return snprintf(buf, PAGE_SIZE, "ltr558 als %d\n", l_flag);
}

static ssize_t ltr_558als_als_enable(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t size)
{
	//struct ltr558_t *chip = dev_get_drvdata(dev);
	bool value;

	if (strtobool(buf, &value))
		return -EINVAL;
    if (value) {
        l_flag = 1;
        ltr558_als_enable(l_gainrange);
    }else {
        l_flag = 0;
        ltr558_als_disable();

    }

	return size;
}


static struct kobject *ltr_558als_kobj;
static struct kobj_attribute ltr_558als_attr =
        __ATTR(ps_threshold, 0644, ltr_558als_show, NULL);
static struct kobj_attribute ltr_558als_prox_attr =
        __ATTR(prox_power_state, 0666, ltr_558als_prox_enable_show, ltr_558als_prox_enable);
static struct kobj_attribute ltr_558als_als_attr =
        __ATTR(als_power_state, 0666, ltr_558als_als_enable_show, ltr_558als_als_enable);
static DEVICE_ATTR(proximity, 0666, ltr_558als_prox_enable_show, ltr_558als_prox_enable);
static DEVICE_ATTR(als, 0666, ltr_558als_als_enable_show, ltr_558als_als_enable);
#ifdef LTR588_DBG
static struct kobj_attribute ltr_558als_val_attr =
        __ATTR(show_val, 0644, ltr_558als_val_show, ltr_558als_val_store);
#endif

static int ltr_558als_sysfs_init(struct input_dev *input_dev)
{
	int ret = -1;

	ltr_558als_kobj = kobject_create_and_add("ltr_558als",
		&(input_dev->dev.kobj));
	if (ltr_558als_kobj == NULL) {
		ret = -ENOMEM;
		PRINT_ERR("register sysfs failed. ret = %d\n", ret);
		return ret;
	}

	ret = sysfs_create_file(ltr_558als_kobj, &ltr_558als_attr.attr);
	if (ret) {
		PRINT_ERR("create sysfs failed. ret = %d\n", ret);
		return ret;
	}
#ifdef LTR588_DBG
	ret = sysfs_create_file(ltr_558als_kobj, &ltr_558als_val_attr.attr);
	if (ret) {
		PRINT_ERR("create sysfs failed. ret = %d\n", ret);
		return ret;
	}
#endif
	return ret;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ltr558_early_suspend(struct early_suspend *handler)
{
	PRINT_INFO("ltr558_early_suspend\n");
}

static void ltr558_late_resume(struct early_suspend *handler)
{
	PRINT_INFO("ltr558_late_resume\n");
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int ltr558_suspend(struct device *dev)
{
	PRINT_INFO("ltr558_suspend\n");
#if 0
//    if (p_flag)
//        ltr558_ps_disable();
    if (l_flag)
        ltr558_als_disable();
#endif
	return 0;
}

static int ltr558_resume(struct device *dev)
{
	//ltr558_t *pls = (ltr558_t *)i2c_get_clientdata(this_client);
	PRINT_INFO("ltr558_resume\n");
	ltr558_reg_init();
#if 0
    /*added by fully for test*/
    if(LTR_PLS_553 == LTR_PLS_MODE){
    ltr558_i2c_write_1_byte(0x80, 2); //sw reset
    }else {
    ltr558_i2c_write_1_byte(0x80, 4);
    }
    ltr558_reg_init();
	/*added by fully for test end */
    if (p_flag)
        ltr558_ps_enable(p_gainrange);
    if (l_flag)
        ltr558_als_enable(l_gainrange);
#else
    if (p_flag)
        ltr558_ps_enable(p_gainrange);
#endif
	return 0;
}
#endif
#ifdef LTR558_ALS_POLL
static void ltr558_work_func_light(struct work_struct *work)
{
	ltr558_t *ltr558 = container_of((struct delayed_work *)work, ltr558_t,
					      work_light);
	int value;
	value = ltr558_als_read(l_gainrange);
	PRINT_DBG("ALS POLL: ALS_DATA_VAL = 0x%04X(%4d)\n", value, value);
	input_report_abs(ltr558->input, ABS_MISC, value);
	input_sync(ltr558->input);
	input_report_abs(ltr558->als_input, ABS_MISC, value);
	input_sync(ltr558->als_input);

	schedule_delayed_work(&ltr558->work_light, msecs_to_jiffies(LIGHT_MEA_INTERVAL));
}
#endif
static int ltr558_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	int ret = 0;
	ltr558_t *ltr_558als = NULL;
	struct input_dev *input_dev = NULL;
    struct input_dev *als_input_dev = NULL;
	struct ltr558_pls_platform_data *pdata = client->dev.platform_data;
    struct class *pls_class;
    struct device *pls_cmd_dev;
#ifdef CONFIG_OF
	struct device_node *np = client->dev.of_node;
	if (np && !pdata) {
		pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
			if (!pdata) {
				dev_err(&client->dev, "Could not allocate struct ltr558_pls_platform_data");
				goto exit_allocate_pdata_failed;
			}
        pdata->prox_name = (char*)kzalloc((256*sizeof(char)), GFP_KERNEL);
        if (pdata->prox_name != NULL) {
            ret = of_property_read_string(np, "prox_name", &pdata->prox_name);
            if(ret){
                PRINT_ERR("fail to get prox_name\n");
            }else{
                PRINT_INFO("prox_name:%s\n", pdata->prox_name);
            }
        }
        pdata->als_name = (char*)kzalloc((256*sizeof(char)), GFP_KERNEL);
        if (pdata->als_name != NULL) {
            ret = of_property_read_string(np, "als_name", &pdata->als_name);
            if(ret){
                PRINT_ERR("fail to get als_name\n");
            }else{
                PRINT_INFO("als_name:%s\n", pdata->als_name);
            }
        }
		client->dev.platform_data = pdata;
	}
#endif
	PRINT_INFO("client->irq = %d\n", client->irq);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		PRINT_ERR("i2c_check_functionality failed!\n");
		ret = -ENODEV;
		goto exit_i2c_check_functionality_failed;
	}

	ltr_558als = kzalloc(sizeof(ltr558_t), GFP_KERNEL);
	if (!ltr_558als) {
		PRINT_ERR("kzalloc failed!\n");
		ret = -ENOMEM;
		goto exit_kzalloc_failed;
	}

	i2c_set_clientdata(client, ltr_558als);
	ltr_558als->client = client;
	this_client = client;
	ltr558_set_power(&client->dev, 1);

    mdelay(WAKEUP_DELAY);
    ret = ltr558_version_check();
    if(ret) {
	PRINT_ERR("ltr558_version_check failed!\n");
        goto exit_ltr558_version_check_failed;
    }
	if (LTR_553_PART_ID == ltr558_i2c_read_1_byte(LTR558_PART_ID)) {
		LTR_PLS_MODE = LTR_PLS_553;
		p_gainrange = PS_553_RANGE16;
		l_gainrange = ALS_553_RANGE48_1K3;
	} else {
		LTR_PLS_MODE = LTR_PLS_558;
		p_gainrange = PS_558_RANGE4;
        l_gainrange = ALS_558_RANGE1_320; //ALS_558_RANGE2_64K;
	}

	input_dev = input_allocate_device();
	if (!input_dev) {
		PRINT_ERR("input_allocate_device failed!\n");
		ret = -ENOMEM;
		goto exit_input_allocate_device_failed;
	}

    input_dev->name = pdata->prox_name;//LTR558_INPUT_DEV;
    //input_dev->phys = LTR558_INPUT_DEV;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;
	//input_dev->id.vendor = 0x0001;
	//input_dev->id.product = 0x0001;
	//input_dev->id.version = 0x0010;
	ltr_558als->input = input_dev;

	__set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);
	input_set_abs_params(input_dev, ABS_MISC, 0, 100001, 0, 0);

	ret = input_register_device(input_dev);
	if (ret < 0) {
		PRINT_ERR("input_register_device failed!\n");
		input_free_device(input_dev);
		input_dev = NULL;
		goto exit_input_register_device_failed;
	}

    als_input_dev = input_allocate_device();
    if (!als_input_dev) {
	    PRINT_ERR("input_allocate_device failed!\n");
	    ret = -ENOMEM;
	    goto exit_als_input_allocate_device_failed;
    }

    als_input_dev->name = pdata->als_name;//LTR558_INPUT_DEV;
    //als_input_dev->phys = LTR558_INPUT_DEV;
    als_input_dev->id.bustype = BUS_I2C;
    als_input_dev->dev.parent = &client->dev;
    ltr_558als->als_input = als_input_dev;

    __set_bit(EV_ABS, als_input_dev->evbit);
    input_set_abs_params(als_input_dev, ABS_MISC, 0, 100001, 0, 0);

    ret = input_register_device(als_input_dev);
    if (ret < 0) {
	    PRINT_ERR("als nput_register_device failed!\n");
	    input_free_device(als_input_dev);
	    als_input_dev = NULL;
	    goto exit_als_input_register_device_failed;
    }

	if (ltr558_reg_init() < 0) {
		PRINT_ERR("ltr558_reg_init failed!\n");
		ret = -1;
		goto exit_ltr558_reg_init_failed;
	}

    ret = misc_register(&ltr558_device);
    if (ret) {
	    PRINT_ERR("misc_register failed!\n");
	    goto exit_misc_register_failed;
    }

    #ifdef LTR558_PROX_WAKE_LOCK
    wake_lock_init(&ltr_558als->prox_wake_lock, WAKE_LOCK_SUSPEND, "prox_wake_lock");
    #endif
#ifdef LTR558_ALS_POLL
	INIT_DELAYED_WORK(&ltr_558als->work_light, ltr558_work_func_light);
#endif
	INIT_WORK(&ltr_558als->work, ltr558_work);
	ltr_558als->ltr_work_queue =
			create_singlethread_workqueue(LTR558_I2C_NAME);
	if (!ltr_558als->ltr_work_queue) {
		PRINT_ERR("create_singlethread_workqueue failed!\n");
		goto exit_create_singlethread_workqueue_failed;
	}

	if (client->irq > 0) {
		ret = request_irq(client->irq, ltr558_irq_handler,
		IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING,
		client->name, ltr_558als);
		if (ret < 0) {
			PRINT_ERR("request_irq failed!\n");
			goto exit_request_irq_failed;
		}
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ltr_558als->ltr_early_suspend.level =
				EARLY_SUSPEND_LEVEL_DISABLE_FB + 25;
	ltr_558als->ltr_early_suspend.suspend = ltr558_early_suspend;
	ltr_558als->ltr_early_suspend.resume = ltr558_late_resume;
	register_early_suspend(&ltr_558als->ltr_early_suspend);
#endif

	ret = ltr_558als_sysfs_init(input_dev);
	if (ret) {
		PRINT_ERR("ltr_558als_sysfs_init failed!\n");
		goto exit_ltr_558als_sysfs_init_failed;
	}
#if 1
	pls_class = class_create(THIS_MODULE,"xr-pls");//client->name
	if(IS_ERR(pls_class))
		PRINT_ERR("Failed to create class(xr-pls)!\n");
	pls_cmd_dev = device_create(pls_class, NULL, 0, NULL, "device");//device
	if(IS_ERR(pls_cmd_dev))
		PRINT_ERR("Failed to create device(pls_cmd_dev)!\n");
	if(device_create_file(pls_cmd_dev, &dev_attr_proximity) < 0) // /sys/class/xr-pls/device/proximity
	{
	    PRINT_ERR("Failed to create device file(%s)!\n", dev_attr_proximity.attr.name);
	}
	if(device_create_file(pls_cmd_dev, &dev_attr_als) < 0) // /sys/class/xr-pls/device/als
	{
	    PRINT_ERR("Failed to create device file(%s)!\n", dev_attr_als.attr.name);
	}
#endif
	PRINT_INFO("probe success!\n");
	return 0;

exit_ltr_558als_sysfs_init_failed:
	free_irq(ltr_558als->client->irq, ltr_558als);
exit_request_irq_failed:
	destroy_workqueue(ltr_558als->ltr_work_queue);
	ltr_558als->ltr_work_queue = NULL;
exit_create_singlethread_workqueue_failed:
    #ifdef LTR558_PROX_WAKE_LOCK
    wake_lock_destroy(&ltr_558als->prox_wake_lock);
    #endif
	misc_deregister(&ltr558_device);
exit_misc_register_failed:
exit_ltr558_reg_init_failed:
	input_unregister_device(als_input_dev);
exit_als_input_register_device_failed:
exit_als_input_allocate_device_failed:
	input_unregister_device(input_dev);
exit_input_register_device_failed:
exit_input_allocate_device_failed:
exit_ltr558_version_check_failed:
	ltr558_set_power(&client->dev, 0);
	kfree(ltr_558als);
	ltr_558als = NULL;
exit_kzalloc_failed:
exit_i2c_check_functionality_failed:
//exit_gpio_request_failed:
//exit_irq_gpio_read_fail:
exit_allocate_pdata_failed:
	PRINT_ERR("probe failed!\n");
	return ret;
}

static int ltr558_remove(struct i2c_client *client)
{
	ltr558_t *ltr_558als = i2c_get_clientdata(client);
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ltr_558als->ltr_early_suspend);
#endif

	flush_workqueue(ltr_558als->ltr_work_queue);
	destroy_workqueue(ltr_558als->ltr_work_queue);
	ltr_558als->ltr_work_queue = NULL;

	misc_deregister(&ltr558_device);
	input_unregister_device(ltr_558als->input);
    input_unregister_device(ltr_558als->als_input);
	input_free_device(ltr_558als->input);
    input_free_device(ltr_558als->als_input);
	ltr_558als->input = NULL;
	free_irq(ltr_558als->client->irq, ltr_558als);
	kfree(ltr_558als);
	ltr_558als = NULL;
	this_client = NULL;

	PRINT_INFO("ltr558_remove\n");
	return 0;
}

static const struct i2c_device_id ltr558_id[] = {
	{LTR558_I2C_NAME, 0},
	{ }
};

static const struct of_device_id ltr558_of_match[] = {
	{ .compatible = "LITEON,ltr_558als", },
	{ }
};
MODULE_DEVICE_TABLE(of, ltr558_of_match);

static const struct dev_pm_ops ltr558_pm_ops = {
	.suspend = ltr558_suspend,
	.resume  = ltr558_resume,
};

static struct i2c_driver ltr558_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = LTR558_I2C_NAME,
		.of_match_table = ltr558_of_match,
        #ifdef CONFIG_PM_RUNTIME
		.pm = &ltr558_pm_ops,
#endif
		},
		.probe = ltr558_probe,
		.remove = ltr558_remove,
		.id_table = ltr558_id,
};

static int __init ltr558_init(void)
{
	int ret = -1;
	ret = i2c_add_driver(&ltr558_driver);
	if (ret) {
		PRINT_ERR("i2c_add_driver failed!\n");
		return ret;
	}
	return ret;
}

static void __exit ltr558_exit(void)
{
	i2c_del_driver(&ltr558_driver);
}

late_initcall(ltr558_init);
module_exit(ltr558_exit);

MODULE_AUTHOR("Yaochuan Li <yaochuan.li@spreadtrum.com>");
MODULE_DESCRIPTION("Proximity&Light Sensor LTR558ALS DRIVER");
MODULE_LICENSE("GPL");
