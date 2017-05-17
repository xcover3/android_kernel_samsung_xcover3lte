/* Copyright (C) 2014 Marvell */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/regulator/machine.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>

#include "../staging/android/timed_output.h"

struct generic_vibrator_info {
	struct device *dev;
	struct pwm_device *pwm;
	struct regulator *vib_regulator;
	struct timed_output_dev vibrator_timed_dev;
	struct timer_list vibrate_timer;
	struct work_struct vibrator_off_work;
	struct mutex vib_mutex;
	int enable;
	int min_timeout;
	unsigned int period;
	unsigned int duty_cycle;
	int vib_gpio;
};

#define VIBRA_OFF_VALUE	0
#define VIBRA_ON_VALUE	1

static int generic_control_vibrator(struct generic_vibrator_info *info,
				unsigned char value)
{
	struct regulator *vib_power = info->vib_regulator;
	int vib_gpio = info->vib_gpio;
	int ret;

	mutex_lock(&info->vib_mutex);
	if (info->enable == value) {
		mutex_unlock(&info->vib_mutex);
		return 0;
	}

	if (value == VIBRA_OFF_VALUE) {
		/* controlled by gpio */
		if (vib_gpio > 0) {
			ret = gpio_request(vib_gpio, "vibrator gpio");
			if (ret < 0) {
				dev_dbg(info->dev,
					"%s: onoff=%d, gpio request failed\n",
					__func__, value);
				goto done;
			}
			dev_dbg(info->dev, "vib gpio set 0\n");
			gpio_direction_output(vib_gpio, 0);
			gpio_free(vib_gpio);
		/* or controlled by pwm */
		} else if (!(IS_ERR(info->pwm))) {
			pwm_config(info->pwm, 0, info->period);
			pwm_disable(info->pwm);
		}

		/* ldo control */
		if (!(IS_ERR(vib_power)))
			regulator_disable(vib_power);
	} else if (value == VIBRA_ON_VALUE) {
		/* controlled by gpio */
		if (vib_gpio > 0) {
			ret = gpio_request(vib_gpio, "vibrator gpio");
			if (ret < 0) {
				dev_err(info->dev,
					"%s: onoff=%d, gpio request failed\n",
					__func__, value);
				goto done;
			}
			dev_dbg(info->dev, "vib gpio set 1\n");
			gpio_direction_output(vib_gpio, 1);
			gpio_free(vib_gpio);
		/* or controlled by pwm */
		} else if (!(IS_ERR(info->pwm))) {
			pwm_config(info->pwm,
				info->duty_cycle ? info->duty_cycle : 0x50,
				info->period);
			pwm_enable(info->pwm);
		}

		/* controlled by ldo */
		if (!(IS_ERR(vib_power))) {
			regulator_set_voltage(vib_power, 3000000, 3000000);
			ret = regulator_enable(vib_power);
			if (ret < 0) {
				dev_err(info->dev,
				"%s: onoff=%d, regulator_enable failed\n",
				__func__, value);
				goto done;
			}
		}
	}
	info->enable = value;

done:
	mutex_unlock(&info->vib_mutex);
	return 0;
}

static void vibrator_off_worker(struct work_struct *work)
{
	struct generic_vibrator_info *info;

	info = container_of(work, struct generic_vibrator_info,
			vibrator_off_work);
	generic_control_vibrator(info, VIBRA_OFF_VALUE);
}

static void on_vibrate_timer_expired(unsigned long x)
{
	struct generic_vibrator_info *info;
	info = (struct generic_vibrator_info *)x;
	schedule_work(&info->vibrator_off_work);
}

static void vibrator_enable_set_timeout(struct timed_output_dev *sdev,
					int timeout)
{
	struct generic_vibrator_info *info;
	info = container_of(sdev, struct generic_vibrator_info,
				vibrator_timed_dev);
	dev_dbg(info->dev, "Vibrator: Set duration: %dms\n", timeout);

	if (timeout <= 0) {
		generic_control_vibrator(info, VIBRA_OFF_VALUE);
		del_timer(&info->vibrate_timer);
	} else {
		if (timeout < info->min_timeout) {
			timeout = info->min_timeout;
			generic_control_vibrator(info, VIBRA_ON_VALUE);
			msleep(timeout);
			generic_control_vibrator(info, VIBRA_OFF_VALUE);
		} else {
			generic_control_vibrator(info, VIBRA_ON_VALUE);
			mod_timer(&info->vibrate_timer,
			  jiffies + msecs_to_jiffies(timeout));
		}
	}

	return;
}

