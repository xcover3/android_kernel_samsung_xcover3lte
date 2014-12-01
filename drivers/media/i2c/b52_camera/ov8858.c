/* Marvell ISP OV8858R2A Driver
 *
 * Copyright (C) 2009-2014 Marvell International Ltd.
 *
 * Based on mt9v011 -Micron 1/4-Inch VGA Digital Image OV8858R2A
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

#include "ov8858.h"

static int OV8858R2A_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int Pll1_predivp, Pll1_prediv2x, Pll1_mult, Pll1_divm;
	int Pll1_predivp_map[] = {1, 2};
	int Pll1_prediv2x_map[] = {2, 3, 4, 5, 6, 8, 12, 16};

	struct b52_sensor *sensor = to_b52_sensor(sd);
	b52_sensor_call(sensor, i2c_read, 0x030a, &temp1, 1);
	temp2 = (temp1) & 0x01;
	Pll1_predivp = Pll1_predivp_map[temp2];
	b52_sensor_call(sensor, i2c_read, 0x0300, &temp1, 1);
	temp2 = temp1 & 0x07;
	Pll1_prediv2x = Pll1_prediv2x_map[temp2];
	b52_sensor_call(sensor, i2c_read, 0x0301, &temp1, 1);
	temp2 = temp1 & 0x03;
	b52_sensor_call(sensor, i2c_read, 0x0302, &temp1, 1);
	Pll1_mult = (temp2<<8) + temp1;
	b52_sensor_call(sensor, i2c_read, 0x0303, &temp1, 1);
	temp2 = temp1 & 0x0f;
	Pll1_divm = temp2 + 1;
	*rate = mclk / Pll1_predivp * 2 / Pll1_prediv2x * Pll1_mult / Pll1_divm;

	return 0;
}

static int OV8858R2A_get_dphy_desc(struct v4l2_subdev *sd,
			struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	return 0;
}

static int OV8858R2A_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int PLL1_divmipi;
	u32 mipi_clk;
	int Pll1_divmipi_map[] = {4, 5, 6, 8};
	struct b52_sensor *sensor = to_b52_sensor(sd);
	OV8858R2A_get_mipiclock(sd, &mipi_clk, mclk);
	b52_sensor_call(sensor, i2c_read, 0x0304, &temp1, 1);
	temp2 = temp1 & 0x03;
	PLL1_divmipi = Pll1_divmipi_map[temp2];
	*rate = mipi_clk / PLL1_divmipi * sensor->drvdata->nr_lane;

	return 0;
}

#if 0
static void OV8858R2A_write_i2c(struct b52_sensor *sensor, u16 reg, u8 val)
{
	b52_sensor_call(sensor, i2c_write, reg, val, 1);
}
static u8 OV8858R2A_read_i2c(struct b52_sensor *sensor, u16 reg)
{
	int temp1;
	b52_sensor_call(sensor, i2c_read, reg, &temp1, 1);
	return temp1;
}
static void OV8858R2A_OTP_access_start(struct b52_sensor *sensor)
{
	int temp;
	temp = OV8858R2A_read_i2c(sensor, 0x5002);
	OV8858R2A_write_i2c(sensor, 0x5002, (0x00 & 0x08)|(temp & (~0x08)));

	OV8858R2A_write_i2c(sensor, 0x100, 0x01);
	OV8858R2A_write_i2c(sensor, 0x3d84, 0xC0);
	OV8858R2A_write_i2c(sensor, 0x3d88, 0x70);
	OV8858R2A_write_i2c(sensor, 0x3d89, 0x10);
	OV8858R2A_write_i2c(sensor, 0x3d8A, 0x72);
	OV8858R2A_write_i2c(sensor, 0x3d8B, 0x0a);
	OV8858R2A_write_i2c(sensor, 0x3d81, 0x01);
	usleep_range(10000, 10010);

	return;
}
static int OV8858R2A_OTP_access_end(struct b52_sensor *sensor)
{
	int temp, i;

	for (i = 0x7010; i <= 0x720a; i++)
		OV8858R2A_write_i2c(sensor, i, 0);

	temp = OV8858R2A_read_i2c(sensor, 0x5002);
	OV8858R2A_write_i2c(sensor, 0x5002, (0x08 & 0x08)|(temp & (~0x08)));
	OV8858R2A_write_i2c(sensor, 0x100, 0x00);

	return 0;
}
static int check_otp_info(struct b52_sensor *sensor)
{
	int flag, addr = 0x0;

	flag = OV8858R2A_read_i2c(sensor, OTP_DRV_START_ADDR);
	if ((flag & 0xc0) == 0x40)
		addr = 0x7011;
	else if ((flag & 0x30) == 0x10)
		addr = 0x7019;

	return addr;
}
static int check_otp_lenc(struct b52_sensor *sensor)
{
	int flag, addr = 0x0;

	flag = OV8858R2A_read_i2c(sensor, OTP_DRV_LENC_START_ADDR);
	if ((flag & 0xc0) == 0x40)
		addr = 0x7029;
	else if ((flag & 0x30) == 0x10)
		addr = 0x711a;

	return addr;
}
static int read_otp_info(struct b52_sensor *sensor, int addr,
				struct b52_sensor_otp *otp)
{
	otp->module_id = OV8858R2A_read_i2c(sensor, addr);
	otp->lens_id = OV8858R2A_read_i2c(sensor, addr + 1);

	return 0;
}
static int read_otp_wb(struct b52_sensor *sensor, int addr,
				struct b52_sensor_otp *otp)
{
	int temp;

	temp = OV8858R2A_read_i2c(sensor, addr + 4);
	temp = OV8858R2A_read_i2c(sensor, addr + 7);
	otp->rg_ratio = (OV8858R2A_read_i2c(sensor, addr + 5) << 2)
				+ ((temp >> 6) & 0x03);
	otp->bg_ratio = (OV8858R2A_read_i2c(sensor, addr + 6) << 2)
			+ ((temp >> 4) & 0x03);
	otp->user_data[0] = 0;
	otp->user_data[1] = 0;
	return 0;
}
static int read_otp_lenc(struct b52_sensor *sensor, int addr, u8 *lenc)
{
	int i;

	for (i = 0; i < OTP_DRV_LENC_SIZE; i++)
		lenc[i] = OV8858R2A_read_i2c(sensor, addr + i);

	return 0;
}
static int update_awb_gain(struct b52_sensor *sensor, int r_gain,
				int g_gain, int b_gain)
{
	if (r_gain >= 0x400) {
		OV8858R2A_write_i2c(sensor, 0x5032, r_gain >> 8);
		OV8858R2A_write_i2c(sensor, 0x5033, r_gain & 0x00ff);
	}
	if (g_gain >= 0x400) {
		OV8858R2A_write_i2c(sensor, 0x5034, g_gain >> 8);
		OV8858R2A_write_i2c(sensor, 0x5035, g_gain & 0x00ff);
	}
	if (b_gain >= 0x400) {
		OV8858R2A_write_i2c(sensor, 0x5036, b_gain >> 8);
		OV8858R2A_write_i2c(sensor, 0x5037, b_gain & 0x00ff);
	}
	return 0;
}
static int update_lenc(struct b52_sensor *sensor, u8 *lenc)
{
	int i, temp;

	temp = OV8858R2A_read_i2c(sensor, 0x5000);
	temp = 0x80 | temp;

	OV8858R2A_write_i2c(sensor, 0x5000, temp);
	for (i = 0; i < OTP_DRV_LENC_SIZE; i++)
		OV8858R2A_write_i2c(sensor, OTP_DRV_LSC_REG_ADDR + i, lenc[i]);

	return 0;
}
static int update_otp_info(struct b52_sensor *sensor,
					struct b52_sensor_otp *otp)
{
	int opt_addr;

	opt_addr = check_otp_info(sensor);

	read_otp_info(sensor, opt_addr, otp);

	return 0;
}

static int update_otp_wb(struct b52_sensor *sensor,
					struct b52_sensor_otp *otp)
{
	int opt_addr;
	int r_gain, g_gain, b_gain, g_gain_r, g_gain_b;
	int rg, bg;

	opt_addr = check_otp_info(sensor);
	read_otp_wb(sensor, opt_addr, otp);

	if (otp->user_data[0] == 0)
		rg = otp->rg_ratio;
	else
		rg = otp->rg_ratio * (otp->user_data[0] + 512) / 1024;
	if (otp->user_data[1] == 0)
		bg = otp->bg_ratio;
	else
		bg = otp->bg_ratio * (otp->user_data[1] + 512) / 1024;
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
	return 0;
}
static int update_otp_lenc(struct b52_sensor *sensor)
{
	int opt_addr;
	u8 lenc[OTP_DRV_LENC_SIZE];

	opt_addr = check_otp_lenc(sensor);
	read_otp_lenc(sensor, opt_addr, lenc);
	update_lenc(sensor, lenc);

	return 0;
}
static int OV8858R2A_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{
	int ret = 0;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	OV8858R2A_OTP_access_start(sensor);

	ret = update_otp_info(sensor, otp);
	if (ret < 0)
		return ret;

	if (otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_WB) {
		ret = update_otp_wb(sensor, otp);
		if (ret < 0)
			return ret;
	}

	if (otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_LENC) {
		ret = update_otp_lenc(sensor);
		if (ret < 0)
			return ret;
	}
	OV8858R2A_OTP_access_end(sensor);

	return 0;
}
#endif
