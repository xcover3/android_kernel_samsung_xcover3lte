/* ltr55x.c
 * LTR-On LTR-55x Proxmity and Light sensor driver
 *
 * Copyright (C) 2011 Lite-On Technology Corp (Singapore)
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
/*
 *  2011-11-18 Thundersoft porting to MSM7x27A platform.
 *  2011-05-01 Lite-On created base driver.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/string.h>
#include <linux/irq.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <asm/atomic.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include "ltr55x.h"

#define SENSOR_NAME			    "proximity"
#define LTR55x_DRV_NAME		    "ltr55x"
#define LTR55x_MANUFAC_ID	    0x05
#define VENDOR_NAME				"lite-on"
#define LTR55x_SENSOR_NAME		"ltr55xals"
#define DRIVER_VERSION		    "1.0"
#define LTR55x_PS_CALIBERATE
#define LIGHT_SENSOR_NAME       "ltr55x_light";
#define PROXIMITY_SENSOR_NAME   "ltr55x_proximity";

#define DEVICE_ATTR2(_name, _mode, _show, _store) \
struct device_attribute dev_attr2_##_name = __ATTR(_name, _mode, _show, _store)

struct ltr55x_data
{

	struct i2c_client *client;
	struct input_dev *input_dev_als;
	struct input_dev *input_dev_ps;
	/* pinctrl data*/
	struct pinctrl *pinctrl;
	struct pinctrl_state *pin_default;
	struct pinctrl_state *pin_sleep;
	struct ltr55x_platform_data *platform_data;
	/* regulator data */
	bool power_state;
	struct regulator *vdd;
	struct regulator *vio;
	/* interrupt type is level-style */
	struct mutex lockw;
	struct mutex op_lock;
	struct delayed_work ps_work;
	struct delayed_work als_work;
	u8 ps_open_state;
	u8 als_open_state;
	u16 irq;
	u32 ps_state;
	u32 last_lux;
#ifdef LTR55x_PS_CALIBERATE
	bool cali_update;
#endif
	u32 dynamic_noise;
};

struct ltr55x_reg
{
	const char *name;
	u8 addr;
	u16 defval;
	u16 curval;
};

enum ltr55x_reg_tbl
{
	REG_ALS_CONTR,
	REG_PS_CONTR,
	REG_ALS_PS_STATUS,
	REG_INTERRUPT,
	REG_PS_LED,
	REG_PS_N_PULSES,
	REG_PS_MEAS_RATE,
	REG_ALS_MEAS_RATE,
	REG_MANUFACTURER_ID,
	REG_INTERRUPT_PERSIST,
	REG_PS_THRES_LOW,
	REG_PS_THRES_UP,
	REG_ALS_THRES_LOW,
	REG_ALS_THRES_UP,
	REG_ALS_DATA_CH1,
	REG_ALS_DATA_CH0,
	REG_PS_DATA
};

/*static int ltr55x_als_set_enable(struct sensors_classdev *sensors_cdev,
								 unsigned int enable);*/
/*static int ltr55x_ps_set_enable(struct sensors_classdev *sensors_cdev,
								unsigned int enable);*/
static int ltr55x_als_set_poll_delay(struct ltr55x_data *data,unsigned long delay);

static ssize_t ltr55x_ps_self_caliberate(struct ltr55x_data *data);

static  struct ltr55x_reg reg_tbl[] = {
	{
		.name   = "ALS_CONTR",
		.addr   = 0x80,
		.defval = 0x00,
		.curval = 0x19,
	},
	{
		.name = "PS_CONTR",
		.addr = 0x81,
		.defval = 0x00,
		.curval = 0x03,
	},
	{
		.name = "ALS_PS_STATUS",
		.addr = 0x8c,
		.defval = 0x00,
		.curval = 0x00,
	},
	{
		.name = "INTERRUPT",
		.addr = 0x8f,
		.defval = 0x00,
		.curval = 0x01,
	},
	{
		.name = "PS_LED",
		.addr = 0x82,
		.defval = 0x7f,
		.curval = 0x7f,
	},
	{
		.name = "PS_N_PULSES",
		.addr = 0x83,
		.defval = 0x01,
		.curval = 0x06,
	},
	{
		.name = "PS_MEAS_RATE",
		.addr = 0x84,
		.defval = 0x02,
		.curval = 0x00,
	},
	{
		.name = "ALS_MEAS_RATE",
		.addr = 0x85,
		//		.defval = 0x03,
		//		.curval = 0x03,
		.defval = 0x03,
		.curval = 0x02,	//200ms
	},
	{
		.name = "MANUFACTURER_ID",
		.addr = 0x87,
		.defval = 0x05,
		.curval = 0x05,
	},
	{
		.name = "INTERRUPT_PERSIST",
		.addr = 0x9e,
		.defval = 0x00,
		.curval = 0x23,
	},
	{
		.name = "PS_THRES_LOW",
		.addr = 0x92,
		.defval = 0x0000,
		.curval = 0x0000,
	},
	{
		.name = "PS_THRES_UP",
		.addr = 0x90,
		.defval = 0x07ff,
		.curval = 0x0000,
	},
	{
		.name = "ALS_THRES_LOW",
		.addr = 0x99,
		.defval = 0x0000,
		.curval = 0x0000,
	},
	{
		.name = "ALS_THRES_UP",
		.addr = 0x97,
		.defval = 0xffff,
		//.defval = 0x0000,
		.curval = 0x0000,
	},
	{
		.name = "ALS_DATA_CH1",
		.addr = 0x88,
		.defval = 0x0000,
		.curval = 0x0000,
	},
	{
		.name = "ALS_DATA_CH0",
		.addr = 0x8a,
		.defval = 0x0000,
		.curval = 0x0000,
	},
	{
		.name = "PS_DATA",
		.addr = 0x8d,
		.defval = 0x0000,
		.curval = 0x0000,
	},
};

static int ltr55x_ps_read(struct i2c_client *client)
{
	int psval_lo, psval_hi, psdata;
	psval_lo = i2c_smbus_read_byte_data(client, LTR55x_PS_DATA_0);
	if(psval_lo < 0)
	{
		psdata = psval_lo;
		goto out;
	}
	psval_hi = i2c_smbus_read_byte_data(client, LTR55x_PS_DATA_1);
	if(psval_hi < 0)
	{
		psdata = psval_hi;
		goto out;
	}

	psdata = ((psval_hi & 7)* 256) + psval_lo;
	printk("ps_rawdata = %d\n", psdata);

	out:
	return psdata;
}


static int ltr55x_chip_reset(struct i2c_client *client)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, LTR55x_ALS_CONTR, MODE_ALS_StdBy);//als standby mode
	ret = i2c_smbus_write_byte_data(client, LTR55x_PS_CONTR, MODE_PS_StdBy);//ps standby mode
	ret = i2c_smbus_write_byte_data(client, LTR55x_ALS_CONTR, 0x02);//reset
	if(ret < 0)
		printk("%s reset chip fail\n",__func__);

	return ret;
}

