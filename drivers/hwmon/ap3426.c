/*
 *  ap3426.c - Linux kernel modules for DynaImage ambient light + proximity
 *sensor ap3426
 *
 *  Copyright (C) 2015 Jian Zhou
 *  Copyright (C) 2015 Marvell Technologies
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
  */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/regmap.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>

struct ap3426_platform_data {
  unsigned int irq;
  char avdd_name[20];
};
//#undef pr_debug
//#define pr_debug pr_info

#define AP3426_I2C_NAME "ap3426"
#define AP3426_LIGHT_INPUT_NAME "ap3426-light"
#define AP3426_PROXIMITY_INPUT_NAME "ap3426-proximity"

/* AP3426 registers */
#define AP3426_REG_CONFIG 0x00
#define AP3426_REG_INT_FLAG 0x01
#define AP3426_REG_INT_CTL 0x02
#define AP3426_REG_WAIT_TIME 0x06
#define AP3426_REG_IR_DATA_LOW 0x0A
#define AP3426_REG_IR_DATA_HIGH 0x0B
#define AP3426_REG_ALS_DATA_LOW 0x0C
#define AP3426_REG_ALS_DATA_HIGH 0x0D
#define AP3426_REG_PS_DATA_LOW 0x0E
#define AP3426_REG_PS_DATA_HIGH 0x0F
#define AP3426_REG_ALS_GAIN 0x10
#define AP3426_REG_ALS_PERSIST 0x14
#define AP3426_REG_ALS_LOW_THRES_0 0x1A
#define AP3426_REG_ALS_LOW_THRES_1 0x1B
#define AP3426_REG_ALS_HIGH_THRES_0 0x1C
#define AP3426_REG_ALS_HIGH_THRES_1 0x1D
#define AP3426_REG_PS_GAIN 0x20
#define AP3426_REG_PS_LED_DRIVER 0x21
#define AP3426_REG_PS_INT_FORM 0x22
#define AP3426_REG_PS_MEAN_TIME 0x23
#define AP3426_REG_PS_SMART_INT 0x24
#define AP3426_REG_PS_INT_TIME 0x25
#define AP3426_REG_PS_PERSIST 0x26
#define AP3426_REG_PS_CAL_L 0x28
#define AP3426_REG_PS_CAL_H 0x29
#define AP3426_REG_PS_LOW_THRES_0 0x2A
#define AP3426_REG_PS_LOW_THRES_1 0x2B
#define AP3426_REG_PS_HIGH_THRES_0 0x2C
#define AP3426_REG_PS_HIGH_THRES_1 0x2D

#define AP3426_ALS_SENSITIVITY 0x10
#define AP3426_PS_SENSITIVITY 0x20

static struct regmap_config ap3426_regmap_config = {
    .reg_bits = 8, .val_bits = 8,
};

static int gain_table[] = {32768, 8192, 2048, 512};

/* PS distance table */
static int ps_distance_table[] = {1023, 740, 340, 200, 180, 176};

#define AP3426_DRV_NAME "dyna,ap3426"
#define DRIVER_VERSION "1.0.0"

#define ABS_LIGHT 0x29 /* added to support LIGHT - light sensor */

#define AP3426_PS_DETECTION_THRESHOLD 150
#define AP3426_PS_HSYTERESIS_THRESHOLD 130

#define AP3426_ALS_THRESHOLD_HSYTERESIS 20 /* 20 = 20% */

#define DEVICE_ATTR2(_name, _mode, _show, _store) \
  struct device_attribute dev_attr2_##_name =     \
      __ATTR(_name, _mode, _show, _store)

#define AP_IOCTL_PS_ENABLE 1
#define AP_IOCTL_PS_GET_ENABLE 2
#define AP_IOCTL_PS_POLL_DELAY 3
#define AP_IOCTL_ALS_ENABLE 4
#define AP_IOCTL_ALS_GET_ENABLE 5
#define AP_IOCTL_ALS_POLL_DELAY 6
#define AP_IOCTL_PS_GET_PDATA 7    /* pdata */
#define AP_IOCTL_ALS_GET_CH0DATA 8 /* ch0data */
#define AP_IOCTL_ALS_GET_CH1DATA 9 /* ch1data */

#define AP_DISABLE_PS 0
#define AP_ENABLE_PS 1

#define AP_DISABLE_ALS 0
#define AP_ENABLE_ALS_WITH_INT 1
#define AP_ENABLE_ALS_NO_INT 2

#define AP_ALS_POLL_SLOW 0   /* 1 Hz (1s) */
#define AP_ALS_POLL_MEDIUM 1 /* 10 Hz (100ms) */
#define AP_ALS_POLL_FAST 2   /* 20 Hz (50ms) */

enum {
  AP3426_ALS_RES_27MS = 0,  /* 27.2ms integration time */
  AP3426_ALS_RES_51MS = 1,  /* 51.68ms integration time */
  AP3426_ALS_RES_100MS = 2, /* 100.64ms integration time */
} ap3426_als_res_e;

/*
 * Structs
 */

struct ap3426_data {
  struct i2c_client *client;
  struct regmap *regmap;
  struct mutex update_lock;
  struct delayed_work dwork;     /* for PS interrupt */
  struct delayed_work als_dwork; /* for ALS polling */
  struct input_dev *input_dev_als;
  struct input_dev *input_dev_ps;
  struct regulator *avdd;

