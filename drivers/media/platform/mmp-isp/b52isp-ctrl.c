/*
 * Copyright (C) 2013 Marvell International Ltd.
 *				 Jianle Wang <wanjl@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <media/b52socisp/b52socisp-mdev.h>
#include <media/b52-sensor.h>

#include "b52isp-ctrl.h"
#include "plat_cam.h"
#include "b52-reg.h"
#include "b52isp.h"

#define USER_GAIN_BIT 16
#define SENSOR_GAIN_BIT 4
#define GAIN_CONVERT (USER_GAIN_BIT - SENSOR_GAIN_BIT)

#define AWB_IDX_OFFSET (AWB_CUSTOMIZED_BASE - 2)

const struct v4l2_ctrl_ops b52isp_ctrl_ops;

static struct v4l2_ctrl_config b52isp_ctrl_af_mode_cfg = {
	.ops = &b52isp_ctrl_ops,
	.id = V4L2_CID_PRIVATE_AF_MODE,
	.name = "AF mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = CID_AF_SNAPSHOT,
	.max = CID_AF_CONTINUOUS,
	.step = 1,
	.def = CID_AF_SNAPSHOT,
};

static struct v4l2_ctrl_config b52isp_ctrl_colorfx_cfg = {
	.ops = &b52isp_ctrl_ops,
	.id = V4L2_CID_PRIVATE_COLORFX,
	.name = "color effect",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = V4L2_PRIV_COLORFX_NONE,
	.max = V4L2_PRIV_COLORFX_MAX - 1,
	.step = 1,
	.def = V4L2_PRIV_COLORFX_NONE,
};

struct b52isp_ctrl_colorfx_reg {
	u32 reg;
	u32 val;
	u8 len;
	u32 mask;
};

struct b52isp_ctrl_colorfx {
	int id;
	struct b52isp_ctrl_colorfx_reg *reg;
	int reg_len;
};

static struct b52isp_ctrl_colorfx_reg __colorfx_none[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00, 1, 0x3f},
	{REG_SDE_YGAIN,     0x80, 2, 0},
	{REG_SDE_YOFFSET,   0x00, 2, 0},
	{REG_SDE_UV_M00,    0x40, 2, 0},
	{REG_SDE_UV_M01,    0x00, 2, 0},
	{REG_SDE_UV_M10,    0x00, 2, 0},
	{REG_SDE_UV_M11,    0x40, 2, 0},
	{REG_SDE_UVOFFSET0, 0x00, 2, 0},
	{REG_SDE_UVOFFSET1, 0x00, 2, 0},
	{REG_SDE_CTRL17,    0x00, 1, 0x7},
	{REG_SDE_CTRL17,    0x00, 1, 0x8},
	{REG_SDE_CTRL17,    0x00, 1, 0x10},
	{REG_SDE_HTHRE,     0x00, 1, 0},
	{REG_SDE_HGAIN,     0x04, 1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_mono_chrome[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00, 1, 0x3f},
	{REG_SDE_YGAIN,     0x80, 2, 0},
	{REG_SDE_YOFFSET,   0x00, 2, 0},
	{REG_SDE_UV_M00,    0x00, 2, 0},
	{REG_SDE_UV_M01,    0x00, 2, 0},
	{REG_SDE_UV_M10,    0x00, 2, 0},
	{REG_SDE_UV_M11,    0x00, 2, 0},
	{REG_SDE_UVOFFSET0, 0x00, 2, 0},
	{REG_SDE_UVOFFSET1, 0x00, 2, 0},
	{REG_SDE_CTRL17,    0x00, 1, 0x7},
	{REG_SDE_CTRL17,    0x00, 1, 0x8},
	{REG_SDE_CTRL17,    0x00, 1, 0x10},
	{REG_SDE_HTHRE,     0x00, 1, 0},
	{REG_SDE_HGAIN,     0x04, 1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_negative[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x140, 2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x140, 2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x00,  1, 0},
	{REG_SDE_HGAIN,     0x04,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_sepia[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x00,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x00,  2, 0},
	{REG_SDE_UVOFFSET0, 0x1f0, 2, 0},
	{REG_SDE_UVOFFSET1, 0x0d,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x00,  1, 0},
	{REG_SDE_HGAIN,     0x04,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_sketch[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x00,  2, 0},
	{REG_SDE_YOFFSET,   0xff,  2, 0},
	{REG_SDE_UV_M00,    0x00,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x00,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x08,  1, 0},
	{REG_SDE_HGAIN,     0x06,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_water_color[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x48,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x48,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x08,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x10,  1, 0},
	{REG_SDE_HGAIN,     0x05,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_ink[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x48,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x48,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x08,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x04,  1, 0},
	{REG_SDE_HGAIN,     0x05,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_cartoon[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x40,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x40,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x01,  1, 0x7},
	{REG_SDE_CTRL17,    0x08,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0xa0,  1, 0},
	{REG_SDE_HGAIN,     0x04,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_color_ink[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x00,  2, 0},
	{REG_SDE_YOFFSET,   0xff,  2, 0},
	{REG_SDE_UV_M00,    0x40,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x40,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x08,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x00,  1, 0},
	{REG_SDE_HGAIN,     0x07,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_aqua[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x00,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x00,  2, 0},
	{REG_SDE_UVOFFSET0, 0x16,  2, 0},
	{REG_SDE_UVOFFSET1, 0x1cd, 2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x00,  1, 0},
	{REG_SDE_HGAIN,     0x04,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_black_board[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x480, 2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x00,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x00,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x00,  1, 0},
	{REG_SDE_HGAIN,     0x06,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_white_board[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0xff,  2, 0},
	{REG_SDE_UV_M00,    0x00,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x00,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x07,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x10,  1, 0},
	{REG_SDE_HGAIN,     0x08,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_poster[] = {
	{REG_SDE_YUVTHRE00, 0x00,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x00,  1, 0x3f},
	{REG_SDE_YGAIN,     0x80,  2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x40,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x40,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x00,  1, 0},
	{REG_SDE_HGAIN,     0x04,  1, 0},
};

static struct b52isp_ctrl_colorfx_reg __colorfx_solariztion[] = {
	{REG_SDE_YUVTHRE00, 0x80,  1, 0},
	{REG_SDE_YUVTHRE01, 0xff,  1, 0},
	{REG_SDE_YUVTHRE10, 0x80,  1, 0},
	{REG_SDE_YUVTHRE11, 0x7f,  1, 0},
	{REG_SDE_YUVTHRE20, 0x80,  1, 0},
	{REG_SDE_YUVTHRE21, 0x7f,  1, 0},
	{REG_SDE_CTRL06,    0x20,  1, 0x3f},
	{REG_SDE_YGAIN,     0x480, 2, 0},
	{REG_SDE_YOFFSET,   0x00,  2, 0},
	{REG_SDE_UV_M00,    0x140,  2, 0},
	{REG_SDE_UV_M01,    0x00,  2, 0},
	{REG_SDE_UV_M10,    0x00,  2, 0},
	{REG_SDE_UV_M11,    0x140,  2, 0},
	{REG_SDE_UVOFFSET0, 0x00,  2, 0},
	{REG_SDE_UVOFFSET1, 0x00,  2, 0},
	{REG_SDE_CTRL17,    0x00,  1, 0x7},
	{REG_SDE_CTRL17,    0x00,  1, 0x8},
	{REG_SDE_CTRL17,    0x00,  1, 0x10},
	{REG_SDE_HTHRE,     0x00,  1, 0},
	{REG_SDE_HGAIN,     0x04,  1, 0},
};

#define B52ISP_CTRL_COLORFX(NAME, name) \
	{	.id = V4L2_PRIV_COLORFX_##NAME, \
		.reg = __colorfx_##name, \
		.reg_len = ARRAY_SIZE(__colorfx_##name), \
	}
static struct b52isp_ctrl_colorfx b52isp_ctrl_effects[] = {
	B52ISP_CTRL_COLORFX(NONE, none),
	B52ISP_CTRL_COLORFX(MONO_CHROME, mono_chrome),
	B52ISP_CTRL_COLORFX(NEGATIVE, negative),
	B52ISP_CTRL_COLORFX(SEPIA, sepia),
	B52ISP_CTRL_COLORFX(SKETCH, sketch),
	B52ISP_CTRL_COLORFX(WATER_COLOR, water_color),
	B52ISP_CTRL_COLORFX(INK, ink),
	B52ISP_CTRL_COLORFX(CARTOON, cartoon),
	B52ISP_CTRL_COLORFX(COLOR_INK, color_ink),
	B52ISP_CTRL_COLORFX(AQUA, aqua),
	B52ISP_CTRL_COLORFX(BLACK_BOARD, black_board),
	B52ISP_CTRL_COLORFX(WHITE_BOARD, white_board),
	B52ISP_CTRL_COLORFX(POSTER, poster),
	B52ISP_CTRL_COLORFX(SOLARIZATION, solariztion),
};

static struct v4l2_rect b52isp_ctrl_af_win = {
	.left = 0,
	.top  = 0,
	.width = 640,
	.height = 480,
};

static struct b52isp_expo_metering b52isp_ctrl_metering_mode[] = {
	[V4L2_EXPOSURE_METERING_AVERAGE] = {
		.mode = V4L2_EXPOSURE_METERING_AVERAGE,
		.stat_win = {
			.left = 16,
			.top  = 16,
			.right = 16,
			.bottom = 16,
		},
		.center_win = {
			.left = 160,
			.top  = 120,
			.width = 160,
			.height = 120,
		},
		.win_weight = {
			[0 ... 12] = 1,
		},
	},
	[V4L2_EXPOSURE_METERING_CENTER_WEIGHTED] = {
		.mode = V4L2_EXPOSURE_METERING_CENTER_WEIGHTED,
		.stat_win = {
			.left = 16,
			.top  = 16,
			.right = 16,
			.bottom = 16,
		},
		.center_win = {
			.left = 160,
			.top  = 120,
			.width = 160,
			.height = 120,
		},
		.win_weight = {
			[0 ... 3]  = 1,
			[5 ... 7]  = 2,
			[8]        = 3,
			[9 ... 12] = 2,
		},
	},
	[V4L2_EXPOSURE_METERING_SPOT] = {
		.mode = V4L2_EXPOSURE_METERING_SPOT,
		.stat_win = {
			.left = 16,
			.top  = 16,
			.right = 16,
			.bottom = 16,
		},
		.center_win = {
			.left = 160,
			.top  = 120,
			.width = 160,
			.height = 120,
		},
		.win_weight = {
			[0 ... 12] = 1,
		},
	},
	[V4L2_EXPOSURE_METERING_MATRIX] = {
		.mode = V4L2_EXPOSURE_METERING_MATRIX,
		.stat_win = {
			.left = 16,
			.top  = 16,
			.right = 16,
			.bottom = 16,
		},
		.center_win = {
			.left = 160,
			.top  = 120,
			.width = 160,
			.height = 120,
		},
		.win_weight = {
			[0 ... 3]  = 1,
			[5 ... 7]  = 2,
			[8]        = 3,
			[9 ... 12] = 2,
		},
	},
};

static struct b52isp_win b52isp_ctrl_metering_roi = {
	.left = 160,
	.top  = 120,
	.right = 160,
	.bottom = 120,
};

static struct b52_sensor *b52isp_ctrl_to_sensor(struct v4l2_ctrl *ctrl)
{
	struct b52isp_ctrls *ctrls = container_of(ctrl->handler,
			struct b52isp_ctrls, ctrl_handler);
	struct isp_subdev *ispsd = &(container_of(ctrls,
			struct b52isp_lpipe, ctrls)->isd);
	struct b52_sensor *sensor = b52_get_sensor(&ispsd->subdev.entity);
	if (!sensor)
		return NULL;

	return sensor;
}

static int b52isp_ctrl_set_saturation(struct v4l2_ctrl *ctrl, int id)
{
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;

	b52_writeb(base + REG_FW_UV_MAX_SATURATON, ctrl->val & 0xFF);
	b52_writeb(base + REG_FW_UV_MIN_SATURATON, (ctrl->val >> 8) & 0xFF);

	return 0;
}

static int b52isp_ctrl_set_sharpness(struct v4l2_ctrl *ctrl, int id)
{
	u32 base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;

	b52_writeb(base + REG_CIP_MIN_SHARPEN, (ctrl->val >> 0) & 0xFF);
	b52_writeb(base + REG_CIP_MAX_SHARPEN, (ctrl->val >> 8) & 0xFF);

	return 0;
}

static int b52isp_ctrl_set_brightness(struct v4l2_ctrl *ctrl, int id)
{
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;

	b52_writeb(base + REG_FW_CMX_PREGAIN0, (ctrl->val >>  0) & 0xFF);
	b52_writeb(base + REG_FW_CMX_PREGAIN1, (ctrl->val >>  8) & 0xFF);
	b52_writeb(base + REG_FW_CMX_PREGAIN2, (ctrl->val >> 16) & 0xFF);

	return 0;
}

static int b52isp_ctrl_set_white_balance(struct b52isp_ctrls *ctrls,
		int id)
{
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;
	int value = ctrls->auto_wb->val;
	bool awb_lock = ctrls->aaa_lock->cur.val & V4L2_LOCK_WHITE_BALANCE;
	if (awb_lock) {
		pr_err("%s: error: the white balance is locked\n", __func__);
		return -EBUSY;
	}

	switch (value) {
	case V4L2_WHITE_BALANCE_MANUAL:
		base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;
		b52_writeb(base + REG_AWB_GAIN_MAN_EN, AWB_MAN_EN);
		break;
	case V4L2_WHITE_BALANCE_AUTO:
		b52_writeb(base + REG_FW_AWB_TYPE, AWB_AUTO);
		base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;
		b52_writeb(base + REG_AWB_GAIN_MAN_EN, AWB_MAN_DIS);
		break;
	case V4L2_WHITE_BALANCE_INCANDESCENT:
	case V4L2_WHITE_BALANCE_FLUORESCENT:
	case V4L2_WHITE_BALANCE_FLUORESCENT_H:
	case V4L2_WHITE_BALANCE_HORIZON:
	case V4L2_WHITE_BALANCE_DAYLIGHT:
	case V4L2_WHITE_BALANCE_FLASH:
	case V4L2_WHITE_BALANCE_CLOUDY:
	case V4L2_WHITE_BALANCE_SHADE:
		b52_writeb(base + REG_FW_AWB_TYPE, value + AWB_IDX_OFFSET);
		base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;
		b52_writeb(base + REG_AWB_GAIN_MAN_EN, AWB_MAN_DIS);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* Supported manual ISO values */