static void ltr55x_set_ps_threshold(struct i2c_client *client, u8 addr, u16 value)
{
	i2c_smbus_write_byte_data(client, addr, (value & 0xff));
	i2c_smbus_write_byte_data(client, addr+1, (value >> 8));
}

static int ltr55x_ps_enable(struct i2c_client *client, int on)
{
	struct ltr55x_data *data = i2c_get_clientdata(client);
	int ret=0;
	int contr_data;

	if(on)
	{
		ltr55x_set_ps_threshold(client, LTR55x_PS_THRES_LOW_0, 0);
		ltr55x_set_ps_threshold(client, LTR55x_PS_THRES_UP_0, data->platform_data->prox_threshold);
		ret = i2c_smbus_write_byte_data(client, LTR55x_PS_CONTR, reg_tbl[REG_PS_CONTR].curval);
		//ret = i2c_smbus_write_byte_data(client, LTR55x_INTERRUPT, reg_tbl[REG_INTERRUPT].curval);//enable irq
		if(ret<0)
		{
			pr_err("%s: enable=(%d) failed!\n", __func__, on);
			return ret;
		}
		contr_data = i2c_smbus_read_byte_data(client, LTR55x_PS_CONTR);
		if(contr_data != reg_tbl[REG_PS_CONTR].curval)
		{
			//data->ps_open_state = 1;
			pr_err("%s: enable=(%d) failed!\n", __func__, on);
			return -EFAULT;
		}

		msleep(WAKEUP_DELAY);
		data->ps_state = 1;
		input_report_abs(data->input_dev_ps, ABS_DISTANCE, data->ps_state);
		ltr55x_ps_self_caliberate(data);
	}
	else
	{
		//ret = i2c_smbus_write_byte_data(client, LTR55x_INTERRUPT, reg_tbl[REG_INTERRUPT].defval);//disable irq
		ret = i2c_smbus_write_byte_data(client, LTR55x_PS_CONTR, MODE_PS_StdBy);
		if(ret<0)
		{
			pr_err("%s: enable=(%d) failed!\n", __func__, on);
			return ret;
		}
		//data->ps_open_state = 0;
		contr_data = i2c_smbus_read_byte_data(client, LTR55x_PS_CONTR);
		if(contr_data != reg_tbl[REG_PS_CONTR].defval)
		{
			//data->ps_open_state = 1;
			pr_err("%s:  enable=(%d) failed!\n", __func__, on);
			return -EFAULT;
		}
	}
        data->ps_open_state = on;
	pr_err("%s: enable=(%d) OK\n", __func__, on);
	return ret;
}

/*
 * Absent Light Sensor Congfig
 */
static int ltr55x_als_enable(struct i2c_client *client, int on)
{
	struct ltr55x_data *data = i2c_get_clientdata(client);
	int ret;
	if(on)
	{
		ret = i2c_smbus_write_byte_data(client, LTR55x_ALS_CONTR, reg_tbl[REG_ALS_CONTR].curval);
		msleep(WAKEUP_DELAY);
		ret |= i2c_smbus_read_byte_data(client, LTR55x_ALS_DATA_CH0_1);
		cancel_delayed_work_sync(&data->als_work);
		schedule_delayed_work(&data->als_work, msecs_to_jiffies(data->platform_data->als_poll_interval));
	}
	else
	{
		cancel_delayed_work_sync(&data->als_work);
		ret = i2c_smbus_write_byte_data(client, LTR55x_ALS_CONTR, MODE_ALS_StdBy);
	}
        data->als_open_state = on;
	pr_err("%s: enable=(%d) ret=%d\n", __func__, on, ret);
	return ret;
}

static int ltr55x_als_read(struct i2c_client *client)
{
	int alsval_ch0_lo, alsval_ch0_hi, alsval_ch0;
	int alsval_ch1_lo, alsval_ch1_hi, alsval_ch1;
	int luxdata;
	int ch1_co, ch0_co, ratio;
	alsval_ch1_lo = i2c_smbus_read_byte_data(client, LTR55x_ALS_DATA_CH1_0);
	alsval_ch1_hi = i2c_smbus_read_byte_data(client, LTR55x_ALS_DATA_CH1_1);
	if(alsval_ch1_lo < 0 || alsval_ch1_hi < 0)
		return -1;
	alsval_ch1 = (alsval_ch1_hi << 8) + alsval_ch1_lo;
	alsval_ch0_lo = i2c_smbus_read_byte_data(client, LTR55x_ALS_DATA_CH0_0);
	alsval_ch0_hi = i2c_smbus_read_byte_data(client, LTR55x_ALS_DATA_CH0_1);
	if(alsval_ch0_lo < 0 || alsval_ch0_hi < 0)
		return -1;
	alsval_ch0 = (alsval_ch0_hi << 8) + alsval_ch0_lo;

	if((alsval_ch0 + alsval_ch1) == 0)
	{
		ratio = 1000;
	}
	else
	{
		ratio = alsval_ch1 * 1000 / (alsval_ch1 + alsval_ch0);
	}

	if(ratio < 450)
	{
		ch0_co = 17743;
		ch1_co = -11059;
	}
	else if((ratio >= 450) && (ratio < 640))
	{
		ch0_co = 42785;
		ch1_co = 19548;
	}
	else if((ratio >= 640) && (ratio < 850))
	{
		ch0_co = 5926;
		ch1_co = -1185;
	}
	else if(ratio >= 850)
	{
		ch0_co = 0;
		ch1_co = 0;
	}
	luxdata = (alsval_ch0 * ch0_co - alsval_ch1 * ch1_co) / 10000;
	return luxdata;
}