  int irq;
  int suspended;
  unsigned int enable_suspended_value; /* suspend_resume usage */

  unsigned int enable;
  unsigned int atime;
  unsigned int ptime;
  unsigned int wtime;
  unsigned int ailt;
  unsigned int aiht;
  unsigned int pilt;
  unsigned int piht;
  unsigned int pers;
  unsigned int config;
  unsigned int ppcount;
  unsigned int control;

  int als_cal;
  int ps_cal;
  int als_gain;
  int als_persist;
  int ps_gain;
  int ps_persist;
  int ps_led_driver;
  int ps_mean_time;
  int ps_integrated_time;
  int wait_time;

  /* control flag from HAL */
  unsigned int enable_ps_sensor;
  unsigned int enable_als_sensor;

  /* PS parameters */
  unsigned int ps_threshold;
  unsigned int ps_hysteresis_threshold; /* always lower than ps_threshold */
  unsigned int ps_detection;            /* 0 = near-to-far; 1 = far-to-near */
  unsigned int ps_data;                 /* to store PS data */

  /* ALS parameters */
  unsigned int als_threshold_l; /* low threshold */
  unsigned int als_threshold_h; /* high threshold */
  unsigned int als_data;        /* to store ALS data */
  int als_prev_lux;             /* to store previous lux value */

  unsigned int
      als_poll_delay; /* needed for light sensor polling : micro-second (us) */

  struct ap3426_platform_data *pdata; /* platform data */
};

/*
 * Global data
 */
static struct i2c_client *
    ap3426_i2c_client; /* global i2c_client to support ioctl */
static struct workqueue_struct *ap3426_workqueue;

static void ap3426_change_ps_threshold(struct i2c_client *client) {
  struct ap3426_data *data = i2c_get_clientdata(client);

  // Todo: get ps_data from ap3426;  data->ps_data =	0;

  if ((data->ps_data > data->pilt) && (data->ps_data >= data->piht)) {
    /* far-to-near detected */
    data->ps_detection = 1;

    data->ps_data = 2;
    /* FAR-to-NEAR detection */
    input_report_abs(data->input_dev_ps, ABS_DISTANCE, data->ps_data);
    input_sync(data->input_dev_ps);

    // Todo: write threshold to ap3426

    data->pilt = data->ps_hysteresis_threshold;
    data->piht = 1023;

    pr_debug("far-to-near detected\n");
  } else if ((data->ps_data <= data->pilt) && (data->ps_data < data->piht)) {
    /* near-to-far detected */
    data->ps_detection = 0;
    /* NEAR-to-FAR detection */
    input_report_abs(data->input_dev_ps, ABS_DISTANCE, 10);
    input_sync(data->input_dev_ps);

    // Todo: write threshold to ap3426

    data->pilt = 0;
    data->piht = data->ps_threshold;

    pr_debug("near-to-far detected\n");
  }
  pr_debug("high threshhold change to %d, the low threshhold change to %d",
           data->pilt, data->piht);
}

static void ap3426_reschedule_work(struct ap3426_data *data,
                                   unsigned long delay) {
  /*
   * If work is already scheduled then subsequent schedules will not
   * change the scheduled time that's why we have to cancel it first.
   */
  cancel_delayed_work(&data->dwork);
  queue_delayed_work(ap3426_workqueue, &data->dwork, delay);
}

/* ALS polling routine */
static void ap3426_als_polling_work_handler(struct work_struct *work) {
  struct ap3426_data *data =
      container_of(work, struct ap3426_data, als_dwork.work);
  struct i2c_client *client = data->client;
  u8 als_data[4];
  u8 ps_data[4];
  int luxValue = 0;
  int rc;
  unsigned int gain;

  /* Read data and clear interrupt */
  rc = regmap_bulk_read(data->regmap, AP3426_REG_ALS_DATA_LOW, als_data, 2);
  if (rc) {
    dev_err(&client->dev, "read %d failed.(%d)\n", AP3426_REG_ALS_DATA_LOW, rc);
    goto exit;
  }

  rc = regmap_bulk_read(data->regmap, AP3426_REG_PS_DATA_LOW, ps_data, 2);
  if (rc) {
    dev_err(&client->dev, "read %d failed.(%d)\n", AP3426_REG_PS_DATA_LOW, rc);
    goto exit;
  }

  /* report value */
  gain = gain_table[data->als_gain & 0x3];
  luxValue =
      (((als_data[0] | (als_data[1] << 8)) * gain) >> 16) * 100 / data->als_cal;
  pr_debug("lux:%d als_data:0x%x-0x%x\n", luxValue, als_data[0], als_data[1]);

  luxValue = luxValue > 0 ? luxValue : 0;
  luxValue = luxValue < 10000 ? luxValue : 10000;

  data->als_data = luxValue;

  input_report_abs(data->input_dev_als, ABS_PRESSURE,
                   luxValue); /*report the lux level */
  input_sync(data->input_dev_als);
exit:
  /* restart timer */
  schedule_delayed_work(&data->als_dwork,
                        msecs_to_jiffies(data->als_poll_delay));
}

/* PS interrupt routine */
static void ap3426_work_handler(struct work_struct *work) {
  struct ap3426_data *data = container_of(work, struct ap3426_data, dwork.work);
  struct i2c_client *client = data->client;
  int status;

  if ((status & 0x02) == 0x02) {
    /* PS is interrupted */
    ap3426_change_ps_threshold(client);
  }
}

