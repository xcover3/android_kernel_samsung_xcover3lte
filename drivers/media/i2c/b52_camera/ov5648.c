/* Marvell ISP OV5648 Driver
 *
 * Copyright (C) 2009-2014 Marvell International Ltd.
 *
 * Based on mt9v011 -Micron 1/4-Inch VGA Digital Image OV5648
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
#include "ov5648.h"

static int OV5648_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int pre_div02x, div_cnt7b, sdiv0, pll_rdiv, mipi_div, VCO;

	int pre_div02x_map[] = {2, 2, 4, 6, 8, 3, 12, 5, 16, 2, 2, 2, 2, 2, 2, 2};
	int sdiv0_map[] = {16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	int pll_rdiv_map[] = {1, 2};
	struct b52_sensor *sensor = to_b52_sensor(sd);

	b52_sensor_call(sensor, i2c_read, 0x3037, &temp1, 1);
	temp2 = (temp1) & 0x0f;
	pre_div02x = pre_div02x_map[temp2];
	temp2 = (temp1 >> 4) & 0x01;
	pll_rdiv = pll_rdiv_map[temp2];
	b52_sensor_call(sensor, i2c_read, 0x3036, &temp1, 1);
	if (temp1 & 0x80)
		div_cnt7b = (int)(temp1/2) * 2;
	else
		div_cnt7b = temp1;
	VCO = mclk * 2 / pre_div02x * div_cnt7b;

	b52_sensor_call(sensor, i2c_read, 0x3035, &temp1, 1);
	temp2 = temp1 >> 4;
	sdiv0 = sdiv0_map[temp2];
	mipi_div = temp1 & 0x0f;

	*rate = VCO / sdiv0 / mipi_div;

	return 0;
}

static int OV5648_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int pre_div02x, div_cnt7b, sdiv0, pll_rdiv, bit_div2x, sclk_div, VCO;

	int pre_div02x_map[] = {2, 2, 4, 6, 8, 3, 12, 5, 16, 2, 2, 2, 2, 2, 2, 2};
	int sdiv0_map[] = {16, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
	int pll_rdiv_map[] = {1, 2};
	int bit_div2x_map[] = {2, 2, 2, 2, 2, 2, 2, 2, 4, 2, 5, 2, 2, 2, 2, 2};
	int sclk_div_map[] = {1, 2, 4, 1};
	struct b52_sensor *sensor = to_b52_sensor(sd);

	b52_sensor_call(sensor, i2c_read, 0x3037, &temp1, 1);
	temp2 = (temp1) & 0x0f;
	pre_div02x = pre_div02x_map[temp2];
	temp2 = (temp1 >> 4) & 0x01;
	pll_rdiv = pll_rdiv_map[temp2];
	b52_sensor_call(sensor, i2c_read, 0x3036, &temp1, 1);
	if (temp1 & 0x80)
		div_cnt7b = (int)(temp1/2) * 2;
	else
		div_cnt7b = temp1;
	VCO = mclk * 2 / pre_div02x * div_cnt7b;
	b52_sensor_call(sensor, i2c_read, 0x3035, &temp1, 1);
	temp2 = temp1 >> 4;
	sdiv0 = sdiv0_map[temp2];
	b52_sensor_call(sensor, i2c_read, 0x3034, &temp1, 1);
	temp2 = temp1 & 0x0f;
	bit_div2x = bit_div2x_map[temp2];
	b52_sensor_call(sensor, i2c_read, 0x3106, &temp1, 1);
	temp2 = (temp1 >> 2) & 0x03;
	sclk_div = sclk_div_map[temp2];

	*rate = VCO * 2 / sdiv0 / pll_rdiv / bit_div2x / sclk_div;

	return 0;
}

static int OV5648_get_dphy_desc(struct v4l2_subdev *sd,
			struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	OV5648_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 50;
	dphy_desc->hs_zero  = 100;

	return 0;
}

static void OV5648_write_i2c(struct b52_sensor *sensor, u16 reg, u8 val)
{
	b52_sensor_call(sensor, i2c_write, reg, val, 1);
}
static u8 OV5648_read_i2c(struct b52_sensor *sensor, u16 reg)
{
	int temp1;
	b52_sensor_call(sensor, i2c_read, reg, &temp1, 1);
	return temp1;
}

static int update_awb_gain(struct b52_sensor *sensor, int r_gain,
				int g_gain, int b_gain)
{
	if (r_gain > 0x400) {
		OV5648_write_i2c(sensor, 0x5186, r_gain >> 8);
		OV5648_write_i2c(sensor, 0x5187, r_gain & 0x00ff);
	}
	if (g_gain > 0x400) {
		OV5648_write_i2c(sensor, 0x5188, g_gain >> 8);
		OV5648_write_i2c(sensor, 0x5189, g_gain & 0x00ff);
	}
	if (b_gain > 0x400) {
		OV5648_write_i2c(sensor, 0x518a, b_gain >> 8);
		OV5648_write_i2c(sensor, 0x518b, b_gain & 0x00ff);
	}
	return 0;
}

static int OV5648_check_OTP(struct b52_sensor *sensor, int index)
{
	int flag, i, rg, bg;

	if (index == 1) {
		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x00);
		OV5648_write_i2c(sensor, 0x3d86, 0x0f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);
		flag = OV5648_read_i2c(sensor, 0x3d05);
		rg = OV5648_read_i2c(sensor, 0x3d07);
		bg = OV5648_read_i2c(sensor, 0x3d08);
	} else if (index == 2) {
		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x00);
		OV5648_write_i2c(sensor, 0x3d86, 0x0f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);
		flag = OV5648_read_i2c(sensor, 0x3d0e);
		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x10);
		OV5648_write_i2c(sensor, 0x3d86, 0x1f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);
		rg = OV5648_read_i2c(sensor, 0x3d00);
		bg = OV5648_read_i2c(sensor, 0x3d01);
	} else if (index == 3) {
		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x10);
		OV5648_write_i2c(sensor, 0x3d86, 0x1f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);
		flag = OV5648_read_i2c(sensor, 0x3d07);
		rg = OV5648_read_i2c(sensor, 0x3d09);
		bg = OV5648_read_i2c(sensor, 0x3d0a);
	}
	OV5648_write_i2c(sensor, 0x3d81, 0x00);
	for (i = 0; i < 16; i++)
		OV5648_write_i2c(sensor, 0x3d00 + i, 0x00);

	flag = flag & 0x80;
	if (flag)
		return 1;
	else {
		if ((rg == 0) | (bg == 0))
			return 0;
		else
			return 2;
	}
	return -1;
}

static void OV5648_read_OTP(struct b52_sensor *sensor,
			int index, struct b52_sensor_otp *otp)
{
	int i, temp, addr;

	if (index == 1)	{
		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x00);
		OV5648_write_i2c(sensor, 0x3d86, 0x0f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);

		addr = 0x3d05;
		otp->module_id = OV5648_read_i2c(sensor, addr) & 0x7f;
		otp->lens_id = OV5648_read_i2c(sensor, addr + 1);
		otp->user_data[0] = OV5648_read_i2c(sensor, addr + 4);
		otp->user_data[1] = OV5648_read_i2c(sensor, addr + 5);
		temp = OV5648_read_i2c(sensor, addr + 6);
		otp->rg_ratio = (OV5648_read_i2c(sensor, addr + 2) << 2) + (temp >> 6);
		otp->bg_ratio = (OV5648_read_i2c(sensor, addr + 3) << 2) + ((temp >> 4) & 0x03);
		otp->golden_rg_ratio = (OV5648_read_i2c(sensor, addr + 7) << 2) +
								((temp >> 2) & 0x03);
		otp->golden_bg_ratio = (OV5648_read_i2c(sensor, addr + 8) << 2) + (temp & 0x03);

		OV5648_write_i2c(sensor, 0x3d81, 0x00);
		for (i = 0; i < 16; i++)
			OV5648_write_i2c(sensor, 0x3d00 + i, 0x00);
	} else if (index == 2) {
		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x00);
		OV5648_write_i2c(sensor, 0x3d86, 0x0f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);

		addr = 0x3d0e;
		otp->module_id = OV5648_read_i2c(sensor, addr) & 0x7f;
		otp->lens_id = OV5648_read_i2c(sensor, addr + 1);
		for (i = 0; i < 16; i++)
			OV5648_write_i2c(sensor, 0x3d00 + i, 0x00);

		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x10);
		OV5648_write_i2c(sensor, 0x3d86, 0x1f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);

		addr = 0x3d00;
		otp->user_data[0] = OV5648_read_i2c(sensor, addr + 2);
		otp->user_data[1] = OV5648_read_i2c(sensor, addr + 3);
		temp = OV5648_read_i2c(sensor, addr + 4);
		otp->rg_ratio = (OV5648_read_i2c(sensor, addr) << 2) + (temp >> 6);
		otp->bg_ratio = (OV5648_read_i2c(sensor, addr + 1) << 2) + ((temp >> 4) & 0x03);
		otp->golden_rg_ratio = (OV5648_read_i2c(sensor, addr + 5) << 2) +
								((temp >> 2) & 0x03);
		otp->golden_bg_ratio = (OV5648_read_i2c(sensor, addr + 6) << 2) + (temp & 0x03);

		OV5648_write_i2c(sensor, 0x3d81, 0x00);
		for (i = 0; i < 16; i++)
			OV5648_write_i2c(sensor, 0x3d00 + i, 0x00);
	} else if (index == 3) {
		OV5648_write_i2c(sensor, 0x3d84, 0xc0);
		OV5648_write_i2c(sensor, 0x3d85, 0x10);
		OV5648_write_i2c(sensor, 0x3d86, 0x1f);
		OV5648_write_i2c(sensor, 0x3d81, 0x01);
		usleep_range(1000, 1100);

		addr = 0x3d07;
		otp->module_id = OV5648_read_i2c(sensor, addr) & 0x7f;
		otp->lens_id = OV5648_read_i2c(sensor, addr + 1);
		otp->user_data[0] = OV5648_read_i2c(sensor, addr + 4);
		otp->user_data[1] = OV5648_read_i2c(sensor, addr + 5);
		temp = OV5648_read_i2c(sensor, addr + 6);
		otp->rg_ratio = (OV5648_read_i2c(sensor, addr + 2) << 2) + (temp >> 6);
		otp->bg_ratio = (OV5648_read_i2c(sensor, addr + 3) << 2) + ((temp >> 4) & 0x03);
		otp->golden_rg_ratio = (OV5648_read_i2c(sensor, addr + 7) << 2) +
								((temp >> 2) & 0x03);
		otp->golden_bg_ratio = (OV5648_read_i2c(sensor, addr + 8) << 2) + (temp & 0x03);

		OV5648_write_i2c(sensor, 0x3d81, 0x00);
		for (i = 0; i < 16; i++)
			OV5648_write_i2c(sensor, 0x3d00 + i, 0x00);
	}
}

static void OV5648_update_wb(struct b52_sensor *sensor,
			struct b52_sensor_otp *otp)
{
	int r_gain, g_gain, b_gain, g_gain_r, g_gain_b;
	int rg, bg;

	if (otp->golden_rg_ratio == 0)
		rg = otp->rg_ratio;
	else
		rg = otp->rg_ratio * (otp->golden_rg_ratio + 512) / 1024;

	if (otp->golden_bg_ratio == 0)
		bg = otp->bg_ratio;
	else
		bg = otp->bg_ratio * (otp->golden_bg_ratio + 512) / 1024;

	if (bg < bg_ratio_typical) {
		if (rg < rg_ratio_typical) {
			g_gain = 0x400;
			b_gain = 0x400 * bg_ratio_typical / bg;
			r_gain = 0x400 * rg_ratio_typical / rg;
		} else {
			r_gain = 0x400;
			g_gain = 0x400 * rg / rg_ratio_typical;
			b_gain = g_gain * bg_ratio_typical / bg;
		}
	} else {
		if (rg < rg_ratio_typical) {
			b_gain = 0x400;
			g_gain = 0x400 * bg / bg_ratio_typical;
			r_gain = g_gain * rg_ratio_typical / rg;
		} else {
			g_gain_b = 0x400 * bg / bg_ratio_typical;
			g_gain_r = 0x400 * rg / rg_ratio_typical;
			if (g_gain_b > g_gain_r) {
				b_gain = 0x400;
				g_gain = g_gain_b;
				r_gain = g_gain * rg_ratio_typical / rg;
			} else {
				r_gain = 0x400;
				g_gain = g_gain_r;
				b_gain = g_gain * bg_ratio_typical / bg;
			}
		}
	}

	update_awb_gain(sensor, r_gain, g_gain, b_gain);
}
static int OV5648_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{
	int i, otp_index, temp;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (otp->otp_type != SENSOR_TO_SENSOR)
		return 0;

	for (i = 1; i <= 3; i++) {
		temp = OV5648_check_OTP(sensor, i);
		if (temp == 2) {
			otp_index = i;
			break;
		}
	}
	if (i > 3)
		return -1;

	OV5648_read_OTP(sensor, otp_index, otp);

	if (otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_WB)
		OV5648_update_wb(sensor, otp);

	return 0;
}