static void ltr55x_ps_work_func(struct work_struct *work)
{
	struct ltr55x_data *data = container_of(work, struct ltr55x_data, ps_work.work);
	struct i2c_client *client=data->client;
	int als_ps_status;
	int psdata;
	static int val_temp = 1;
	static u32 ps_state_last = 1;	// xuke @ 20140828	Far as default.
	mutex_lock(&data->op_lock);

	als_ps_status = i2c_smbus_read_byte_data(client, LTR55x_ALS_PS_STATUS);
	printk("%s ps_open_state=%d, als_ps_status=0x%x\n",__func__,data->ps_open_state,als_ps_status);
	if(als_ps_status < 0)
		goto workout;
	/* Here should check data status,ignore interrupt status. */
	/* Bit 0: PS Data
	 * Bit 1: PS interrupt
	 * Bit 2: ASL Data
	 * Bit 3: ASL interrupt
	 * Bit 4: ASL Gain 0: ALS measurement data is in dynamic range 2 (2 to 64k lux)
	 *                 1: ALS measurement data is in dynamic range 1 (0.01 to 320 lux)
	 */
	if((data->ps_open_state == 1) && (als_ps_status & 0x02))
	{
		psdata = ltr55x_ps_read(client);
		printk("%s ps data=%d(0x%x), prox_threshold=%d, prox_hsyteresis_threshold=%d\n",__func__,psdata,psdata,data->platform_data->prox_threshold,data->platform_data->prox_hsyteresis_threshold);
		if(psdata > data->platform_data->prox_threshold)
		{
			data->ps_state = 0;	//near
			ltr55x_set_ps_threshold(client, LTR55x_PS_THRES_LOW_0, data->platform_data->prox_hsyteresis_threshold);
			ltr55x_set_ps_threshold(client, LTR55x_PS_THRES_UP_0, 0x07ff);
			val_temp = 0;
		}
		else if(psdata < data->platform_data->prox_hsyteresis_threshold)
		{
			data->ps_state = 1;	//far
			ltr55x_set_ps_threshold(client, LTR55x_PS_THRES_LOW_0, 0);
			ltr55x_set_ps_threshold(client, LTR55x_PS_THRES_UP_0, data->platform_data->prox_threshold);
			val_temp = 1;
		}
		else
		{
			data->ps_state = val_temp;
		}

		if(ps_state_last != data->ps_state)
		{
			input_report_abs(data->input_dev_ps, ABS_DISTANCE, data->ps_state);
			input_sync(data->input_dev_ps);
			printk("%s, report ABS_DISTANCE=%s\n",__func__, data->ps_state ? "far" : "near");
			if(data->ps_state == 1 && data->dynamic_noise > 20 && psdata < (data->dynamic_noise - 50))
			{
				data->dynamic_noise = psdata;
				if(psdata < 50)
				{
					data->platform_data->prox_threshold = psdata+150;
					data->platform_data->prox_hsyteresis_threshold = psdata+100;
				}
				else if(psdata < 100)
				{
					data->platform_data->prox_threshold = psdata+160;
					data->platform_data->prox_hsyteresis_threshold = psdata+110;
				}
				else if(psdata < 600)
				{
					data->platform_data->prox_threshold = psdata+160;
					data->platform_data->prox_hsyteresis_threshold = psdata+120;
				}
				else if(psdata < 1500)
				{
					data->platform_data->prox_threshold = psdata+200;
					data->platform_data->prox_hsyteresis_threshold = psdata+150;
				}
				else if(psdata < 1650)
				{
					data->platform_data->prox_threshold = psdata+400;
					data->platform_data->prox_hsyteresis_threshold = psdata+160;
				}
				else
				{
					pr_err("ltr55x the proximity sensor rubber or structure is error!\n");
				}
				pr_info("ltr55x self calibrate when state far : noise = %d , thd_val_low = %d , htd_val_high = %d \n", psdata, data->platform_data->prox_hsyteresis_threshold, data->platform_data->prox_threshold);
				ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_LOW_0, 0);
				ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_UP_0, data->platform_data->prox_threshold);
			}

			ps_state_last = data->ps_state;		// xuke @ 20140828	Report ABS value only if the state changed.
		}
		else
			printk("%s, ps_state still %s\n", __func__, data->ps_state ? "far" : "near");
	}
	workout:
	enable_irq(data->irq);
	mutex_unlock(&data->op_lock);
}

static void ltr55x_als_work_func(struct work_struct *work)
{
	struct ltr55x_data *data = container_of(work, struct ltr55x_data, als_work.work);
	struct i2c_client *client=data->client;
	int als_ps_status;
	int als_data;

	mutex_lock(&data->op_lock);

	//printk("%s\n",__func__);

	if(!data->als_open_state)
		goto workout;

	als_ps_status = i2c_smbus_read_byte_data(client, LTR55x_ALS_PS_STATUS);
	if(als_ps_status < 0)
		goto workout;


	if((data->als_open_state == 1) && (als_ps_status & 0x04))
	{
		als_data = ltr55x_als_read(client);
		if(als_data > 50000)
			als_data = 50000;

		//printk("%s als data %d\n",__func__,als_data);
		if((als_data >= 0) && (als_data != data->last_lux))
		{
			data->last_lux = als_data;
			input_report_abs(data->input_dev_als, ABS_MISC, als_data);
			input_sync(data->input_dev_als);
		}
	}

	schedule_delayed_work(&data->als_work, msecs_to_jiffies(data->platform_data->als_poll_interval));
	workout:
	mutex_unlock(&data->op_lock);
}

static irqreturn_t ltr55x_irq_handler(int irq, void *arg)
{
	struct ltr55x_data *data = (struct ltr55x_data *)arg;

	printk("%s\n",__func__);
	if(NULL == data)
		return IRQ_HANDLED;
	disable_irq_nosync(data->irq);
	schedule_delayed_work(&data->ps_work, 0);
	return IRQ_HANDLED;
}

static int ltr55x_gpio_irq(struct ltr55x_data *data)
{
	struct device_node *np = data->client->dev.of_node;
	int err = 0;

    data->platform_data->int_gpio = of_get_named_gpio_flags(np, "irq-gpios", 0, &data->platform_data->irq_gpio_flags);
	if(data->platform_data->int_gpio < 0)
		return -EIO;

	if(gpio_is_valid(data->platform_data->int_gpio))
	{
		err = gpio_request(data->platform_data->int_gpio, "ltr55x_irq_gpio");
		if(err)
		{
			printk( "%s irq gpio request failed\n",__func__);
			return -EINTR;
		}

		err = gpio_direction_input(data->platform_data->int_gpio);
		if(err)
		{
			printk("%s set_direction for irq gpio failed\n",__func__);
			return -EIO;
		}
	}

	data->irq = data->client->irq = gpio_to_irq(data->platform_data->int_gpio);

	if(request_irq(data->irq, ltr55x_irq_handler, IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING/*IRQF_DISABLED|IRQ_TYPE_EDGE_FALLING*/,
				   LTR55x_DRV_NAME, data))
	{
		printk("%s Could not allocate ltr55x_INT !\n", __func__);
		return -EINTR;
	}

	irq_set_irq_wake(data->irq, 1);

	printk(KERN_INFO "%s: INT No. %d", __func__, data->irq);
	return 0;
}


static void ltr55x_gpio_irq_free(struct ltr55x_data *data)
{
	free_irq(data->irq, data);
	gpio_free(data->platform_data->int_gpio);
}

static ssize_t ltr55x_show_enable_ps(struct device *dev,
									 struct device_attribute *attr, char *buf)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->ps_open_state);
}

static ssize_t ltr55x_store_enable_ps(struct device *dev,
									  struct device_attribute *attr,
									  const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	unsigned long val;
	char *after;
	struct ltr55x_data *data = dev_get_drvdata(dev);

	val = simple_strtoul(buf, &after, 10);

	printk(KERN_INFO "enable 55x PS sensor -> %ld\n", val);

	mutex_lock(&data->lockw);

	ltr55x_ps_enable(data->client, (int)val);

	mutex_unlock(&data->lockw);

	return size;
}

static ssize_t ltr55x_show_enable_ps_for_input_device(struct device *dev,
									 struct device_attribute *attr, char *buf)
{
	struct input_dev *input = to_input_dev(dev);

	struct ltr55x_data *data = input_get_drvdata(input);

	return sprintf(buf, "%u\n", data->ps_open_state);
}