/* assume this is ISR */
static irqreturn_t ap3426_interrupt(int vec, void *info) {
  struct i2c_client *client = (struct i2c_client *)info;
  struct ap3426_data *data = i2c_get_clientdata(client);

  ap3426_reschedule_work(data, 0);

  return IRQ_HANDLED;
}

/*
 * IOCTL support
 */

static int ap3426_enable_als_sensor(struct i2c_client *client, int val) {
  struct ap3426_data *data = i2c_get_clientdata(client);
  unsigned int config;
  int rc;

  pr_debug("%s: enable als sensor ( %d)\n", __func__, val);

  if ((val != AP_DISABLE_ALS) && (val != AP_ENABLE_ALS_WITH_INT) &&
      (val != AP_ENABLE_ALS_NO_INT)) {
    pr_debug("%s: enable als sensor=%d\n", __func__, val);
    return -1;
  }

  /* Read the system config register */
  rc = regmap_read(data->regmap, AP3426_REG_CONFIG, &config);
  if (rc) {
    dev_err(&client->dev, "read %d failed.(%d)\n", AP3426_REG_CONFIG, rc);
    goto out;
  }

  if ((val == AP_ENABLE_ALS_WITH_INT) || (val == AP_ENABLE_ALS_NO_INT)) {
    u8 als_data[4];
    int rc = 0;

    if (regulator_enable(data->avdd)) {
      dev_err(&client->dev, "dyna sensor avdd power supply enable failed\n");
      goto out;
    }

    /* turn on light  sensor */
    data->enable_als_sensor = val;

    /* lower threshold */
    als_data[0] = 0x0;
    als_data[1] = 0x0;
    /* upper threshold */
    als_data[2] = 0xff;
    als_data[3] = 0xff;
    rc = regmap_bulk_write(data->regmap, AP3426_REG_ALS_LOW_THRES_0, als_data,
                           4);
    if (rc) {
      dev_err(&client->dev, "write %d failed.(%d)\n",
              AP3426_REG_ALS_LOW_THRES_0, rc);
      goto out;
    }

    /* enable als_sensor */
    rc = regmap_write(data->regmap, AP3426_REG_CONFIG, config | 0x01);
    if (rc) {
      dev_err(&client->dev, "write %d failed.(%d)\n", AP3426_REG_CONFIG, rc);
      goto out;
    }

    /*
     * If work is already scheduled then subsequent schedules will not
     * change the scheduled time that's why we have to cancel it first.
     */
    cancel_delayed_work(&data->als_dwork);
    flush_delayed_work(&data->als_dwork);
    queue_delayed_work(ap3426_workqueue, &data->als_dwork,
                       msecs_to_jiffies(data->als_poll_delay));
  } else {
    /* turn off light sensor
     * what if the p sensor is active?
     */
    data->enable_als_sensor = AP_DISABLE_ALS;

    /* disable als_sensor */
    regmap_write(data->regmap, AP3426_REG_CONFIG, (config & (~0x01)));
    /*
     * If work is already scheduled then subsequent schedules will not
     * change the scheduled time that's why we have to cancel it first.
     */
    cancel_delayed_work(&data->als_dwork);
    flush_delayed_work(&data->als_dwork);
    regulator_disable(data->avdd);
  }
out:
  return 0;
}

static int ap3426_set_als_poll_delay(struct i2c_client *client,
                                     unsigned int val) {
  struct ap3426_data *data = i2c_get_clientdata(client);
  int atime_index = 0;

  pr_debug("%s : %d\n", __func__, val);

  if ((val != AP_ALS_POLL_SLOW) && (val != AP_ALS_POLL_MEDIUM) &&
      (val != AP_ALS_POLL_FAST)) {
    pr_debug("%s:invalid value=%d\n", __func__, val);
    return -1;
  }

  if (val == AP_ALS_POLL_FAST) {
    data->als_poll_delay = 50; /* 50ms */
    atime_index = AP3426_ALS_RES_27MS;
  } else if (val == AP_ALS_POLL_MEDIUM) {
    data->als_poll_delay = 100; /* 100ms */
    atime_index = AP3426_ALS_RES_51MS;
  } else {                       /* AP_ALS_POLL_SLOW */
    data->als_poll_delay = 1000; /* 1000ms */
    atime_index = AP3426_ALS_RES_100MS;
  }
  // Todo: write atime to ap3426
  /*
   * If work is already scheduled then subsequent schedules will not
   * change the scheduled time that's why we have to cancel it first.
   */
  cancel_delayed_work(&data->als_dwork);
  flush_delayed_work(&data->als_dwork);
  queue_delayed_work(ap3426_workqueue, &data->als_dwork,
                     msecs_to_jiffies(data->als_poll_delay));

  return 0;
}