static const s64 iso_qmenu[] = {
	100, 200, 400, 800, 1600
};

/* Supported Exposure Bias values, -2.0EV...+2.0EV */
static const s64 ev_bias_qmenu[] = {
	/* AE_INDEX: 0...8 */
	-3, -2, -1, 0, 1, 2, 3
};

static int b52isp_ctrl_set_expo_bias(int idx, int id)
{
	static u8 low_def[2];
	static u8 high_def[2];
	u16 low_target;
	u16 high_target;
	s64 bias;
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;

	/* get the blue print value */
	if (!low_def[id]) {
		low_def[id] = b52_readb(base + REG_FW_AEC_TARGET_LOW);
		high_def[id] = b52_readb(base + REG_FW_AEC_TARGET_HIGH);
	}

	if (idx >= ARRAY_SIZE(ev_bias_qmenu)) {
			pr_err("exposure bias value %d is wrong\n", idx);
			return -EINVAL;
	}

	bias = ev_bias_qmenu[idx];

	/* power(2, bias)*(low_def[id]+ high_def[id])/2*/
	if (bias > 0) {
		low_target = (low_def[id] + high_def[id]) << (bias - 1);
		high_target = low_target;
	} else if (bias < 0) {
		bias *= -1;
		low_target = (low_def[id] + high_def[id]) >> (bias + 1);
		high_target = low_target;
	} else {
		low_target = low_def[id];
		high_target = high_def[id];
	}

	if (low_target & 0xFF00)
		low_target = 0xFF;
	if (high_target & 0xFF00)
		high_target = 0xFF;

	b52_writeb(base + REG_FW_AEC_TARGET_LOW, low_target);
	b52_writeb(base + REG_FW_AEC_TARGET_HIGH, high_target);

	return 0;
}