static ssize_t ltr55x_store_enable_ps_for_input_device(struct device *dev,
									  struct device_attribute *attr,
									  const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	unsigned long val;
	char *after;

	struct input_dev *input = to_input_dev(dev);

	struct ltr55x_data *data = input_get_drvdata(input);

	val = simple_strtoul(buf, &after, 10);

	printk(KERN_INFO "enable 55x PS sensor -> %ld\n", val);

	mutex_lock(&data->lockw);

	ltr55x_ps_enable(data->client, (int)val);

	mutex_unlock(&data->lockw);

	return size;
}

static ssize_t ltr55x_show_poll_delay_als(struct device *dev,
										  struct device_attribute *attr, char *buf)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->platform_data->als_poll_interval);
}

static ssize_t ltr55x_store_poll_delay_als(struct device *dev,
										   struct device_attribute *attr,
										   const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	unsigned long val;
	char *after;
	struct ltr55x_data *data = dev_get_drvdata(dev);

	val = simple_strtoul(buf, &after, 10);

	printk(KERN_INFO "set 55x ALS sensor poll delay -> %ld\n", val);

	mutex_lock(&data->lockw);
	ltr55x_als_set_poll_delay(data,val);
	mutex_unlock(&data->lockw);

	return size;
}

static ssize_t ltr55x_show_enable_als(struct device *dev,
									  struct device_attribute *attr, char *buf)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->als_open_state);
}

static ssize_t ltr55x_store_enable_als(struct device *dev,
									   struct device_attribute *attr,
									   const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	unsigned long val;
	char *after;
	struct ltr55x_data *data = dev_get_drvdata(dev);

	val = simple_strtoul(buf, &after, 10);

	printk(KERN_INFO "enable 55x ALS sensor -> %ld\n", val);

	mutex_lock(&data->lockw);
	ltr55x_als_enable(data->client, (int)val);
	mutex_unlock(&data->lockw);
	return size;
}

static ssize_t ltr55x_show_enable_als_for_input_device(struct device *dev,
									  struct device_attribute *attr, char *buf)
{

	struct input_dev *input = to_input_dev(dev);

	struct ltr55x_data *data = input_get_drvdata(input);

	return sprintf(buf, "%u\n", data->als_open_state);
}

static ssize_t ltr55x_store_enable_als_for_input_device(struct device *dev,
									   struct device_attribute *attr,
									   const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	unsigned long val;

	char *after;

	struct input_dev *input = to_input_dev(dev);

	struct ltr55x_data *data = input_get_drvdata(input);

	val = simple_strtoul(buf, &after, 10);

	printk(KERN_INFO "enable 55x ALS sensor -> %ld\n", val);

	mutex_lock(&data->lockw);
	ltr55x_als_enable(data->client, (int)val);
	mutex_unlock(&data->lockw);
	return size;
}


static ssize_t ltr55x_driver_info_show(struct device *dev,
									   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "Chip: %s %s\nVersion: %s\n",VENDOR_NAME, LTR55x_SENSOR_NAME, DRIVER_VERSION);
}

static ssize_t ltr55x_show_debug_regs(struct device *dev,
									  struct device_attribute *attr, char *buf)
{
	u8 val,high,low;
	int i;
	char *after;
	struct ltr55x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	after = buf;
	after += sprintf(after, "%-17s%5s%14s%16s\n", "Register Name", "address", "default", "current");
	for(i = 0; i < sizeof(reg_tbl)/sizeof(reg_tbl[0]); i++)
	{
		if(reg_tbl[i].name == NULL || reg_tbl[i].addr == 0)
		{
			break;
		}
		if(i < 10)
		{
			val = i2c_smbus_read_byte_data(client, reg_tbl[i].addr);
			after += sprintf(after, "%-20s0x%02x\t  0x%02x\t\t  0x%02x\n", reg_tbl[i].name, reg_tbl[i].addr, reg_tbl[i].defval, val);
		}
		else
		{
			low = i2c_smbus_read_byte_data(client, reg_tbl[i].addr);
			high = i2c_smbus_read_byte_data(client, reg_tbl[i].addr+1);
			after += sprintf(after, "%-20s0x%02x\t0x%04x\t\t0x%04x\n", reg_tbl[i].name, reg_tbl[i].addr, reg_tbl[i].defval,(high << 8) + low);
		}
	}
	after += sprintf(after, "\nYou can echo '0xaa=0xbb' to set the value 0xbb to the register of address 0xaa.\n ");

	return(after - buf);
}

static ssize_t ltr55x_store_debug_regs(struct device *dev,
									   struct device_attribute *attr,
									   const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	char *after, direct;
	u8 addr, val;
	struct ltr55x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	addr = simple_strtoul(buf, &after, 16);
	direct = *after;
	val = simple_strtoul((after+1), &after, 16);

	if(!((addr >= 0x80 && addr <= 0x93)
		 || (addr >= 0x97 && addr <= 0x9e)))
		return -EINVAL;

	mutex_lock(&data->lockw);
	if(direct == '=')
		i2c_smbus_write_byte_data(client, addr, val);
	else
		printk("%s: register(0x%02x) is: 0x%02x\n", __func__, addr, i2c_smbus_read_byte_data(client, addr));
	mutex_unlock(&data->lockw);

	return(after - buf);
}

static ssize_t ltr55x_show_adc_data(struct device *dev,
									struct device_attribute *attr, char *buf)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 high,low;
	char *after;

	after = buf;

	low = i2c_smbus_read_byte_data(client, LTR55x_PS_DATA_0);
	high = i2c_smbus_read_byte_data(client, LTR55x_PS_DATA_1);
	if(low < 0 || high < 0)
		after += sprintf(after, "Failed to read PS adc data.\n");
	else
		after += sprintf(after, "%d\n", (high << 8) + low);

	return(after - buf);
}

static ssize_t ltr55x_show_lux_data(struct device *dev,
									struct device_attribute *attr, char *buf)
{
	int lux;
	struct ltr55x_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	lux = ltr55x_als_read(client);

	return sprintf(buf, "%d\n", lux);
}

#ifdef LTR55x_PS_CALIBERATE
static ssize_t ltr55x_show_ps_threshold(struct device *dev,
										struct device_attribute *attr, char *buf)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->platform_data->prox_threshold);
}

static ssize_t ltr55x_store_ps_threshold(struct device *dev,
										 struct device_attribute *attr,
										 const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	unsigned long val;
	char *after;
	struct ltr55x_data *data = dev_get_drvdata(dev);

	val = simple_strtoul(buf, &after, 10);

	printk(KERN_INFO "ltr55x PS prox_threshold -> %ld\n", val);

	mutex_lock(&data->lockw);
	data->platform_data->prox_threshold = val;
	if(data->ps_state == 1)
	{
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_LOW_0, 0);
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_UP_0, data->platform_data->prox_threshold);
	}
	mutex_unlock(&data->lockw);

	return size;
}

static ssize_t ltr55x_show_ps_hsyteresis_threshold(struct device *dev,
												   struct device_attribute *attr, char *buf)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->platform_data->prox_hsyteresis_threshold);
}