static int ap3426_enable_ps_sensor(struct i2c_client *client, int val) {
  struct ap3426_data *data = i2c_get_clientdata(client);
  int rc;
  unsigned int config;

  pr_debug("enable ps senosr ( %d)\n", val);

  if ((val != AP_DISABLE_PS) && (val != AP_ENABLE_PS)) {
    pr_debug("%s:invalid value=%d\n", __func__, val);
    return -1;
  }
  rc = regmap_read(data->regmap, AP3426_REG_CONFIG, &config);
  if (rc) {
    dev_err(&client->dev, "read %d failed.(%d)\n", AP3426_REG_CONFIG, rc);
    goto out;
  }
  if (val == AP_ENABLE_PS) {
    u8 buffer[6];
    unsigned int tmp;

    if (regulator_enable(data->avdd)) {
      dev_err(&client->dev, "dyna sensor avdd power supply enable failed\n");
      goto out;
    }

    /* turn on p sensor */
    if (data->enable_ps_sensor == AP_DISABLE_PS) {
      data->enable_ps_sensor = AP_ENABLE_PS;
      regmap_write(data->regmap, AP3426_REG_CONFIG, config | 0x02);

      regmap_bulk_read(data->regmap, AP3426_REG_ALS_DATA_LOW, buffer, 4);

      tmp = buffer[2] | (buffer[3] << 8);

      pr_debug("ps senosr data( 0x%x)\n", tmp);
    }
  } else {
    regmap_write(data->regmap, AP3426_REG_CONFIG,
                 (config & (~0x02))); /* Power Off */

    pr_debug("disable ps senosr ( 0x%x)\n", (config & (~0x02)));

    data->enable_ps_sensor = AP_DISABLE_PS;

    regulator_disable(data->avdd);
  }
out:
  return 0;
}

static int ap3426_ps_open(struct inode *inode, struct file *file) {
  pr_debug("ap3426_ps_open\n");
  return 0;
}

static int ap3426_ps_release(struct inode *inode, struct file *file) {
  pr_debug("ap3426_ps_release\n");
  return 0;
}

static long ap3426_ps_ioctl(struct file *file, unsigned int cmd,
                            unsigned long arg) {
  struct ap3426_data *data;
  struct i2c_client *client;
  int enable;
  u8 ps_data[4];
  int ret = -1;

  if (arg == 0) return -1;
  if (ap3426_i2c_client == NULL) {
    pr_debug("ap3426_ps_ioctl error: i2c driver not installed\n");
    return -EFAULT;
  }

  client = ap3426_i2c_client;
  data = i2c_get_clientdata(ap3426_i2c_client);

  switch (cmd) {
    case AP_IOCTL_PS_ENABLE:
      if (copy_from_user(&enable, (void __user *)arg, sizeof(enable))) {
        pr_debug("ap3426_ps_ioctl: copy_from_user failed\n");
        return -EFAULT;
      }
      ret = ap3426_enable_ps_sensor(client, enable);
      if (ret < 0) return ret;
      break;
    case AP_IOCTL_PS_GET_ENABLE:
      if (copy_to_user((void __user *)arg, &data->enable_ps_sensor,
                       sizeof(data->enable_ps_sensor))) {
        pr_debug("ap3426_ps_ioctl: copy_to_user failed\n");
        return -EFAULT;
      }
      break;
    case AP_IOCTL_PS_GET_PDATA:
      regmap_bulk_read(data->regmap, AP3426_REG_PS_DATA_LOW, ps_data, 2);
      data->ps_data = ps_data[0] | (ps_data[1] << 8);
      if (copy_to_user((void __user *)arg, &data->ps_data,
                       sizeof(data->ps_data))) {
        pr_debug("ap3426_ps_ioctl: copy_to_user failed\n");
        return -EFAULT;
      }
      break;
    default:
      break;
  }

  return 0;
}

static int ap3426_als_open(struct inode *inode, struct file *file) {
  pr_debug("ap3426_als_open\n");
  return 0;
}

static int ap3426_als_release(struct inode *inode, struct file *file) {
  pr_debug("ap3426_als_release\n");
  return 0;
}

static long ap3426_als_ioctl(struct file *file, unsigned int cmd,
                             unsigned long arg) {
  struct ap3426_data *data;
  struct i2c_client *client;
  int enable;
  int ret = -1;
  unsigned int delay;

  if (arg == 0) return -1;

  if (ap3426_i2c_client == NULL) {
    pr_debug("ap3426_als_ioctl error: i2c driver not installed\n");
    return -EFAULT;
  }

  client = ap3426_i2c_client;
  data = i2c_get_clientdata(ap3426_i2c_client);

  switch (cmd) {
    case AP_IOCTL_ALS_ENABLE:
      if (copy_from_user(&enable, (void __user *)arg, sizeof(enable))) {
        pr_debug("ap3426_als_ioctl: copy_from_user failed\n");
        return -EFAULT;
      }
      ret = ap3426_enable_als_sensor(client, enable);
      if (ret < 0) return ret;
      break;
    case AP_IOCTL_ALS_POLL_DELAY:
      if (data->enable_als_sensor == AP_ENABLE_ALS_NO_INT) {
        if (copy_from_user(&delay, (void __user *)arg, sizeof(delay))) {
          pr_debug("ap3426_als_ioctl: copy_to_user failed\n");
          return -EFAULT;
        }
        ret = ap3426_set_als_poll_delay(client, delay);

        if (ret < 0) return ret;
      } else {
        pr_debug("ap3426_als_ioctl: als is not in polling mode!\n");
        return -EFAULT;
      }
      break;
    case AP_IOCTL_ALS_GET_ENABLE:
      if (copy_to_user((void __user *)arg, &data->enable_als_sensor,
                       sizeof(data->enable_als_sensor))) {
        pr_debug("ap3426_als_ioctl: copy_to_user failed\n");
        return -EFAULT;
      }
      break;
    case AP_IOCTL_ALS_GET_CH0DATA:
      regmap_read(data->regmap, AP3426_REG_ALS_DATA_LOW, &data->als_data);
      if (copy_to_user((void __user *)arg, &data->als_data,
                       sizeof(data->als_data))) {
        pr_debug("ap3426_ps_ioctl: copy_to_user failed\n");
        return -EFAULT;
      }
      break;
    case AP_IOCTL_ALS_GET_CH1DATA:
      regmap_read(data->regmap, AP3426_REG_ALS_DATA_HIGH, &data->als_data);
      if (copy_to_user((void __user *)arg, &data->als_data,
                       sizeof(data->als_data))) {
        pr_debug("ap3426_ps_ioctl: copy_to_user failed\n");
        return -EFAULT;
      }
      break;
    default:
      break;
  }

  return 0;
}

