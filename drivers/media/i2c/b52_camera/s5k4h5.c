/* Marvell ISP S5K3L2 Driver
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *
 * Based on mt9v011 -Micron 1/4-Inch VGA Digital Image OV5642
 *
 * Copyright (c) 2009 Mauro Carvalho Chehab (mchehab@redhat.com)
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/fixed.h>

#include <asm/div64.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/crc32.h>
#include "s5k4h5.h"
#include <media/mv_sc2_twsi_conf.h>
#include <linux/clk.h>
static int S5K4H5_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
#if 0
	u32 op_sys_clk_div;
	u32 secnd_pll_mul;
	u32 secnd_pre_pll_div;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	b52_sensor_call(sensor, i2c_read, 0x030a, &op_sys_clk_div, 1);
	b52_sensor_call(sensor, i2c_read, 0x030e, &secnd_pll_mul, 1);
	b52_sensor_call(sensor, i2c_read, 0x030c, &secnd_pre_pll_div, 1);
	*rate = mclk / op_sys_clk_div * secnd_pll_mul / secnd_pre_pll_div / 2;
#endif
	*rate = 719942000;
	return 0;
}

static int S5K4H5_get_dphy_desc(struct v4l2_subdev *sd,
			struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	S5K4H5_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 71;
	dphy_desc->hs_zero  = 100;

	return 0;
}
static int S5K4H5_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
#if 0
	u32 pll_mul;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	b52_sensor_call(sensor, i2c_read, 0x0306, &pll_mul, 1);
	*rate = mclk / 8 * pll_mul / 2;
#endif
	*rate = 279736500;
	return 0;
}
static int S5K4H5_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{
	pr_err("Marvell_Unifiled_OTP_ID_INF for S5K4H5: Module=0x%x, Lens=0x%x, VCM=0x%x, DriverIC=0x%x\n",
		0, 0, 0, 0);
	return 0;
}

static int S5K4H5_s_power(struct v4l2_subdev *sd, int on)
{
	int ret = 0;
	int reset_delay = 100;
	struct sensor_power *power;
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct b52_sensor *sensor = to_b52_sensor(sd);

	power = (struct sensor_power *) &(sensor->power);
	if (on) {
		if (power->ref_cnt++ > 0)
			return 0;
		power->pwdn = devm_gpiod_get(&client->dev, "pwdn");
		if (IS_ERR(power->pwdn)) {
			dev_warn(&client->dev, "Failed to get gpio pwdn\n");
			power->pwdn = NULL;
		} else {
			ret = gpiod_direction_output(power->pwdn, 0);
			if (ret < 0) {
				dev_err(&client->dev, "Failed to set gpio pwdn\n");
				goto st_err;
			}
		}

		power->rst = devm_gpiod_get(&client->dev, "reset");
		if (IS_ERR(power->rst)) {
			dev_warn(&client->dev, "Failed to get gpio reset\n");
			power->rst = NULL;
		} else {
			ret = gpiod_direction_output(power->rst, 0);
			if (ret < 0) {
				dev_err(&client->dev, "Failed to set gpio rst\n");
				goto rst_err;
			}
		}
		if (power->dovdd_1v8) {
			regulator_set_voltage(power->dovdd_1v8,
						1800000, 1800000);
			ret = regulator_enable(power->dovdd_1v8);
			if (ret < 0)
				goto dovdd_err;
		}
		if (power->avdd_2v8) {
			regulator_set_voltage(power->avdd_2v8,
						2800000, 2800000);
			ret = regulator_enable(power->avdd_2v8);
			if (ret < 0)
				goto avdd_err;
		}
		if (power->dvdd_1v2) {
			regulator_set_voltage(power->dvdd_1v2,
						1200000, 1200000);
			ret = regulator_enable(power->dvdd_1v2);
			if (ret < 0)
				goto dvdd_err;
		}
		if (power->af_2v8) {
			regulator_set_voltage(power->af_2v8,
						2800000, 2800000);
			ret = regulator_enable(power->af_2v8);
			if (ret < 0)
				goto af_err;
		}
		if (power->pwdn)
			gpiod_set_value_cansleep(power->pwdn, 0);

		clk_set_rate(sensor->clk, sensor->mclk);
		clk_prepare_enable(sensor->clk);

		if (power->rst) {
			if (sensor->drvdata->reset_delay)
				reset_delay = sensor->drvdata->reset_delay;
			usleep_range(reset_delay, reset_delay + 10);
			gpiod_set_value_cansleep(power->rst, 0);
		}
		if (sensor->i2c_dyn_ctrl) {
			ret = sc2_select_pins_state(sensor->pos - 1,
					SC2_PIN_ST_SCCB, SC2_MOD_B52ISP);
			if (ret < 0) {
				pr_err("b52 sensor i2c pin is not configured\n");
				goto i2c_err;
			}
		}
	} else {
		if (WARN_ON(power->ref_cnt == 0))
			return -EINVAL;

		if (--power->ref_cnt > 0)
			return 0;

		if (check_load_firmware()) {
			int distance =  0x0;
			b52isp_set_focus_distance(distance, 0);
		}

		if (sensor->i2c_dyn_ctrl) {
			ret = sc2_select_pins_state(sensor->pos - 1,
					SC2_PIN_ST_GPIO, SC2_MOD_B52ISP);
			if (ret < 0)
				pr_err("b52 sensor gpio pin is not configured\n");
		}
		if (power->rst)
			gpiod_set_value_cansleep(power->rst, 1);
		clk_disable_unprepare(sensor->clk);
		if (power->pwdn)
			gpiod_set_value_cansleep(power->pwdn, 1);
		if (power->dvdd_1v2)
			regulator_disable(power->dvdd_1v2);
		if (power->dovdd_1v8)
			regulator_disable(power->dovdd_1v8);
		if (power->avdd_2v8)
			regulator_disable(power->avdd_2v8);
		if (power->af_2v8)
			regulator_disable(power->af_2v8);
		if (sensor->power.rst)
			devm_gpiod_put(&client->dev, sensor->power.rst);
		if (sensor->power.pwdn)
			devm_gpiod_put(&client->dev, sensor->power.pwdn);
		sensor->sensor_init = 0;
	}

	return ret;
i2c_err:
	clk_disable_unprepare(sensor->clk);
	if (power->af_2v8)
		regulator_disable(power->af_2v8);
af_err:
	if (power->dvdd_1v2)
		regulator_disable(power->dvdd_1v2);
dvdd_err:
	if (power->dovdd_1v8)
		regulator_disable(power->dovdd_1v8);
avdd_err:
	if (sensor->power.rst)
		devm_gpiod_put(&client->dev, sensor->power.rst);
dovdd_err:
	if (power->avdd_2v8)
		regulator_disable(power->af_2v8);
rst_err:
	if (sensor->power.pwdn)
		devm_gpiod_put(&client->dev, sensor->power.pwdn);

st_err:
	power->ref_cnt--;
	return ret;
}