static int b52isp_ctrl_set_expo(struct b52isp_ctrls *ctrls, int id)
{
	int ret = 0;
	u32 lines = 0;
	u32 expo;
	u16 max_expo;
	u16 min_expo;
	int value = ctrls->auto_expo->val;
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;
	bool ae_lock  = ctrls->aaa_lock->cur.val & V4L2_LOCK_EXPOSURE;
	struct b52_sensor *sensor = b52isp_ctrl_to_sensor(ctrls->auto_expo);
	if (ae_lock) {
		pr_err("%s: error: the expo is locked\n", __func__);
		return -EINVAL;
	}

	switch (value) {
	case V4L2_EXPOSURE_AUTO:
		if (ctrls->auto_expo->is_new) {
			if (!sensor)
				return -EINVAL;
			b52_sensor_call(sensor, g_param_range, B52_SENSOR_EXPO,
				&min_expo, &max_expo);
			b52_writel(base + REG_FW_MAX_CAM_EXP, max_expo);
			b52_writel(base + REG_FW_MIN_CAM_EXP, min_expo);

			b52_writeb(base + REG_FW_AEC_MANUAL_EN, AEC_AUTO);
			b52_writeb(base + REG_FW_AUTO_FRAME_RATE, AUTO_FRAME_RATE_ENABLE);
		}

		if (ctrls->expo_bias->is_new)
			b52isp_ctrl_set_expo_bias(ctrls->expo_bias->val, id);
		break;

	case V4L2_EXPOSURE_MANUAL:
		if (ctrls->auto_expo->is_new)
			b52_writeb(base + REG_FW_AEC_MANUAL_EN, AEC_AUTO);

		b52_writeb(base + REG_FW_AUTO_FRAME_RATE, AUTO_FRAME_RATE_DISABLE);

		if (ctrls->exposure->is_new) {
			if (!sensor)
				return -EINVAL;

			expo = ctrls->exposure->val;
			ret = b52_sensor_call(sensor, to_expo_line, expo, &lines);
			if (ret < 0)
				return ret;

			b52_writel(base + REG_FW_MAX_CAM_EXP, lines);
			b52_writel(base + REG_FW_MIN_CAM_EXP, lines);
		} else if (ctrls->expo_line->is_new) {
			expo = ctrls->expo_line->val;
			b52_writel(base + REG_FW_MAX_CAM_EXP, expo >> 4);
			b52_writel(base + REG_FW_MIN_CAM_EXP, expo >> 4);
		}
		break;

	default:
		return -EINVAL;
	}

	b52_writeb(base + REG_FW_NIGHT_MODE, NIGHT_MODE_DISABLE);

	return ret;
}