/*
 * SysFS support
 */

static ssize_t ap3426_show_ch0data(struct device *dev,
                                   struct device_attribute *attr, char *buf) {
  struct input_dev *input = to_input_dev(dev);
  struct ap3426_data *data = input_get_drvdata(input);

  int ch0data;
  regmap_read(data->regmap, AP3426_REG_ALS_DATA_LOW, &ch0data);

  return sprintf(buf, "%d\n", ch0data);
}

static DEVICE_ATTR(ch0data, S_IRUGO, ap3426_show_ch0data, NULL);

static ssize_t ap3426_show_ch1data(struct device *dev,
                                   struct device_attribute *attr, char *buf) {
  struct input_dev *input = to_input_dev(dev);
  struct ap3426_data *data = input_get_drvdata(input);

  int ch1data;

  regmap_read(data->regmap, AP3426_REG_ALS_DATA_HIGH, &ch1data);

  return sprintf(buf, "%d\n", ch1data);
}

static DEVICE_ATTR(ch1data, S_IRUGO, ap3426_show_ch1data, NULL);

static ssize_t ap3426_show_pdata(struct device *dev,
                                 struct device_attribute *attr, char *buf) {
  struct input_dev *input = to_input_dev(dev);
  struct ap3426_data *data = input_get_drvdata(input);

  int pdata;
  u8 ps_data[4];

  regmap_bulk_read(data->regmap, AP3426_REG_PS_DATA_LOW, ps_data, 2);
  pdata = ps_data[0] | (ps_data[1] << 8);

  return sprintf(buf, "%d\n", pdata);
}

static DEVICE_ATTR(pdata, S_IRUGO, ap3426_show_pdata, NULL);

static ssize_t ap3426_show_proximity_enable(struct device *dev,
                                            struct device_attribute *attr,
                                            char *buf) {
  struct input_dev *input = to_input_dev(dev);
  struct ap3426_data *data = input_get_drvdata(input);

  return sprintf(buf, "%d\n", data->enable_ps_sensor);
}

static ssize_t ap3426_store_proximity_enable(struct device *dev,
                                             struct device_attribute *attr,
                                             const char *buf, size_t count) {
  struct input_dev *input = to_input_dev(dev);
  struct ap3426_data *data = input_get_drvdata(input);
  struct i2c_client *client = data->client;

  unsigned long val;
  int success = kstrtoul(buf, 10, &val);

  if (success == 0) {
    pr_debug("%s: enable ps senosr ( %ld)\n", __func__, val);
    if ((val != AP_DISABLE_PS) && (val != AP_ENABLE_PS)) {
      pr_debug("**%s:store invalid value=%ld\n", __func__, val);
      return count;
    }
    ap3426_enable_ps_sensor(client, val);
  }

  return count;
}

static DEVICE_ATTR(active, S_IRUGO | S_IWUSR | S_IWGRP,
                   ap3426_show_proximity_enable, ap3426_store_proximity_enable);

static ssize_t ap3426_show_light_enable(struct device *dev,
                                        struct device_attribute *attr,
                                        char *buf) {
  struct input_dev *input = to_input_dev(dev);
  struct ap3426_data *data = input_get_drvdata(input);

  return sprintf(buf, "%d\n", data->enable_als_sensor);
}

static ssize_t ap3426_store_light_enable(struct device *dev,
                                         struct device_attribute *attr,
                                         const char *buf, size_t count) {
  struct input_dev *input = to_input_dev(dev);
  struct ap3426_data *data = input_get_drvdata(input);
  struct i2c_client *client = data->client;

  unsigned long val;
  int success = kstrtoul(buf, 10, &val);

  if (success == 0) {
    pr_debug("%s: enable als sensor ( %ld)\n", __func__, val);
    if ((val != AP_DISABLE_ALS) && (val != AP_ENABLE_ALS_WITH_INT) &&
        (val != AP_ENABLE_ALS_NO_INT)) {
      pr_debug("**%s: store invalid valeu=%ld\n", __func__, val);
      return count;
    }
    ap3426_enable_als_sensor(client, val);
  }

  return count;
}

static DEVICE_ATTR2(active, S_IRUGO | S_IWUSR | S_IWGRP,
                    ap3426_show_light_enable, ap3426_store_light_enable);

