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
	OV8858R2A_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 71;
	dphy_desc->hs_zero = 100;

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
static void OV8858R2A_OTP_access_start(struct b52_sensor *sensor,
					int start_addr, int end_addr)
{
	int temp;
	temp = OV8858R2A_read_i2c(sensor, 0x5002);
	OV8858R2A_write_i2c(sensor, 0x5002, (0x00 & 0x08)|(temp & (~0x08)));

	OV8858R2A_write_i2c(sensor, 0x100, 0x01);
	OV8858R2A_write_i2c(sensor, 0x3d84, 0xC0);
	OV8858R2A_write_i2c(sensor, 0x3d88, (start_addr >> 8) & 0xff);
	OV8858R2A_write_i2c(sensor, 0x3d89, (start_addr & 0xff));
	OV8858R2A_write_i2c(sensor, 0x3d8A, (end_addr >> 8) & 0xff);
	OV8858R2A_write_i2c(sensor, 0x3d8B, (end_addr & 0xff));
	OV8858R2A_write_i2c(sensor, 0x3d81, 0x01);
	usleep_range(10000, 10010);

	return;
}

static int check_otp_info(struct b52_sensor *sensor, int index)
{
	int flag;

	OV8858R2A_OTP_access_start(sensor, 0x7010, 0x7010);
	flag = OV8858R2A_read_i2c(sensor, 0x7010);
	if (index == 1)
		flag = (flag >> 6) & 0x03;
	else if (index == 2)
		flag = (flag >> 4) & 0x03;
	else if (index == 3)
		flag = (flag >> 2) & 0x03;

	OV8858R2A_write_i2c(sensor, 0x7010, 0x00);

	if (flag == 0x00)
		return 0;
	else if (flag & 0x02)
		return 1;
	else
		return 2;

	return -1;
}

static int check_otp_wb(struct b52_sensor *sensor, int index)
{
	int flag;

	OV8858R2A_OTP_access_start(sensor, 0x7020, 0x7020);
	flag = OV8858R2A_read_i2c(sensor, 0x7020);
	if (index == 1)
		flag = (flag >> 6) & 0x03;
	else if (index == 2)
		flag = (flag >> 4) & 0x03;
	else if (index == 3)
		flag = (flag >> 2) & 0x03;

	OV8858R2A_write_i2c(sensor, 0x7020, 0x00);

	if (flag == 0x00)
		return 0;
	else if (flag & 0x02)
		return 1;
	else
		return 2;

	return -1;
}
static int check_otp_lenc(struct b52_sensor *sensor, int index)
{
	int flag;

	OV8858R2A_OTP_access_start(sensor, 0x703a, 0x703a);

	flag = OV8858R2A_read_i2c(sensor, 0x703a);
	if (index == 1)
		flag = (flag >> 6) & 0x03;
	else if (index == 2)
		flag = (flag >> 4) & 0x03;
	else if (index == 3)
		flag = (flag >> 2) & 0x03;

	OV8858R2A_write_i2c(sensor, 0x703a, 0x00);

	if (flag == 0x00)
		return 0;
	else if (flag & 0x02)
		return 1;
	else
		return 2;

	return -1;
}
static int read_otp_info(struct b52_sensor *sensor, int index,
				struct b52_sensor_otp *otp)
{
	int i, start_addr, end_addr;

	if (index == 1) {
		start_addr = 0x7011;
		end_addr = 0x7015;
	} else if (index == 2) {
		start_addr = 0x7016;
		end_addr = 0x701a;
	} else if (index == 3) {
		start_addr = 0x701b;
		end_addr = 0x701f;
	}
	OV8858R2A_OTP_access_start(sensor, start_addr, end_addr);

	otp->module_id = OV8858R2A_read_i2c(sensor, start_addr);
	otp->lens_id = OV8858R2A_read_i2c(sensor, start_addr + 1);

	for (i = start_addr; i <= end_addr; i++)
		OV8858R2A_write_i2c(sensor, i, 0x00);

	return 0;
}
static int read_otp_wb(struct b52_sensor *sensor, int index,
				struct b52_sensor_otp *otp)
{
	int i, temp, start_addr, end_addr;

	if (index == 1) {
		start_addr = 0x7021;
		end_addr = 0x7025;
	} else if (index == 2) {
		start_addr = 0x7026;
		end_addr = 0x702a;
	} else if (index == 3) {
		start_addr = 0x702b;
		end_addr = 0x702f;
	}
	OV8858R2A_OTP_access_start(sensor, start_addr, end_addr);