static ssize_t ltr55x_store_ps_hsyteresis_threshold(struct device *dev,
													struct device_attribute *attr,
													const char *buf, size_t size)
{
	/* If proximity work,then ALS must be enable */
	unsigned long val;
	char *after;
	struct ltr55x_data *data = dev_get_drvdata(dev);

	val = simple_strtoul(buf, &after, 10);

	printk(KERN_INFO "ltr55x PS prox_hsyteresis_threshold -> %ld\n", val);

	mutex_lock(&data->lockw);
	data->platform_data->prox_hsyteresis_threshold = val;
	if(data->ps_state == 0)
	{
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_LOW_0, data->platform_data->prox_hsyteresis_threshold);
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_UP_0, 0x07ff);
	}
	mutex_unlock(&data->lockw);

	return size;
}

static ssize_t ltr55x_show_ps_cali_update_flag(struct device *dev,
											   struct device_attribute *attr, char *buf)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->cali_update);
}

static ssize_t ltr55x_store_ps_cali_update_flag(struct device *dev,
												struct device_attribute *attr,
												const char *buf, size_t size)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);

	mutex_lock(&data->lockw);
	data->cali_update = 0;
	mutex_unlock(&data->lockw);

	return size;
}
#endif

static DEVICE_ATTR(debug_regs, S_IRUGO | S_IWUGO, ltr55x_show_debug_regs,ltr55x_store_debug_regs);
static DEVICE_ATTR(enable_als_sensor, S_IRUGO | S_IWUGO, ltr55x_show_enable_als,ltr55x_store_enable_als);
static DEVICE_ATTR(enable, S_IRUGO | S_IWUGO, ltr55x_show_enable_ps,ltr55x_store_enable_ps);
static DEVICE_ATTR(poll_delay, S_IRUGO | S_IWUGO, ltr55x_show_poll_delay_als,ltr55x_store_poll_delay_als);
static DEVICE_ATTR(info, S_IRUGO, ltr55x_driver_info_show, NULL);
static DEVICE_ATTR(raw_adc, S_IRUGO, ltr55x_show_adc_data, NULL);
static DEVICE_ATTR(lux_adc, S_IRUGO, ltr55x_show_lux_data, NULL);
#ifdef LTR55x_PS_CALIBERATE
static DEVICE_ATTR(cali_param_1, S_IRUGO | S_IWUGO, ltr55x_show_ps_threshold,ltr55x_store_ps_threshold);
static DEVICE_ATTR(cali_param_2, S_IRUGO | S_IWUGO, ltr55x_show_ps_hsyteresis_threshold,ltr55x_store_ps_hsyteresis_threshold);
static DEVICE_ATTR(cali_update, S_IRUGO | S_IWUGO, ltr55x_show_ps_cali_update_flag,ltr55x_store_ps_cali_update_flag);
#endif

static DEVICE_ATTR(active, S_IRUGO | S_IWUGO,ltr55x_show_enable_als_for_input_device, ltr55x_store_enable_als_for_input_device);
static DEVICE_ATTR2(active, S_IRUGO | S_IWUGO,ltr55x_show_enable_ps_for_input_device, ltr55x_store_enable_ps_for_input_device);
static struct attribute *ltr55x_als_attributes[] = {
	&dev_attr_active.attr,
	NULL
};
static const struct attribute_group ltr55x_als_attr_group = {
.attrs = ltr55x_als_attributes,
};
static struct attribute *ltr55x_ps_attributes[] = {
	&dev_attr2_active.attr,
	NULL
};
static const struct attribute_group ltr55x_ps_attr_group = {
	.attrs = ltr55x_ps_attributes,
};


static struct attribute *ltr55x_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_info.attr,
	&dev_attr_enable_als_sensor.attr,
	&dev_attr_poll_delay.attr,
	&dev_attr_debug_regs.attr,
	&dev_attr_raw_adc.attr,
	&dev_attr_lux_adc.attr,
#ifdef LTR55x_PS_CALIBERATE
	&dev_attr_cali_param_1.attr,
	&dev_attr_cali_param_2.attr,
	&dev_attr_cali_update.attr,
#endif
	NULL,
};

static const struct attribute_group ltr55x_attr_group = {
	.attrs = ltr55x_attributes,
};

static int ltr55x_als_set_poll_delay(struct ltr55x_data *data,unsigned long delay)
{
	//mutex_lock(&data->op_lock);
	if(delay < 1)
		delay = 1;
	if(delay > 1000)
		delay = 1000;

	if(data->platform_data->als_poll_interval != delay)
	{
		data->platform_data->als_poll_interval = delay;
	}

	if(!data->als_open_state)
		return -ESRCH;

	pr_info("%s poll_interval=%d",__func__,data->platform_data->als_poll_interval);
	//cancel_delayed_work_sync(&data->als_work);
	//schedule_delayed_work(&data->als_work,msecs_to_jiffies(data->platform_data->als_poll_interval));
	//mutex_unlock(&data->op_lock);
	return 0;
}

#ifdef LTR55x_PS_CALIBERATE
static ssize_t ltr55x_ps_self_caliberate(struct ltr55x_data *data)
{
    struct ltr55x_platform_data *pdata = data->platform_data;
	int i=0;
	int ps;
	int data_total=0;
	int noise = 0;
	int count = 5;
	int max = 0;

	if(!data)
	{
		pr_err("ltr55x_data is null!!\n");
		return -EFAULT;
	}

	// wait for register to be stable
	msleep(15);

	for(i = 0; i < count; i++)
	{
		// wait for ps value be stable

		msleep(15);

		ps = ltr55x_ps_read(data->client);
		if(ps < 0)
		{
			i--;
			continue;
		}

		if(ps & 0x8000)
		{
			noise = 0;
			break;
		}
		else
		{
			noise = ps;
		}

		data_total += ps;

		if(max++ > 10)
		{
			pr_err("ltr55x read data error!\n");
			return -EFAULT;
		}
	}

	noise = data_total/count;
	data->dynamic_noise = noise;

	if(noise < 50)
	{
		pdata->prox_threshold = noise+150;
		pdata->prox_hsyteresis_threshold = noise+100;
	}
	else if(noise < 100)
	{
		pdata->prox_threshold = noise+160;
		pdata->prox_hsyteresis_threshold = noise+110;
	}
	else if(noise < 600)
	{
		pdata->prox_threshold = noise+160;
		pdata->prox_hsyteresis_threshold = noise+120;
	}
	else if(noise < 1500)
	{
		pdata->prox_threshold = noise+200;
		pdata->prox_hsyteresis_threshold = noise+150;
	}
	else if(noise < 1650)
	{
		pdata->prox_threshold = noise+400;
		pdata->prox_hsyteresis_threshold = noise+160;
	}
	else
	{
		pr_err("ltr55x the proximity sensor rubber or structure is error!\n");
		return -EAGAIN;
	}

	if(data->ps_state == 1)
	{
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_LOW_0, 0);
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_UP_0, data->platform_data->prox_threshold);
	}
	else if(data->ps_state == 0)
	{
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_LOW_0, data->platform_data->prox_hsyteresis_threshold);
		ltr55x_set_ps_threshold(data->client, LTR55x_PS_THRES_UP_0, 0x07ff);
	}

	data->cali_update = true;

	printk("%s : noise = %d , thd_val_low = %d , htd_val_high = %d \n",__func__, noise, pdata->prox_hsyteresis_threshold, pdata->prox_threshold);
	return 0;
}
#endif