static struct attribute *ap3426_als_attributes[] = {
    &dev_attr_ch0data.attr, &dev_attr_ch1data.attr, &dev_attr2_active.attr,
    NULL};

static const struct attribute_group ap3426_als_attr_group = {
    .attrs = ap3426_als_attributes,
};

static struct attribute *ap3426_ps_attributes[] = {&dev_attr_pdata.attr,
                                                   &dev_attr_active.attr, NULL};

static const struct attribute_group ap3426_ps_attr_group = {
    .attrs = ap3426_ps_attributes,
};

static const struct file_operations ap3426_ps_fops = {
    .owner = THIS_MODULE,
    .open = ap3426_ps_open,
    .release = ap3426_ps_release,
    .unlocked_ioctl = ap3426_ps_ioctl,
};

static struct miscdevice ap3426_ps_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ap3426_ps_dev",
    .fops = &ap3426_ps_fops,
};

static const struct file_operations ap3426_als_fops = {
    .owner = THIS_MODULE,
    .open = ap3426_als_open,
    .release = ap3426_als_release,
    .unlocked_ioctl = ap3426_als_ioctl,
};

static struct miscdevice ap3426_als_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "ap3426_als_dev",
    .fops = &ap3426_als_fops,
};

/*
 * Initialization function
 */

static int ap3426_init_client(struct i2c_client *client) {
  struct ap3426_data *di = i2c_get_clientdata(client);
  int rc;

  /* Enable ps interrupt and auto clear interrupt */
  rc = regmap_write(di->regmap, AP3426_REG_INT_CTL, 0x80);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_INT_CTL);
    return rc;
  }

  /* Set als gain */
  rc = regmap_write(di->regmap, AP3426_REG_ALS_GAIN, di->als_gain << 4);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_ALS_GAIN);
    return rc;
  }

  /* Set als persistense */
  rc = regmap_write(di->regmap, AP3426_REG_ALS_PERSIST, di->als_persist);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_ALS_PERSIST);
    return rc;
  }

  /* Set ps interrupt form */
  rc = regmap_write(di->regmap, AP3426_REG_PS_INT_FORM, 0);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_PS_INT_FORM);
    return rc;
  }

  /* Set ps gain */
  rc = regmap_write(di->regmap, AP3426_REG_PS_GAIN, di->ps_gain << 2);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_PS_GAIN);
    return rc;
  }

  /* Set ps persist */
  rc = regmap_write(di->regmap, AP3426_REG_PS_PERSIST, di->ps_persist);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_PS_PERSIST);
    return rc;
  }

  /* Set PS LED driver strength */
  rc = regmap_write(di->regmap, AP3426_REG_PS_LED_DRIVER, di->ps_led_driver);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n",
            AP3426_REG_PS_LED_DRIVER);
    return rc;
  }

  /* Set waiting time */
  rc = regmap_write(di->regmap, AP3426_REG_WAIT_TIME, di->wait_time);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_WAIT_TIME);
    return rc;
  }

  /* Set PS mean time */
  rc = regmap_write(di->regmap, AP3426_REG_PS_MEAN_TIME, di->ps_mean_time);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n",
            AP3426_REG_PS_MEAN_TIME);
    return rc;
  }

  /* Set PS integrated time */
  rc = regmap_write(di->regmap, AP3426_REG_PS_INT_TIME, di->ps_integrated_time);
  if (rc) {
    dev_err(&client->dev, "write %d register failed\n", AP3426_REG_PS_INT_TIME);
    return rc;
  }

  dev_dbg(&client->dev, "ap3426 initialize sucessful\n");

  return 0;
}

#ifdef CONFIG_OF
static int ap3426_probe_dt(struct i2c_client *client) {
  struct ap3426_platform_data *platform_data;
  struct device_node *np = client->dev.of_node;

  platform_data = kzalloc(sizeof(*platform_data), GFP_KERNEL);
  if (platform_data == NULL) {
    dev_err(&client->dev, "Alloc GFP_KERNEL memory failed.");
    return -ENOMEM;
  }
  client->dev.platform_data = platform_data;
  platform_data->irq = of_get_named_gpio(np, "irq-gpios", 0);
  if (platform_data->irq < 0) {
    dev_err(&client->dev, "of_get_named_gpio irq faild\n");
    return -EINVAL;
  }

  return 0;
}

static struct of_device_id inv_match_table[] = {{
                                                 .compatible = "dyna,ap3426",
                                                },
                                                {}};
#endif

/*
 * I2C init/probing/exit functions
 */