static int b52isp_ctrl_get_expo(struct b52isp_ctrls *ctrls, int id)
{
	u32 time;
	u32 lines;
	int ret = 0;
	u32 reg = id ? REG_FW_AEC_RD_EXP2 : REG_FW_AEC_RD_EXP1;
	struct b52_sensor *sensor = b52isp_ctrl_to_sensor(ctrls->auto_expo);

	lines = b52_readl(reg);
	if (!sensor)
		ctrls->exposure->val = lines;
	else {
		ret = b52_sensor_call(sensor, to_expo_time, &time, lines);
		ctrls->exposure->val = time;
	}
	return ret;
}

static int b52isp_ctrl_set_3a_lock(struct v4l2_ctrl *ctrl, int id)
{
	u32 base;
	bool awb_lock = ctrl->val & V4L2_LOCK_WHITE_BALANCE;
	bool ae_lock  = ctrl->val & V4L2_LOCK_EXPOSURE;
	bool af_lock  = ctrl->val & V4L2_LOCK_FOCUS;
	int changed = ctrl->val ^ ctrl->cur.val;
	base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;
	if (changed & V4L2_LOCK_EXPOSURE)
		b52_writeb(base + REG_FW_AEC_MANUAL_EN,
			ae_lock ? AEC_MANUAL : AEC_AUTO);

	base = FW_P1_REG_AF_BASE + id * FW_P1_P2_AF_OFFSET;
	if (changed & V4L2_LOCK_FOCUS)
		b52_writeb(base + REG_FW_AF_ACIVE,
			af_lock ? AF_STOP : AF_START);

	base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;
	if (changed & V4L2_LOCK_WHITE_BALANCE)
		b52_writeb(base + REG_AWB_GAIN_MAN_EN,
			awb_lock ? AWB_MAN_EN : AWB_MAN_DIS);

	return 0;
}

static int b52_set_metering_win(
		struct b52isp_expo_metering *metering, int id)
{
	int i;

	u32 base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;

	b52_writew(base + REG_AEC_STATWIN_LEFT, metering->stat_win.left);
	b52_writew(base + REG_AEC_STATWIN_TOP, metering->stat_win.top);
	b52_writew(base + REG_AEC_STATWIN_RIGHT, metering->stat_win.right);
	b52_writew(base + REG_AEC_STATWIN_BOTTOM, metering->stat_win.bottom);

	b52_writew(base + REG_AEC_WINLEFT, metering->center_win.left);
	b52_writew(base + REG_AEC_WINTOP, metering->center_win.top);
	b52_writew(base + REG_AEC_WINWIDTH, metering->center_win.width);
	b52_writew(base + REG_AEC_WINHEIGHT, metering->center_win.height);
	for (i = 0; i < NR_METERING_WIN_WEIGHT; i++)
		b52_writeb(base + REG_AEC_WIN_WEIGHT0 + i,
				metering->win_weight[i]);

	return 0;
}

static int b52_set_metering_roi(struct b52isp_win *r,
		int enable, int id)
{
	int weight_in = 1;
	int weight_out = 1;
	u32 base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;

	if (!enable)
		goto out;

	b52_writew(base + REG_AEC_ROI_LEFT, r->left);
	b52_writew(base + REG_AEC_ROI_TOP, r->top);
	b52_writew(base + REG_AEC_ROI_RIGHT, r->right);
	b52_writew(base + REG_AEC_ROI_BOTTOM, r->bottom);
	weight_out = 0;

out:
	b52_writeb(base + REG_AEC_ROI_WEIGHT_IN, weight_in);
	b52_writeb(base + REG_AEC_ROI_WEIGHT_OUT, weight_out);

	return 0;
}