static int vibrator_get_remaining_time(struct timed_output_dev *sdev)
{
	struct generic_vibrator_info *info;
	int rettime;
	info = container_of(sdev, struct generic_vibrator_info,
			vibrator_timed_dev);
	rettime = jiffies_to_msecs(jiffies - info->vibrate_timer.expires);
	dev_dbg(info->dev, "Vibrator: Current duration: %dms\n", rettime);
	return rettime;
}

static int vibrator_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct device_node *np = pdev->dev.of_node;
	struct generic_vibrator_info *info;

	info = devm_kzalloc(&pdev->dev, sizeof(struct generic_vibrator_info),
								GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF)) {
		/* parse gpio control pin */
		info->vib_gpio = of_get_named_gpio(np, "vib_gpio", 0);
		if (info->vib_gpio < 0)
			dev_dbg(info->dev,
				"Vibrator: of_get_named_gpio failed\n");

		/* parse pwm control pin */
		info->pwm = devm_pwm_get(&pdev->dev, NULL);
		if (IS_ERR(info->pwm)) {
			dev_dbg(info->dev, "Vibrator: unable to request PWM\n");
		} else {
			info->period = pwm_get_period(info->pwm);
			ret = of_property_read_u32(np, "duty_cycle",
					&info->duty_cycle);
			if (ret < 0)
				dev_dbg(info->dev,
					"Vibrator: duty cycle is not set\n");
		}

		/* parse ldo control */
		info->vib_regulator = regulator_get(&pdev->dev, "vibrator");
		if (IS_ERR(info->vib_regulator))
			dev_dbg(info->dev, "Vibrator: regulator_get failed\n");

		ret = of_property_read_u32(np, "min_timeout",
				&info->min_timeout);
		if (ret < 0) {
			dev_dbg(info->dev,
				"Vibrator: get min_timeout failed. use 0\n");
			info->min_timeout = 0;
		}

		if ((info->vib_gpio < 0) && (IS_ERR(info->pwm))
				&& (IS_ERR(info->vib_regulator))) {
			dev_err(info->dev,
			"Vibrator: probe failed for no useful control\n");
			return -EINVAL;
		}
	}

	info->dev = &pdev->dev;
	/* Setup timed_output obj */
	info->vibrator_timed_dev.name = "vibrator";
	info->vibrator_timed_dev.enable = vibrator_enable_set_timeout;
	info->vibrator_timed_dev.get_time = vibrator_get_remaining_time;
	/* Vibrator dev register in /sys/class/timed_output/ */
	ret = timed_output_dev_register(&info->vibrator_timed_dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Vibrator: timed output dev register fail\n");
		return ret;
	}

	INIT_WORK(&info->vibrator_off_work, vibrator_off_worker);
	mutex_init(&info->vib_mutex);
	info->enable = 0;

	init_timer(&info->vibrate_timer);
	info->vibrate_timer.function = on_vibrate_timer_expired;
	info->vibrate_timer.data = (unsigned long)info;
	platform_set_drvdata(pdev, info);
	return 0;
}

static int vibrator_remove(struct platform_device *pdev)
{
	struct generic_vibrator_info *info;
	info = platform_get_drvdata(pdev);
	timed_output_dev_unregister(&info->vibrator_timed_dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id vibrator_dt_match[] = {
	{ .compatible = "marvell,generic-vibrator" },
	{},
};
#endif

static struct platform_driver vibrator_driver = {
	.probe = vibrator_probe,
	.remove = vibrator_remove,
	.driver = {
		   .name = "generic-vibrator",
		   .of_match_table = of_match_ptr(vibrator_dt_match),
		   .owner = THIS_MODULE,
		   },
};

static int __init vibrator_init(void)
{
	return platform_driver_register(&vibrator_driver);
}

static void __exit vibrator_exit(void)
{
	platform_driver_unregister(&vibrator_driver);
}

module_init(vibrator_init);
module_exit(vibrator_exit);

MODULE_DESCRIPTION("Android Vibrator driver");
MODULE_LICENSE("GPL");