static struct i2c_driver ap3426_driver;
static int ap3426_probe(struct i2c_client *client,
                        const struct i2c_device_id *id) {
  struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
  struct ap3426_data *data;
  struct ap3426_platform_data *pdata;
  struct regulator *avdd;
  int err = 0;

#ifdef CONFIG_OF
  err = ap3426_probe_dt(client);

  if (err == -ENOMEM) {
    dev_err(&client->dev, "%s: Failed to alloc mem for ap3426_platform_data\n",
            __func__);
    return err;
  } else if (err == -EINVAL) {
    kfree(client->dev.platform_data);
    dev_err(&client->dev, "%s: Probe device tree data failed\n", __func__);
    return err;
  }

  pdata = client->dev.platform_data;
#else
  pdata = client->dev.platform_data;
  if (!pdata) {
    dev_err(&client->dev, "%s: No platform data found\n", __func__);
    return -EINVAL;
  }
#endif

  avdd = regulator_get(&client->dev, "avdd");
  if (IS_ERR(avdd)) {
    dev_err(&client->dev, "sensor avdd power supply get failed\n");
    goto out;
  }

  regulator_set_voltage(avdd, 3100000, 3100000);
  if (regulator_enable(avdd)) {
    dev_err(&client->dev, "dyna sensors regulator enable failed\n");
    goto out;
  }

  /* add delay to make sure ldo enabled */
  usleep_range(2000, 2200);

  if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE)) {
    err = -EIO;
    goto exit;
  }

  if (i2c_smbus_read_byte(client) < 0) {
    dev_err(&client->dev, "i2c_smbus_read_byte error!\n");
    err = -EIO;
    goto exit;
  }

  data = kzalloc(sizeof(struct ap3426_data), GFP_KERNEL);
  if (!data) {
    err = -ENOMEM;
    goto exit;
  }

  data->client = client;
  ap3426_i2c_client = client;

  data->avdd = avdd;

  i2c_set_clientdata(client, data);

  data->regmap = devm_regmap_init_i2c(client, &ap3426_regmap_config);
  if (IS_ERR(data->regmap)) {
    dev_err(&client->dev, "init regmap failed.(%ld)\n", PTR_ERR(data->regmap));
    goto exit_kfree;
  }

  data->enable = 0; /* default mode is standard */
  data->ps_threshold = AP3426_PS_DETECTION_THRESHOLD;
  data->ps_hysteresis_threshold = AP3426_PS_HSYTERESIS_THRESHOLD;
  data->ps_detection = 0;      /* default to no detection */
  data->enable_als_sensor = 0; /* default to 0 */
  data->enable_ps_sensor = 0;  /* default to 0 */
  data->als_poll_delay = 100;  /* default to 100ms */
  data->als_prev_lux = 0;
  data->suspended = 0;
  data->enable_suspended_value = 0; /* suspend_resume usage */

  //######################################################
  data->als_gain = 0;
  data->als_persist = 1;
  data->ps_gain = 1;
  data->ps_persist = 1;
  data->ps_led_driver = 3;
  data->wait_time = 0;
  data->ps_mean_time = 0;
  data->ps_integrated_time = 0;
  data->als_cal = 94;

  //######################################################
  mutex_init(&data->update_lock);
  INIT_DELAYED_WORK(&data->dwork, ap3426_work_handler);
  INIT_DELAYED_WORK(&data->als_dwork, ap3426_als_polling_work_handler);

  if (request_irq((client->irq), ap3426_interrupt,
                  IRQF_TRIGGER_FALLING | IRQF_ONESHOT, AP3426_DRV_NAME,
                  (void *)client)) {
    pr_debug("%s Could not allocate irq resource !\n", __func__);
    goto exit_kfree;
  }

  pr_info("%s interrupt is hooked\n", __func__);

  /* Initialize the AP3426 chip */
  err = ap3426_init_client(client);
  if (err) goto exit_kfree;

  /* Register to Input Device */
  data->input_dev_als = input_allocate_device();
  if (!data->input_dev_als) {
    err = -ENOMEM;
    pr_debug("Failed to allocate input device als\n");
    goto exit_free_irq;
  }

  data->input_dev_ps = input_allocate_device();
  if (!data->input_dev_ps) {
    err = -ENOMEM;
    pr_debug("Failed to allocate input device ps\n");
    goto exit_free_dev_als;
  }

  data->input_dev_als->name = "AP3426_light_sensor";
  data->input_dev_als->id.bustype = BUS_I2C;
  input_set_capability(data->input_dev_als, EV_ABS, ABS_MISC);
  __set_bit(EV_ABS, data->input_dev_als->evbit);
  __set_bit(ABS_PRESSURE, data->input_dev_als->absbit);
  input_set_abs_params(data->input_dev_als, ABS_LIGHT, 0, 30000, 0, 0);
  input_set_drvdata(data->input_dev_als, data);

  data->input_dev_ps->name = "AP3426_proximity_sensor";
  data->input_dev_ps->id.bustype = BUS_I2C;
  input_set_capability(data->input_dev_ps, EV_ABS, ABS_MISC);
  __set_bit(EV_ABS, data->input_dev_ps->evbit);
  __set_bit(ABS_DISTANCE, data->input_dev_ps->absbit);
  input_set_abs_params(data->input_dev_ps, ABS_DISTANCE, 0, 10, 0, 0);
  input_set_drvdata(data->input_dev_ps, data);

  err = input_register_device(data->input_dev_als);
  if (err) {
    err = -ENOMEM;
    pr_debug("Unable to register input device als: %s\n",
             data->input_dev_als->name);
    goto exit_free_dev_ps;
  }

  err = input_register_device(data->input_dev_ps);
  if (err) {
    err = -ENOMEM;
    pr_debug("Unable to register input device ps: %s\n",
             data->input_dev_ps->name);
    goto exit_unregister_dev_als;
  }

  /* Register sysfs hooks */
  err = sysfs_create_group(&data->input_dev_als->dev.kobj,
                           &ap3426_als_attr_group);
  if (err) goto exit_unregister_dev_als;

  err =
      sysfs_create_group(&data->input_dev_ps->dev.kobj, &ap3426_ps_attr_group);
  if (err) goto exit_unregister_dev_ps;

  /* Register for sensor ioctl */
  err = misc_register(&ap3426_ps_device);
  if (err) {
    pr_debug("Unalbe to register ps ioctl: %d", err);
    goto exit_remove_sysfs_group;
  }

  err = misc_register(&ap3426_als_device);
  if (err) {
    pr_debug("Unalbe to register als ioctl: %d", err);
    goto exit_unregister_ps_ioctl;
  }

  pr_debug("%s support ver. %s enabled\n", __func__, DRIVER_VERSION);
  regulator_disable(avdd);

  return 0;