static int b52isp_ctrl_set_metering_mode(struct b52isp_ctrls *ctrls,
		int id)
{
	int ret;

	switch (ctrls->exp_metering->val) {
	case V4L2_EXPOSURE_METERING_AVERAGE:
		ret = b52_set_metering_win(&ctrls->metering_mode[0], id);
		ret |= b52_set_metering_roi(&ctrls->metering_roi, 0, id);
		break;
	case V4L2_EXPOSURE_METERING_CENTER_WEIGHTED:
		ret = b52_set_metering_win(&ctrls->metering_mode[1], id);
		ret |= b52_set_metering_roi(&ctrls->metering_roi, 0, id);
		break;
	case V4L2_EXPOSURE_METERING_SPOT:
		ret = b52_set_metering_win(&ctrls->metering_mode[2], id);
		ret |= b52_set_metering_roi(&ctrls->metering_roi, 1, id);
		break;
	case V4L2_EXPOSURE_METERING_MATRIX:
		ret = b52_set_metering_win(&ctrls->metering_mode[3], id);
		ret |= b52_set_metering_roi(&ctrls->metering_roi, 0, id);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int b52isp_ctrl_set_iso(struct b52isp_ctrls *ctrls, int id)
{
	u32 iso;
	u32 gain;
	int ret;
	int idx = ctrls->iso->val;
	int auto_iso = ctrls->auto_iso->val;
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;
	u16 min_gain, max_gain;
	struct b52_sensor *sensor = b52isp_ctrl_to_sensor(ctrls->auto_iso);
	if (!sensor)
		return -EINVAL;

	/* FIXME:expo and gain auto register are same, how to handle*/
	switch (auto_iso) {
	case V4L2_ISO_SENSITIVITY_AUTO:
		b52_sensor_call(sensor, g_param_range, B52_SENSOR_GAIN,
			&min_gain, &max_gain);
		b52_writew(base + REG_FW_MIN_CAM_GAIN, min_gain);
		b52_writew(base + REG_FW_MAX_CAM_GAIN, max_gain);

		b52_writeb(base + REG_FW_AEC_MANUAL_EN, AEC_AUTO);
		break;

	case V4L2_ISO_SENSITIVITY_MANUAL:
		if (ctrls->auto_iso->is_new)
			b52_writeb(base + REG_FW_AEC_MANUAL_EN, AEC_AUTO);

		if (!ctrls->iso->is_new)
			break;

		if (idx >= ARRAY_SIZE(iso_qmenu))
			return -EINVAL;
		iso = iso_qmenu[idx];

		ret = b52_sensor_call(sensor, iso_to_gain, iso, &gain);
		if (ret < 0)
			return ret;

		/*
		 *the max gain should be a little bigger(e.g %20) than
		 *the min gain for banding control tolerance.
		 */
		b52_writew(base + REG_FW_MIN_CAM_GAIN, gain);
		b52_writew(base + REG_FW_MAX_CAM_GAIN, gain * 6 / 5);

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int b52isp_ctrl_get_iso(struct b52isp_ctrls *ctrls, int id)
{
	int i;
	int ret = 0;
	u32 gain = 0;
	u32 iso = 0;
	u32 reg = id ? REG_FW_AEC_RD_GAIN2 : REG_FW_AEC_RD_GAIN1;
	struct b52_sensor *sensor = b52isp_ctrl_to_sensor(ctrls->auto_iso);
	if (!sensor)
		return -EINVAL;

	gain = b52_readw(reg);
	ret = b52_sensor_call(sensor, gain_to_iso, gain, &iso);
	if (ret < 0)
			return ret;

	for (i = 0; i < ARRAY_SIZE(iso_qmenu); i++)
		if (iso <= iso_qmenu[i])
			break;

	ctrls->iso->val = i;
	return ret;
}

static int b52isp_ctrl_set_gain(struct b52isp_ctrls *ctrls, int id)
{
	int ret = 0;
	u16 min_gain;
	u16 max_gain;
	int gain = ctrls->gain->val;
	int auto_gain = ctrls->auto_gain->val;
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;
	struct b52_sensor *sensor = b52isp_ctrl_to_sensor(ctrls->auto_iso);
	/*FIXME: convert the unit*/
	gain >>= GAIN_CONVERT;

	/* FIXME:expo and gain auto register are same, how to handle*/
	switch (auto_gain) {
	case 1:
		b52_sensor_call(sensor, g_param_range, B52_SENSOR_GAIN,
			&min_gain, &max_gain);
		b52_writew(base + REG_FW_MAX_CAM_GAIN, max_gain);
		b52_writew(base + REG_FW_MIN_CAM_GAIN, min_gain);

		b52_writeb(base + REG_FW_AEC_MANUAL_EN, AEC_AUTO);
		b52_writeb(base + REG_FW_AUTO_FRAME_RATE, AUTO_FRAME_RATE_ENABLE);
		break;
	case 0:
		b52_writeb(base + REG_FW_AUTO_FRAME_RATE, AUTO_FRAME_RATE_DISABLE);
		if (ctrls->auto_gain->is_new)
			b52_writeb(base + REG_FW_AEC_MANUAL_EN, AEC_AUTO);

		if (ctrls->gain->is_new) {
			b52_writew(base + REG_FW_MAX_CAM_GAIN, gain);
			b52_writew(base + REG_FW_MIN_CAM_GAIN, gain);
		}
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int b52isp_ctrl_get_gain(struct b52isp_ctrls *ctrls, int id)
{
	u32 reg = id ? REG_FW_AEC_RD_GAIN2 : REG_FW_AEC_RD_GAIN1;

	ctrls->gain->val = b52_readw(reg) << GAIN_CONVERT;

	return 0;
}

static int b52isp_ctrl_set_band(struct v4l2_ctrl *ctrl, int id)
{
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;
	struct b52_sensor *sensor = b52isp_ctrl_to_sensor(ctrl);
	if (!sensor)
		return -EINVAL;

	switch (ctrl->val) {
	case V4L2_CID_POWER_LINE_FREQUENCY_DISABLED:
		b52_writeb(base + REG_FW_AUTO_BAND_DETECTION,
				AUTO_BAND_DETECTION_OFF);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_50HZ:
		b52_writeb(base + REG_FW_AUTO_BAND_DETECTION,
				AUTO_BAND_DETECTION_OFF);
		b52_writeb(base + REG_FW_BAND_FILTER_MODE, BAND_FILTER_50HZ);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_60HZ:
		b52_writeb(base + REG_FW_AUTO_BAND_DETECTION,
				AUTO_BAND_DETECTION_OFF);
		b52_writeb(base + REG_FW_BAND_FILTER_MODE, BAND_FILTER_60HZ);
		break;
	case V4L2_CID_POWER_LINE_FREQUENCY_AUTO:
		b52_writeb(base + REG_FW_AUTO_BAND_DETECTION,
				AUTO_BAND_DETECTION_ON);
		break;
	default:
		return -EINVAL;
	}

	b52_writeb(base + REG_FW_BAND_FILTER_LESS1BAND,
		   LESS_THAN_1_BAND_ENABLE);

	return 0;
}

static int b52isp_ctrl_set_band_filter(struct v4l2_ctrl *ctrl, int id)
{
	u32 base = FW_P1_REG_BASE + id * FW_P1_P2_OFFSET;
	struct b52_sensor *sensor;

	switch (ctrl->val) {
	case 1:
		sensor = b52isp_ctrl_to_sensor(ctrl);
		if (!sensor)
			return -EINVAL;

		b52_writeb(base + REG_FW_BAND_FILTER_EN,
				BAND_FILTER_ENABLE);
		break;
	case 0:
		b52_writeb(base + REG_FW_BAND_FILTER_EN,
				BAND_FILTER_DISABLE);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int b52isp_ctrl_set_image_effect(int value, int id)
{
	int i;
	int j;
	u32 base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;
	struct b52isp_ctrl_colorfx_reg *reg;

	for (i = 0; i < ARRAY_SIZE(b52isp_ctrl_effects); i++)
		if (b52isp_ctrl_effects[i].id == value)
			break;

	reg = b52isp_ctrl_effects[i].reg;
	for (j = 0; j < b52isp_ctrl_effects[i].reg_len; j++) {
		if (reg[j].len == 1) {
			if (!reg[j].mask)
				b52_writeb(base + reg[j].reg, reg[j].val);
			else
				b52_writeb_mask(base + reg[j].reg,
					reg[j].val, reg[j].mask);
		} else if (reg[j].len == 2) {
			if (!reg[j].mask)
				b52_writew(base + reg[j].reg, reg[j].val);
			else
				b52_writew_mask(base + reg[j].reg,
					reg[j].val, reg[j].mask);
		}
	}

	return 0;
}

static int b52isp_af_run(struct v4l2_rect *r,
	int af_mode, int enable, int id)
{
	u32 base = FW_P1_REG_AF_BASE + id * FW_P1_P2_AF_OFFSET;

	if (!enable) {
		b52_writeb(base + REG_FW_AF_ACIVE, AF_STOP);
		return 0;
	}

	b52_writeb(base + REG_FW_AF_ACIVE, AF_STOP);

	/* set AF window */
	base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;
	b52_clearb_bit(base + REG_AFC_CTRL0, AFC_5X5_WIN);
	b52_set_focus_win(r, id);

	/* set af mode */
	base = FW_P1_REG_AF_BASE + id * FW_P1_P2_AF_OFFSET;
	if (af_mode == CID_AF_SNAPSHOT)
		b52_writeb(base + REG_FW_AF_MODE, AF_SNAPSHOT);
	else if (af_mode == CID_AF_CONTINUOUS)
		b52_writeb(base + REG_FW_AF_MODE, AF_CONTINUOUS);

	if (af_mode == CID_AF_CONTINUOUS)
		b52_writeb(base + REG_FW_AF_ACIVE, AF_START);

	return 0;
}

/* only used in manual mode*/
static int b52isp_set_focus_distance(u32 distance, int id)
{
	u32 base = FW_P1_REG_AF_BASE + id * FW_P1_P2_AF_OFFSET;

	if (b52_readb(base + REG_FW_AF_ACIVE) == AF_START) {
		pr_err("%s: warning still in focus\n", __func__);
		b52_writeb(base + REG_FW_AF_ACIVE, AF_STOP);
	}

	b52_writew(base + REG_FW_FOCUS_POS, distance);
	b52_writeb(base + REG_FW_FOCUS_MAN_TRIGGER, FOCUS_MAN_TRIGGER);

	return 0;
}

static int b52isp_force_start(int start, int af_mode, int id)
{
	u32 base = FW_P1_REG_AF_BASE + id * FW_P1_P2_AF_OFFSET;
	u32 reg;
	u8 val;

	if (af_mode == CID_AF_SNAPSHOT) {
		reg = REG_FW_AF_ACIVE;
		val = start ? AF_START : AF_STOP;
		if (val == AF_START && b52_readb(base + reg) == AF_START) {
			pr_err("In focusing, not need to start\n");
			return 0;
		}
	} else {
		reg = REG_FW_AF_FORCE_START;
		val = start ? AF_FORCE_START :  AF_FORCE_STOP;
	}

	b52_writeb(base + reg, val);

	return 0;
}

static int b52isp_ctrl_set_focus(struct b52isp_ctrls *ctrls, int id)
{
	bool af_lock  = ctrls->aaa_lock->cur.val & V4L2_LOCK_FOCUS;

	if (af_lock) {
		pr_err("%s: error: the focus is locked\n", __func__);
		return -EINVAL;
	}

	if (ctrls->af_range->is_new) {
		switch (ctrls->af_range->val) {
		case V4L2_AUTO_FOCUS_RANGE_AUTO:
			break;
		case V4L2_AUTO_FOCUS_RANGE_NORMAL:
			break;
		case V4L2_AUTO_FOCUS_RANGE_MACRO:
			break;
		case V4L2_AUTO_FOCUS_RANGE_INFINITY:
			break;
		default:
			return -EINVAL;
		}
	}

	if (ctrls->auto_focus->is_new && ctrls->auto_focus->val)
		b52isp_af_run(&ctrls->af_win, ctrls->af_mode->val, 1, id);
	else if (ctrls->auto_focus->is_new && !ctrls->auto_focus->val)
		b52isp_af_run(&ctrls->af_win, ctrls->af_mode->val, 0, id);

	if (ctrls->auto_focus->val && ctrls->af_start->is_new)
		b52isp_force_start(1, ctrls->af_mode->val, id);
	else if (ctrls->auto_focus->val && ctrls->af_stop->is_new)
		b52isp_force_start(0, ctrls->af_mode->val, id);

	if (!ctrls->auto_focus->val)
		if (ctrls->f_distance->is_new)
			b52isp_set_focus_distance(ctrls->f_distance->val, id);

	return 0;
}

static int b52isp_ctrl_get_focus(struct b52isp_ctrls *ctrls, int id)
{
	u32 base = FW_P1_REG_AF_BASE + id * FW_P1_P2_AF_OFFSET;
	u8 status = b52_readb(base + REG_FW_AF_STATUS);

	switch (status) {
	case AF_ST_FOCUSING:
		ctrls->af_status->val = V4L2_AUTO_FOCUS_STATUS_BUSY;
		break;
	case AF_ST_SUCCESS:
		ctrls->af_status->val = V4L2_AUTO_FOCUS_STATUS_REACHED;
		break;
	case AF_ST_FAILED:
		ctrls->af_status->val = V4L2_AUTO_FOCUS_STATUS_FAILED;
		break;
	default:
		pr_info("Unknown AF status %#x\n", status);
		break;
	}

	ctrls->f_distance->val = b52_readw(base + REG_FW_AF_CURR_POS);

	return 0;
}

static int b52isp_ctrl_set_zoom(int zoom, int id)
{
	int ret;
	struct b52isp_cmd *cmd;

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (NULL == cmd)
		return -ENOMEM;

	cmd->path = id ? B52ISP_ISD_PIPE2 : B52ISP_ISD_PIPE1;
	cmd->cmd_name = CMD_SET_ZOOM;
	cmd->zoom = zoom;

	ret = b52_hdl_cmd(cmd);

	kfree(cmd);

	return ret;
}

int b52isp_rw_awb_gain(struct b52isp_awb_gain *awb_gain, int id)
{
	u32 base = ISP1_REG_BASE + id * ISP1_ISP2_OFFSER;

	if (awb_gain->write) {
		if (b52_readb(base + REG_AWB_GAIN_MAN_EN) != AWB_MAN_EN) {
			pr_err("awb gain not in manual mode\n");
			return -EINVAL;
		}

		b52_writew(base + REG_AWB_GAIN_B, awb_gain->b & 0x3FF);
		b52_writew(base + REG_AWB_GAIN_GB, awb_gain->gb & 0x3FF);
		b52_writew(base + REG_AWB_GAIN_GB, awb_gain->gr & 0x3FF);
		b52_writew(base + REG_AWB_GAIN_R, awb_gain->r & 0x3FF);
	} else {
		awb_gain->b = b52_readw(base + REG_AWB_GAIN_B);
		awb_gain->gb = b52_readw(base + REG_AWB_GAIN_GB);
		awb_gain->gr = b52_readw(base + REG_AWB_GAIN_GR);
		awb_gain->r = b52_readw(base + REG_AWB_GAIN_R);
	}

	return 0;
}

static int b52isp_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	int id;
	int ret = 0;
	struct b52isp_ctrls *ctrls = container_of(ctrl->handler,
			struct b52isp_ctrls, ctrl_handler);
	struct isp_subdev *ispsd = &container_of(ctrls,
			struct b52isp_lpipe, ctrls)->isd;
	if (ispsd->sd_code == SDCODE_B52ISP_PIPE1 ||
		ispsd->sd_code == SDCODE_B52ISP_MS1)
		id = 0;
	else if (ispsd->sd_code == SDCODE_B52ISP_PIPE2 ||
		ispsd->sd_code == SDCODE_B52ISP_MS2)
		id = 1;
	else
		return -EINVAL;
	switch (ctrl->id) {
	case V4L2_CID_FOCUS_AUTO:
		ret = b52isp_ctrl_get_focus(ctrls, id);
		break;
	case V4L2_CID_EXPOSURE_AUTO:
		ret = b52isp_ctrl_get_expo(ctrls, id);
		break;
	case V4L2_CID_ISO_SENSITIVITY_AUTO:
		ret = b52isp_ctrl_get_iso(ctrls, id);
		break;
	case V4L2_CID_AUTOGAIN:
		ret = b52isp_ctrl_get_gain(ctrls, id);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int b52isp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	int id;

	struct b52isp_ctrls *ctrls = container_of(ctrl->handler,
			struct b52isp_ctrls, ctrl_handler);
	struct isp_subdev *ispsd = &container_of(ctrls,
			struct b52isp_lpipe, ctrls)->isd;
	if (ispsd->sd_code == SDCODE_B52ISP_PIPE1 ||
		ispsd->sd_code == SDCODE_B52ISP_MS1)
		id = 0;
	else if (ispsd->sd_code == SDCODE_B52ISP_PIPE2 ||
		ispsd->sd_code == SDCODE_B52ISP_MS2)
		id = 1;
	else
		return -EINVAL;

	switch (ctrl->id) {
	case V4L2_CID_CONTRAST:
		break;

	case V4L2_CID_SATURATION:
		ret = b52isp_ctrl_set_saturation(ctrl, id);
		break;

	case V4L2_CID_SHARPNESS:
		ret = b52isp_ctrl_set_sharpness(ctrl, id);
		break;

	case V4L2_CID_HUE:
		break;

	case V4L2_CID_BRIGHTNESS:
		ret = b52isp_ctrl_set_brightness(ctrl, id);
		break;

	case V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE:
		ret = b52isp_ctrl_set_white_balance(ctrls, id);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		ret = b52isp_ctrl_set_expo(ctrls, id);
		break;

	case V4L2_CID_EXPOSURE_METERING:
		ret = b52isp_ctrl_set_metering_mode(ctrls, id);
		break;

	case V4L2_CID_ISO_SENSITIVITY_AUTO:
		ret = b52isp_ctrl_set_iso(ctrls, id);
		break;

	case V4L2_CID_AUTOGAIN:
		ret = b52isp_ctrl_set_gain(ctrls, id);
		break;

	case V4L2_CID_FOCUS_AUTO:
		ret = b52isp_ctrl_set_focus(ctrls, id);
		break;

	case V4L2_CID_3A_LOCK:
		ret = b52isp_ctrl_set_3a_lock(ctrl, id);
		break;

	case V4L2_CID_POWER_LINE_FREQUENCY:
		ret = b52isp_ctrl_set_band(ctrl, id);
		break;

	case V4L2_CID_BAND_STOP_FILTER:
		ret = b52isp_ctrl_set_band_filter(ctrl, id);
		break;

	case V4L2_CID_ZOOM_ABSOLUTE:
		ret = b52isp_ctrl_set_zoom(ctrl->val, id);
		break;
	case V4L2_CID_PRIVATE_COLORFX:
		ret = b52isp_ctrl_set_image_effect(ctrl->val, id);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	if (ret < 0)
		pr_err("Failed to s_ctrl: %s (%d)\n", ctrl->name, ctrl->val);

	return ret;
}

const struct v4l2_ctrl_ops b52isp_ctrl_ops = {
	.s_ctrl	= b52isp_s_ctrl,
	.g_volatile_ctrl = b52isp_g_volatile_ctrl,
};

int b52isp_init_ctrls(struct b52isp_ctrls *ctrls)
{
	int i;
	struct v4l2_ctrl_handler *handler = &ctrls->ctrl_handler;
	const struct v4l2_ctrl_ops *ops = &b52isp_ctrl_ops;

	v4l2_ctrl_handler_init(handler, 30);

	ctrls->saturation = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_SATURATION, 0, 0xFF, 1, 0);
	ctrls->brightness = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_BRIGHTNESS, 0, 0xFFF, 1, 0);
	ctrls->contrast = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_CONTRAST, -2, 2, 1, 0);
	ctrls->sharpness = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_SHARPNESS, 0, 0xFF, 1, 0);
	ctrls->hue = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_HUE, -2, 2, 1, 0);

	/* white balance */
	ctrls->auto_wb = v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE,
			9, 0, V4L2_WHITE_BALANCE_AUTO);
	/* Auto exposure */
	ctrls->auto_expo = v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_EXPOSURE_AUTO, 1, 0, V4L2_EXPOSURE_AUTO);
	ctrls->expo_line = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_EXPOSURE, 0, 0xFFFFFF, 1, 0);
	ctrls->exposure = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_EXPOSURE_ABSOLUTE, 0, 0xFFFFFF, 1, 0);
	ctrls->exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;
	ctrls->expo_bias = v4l2_ctrl_new_int_menu(handler, ops,
			V4L2_CID_AUTO_EXPOSURE_BIAS,
			ARRAY_SIZE(ev_bias_qmenu) - 1,
			ARRAY_SIZE(ev_bias_qmenu)/2,
			ev_bias_qmenu);

	ctrls->exp_metering = v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_EXPOSURE_METERING, 3, ~0xf,
			V4L2_EXPOSURE_METERING_AVERAGE);

	ctrls->pwr_line_freq = v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_POWER_LINE_FREQUENCY, 3, ~0xf,
			V4L2_CID_POWER_LINE_FREQUENCY_AUTO);
	ctrls->band_filter = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_BAND_STOP_FILTER, 0, 1, 1, 0);

	/* Auto focus */
	ctrls->auto_focus = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_FOCUS_AUTO, 0, 1, 1, 1);
	/*FIXME: how to get the distance min/max value*/
	ctrls->f_distance = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_FOCUS_ABSOLUTE, 0, 0xFFFF, 1, 0);
	ctrls->f_distance->flags |= V4L2_CTRL_FLAG_VOLATILE;
	ctrls->af_start = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_AUTO_FOCUS_START, 0, 1, 1, 0);
	ctrls->af_stop = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_AUTO_FOCUS_STOP, 0, 1, 1, 0);
	ctrls->af_status = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_AUTO_FOCUS_STATUS, 0,
			(V4L2_AUTO_FOCUS_STATUS_BUSY |
			 V4L2_AUTO_FOCUS_STATUS_REACHED |
			 V4L2_AUTO_FOCUS_STATUS_FAILED),
			0, V4L2_AUTO_FOCUS_STATUS_IDLE);
	ctrls->af_status->flags |= V4L2_CTRL_FLAG_VOLATILE;
	ctrls->af_range = v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_AUTO_FOCUS_RANGE, 3,
			~(1 << V4L2_AUTO_FOCUS_RANGE_NORMAL |
			  1 << V4L2_AUTO_FOCUS_RANGE_MACRO),
			V4L2_AUTO_FOCUS_RANGE_NORMAL);
	ctrls->af_mode = v4l2_ctrl_new_custom(handler,
			&b52isp_ctrl_af_mode_cfg, NULL);

	/* ISO sensitivity */
	ctrls->auto_iso = v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_ISO_SENSITIVITY_AUTO, 1, 0,
			V4L2_ISO_SENSITIVITY_AUTO);
	ctrls->iso = v4l2_ctrl_new_int_menu(handler, ops,
			V4L2_CID_ISO_SENSITIVITY, ARRAY_SIZE(iso_qmenu) - 1,
			ARRAY_SIZE(iso_qmenu)/2 - 1, iso_qmenu);

	/* GAIN: 0x100 for 1x gain */
	ctrls->auto_gain = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_AUTOGAIN, 0, 1, 1, 1);
	ctrls->gain = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_GAIN, 0, 0xFFFFFFF, 1, 0x100);

	ctrls->aaa_lock = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_3A_LOCK, 0, 0x7, 0, 0);

	ctrls->zoom = v4l2_ctrl_new_std(handler, ops,
			V4L2_CID_ZOOM_ABSOLUTE, 0x100, 0x400, 1, 0x100);

	ctrls->colorfx = v4l2_ctrl_new_custom(handler,
			&b52isp_ctrl_colorfx_cfg, NULL);

	if (handler->error) {
		pr_err("%s: failed to init all ctrls", __func__);
		return handler->error;
	}

	v4l2_ctrl_cluster(4, &ctrls->auto_expo);

	v4l2_ctrl_auto_cluster(2, &ctrls->auto_iso,
			V4L2_ISO_SENSITIVITY_MANUAL, true);

	v4l2_ctrl_auto_cluster(2, &ctrls->auto_gain, 0, true);

	v4l2_ctrl_cluster(7, &ctrls->auto_focus);

	ctrls->af_win = b52isp_ctrl_af_win;
	ctrls->metering_roi = b52isp_ctrl_metering_roi;

	for (i = 0; i < NR_METERING_MODE; i++)
		ctrls->metering_mode[i] = b52isp_ctrl_metering_mode[i];

	return 0;
}
