/* Marvell ISP SR544 Driver
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
#include "sr544.h"
static int SR544_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{

	int temp1, temp2;
	int Pll_prediv, Pll_multiplier, Pll_mipi_clk_div;
	int Pll_mipi_clk_div_map[] = {1, 2, 4, 8};

	struct b52_sensor *sensor = to_b52_sensor(sd);
	b52_sensor_call(sensor, i2c_read, 0x0b14, &temp1, 1);
	Pll_prediv = (temp1>>2) & 0x0f;
	Pll_multiplier = (temp1>>8) & 0x07f;
	b52_sensor_call(sensor, i2c_read, 0x0b16, &temp1, 1);
	temp2 = (temp1>>7) & 0x03;
	Pll_mipi_clk_div = Pll_mipi_clk_div_map[temp2];


	*rate = mclk / (Pll_prediv + 1) * Pll_multiplier / Pll_mipi_clk_div;
	return 0;
}

static int SR544_get_dphy_desc(struct v4l2_subdev *sd,
			struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	SR544_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 50;
	dphy_desc->hs_zero  = 150;

	return 0;
}

static int SR544_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int Pll_prediv, Pll_multiplier, Pll_vt_syst_clk_div;
	int Pll_vt_syst_clk_div_map[] = {1, 2, 3, 4, 5, 6, 8, 10};

	struct b52_sensor *sensor = to_b52_sensor(sd);
	b52_sensor_call(sensor, i2c_read, 0x0b14, &temp1, 1);
	Pll_prediv = (temp1>>2) & 0x0f;
	Pll_multiplier = (temp1>>8) & 0x07f;
	b52_sensor_call(sensor, i2c_read, 0x0b16, &temp1, 1);
	temp2 = (temp1>>12) & 0x07;
	Pll_vt_syst_clk_div = Pll_vt_syst_clk_div_map[temp2];

	*rate = mclk / (Pll_prediv + 1) *  Pll_multiplier / Pll_vt_syst_clk_div;
	*rate = *rate * 2;
	return 0;
}
static int SR544_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{

	return 0;
}