exit_unregister_ps_ioctl:
  misc_deregister(&ap3426_ps_device);
exit_remove_sysfs_group:
  sysfs_remove_group(&data->input_dev_als->dev.kobj, &ap3426_als_attr_group);
  sysfs_remove_group(&data->input_dev_ps->dev.kobj, &ap3426_ps_attr_group);
exit_unregister_dev_ps:
  input_unregister_device(data->input_dev_ps);
exit_unregister_dev_als:
  input_unregister_device(data->input_dev_als);
exit_free_dev_ps:
exit_free_dev_als:
exit_free_irq:
  free_irq((client->irq), client);
exit_kfree:
  kfree(data);
exit:
  regulator_disable(avdd);
out:
  regulator_put(avdd);
  return err;
}

static int ap3426_remove(struct i2c_client *client) {
  struct ap3426_data *data = i2c_get_clientdata(client);

  /* Power down the device */
  // Todo, disable ap3426

  misc_deregister(&ap3426_als_device);
  misc_deregister(&ap3426_ps_device);

  sysfs_remove_group(&data->input_dev_als->dev.kobj, &ap3426_als_attr_group);
  sysfs_remove_group(&data->input_dev_ps->dev.kobj, &ap3426_ps_attr_group);

  input_unregister_device(data->input_dev_ps);
  input_unregister_device(data->input_dev_als);

  free_irq((client->irq), client);

  kfree(data);

  return 0;
}

#ifdef CONFIG_PM

static int ap3426_suspend(struct device *dev) {
  struct i2c_client *client = to_i2c_client(dev);
  struct ap3426_data *data = i2c_get_clientdata(client);

  pr_debug("ap3426_suspend\n");

  /* Do nothing as p-sensor is in active */
  if (!data->enable) return 0;

  data->suspended = 1;
  data->enable_suspended_value = data->enable;

  // Todo disable ap3426

  cancel_delayed_work(&data->als_dwork);
  flush_delayed_work(&data->als_dwork);

  cancel_delayed_work(&data->dwork);
  flush_delayed_work(&data->dwork);

  flush_workqueue(ap3426_workqueue);

  disable_irq(client->irq);

  if (NULL != ap3426_workqueue) {
    destroy_workqueue(ap3426_workqueue);
    pr_debug(KERN_INFO "%s, Destroy workqueue\n", __func__);
    ap3426_workqueue = NULL;
  }

  regulator_disable(data->avdd);
  return 0;
}

static int ap3426_resume(struct device *dev) {
  struct i2c_client *client = to_i2c_client(dev);
  struct ap3426_data *data = i2c_get_clientdata(client);

  /* Do nothing as it was not suspended */
  pr_debug("ap3426_resume (enable=%d)\n", data->enable_suspended_value);

  if (!data->enable_suspended_value) return 0;

  if (ap3426_workqueue == NULL) {
    ap3426_workqueue = create_workqueue("proximity_als");
    if (NULL == ap3426_workqueue) return -ENOMEM;
  }

  if (!data->suspended) return 0; /* if previously not suspended, leave it */
  if (regulator_enable(data->avdd)) {
    dev_err(&client->dev, "dyna sensor avdd power supply enable failed\n");
    goto out;
  }

  enable_irq(client->irq);

  // Todo: resume config to data->enable_suspended_value

  data->suspended = 0;

// Todo: clear pending interrupt
out:
  return 0;
}

#else

#define ap3426_suspend NULL
#define ap3426_resume NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id ap3426_id[] = {{"ap3426", 0}, {}};
MODULE_DEVICE_TABLE(i2c, ap3426_id);

static SIMPLE_DEV_PM_OPS(ap3426_pm_ops, ap3426_suspend, ap3426_resume);
static struct i2c_driver ap3426_driver = {
    .driver =
        {
         .name = AP3426_DRV_NAME,
         .owner = THIS_MODULE,
#ifdef CONFIG_OF
         .of_match_table = of_match_ptr(inv_match_table),
#endif
         .pm = &ap3426_pm_ops,
        },
    .probe = ap3426_probe,
    .remove = ap3426_remove,
    .id_table = ap3426_id,
};

static int __init ap3426_init(void) {
  ap3426_workqueue = create_workqueue("proximity_als");

  if (!ap3426_workqueue) return -ENOMEM;

  return i2c_add_driver(&ap3426_driver);
}

static void __exit ap3426_exit(void) {
  if (ap3426_workqueue) destroy_workqueue(ap3426_workqueue);

  ap3426_workqueue = NULL;

  i2c_del_driver(&ap3426_driver);
}

MODULE_AUTHOR("Jian Zhou");
MODULE_DESCRIPTION("AP3426 ambient light + proximity sensor driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);

module_init(ap3426_init);
module_exit(ap3426_exit);
