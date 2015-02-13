/* Marvell ISP S5K5E3 Driver
 *
 * Copyright (C) 2009-2014 Marvell International Ltd.
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
#include <uapi/media/b52_api.h>
#include "s5k5e3.h"

static int S5K5E3_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	/*pixel clock is 179.4Mhz*/
	u32 pre_pll_clk_div;
	u32 pll_mul;
	u32 pll_s;
	u32 pll_power_table[8] = {1, 2, 4, 8, 16, 32, 64, 128};
	u32 tmp;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	b52_sensor_call(sensor, i2c_read, 0x0305, &pre_pll_clk_div, 1);
	pre_pll_clk_div = pre_pll_clk_div & 0x3f;
	b52_sensor_call(sensor, i2c_read, 0x0306, &tmp, 1);
	pll_mul =  (tmp & 0x03)<<8;
	b52_sensor_call(sensor, i2c_read, 0x0307, &tmp, 1);
	pll_mul +=  (tmp & 0xff);
	b52_sensor_call(sensor, i2c_read, 0x3c1f, &pll_s, 1);
	pll_s =  pll_power_table[pll_s & 0x7];
	*rate = mclk / pre_pll_clk_div * pll_mul / pll_s / 5;
	return 0;
}
static int S5K5E3_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	S5K5E3_get_pixelclock(sd, rate, mclk);
	*rate =  *rate / 2 * 10 /2;
	return 0;
}
static int S5K5E3_get_dphy_desc(struct v4l2_subdev *sd,
				struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	S5K5E3_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 58;
	dphy_desc->hs_zero  = 100;
	return 0;
}