static int ltr55x_suspend(struct device *dev)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);
	int ret = 0;

	printk("%s\n", __func__);
	mutex_lock(&data->lockw);
#if 1	// xuke @ 20140828	Can not suspend p-sensor with kernel PM.
// None.
#else
	ret = ltr55x_ps_enable(data->client, 0);
#endif
	if(data->als_open_state == 1)
		ret |= ltr55x_als_enable(data->client, 0);
	mutex_unlock(&data->lockw);
	return ret;
}

static int ltr55x_resume(struct device *dev)
{
	struct ltr55x_data *data = dev_get_drvdata(dev);
	int ret = 0;

	printk("%s\n", __func__);
	mutex_lock(&data->lockw);
	if(data->als_open_state == 1)
		ret = ltr55x_als_enable(data->client, 1);
#if 1	// xuke @ 20140828	No need to resume p-sensor with kernel PM.
// None.
#else
	if(data->ps_open_state == 1)
		ret = ltr55x_ps_enable(data->client, 1);
#endif
	mutex_unlock(&data->lockw);
	return ret;
}

static int ltr55x_check_chip_id(struct i2c_client *client)
{
	int id;

	id = i2c_smbus_read_byte_data(client, LTR55x_MANUFACTURER_ID);
	printk("%s read the  LTR55x_MANUFAC_ID is 0x%x\n", __func__, id);
	if(id != LTR55x_MANUFAC_ID)
	{
		return -EINVAL;
	}
	return 0;
}

int ltr55x_device_init(struct i2c_client *client)
{
	int retval = 0;
	int i;

	retval = i2c_smbus_write_byte_data(client, LTR55x_ALS_CONTR, 0x02);//reset chip
	if(retval < 0)
	{
		printk("%s   i2c_smbus_write_byte_data(LTR55x_ALS_CONTR, 0x02);  ERROR !!!.\n",__func__);
	}

	msleep(WAKEUP_DELAY);
	for(i = 2; i < sizeof(reg_tbl)/sizeof(reg_tbl[0]); i++)
	{
		if(reg_tbl[i].name == NULL || reg_tbl[i].addr == 0)
		{
			break;
		}
		if(reg_tbl[i].defval != reg_tbl[i].curval)
		{
			if(i < 10)
			{
				retval = i2c_smbus_write_byte_data(client, reg_tbl[i].addr, reg_tbl[i].curval);
				printk("___CAOYI____  ltr55x  write 0x%x to addr:0x%x\n", reg_tbl[i].curval,reg_tbl[i].addr);//add by caoyi
				printk("___CAOYI____ write is OK(0) or Error (-x) ? = return is %d\n", i2c_smbus_write_byte_data(client, reg_tbl[i].addr, reg_tbl[i].curval));
			}
			else
			{
				retval = i2c_smbus_write_byte_data(client, reg_tbl[i].addr, reg_tbl[i].curval & 0xff);
				printk("___CAOYI____  ltr55x  write 0x%x to addr:0x%x\n", reg_tbl[i].curval & 0xff,reg_tbl[i].addr);//add by caoyi
				retval = i2c_smbus_write_byte_data(client, reg_tbl[i].addr + 1, reg_tbl[i].curval >> 8);
				printk("___CAOYI____  ltr55x  write 0x%x to addr:0x%x\n", reg_tbl[i].curval >> 8,reg_tbl[i].addr);//add by caoyi
			}
		}
	}

	printk("___CAOYI______ read addr 0x9e is 0x%x\n",i2c_smbus_read_byte_data(client, 0x9e));  //add by caoyi
	printk("___CAOYI______ read addr 0x90 is 0x%x\n",i2c_smbus_read_byte_data(client, 0x90));  //add by caoyi


	printk("___CAOYI______||||  read is OK(+x) or Error (-x) ? = return is %d\n", i2c_smbus_read_byte_data(client, 0x1a));
	printk("___CAOYI______||||  write is OK(0) or Error (-x) ? = return is %d\n", i2c_smbus_write_byte_data(client, 0x1b,0x88));
	printk("___CAOYI______||||  write is OK(0) or Error (-x) ? = return is %d\n", i2c_smbus_write_byte_data(client, 0x11,0x88));

	return retval;
}

static int sensor_regulator_configure(struct ltr55x_data *data, bool on)
{
	int rc;

	if(!on)
	{
		if(regulator_count_voltages(data->vdd) > 0)
			regulator_set_voltage(data->vdd, 0, LTR55x_VDD_MAX_UV);
		regulator_put(data->vdd);

		if(regulator_count_voltages(data->vio) > 0)
			regulator_set_voltage(data->vio, 0, LTR55x_VIO_MAX_UV);
		regulator_put(data->vio);
	}
	else
	{
		data->vdd = regulator_get(&data->client->dev, "vdd");
		if(IS_ERR(data->vdd))
		{
			rc = PTR_ERR(data->vdd);
			dev_err(&data->client->dev,"Regulator get failed vdd rc=%d\n", rc);
			return rc;
		}

		if(regulator_count_voltages(data->vdd) > 0)
		{
			rc = regulator_set_voltage(data->vdd, LTR55x_VDD_MIN_UV, LTR55x_VDD_MAX_UV);
			if(rc)
			{
				dev_err(&data->client->dev,"Regulator set failed vdd rc=%d\n",rc);
				goto reg_vdd_put;
			}
		}

		data->vio = regulator_get(&data->client->dev, "vio");
		if(IS_ERR(data->vio))
		{
			rc = PTR_ERR(data->vio);
			dev_err(&data->client->dev,"Regulator get failed vio rc=%d\n", rc);
			goto reg_vdd_set;
		}

		if(regulator_count_voltages(data->vio) > 0)
		{
			rc = regulator_set_voltage(data->vio, LTR55x_VIO_MIN_UV, LTR55x_VIO_MAX_UV);
			if(rc)
			{
				dev_err(&data->client->dev, "Regulator set failed vio rc=%d\n", rc);
				goto reg_vio_put;
			}
		}
	}

	return 0;
	reg_vio_put:
	regulator_put(data->vio);

	reg_vdd_set:
	if(regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, LTR55x_VDD_MAX_UV);
	reg_vdd_put:
	regulator_put(data->vdd);
	return rc;
}