	temp = OV8858R2A_read_i2c(sensor, start_addr + 4);
	otp->rg_ratio = (OV8858R2A_read_i2c(sensor, start_addr) << 2)
				+ ((temp >> 6) & 0x03);
	otp->bg_ratio = (OV8858R2A_read_i2c(sensor, start_addr + 1) << 2)
			+ ((temp >> 4) & 0x03);
	otp->user_data[0] = (OV8858R2A_read_i2c(sensor, start_addr + 2) << 2)
			+ ((temp >> 2) & 0x03);
	otp->user_data[1] = (OV8858R2A_read_i2c(sensor, start_addr + 3) << 2)
			+ ((temp) & 0x03);

	for (i = start_addr; i <= end_addr; i++)
		OV8858R2A_write_i2c(sensor, i, 0x00);

	return 0;
}
static int read_otp_lenc(struct b52_sensor *sensor, int index, u8 *lenc)
{
	int i, start_addr, end_addr;

	if (index == 1) {
		start_addr = 0x703b;
		end_addr = 0x70a8;
	} else if (index == 2) {
		start_addr = 0x70a9;
		end_addr = 0x7116;
	} else if (index == 3) {
		start_addr = 0x7117;
		end_addr = 0x7184;
	}
	OV8858R2A_OTP_access_start(sensor, start_addr, end_addr);

	for (i = 0; i < 110; i++)
		lenc[i] = OV8858R2A_read_i2c(sensor, start_addr + i);

	for (i = start_addr; i <= end_addr; i++)
		OV8858R2A_write_i2c(sensor, i, 0x00);

	return 0;
}
static int update_awb_gain(struct b52_sensor *sensor, int r_gain,
				int g_gain, int b_gain)
{
	if (r_gain > 0x400) {
		OV8858R2A_write_i2c(sensor, 0x5032, r_gain >> 8);
		OV8858R2A_write_i2c(sensor, 0x5033, r_gain & 0x00ff);
	}
	if (g_gain > 0x400) {
		OV8858R2A_write_i2c(sensor, 0x5034, g_gain >> 8);
		OV8858R2A_write_i2c(sensor, 0x5035, g_gain & 0x00ff);
	}
	if (b_gain > 0x400) {
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
	for (i = 0; i < 110; i++)
		OV8858R2A_write_i2c(sensor, 0x5800 + i, lenc[i]);

	return 0;
}
static int update_otp_info(struct b52_sensor *sensor,
					struct b52_sensor_otp *otp)
{
	int i, otp_index, temp;

	for (i = 1; i <= 3; i++) {
		temp = check_otp_info(sensor, i);
		if (temp == 2)
			otp_index = i;
			break;
	}

	if (i > 3)
		return 1;

	read_otp_info(sensor, otp_index, otp);

	return 0;
}

static int update_otp_wb(struct b52_sensor *sensor,
					struct b52_sensor_otp *otp)
{
	int i, otp_index, temp;
	int r_gain, g_gain, b_gain;
	int nR_G_gain, nB_G_gain, nG_G_gain, nBase_gain;
	int rg, bg;

	for (i = 1; i <= 3; i++) {
		temp = check_otp_wb(sensor, i);
		if (temp == 2)
			otp_index = i;
			break;
	}

	if (i > 3)
		return 1;

	read_otp_wb(sensor, otp_index, otp);

	if (otp->golden_rg_ratio == 0)
		rg = otp->rg_ratio;
	else
		rg = otp->rg_ratio * (otp->user_data[0] + 512) / 1024;

	if (otp->golden_bg_ratio == 0)
		bg = otp->bg_ratio;
	else
		bg = otp->bg_ratio * (otp->user_data[1] + 512) / 1024;

	nR_G_gain = (rg_ratio_typical * 1000) / rg;
	nB_G_gain = (bg_ratio_typical * 1000) / bg;
	nG_G_gain = 1000;

	if (nR_G_gain < 1000 || nB_G_gain < 1000) {
		if (nR_G_gain < nB_G_gain)
			nBase_gain = nR_G_gain;
		else
			nBase_gain = nB_G_gain;
	} else {
		nBase_gain = nG_G_gain;
	}

	r_gain = 0x400 * nR_G_gain / nBase_gain;
	b_gain = 0x400 * nB_G_gain / nBase_gain;
	g_gain = 0x400 * nG_G_gain / nBase_gain;

	update_awb_gain(sensor, r_gain, g_gain, b_gain);
	return 0;
}
static int update_otp_lenc(struct b52_sensor *sensor)
{
	int i, otp_index, temp;
	u8 lenc[110];

	for (i = 1; i <= 3; i++) {
		temp = check_otp_lenc(sensor, i);
		if (temp == 2)
			otp_index = i;
			break;
	}

	if (i > 3)
		return 1;

	read_otp_lenc(sensor, otp_index, lenc);
	update_lenc(sensor, lenc);

	return 0;
}
static int OV8858R2A_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{
	int ret = 0;
	struct b52_sensor *sensor = to_b52_sensor(sd);

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

	return 0;
}
