/* Marvell ISP OV13850R2A Driver
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
#include <uapi/media/b52_api.h>

#include "ov13850r2a.h"
static int OV13850R2A_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
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

static int OV13850R2A_get_dphy_desc(struct v4l2_subdev *sd,
			struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	OV13850R2A_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 50;
	dphy_desc->hs_zero  = 150;

	return 0;
}

static int OV13850R2A_get_pixelclock(struct v4l2_subdev *sd,
						u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int PLL1_divmipi;
	u32 mipi_clk;
	int Pll1_divmipi_map[] = {4, 5, 6, 8};
	struct b52_sensor *sensor = to_b52_sensor(sd);
	OV13850R2A_get_mipiclock(sd, &mipi_clk, mclk);
	b52_sensor_call(sensor, i2c_read, 0x0304, &temp1, 1);
	temp2 = temp1 & 0x03;
	PLL1_divmipi = Pll1_divmipi_map[temp2];
	*rate = mipi_clk / PLL1_divmipi * sensor->drvdata->nr_lane;
	return 0;
}
static void OV13850r2a_write_i2c(struct b52_sensor *sensor, u16 reg, u8 val)
{
	b52_sensor_call(sensor, i2c_write, reg, val, 1);
}

static u8 OV13850r2a_read_i2c(struct b52_sensor *sensor, u16 reg)
{
	int temp1;
	b52_sensor_call(sensor, i2c_read, reg, &temp1, 1);
	return temp1;
}

static u16 OV13850r2a_read_eeprom(struct v4l2_subdev *sd, u16 reg)
{
	const struct b52_sensor_i2c_attr attr = {
		.reg_len = I2C_16BIT,
		.val_len = I2C_8BIT,
		.addr = 0x58,
		};
	struct b52_cmd_i2c_data data;
	struct regval_tab tab;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	data.attr = &attr;
	data.tab = &tab;
	data.num = 1;
	data.pos = sensor->pos;

	tab.reg = reg;
	tab.mask = 0xff;
	b52_cmd_read_i2c(&data);
	return tab.val;
}

static int OV13850r2a_OTP_access_start(struct b52_sensor *sensor)
{
	int temp;
	temp = OV13850r2a_read_i2c(sensor, 0x5002);
	OV13850r2a_write_i2c(sensor, 0x5002, (0x00 & 0x02) | (temp & (~0x02)));

	OV13850r2a_write_i2c(sensor, 0x3d84, 0xC0);
	OV13850r2a_write_i2c(sensor, 0x3d88, 0x72);
	OV13850r2a_write_i2c(sensor, 0x3d89, 0x20);
	OV13850r2a_write_i2c(sensor, 0x3d8A, 0x73);
	OV13850r2a_write_i2c(sensor, 0x3d8B, 0xBE);
	OV13850r2a_write_i2c(sensor, 0x3d81, 0x01);
	usleep_range(5000, 5010);

	return 0;
}

static int OV13850r2a_OTP_access_end(struct b52_sensor *sensor)
{
	int temp, i;
	for (i = 0x7220; i <= 0x73be; i++)
		OV13850r2a_write_i2c(sensor, i, 0);

	temp = OV13850r2a_read_i2c(sensor, 0x5002);
	OV13850r2a_write_i2c(sensor, 0x5002, (0x02 & 0x02) | (temp & (~0x02)));
	return 0;
}

static int check_otp_info(struct b52_sensor *sensor)
{
	int flag, addr = 0x0;

	flag = OV13850r2a_read_i2c(sensor, 0x7220);
	if ((flag & 0xc0) == 0x40)
		addr = 0x7221;
	else if ((flag & 0x30) == 0x10)
		addr = 0x7229;

	return addr;
}

static int read_otp_info(struct b52_sensor *sensor, int addr,
				struct b52_sensor_otp *otp)
{
	otp->module_id = OV13850r2a_read_i2c(sensor, addr);
	otp->lens_id = OV13850r2a_read_i2c(sensor, addr + 1);
	return 0;
}

static int read_otp_wb(struct b52_sensor *sensor, int addr,
				struct b52_sensor_otp *otp)
{
	int temp;

	temp = OV13850r2a_read_i2c(sensor, addr + 7);
	otp->rg_ratio = (OV13850r2a_read_i2c(sensor, addr + 5) << 2)
				+ ((temp >> 6) & 0x03);
	otp->bg_ratio = (OV13850r2a_read_i2c(sensor, addr + 6) << 2)
			+ ((temp >> 4) & 0x03);
	otp->user_data[0] = 0;
	otp->user_data[1] = 0;

	return 0;
}

static int read_otp_lenc(struct v4l2_subdev *sd, u8 *lenc)
{
	int i;

	for (i = 0; i < 360; i++)
		lenc[i] = OV13850r2a_read_eeprom(sd, 0x042F + i);

	return 0;
}

static int update_lenc(struct b52_sensor *sensor, u8 *lenc)
{
	int i, temp;

	for (i = 0; i < 360; i++)
		OV13850r2a_write_i2c(sensor, 0x5200 + i, lenc[i]);

	temp = OV13850r2a_read_i2c(sensor, 0x5000);
	temp = 0x01 | temp;
	OV13850r2a_write_i2c(sensor, 0x5000, temp);

	return 0;
}


static int update_awb_gain(struct b52_sensor *sensor, int r_gain,
				int g_gain, int b_gain)
{
	if (r_gain >= 0x400) {
		OV13850r2a_write_i2c(sensor, 0x5056, r_gain >> 8);
		OV13850r2a_write_i2c(sensor, 0x5057, r_gain & 0x00ff);
	}
	if (g_gain >= 0x400) {
		OV13850r2a_write_i2c(sensor, 0x5058, g_gain >> 8);
		OV13850r2a_write_i2c(sensor, 0x5059, g_gain & 0x00ff);
	}
	if (b_gain >= 0x400) {
		OV13850r2a_write_i2c(sensor, 0x505a, b_gain >> 8);
		OV13850r2a_write_i2c(sensor, 0x505b, b_gain & 0x00ff);
	}
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

static int update_otp_wb(struct b52_sensor *sensor, struct b52_sensor_otp *otp)
{
	int opt_addr;
	int r_gain, g_gain, b_gain, base_gain;

	opt_addr = check_otp_info(sensor);
	read_otp_wb(sensor, opt_addr, otp);

	r_gain = (rg_ratio_typical * 1000) / otp->rg_ratio;
	b_gain = (bg_ratio_typical * 1000) / otp->bg_ratio;
	g_gain = 1000;

	if (r_gain < 1000 || b_gain < 1000) {
		if (r_gain < b_gain)
			base_gain = r_gain;
		else
			base_gain = b_gain;
	} else {
		base_gain = g_gain;
	}

	r_gain = 0x400 * r_gain / base_gain;
	b_gain = 0x400 * b_gain / base_gain;
	g_gain = 0x400 * g_gain / base_gain;

	update_awb_gain(sensor, r_gain, g_gain, b_gain);
	return 0;
}

static int update_otp_lenc(struct v4l2_subdev *sd)
{
	u8 lenc[360];
	struct b52_sensor *sensor = to_b52_sensor(sd);

	read_otp_lenc(sd,  lenc);
	update_lenc(sensor, lenc);

	return 0;
}

static int OV13850R2A_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{
	int ret = 0;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	OV13850r2a_OTP_access_start(sensor);

	ret = update_otp_info(sensor, otp);
	if (ret < 0)
		return ret;

	if (otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_WB) {
		ret = update_otp_wb(sensor, otp);
		if (ret < 0)
			return ret;
	}

	if (otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_LENC) {
		ret = update_otp_lenc(sd);
		if (ret < 0)
			return ret;
	}

	OV13850r2a_OTP_access_end(sensor);
	pr_err("Marvell_Unified_OTP_ID_INF:	Module=0x%x, Lens=0x%x,	VCM=0x%x, DriverIC=0x%x\n",
		OV13850r2a_read_eeprom(sd, 0x401),
				OV13850r2a_read_eeprom(sd, 0x409),
		OV13850r2a_read_eeprom(sd, 0x40A),
				OV13850r2a_read_eeprom(sd, 0x40B));
	return 0;
}