static int sensor_regulator_power_on(struct ltr55x_data *data, bool on)
{
	int rc = 0;

	if(!on)
	{
		rc = regulator_disable(data->vdd);
		if(rc)
		{
			dev_err(&data->client->dev, "Regulator vdd disable failed rc=%d\n", rc);
			return rc;
		}

		rc = regulator_disable(data->vio);
		if(rc)
		{
			dev_err(&data->client->dev, "Regulator vio disable failed rc=%d\n", rc);
			rc = regulator_enable(data->vdd);
			dev_err(&data->client->dev, "Regulator vio re-enabled rc=%d\n", rc);
			/*
			 * Successfully re-enable regulator.
			 * Enter poweron delay and returns error.
			 */
			if(!rc)
			{
				rc = -EBUSY;
				goto enable_delay;
			}
		}
		return rc;
	}
	else
	{
		rc = regulator_enable(data->vdd);
		if(rc)
		{
			dev_err(&data->client->dev, "Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}

		rc = regulator_enable(data->vio);
		if(rc)
		{
			dev_err(&data->client->dev, "Regulator vio enable failed rc=%d\n", rc);
			regulator_disable(data->vdd);
			return rc;
		}
	}

	enable_delay:
	msleep(130);
	dev_dbg(&data->client->dev, "Sensor regulator power on =%d\n", on);
	return rc;
}

static int sensor_platform_hw_power_onoff(struct ltr55x_data *data, bool on)
{
	int err = 0;

	if(data->power_state != on)
	{
		if(on)
		{
			if(!IS_ERR_OR_NULL(data->pinctrl))
			{
				err = pinctrl_select_state(data->pinctrl, data->pin_default);
				if(err)
				{
					dev_err(&data->client->dev, "Can't select pinctrl state on=%d\n", on);
					goto power_out;
				}
			}

			err = sensor_regulator_configure(data, true);
			if(err)
			{
				dev_err(&data->client->dev, "unable to configure regulator on=%d\n", on);
				goto power_out;
			}

			err = sensor_regulator_power_on(data, true);
			if(err)
			{
				dev_err(&data->client->dev, "Can't configure regulator on=%d\n", on);
				goto power_out;
			}

			data->power_state = true;
		}
		else
		{
			if(!IS_ERR_OR_NULL(data->pinctrl))
			{
				err = pinctrl_select_state(data->pinctrl, data->pin_sleep);
				if(err)
				{
					dev_err(&data->client->dev, "Can't select pinctrl state on=%d\n", on);
					goto power_out;
				}
			}

			err = sensor_regulator_power_on(data, false);
			if(err)
			{
				dev_err(&data->client->dev, "Can't configure regulator on=%d\n", on);
				goto power_out;
			}

			err = sensor_regulator_configure(data, false);
			if(err)
			{
				dev_err(&data->client->dev, "unable to configure regulator on=%d\n", on);
				goto power_out;
			}

			data->power_state = false;
		}
	}
	power_out:
	return err;
}
#if 0
static int ltr55x_pinctrl_init(struct ltr55x_data *data)
{
	struct i2c_client *client = data->client;

	data->pinctrl = devm_pinctrl_get(&client->dev);
	if(IS_ERR_OR_NULL(data->pinctrl))
	{
		dev_err(&client->dev, "Failed to get pinctrl\n");
		return PTR_ERR(data->pinctrl);
	}
	data->pin_default = pinctrl_lookup_state(data->pinctrl, "default");
	if(IS_ERR_OR_NULL(data->pin_default))
	{
		dev_err(&client->dev, "Failed to look up default state\n");
		return PTR_ERR(data->pin_default);
	}

	data->pin_sleep = pinctrl_lookup_state(data->pinctrl, "sleep");
	if(IS_ERR_OR_NULL(data->pin_sleep))
	{
		dev_err(&client->dev, "Failed to look up sleep state\n");
		return PTR_ERR(data->pin_sleep);
	}


	return 0;
}
#endif

static int ltr55x_parse_dt(struct device *dev, struct ltr55x_data *data)
{
	struct ltr55x_platform_data *pdata = data->platform_data;
	struct device_node *np = dev->of_node;
	unsigned int tmp;
	int rc = 0;

	/* ps tuning data*/
	rc = of_property_read_u32(np, "ps-threshold", &tmp);
	if(rc)
	{
		dev_err(dev, "Unable to read ps threshold\n");
		return rc;
	}
	pdata->prox_threshold = tmp;

	rc = of_property_read_u32(np, "ps-hysteresis-threshold", &tmp);
	if(rc)
	{
		dev_err(dev, "Unable to read ps hysteresis threshold\n");
		return rc;
	}
	pdata->prox_hsyteresis_threshold = tmp;

	rc = of_property_read_u32(np, "als-polling-time", &tmp);
	if(rc)
	{
		dev_err(dev, "Unable to read ps hysteresis threshold\n");
		return rc;
	}
	pdata->als_poll_interval = tmp;

	return 0;
}

int ltr55x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct ltr55x_data *data;
	struct ltr55x_platform_data *pdata;
	int ret = 0;

	/* check i2c*/
	if(!i2c_check_functionality(adapter,I2C_FUNC_SMBUS_WRITE_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
	{
		dev_err(&client->dev, "+++++++++++++++++++++++++++++++++++++++LTR-55xALS functionality check failed.\n");
		return -EIO;
	}

	/* platform data memory allocation*/
	if(client->dev.of_node)
	{
		pdata = devm_kzalloc(&client->dev,sizeof(struct ltr55x_platform_data),GFP_KERNEL);
		if(!pdata)
		{
			dev_err(&client->dev, "+++++++++++++++++++++++++++++++++++++++LTR-55xALSFailed to allocate memory\n");
			return -ENOMEM;
		}

		client->dev.platform_data = pdata;
	}
	else
	{
		pdata = client->dev.platform_data;
		if(!pdata)
		{
			dev_err(&client->dev, "+++++++++++++++++++++++++++++++++++++++No platform data\n");
			return -ENODEV;
		}
	}

	/* data memory allocation */
	data = kzalloc(sizeof(struct ltr55x_data), GFP_KERNEL);
	if(!data)
	{
		dev_err(&client->dev, "+++++++++++++++++++++++++++++++++++++++kzalloc failed\n");
		ret = -ENOMEM;
		goto exit_kfree_pdata;
	}
	data->client = client;
	data->platform_data = pdata;
	ret = ltr55x_parse_dt(&client->dev, data);
	if(ret)
	{
		dev_err(&client->dev, "+++++++++++++++++++++++++++++++++++++++can't parse platform data\n");
		ret = -EFAULT;
		goto exit_kfree_data;
	}

	#if 0
	/* pinctrl initialization */
	ret = ltr55x_pinctrl_init(data);
	if(ret)
	{
		ret = -EFAULT;
		dev_err(&client->dev, "+++++++++++++++++++++++++++++++++++++++Can't initialize pinctrl\n");
		goto exit_kfree_data;
	}
	#endif

	/* power initialization */
	ret = sensor_platform_hw_power_onoff(data, true);
	if(ret)
	{
		ret = -ENOEXEC;
		dev_err(&client->dev,"+++++++++++++++++++++++++++++++++++++++power on fail\n");
		goto exit_kfree_data;
	}
	/* set client data as ltr55x_data*/
	i2c_set_clientdata(client, data);


	ret = ltr55x_check_chip_id(client);
	if(ret)
	{
		ret = -ENXIO;
		dev_err(&client->dev,"+++++++++++++++++++++++++++++++++++++++the manufacture id is not match\n");
		goto exit_power_off;
	}


	ret = ltr55x_device_init(client);
	if(ret)
	{
		ret = -ENXIO;
		dev_err(&client->dev,"+++++++++++++++++++++++++++++++++++++++device init failed\n");
		goto exit_power_off;
	}

	/* request gpio and irq */
	ret = ltr55x_gpio_irq(data);
	if(ret)
	{
		ret = -ENXIO;
		dev_err(&client->dev,"+++++++++++++++++++++++++++++++++++++++gpio_irq failed\n");
		goto exit_chip_reset;
	}
	/* Register Input Device */
	data->input_dev_als = input_allocate_device();
	if(!data->input_dev_als)
	{
		ret = -ENOMEM;
		dev_err(&client->dev,"+++++++++++++++++++++++++++++++++++++++Failed to allocate input device als\n");
		goto exit_free_irq;
	}
	data->input_dev_ps = input_allocate_device();
	if(!data->input_dev_ps)
	{
		ret = -ENOMEM;
		dev_err(&client->dev,"+++++++++++++++++++++++++++++++++++++++Failed to allocate input device ps\n");
		goto exit_free_dev_als;
	}
	set_bit(EV_ABS, data->input_dev_als->evbit);
	set_bit(EV_ABS, data->input_dev_ps->evbit);
	input_set_abs_params(data->input_dev_als, ABS_MISC, 0, 65535, 0, 0);
	input_set_abs_params(data->input_dev_ps, ABS_DISTANCE, 0, 1, 0, 0);
	data->input_dev_als->name = LIGHT_SENSOR_NAME ;
	data->input_dev_ps->name = PROXIMITY_SENSOR_NAME;
	data->input_dev_als->id.bustype = BUS_I2C;
	data->input_dev_als->dev.parent =&data->client->dev;
	data->input_dev_ps->id.bustype = BUS_I2C;
	data->input_dev_ps->dev.parent =&data->client->dev;
	input_set_drvdata(data->input_dev_als, data);
	input_set_drvdata(data->input_dev_ps, data);

	ret = input_register_device(data->input_dev_als);
	if(ret)
	{
		ret = -ENOMEM;
		dev_err(&client->dev,"Unable to register input device als: %s\n",data->input_dev_als->name);
		goto exit_free_dev_ps;
	}

	ret = input_register_device(data->input_dev_ps);
	if(ret)
	{
		ret = -ENOMEM;
		dev_err(&client->dev,"Unable to register input device ps: %s\n",data->input_dev_ps->name);
		goto exit_unregister_dev_als;
	}
	printk("%s input device success.\n",__func__);

	/* init delayed works */
	INIT_DELAYED_WORK(&data->ps_work, ltr55x_ps_work_func);
	INIT_DELAYED_WORK(&data->als_work, ltr55x_als_work_func);

	/* init mutex */
	mutex_init(&data->lockw);
	mutex_init(&data->op_lock);

	/* create sysfs group */
	ret = sysfs_create_group(&client->dev.kobj, &ltr55x_attr_group);
	if(ret)
	{
		ret = -EROFS;
		dev_err(&client->dev,"+++++++++++++++++++Unable to creat sysfs group\n");
		goto exit_unregister_dev_ps;
	}

    /* Register sysfs hooks in input devices */
	ret = sysfs_create_group(&data->input_dev_als->dev.kobj, &ltr55x_als_attr_group);
	if (ret)
	{
		dev_err(&client->dev,"+++++++++++++++++++Unable to creat sysfs group for als in input node\n");
		goto exit_unregister_dev_ps;
	}

	ret = sysfs_create_group(&data->input_dev_ps->dev.kobj, &ltr55x_ps_attr_group);
	if (ret)
	{
		dev_err(&client->dev,"+++++++++++++++++++Unable to creat sysfs group for ps in input node\n");
		goto exit_unregister_dev_ps;
	}

	data->dynamic_noise = 0;

	dev_dbg(&client->dev,"probe succece\n");
	return 0;
	#if 0
	exit_remove_sysfs_group:
	sysfs_remove_group(&client->dev.kobj, &ltr55x_attr_group);
	sysfs_remove_group(&data->input_dev_ps->dev.kobj, &ltr55x_ps_attr_group);
	sysfs_remove_group(&data->input_dev_als->dev.kobj, &ltr55x_als_attr_group);
	#endif
	exit_unregister_dev_ps:
	input_unregister_device(data->input_dev_ps);
	exit_unregister_dev_als:
	input_unregister_device(data->input_dev_als);
	exit_free_dev_ps:
	if(data->input_dev_ps)
		input_free_device(data->input_dev_ps);
	exit_free_dev_als:
	if(data->input_dev_als)
		input_free_device(data->input_dev_als);
	exit_free_irq:
	ltr55x_gpio_irq_free(data);
	exit_chip_reset:
	ltr55x_chip_reset(client);
	exit_power_off:
	sensor_platform_hw_power_onoff(data, false);
	exit_kfree_data:
	kfree(data);
	exit_kfree_pdata:
	if(pdata && (client->dev.of_node))
		devm_kfree(&client->dev, pdata);
	data->platform_data= NULL;

	return ret;
}

static int ltr55x_remove(struct i2c_client *client)
{
	struct ltr55x_data *data = i2c_get_clientdata(client);
	struct ltr55x_platform_data *pdata=data->platform_data;

	if(data == NULL || pdata == NULL)
		return 0;

	ltr55x_ps_enable(client, 0);
	ltr55x_als_enable(client, 0);
	input_unregister_device(data->input_dev_als);
	input_unregister_device(data->input_dev_ps);

	input_free_device(data->input_dev_als);
	input_free_device(data->input_dev_ps);

	ltr55x_gpio_irq_free(data);

	sysfs_remove_group(&client->dev.kobj, &ltr55x_attr_group);

	cancel_delayed_work_sync(&data->ps_work);
	cancel_delayed_work_sync(&data->als_work);

	if(pdata && (client->dev.of_node))
		devm_kfree(&client->dev, pdata);
	pdata = NULL;

	kfree(data);
	data = NULL;

	return 0;
}

static struct i2c_device_id ltr55x_id[] = {
	{"ltr55x", 0},
	{}
};

static struct of_device_id ltr_match_table[] = {
	{ .compatible = "LITEON,ltr_55x",},
	{},
};

MODULE_DEVICE_TABLE(i2c, ltr55x_id);
static SIMPLE_DEV_PM_OPS(ltr55x_pm_ops, ltr55x_suspend, ltr55x_resume);
static struct i2c_driver ltr55x_driver = {
	.driver = {
		.name = LTR55x_DRV_NAME,
		.owner = THIS_MODULE,
		.pm = &ltr55x_pm_ops,
		.of_match_table = ltr_match_table,
	},
	.probe = ltr55x_probe,
	.remove = ltr55x_remove,
	.id_table = ltr55x_id,
};

static int ltr55x_driver_init(void)
{
	pr_info("Driver ltr55x0 init.\n");
	return i2c_add_driver(&ltr55x_driver);
};

static void ltr55x_driver_exit(void)
{
	pr_info("Unload ltr55x module...\n");
	i2c_del_driver(&ltr55x_driver);
}

module_init(ltr55x_driver_init);
module_exit(ltr55x_driver_exit);
MODULE_AUTHOR("Lite-On Technology Corp.");
MODULE_DESCRIPTION("Lite-On LTR-55x Proximity and Light Sensor Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");

