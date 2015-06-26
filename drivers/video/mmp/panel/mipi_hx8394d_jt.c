/*
 * drivers/video/mmp/panel/mipi_hx8394d.c
 * active panel using DSI interface to do init
 *
 * Copyright (C) 2013 Marvell Technology Group Ltd.
 * Authors: Yonghai Huang <huangyh@marvell.com>
 *		Xiaolong Ye <yexl@marvell.com>
 *          Guoqing Li <ligq@marvell.com>
 *          Lisa Du <cldu@marvell.com>
 *          Zhou Zhu <zzhu3@marvell.com>
 *          Jing Xiang <jxiang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <video/mmp_disp.h>
#include <video/mipi_display.h>
#include <video/mmp_esd.h>

struct hx8394d_plat_data {
	const char *name;
	struct mmp_panel *panel;
	u32 esd_enable;
	void (*plat_onoff)(int status);
	void (*plat_set_backlight)(struct mmp_panel *panel, int level);
};

static u8 hx8394dd_720p_video_on_cmd_list0[] = {0xB9, 0xFF, 0x83, 0x94};
static u8 hx8394dd_720p_video_on_cmd_list1[] = {0xBA, 0x73, 0x83};
static u8 hx8394dd_720p_video_on_cmd_list2[] = {
	0xB1, 0x6c, 0x12, 0x12, 0x26,
	0x04, 0x11, 0xF1, 0x81, 0x3a,
	0x54, 0x23, 0x80, 0xC0, 0xD2,
	0x58
};
static u8 hx8394dd_720p_video_on_cmd_list3[] = {
	0xB2, 0x00, 0x64, 0x0e, 0x0d,
	0x22, 0x1C, 0x08, 0x08, 0x1C,
	0x4D, 0x00
};
static u8 hx8394dd_720p_video_on_cmd_list4[] = {
	0xB4, 0x00, 0xFF, 0x51, 0x5A,
	0x59, 0x5A, 0x03, 0x5A, 0x01,
	0x70, 0x20, 0x70
};
static u8 hx8394dd_720p_video_on_cmd_list5[] = {0xBC, 0x07};
static u8 hx8394dd_720p_video_on_cmd_list6[] = {0xBF, 0x41, 0x0E, 0x01};
/* static u8 hx8394dd_720p_video_on_cmd_list7[] = {0xD2, 0x55}; */
static u8 hx8394dd_720p_video_on_cmd_list7[] = {0xB6, 0x6B, 0x6B};
static u8 hx8394dd_720p_video_on_cmd_list8[] = {
	0xD3, 0x00, 0x0F, 0x00, 0x40,
	0x07, 0x10, 0x00, 0x08, 0x10,
	0x08, 0x00, 0x08, 0x54, 0x15,
	0x0e, 0x05, 0x0e, 0x02, 0x15,
	0x06, 0x05, 0x06, 0x47, 0x44,
	0x0a, 0x0a, 0x4b, 0x10, 0x07,
	0x07
};
static u8 hx8394dd_720p_video_on_cmd_list9[] = {
	0xD5, 0x1a, 0x1a, 0x1b, 0x1b,
	0x00, 0x01, 0x02, 0x03, 0x04,
	0x05, 0x06, 0x07, 0x08, 0x09,
	0x0a, 0x0b, 0x24, 0x25, 0x18,
	0x18, 0x26, 0x27, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x20,
	0x21, 0x18, 0x18, 0x18, 0x18
};
static u8 hx8394dd_720p_video_on_cmd_list10[] = {
	0xD6, 0x1a, 0x1a, 0x1b, 0x1b,
	0x0B, 0x0a, 0x09, 0x08, 0x07,
	0x06, 0x05, 0x04, 0x03, 0x02,
	0x01, 0x00, 0x21, 0x20, 0x58,
	0x58, 0x27, 0x26, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x18,
	0x18, 0x18, 0x18, 0x18, 0x25,
	0x24, 0x18, 0x18, 0x18, 0x18
};
/* static u8 hx8394dd_720p_video_on_cmd_list11[] = {0xB6, 0x82, 0x82}; */
/* Gammer = 2.6.. */
#if 0
static u8 hx8394dd_720p_video_on_cmd_list12[] = {
	0xE0, 0x00, 0x1b, 0x1f, 0x3a,
	0x3f, 0x3F, 0x28, 0x41, 0x09,
	0x0b, 0x0C, 0x17, 0x0D, 0x10,
	0x13, 0x11, 0x10, 0x02, 0x0f,
	0x06, 0x11, 0x00, 0x1b, 0x20,
	0x3b, 0x3f, 0x3F, 0x28, 0x47,
	0x08, 0x0b, 0x0C, 0x17, 0x0c,
	0x10, 0x11, 0x10, 0x12, 0x04,
	0x0c, 0x0d, 0x0a
};
#else /* Gammer = 2.03 */
static u8 hx8394dd_720p_video_on_cmd_list12[] = {
	0xE0, 0x00, 0x0b, 0x10, 0x2c,
	0x32, 0x3F, 0x1d, 0x39, 0x06,
	0x0b, 0x0d, 0x17, 0x0e, 0x11,
	0x14, 0x11, 0x13, 0x08, 0x13,
	0x14, 0x16, 0x00, 0x0b, 0x10,
	0x2c, 0x32, 0x3F, 0x1d, 0x39,
	0x06, 0x0b, 0x0d, 0x17, 0x0e,
	0x11, 0x14, 0x11, 0x13, 0x08,
	0x13, 0x14, 0x16
};
#endif
static u8 hx8394dd_720p_video_on_cmd_list13[] = {0xC0, 0x30, 0x14};
static u8 hx8394dd_720p_video_on_cmd_list14[] = {0xC7, 0x00, 0xC0, 0x40, 0xC0};
static u8 hx8394dd_720p_video_on_cmd_list15[] = {0xCC, 0x09};
/* static u8 hx8394dd_720p_video_on_cmd_list16[] = {0xDF, 0x88}; */
static u8 hx8394dd_720p_video_on_cmd_list17[] = {0x11};
static u8 hx8394dd_720p_video_on_cmd_list18[] = {0x29};

static struct mmp_dsi_cmd_desc hx8394d_display_on_cmds[] = {
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list0),
		hx8394dd_720p_video_on_cmd_list0},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list1),
		hx8394dd_720p_video_on_cmd_list1},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list2),
		hx8394dd_720p_video_on_cmd_list2},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list3),
		hx8394dd_720p_video_on_cmd_list3},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list4),
		hx8394dd_720p_video_on_cmd_list4},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list5),
		hx8394dd_720p_video_on_cmd_list5},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list7),
		hx8394dd_720p_video_on_cmd_list7},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list8),
		hx8394dd_720p_video_on_cmd_list8},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list9),
		hx8394dd_720p_video_on_cmd_list9},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list6),
		hx8394dd_720p_video_on_cmd_list6},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list10),
		hx8394dd_720p_video_on_cmd_list10},
	/*
	 * {MIPI_DSI_DCS_LONG_WRITE, 1, 0,
	 * sizeof(hx8394dd_720p_video_on_cmd_list11),
	 * hx8394dd_720p_video_on_cmd_list11},
	 */
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list12),
		hx8394dd_720p_video_on_cmd_list12},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list13),
		hx8394dd_720p_video_on_cmd_list13},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list14),
		hx8394dd_720p_video_on_cmd_list14},
	{MIPI_DSI_DCS_LONG_WRITE, 1, 0,
		sizeof(hx8394dd_720p_video_on_cmd_list15),
		hx8394dd_720p_video_on_cmd_list15},
	/*
	 * {MIPI_DSI_DCS_LONG_WRITE, 1, 0,
	 * sizeof(hx8394dd_720p_video_on_cmd_list16),
	 * hx8394dd_720p_video_on_cmd_list16},
	 */
	{MIPI_DSI_DCS_SHORT_WRITE, 1, 120,
		sizeof(hx8394dd_720p_video_on_cmd_list17),
		hx8394dd_720p_video_on_cmd_list17},
	{MIPI_DSI_DCS_SHORT_WRITE, 1, 200,
		sizeof(hx8394dd_720p_video_on_cmd_list18),
		hx8394dd_720p_video_on_cmd_list18}
};

static char enter_sleep[] = {0x10};
static char display_off[] = {0x28};

static struct mmp_dsi_cmd_desc hx8394d_display_off_cmds[] = {
	{MIPI_DSI_DCS_SHORT_WRITE, 0, 200,
		sizeof(enter_sleep), enter_sleep},
	{MIPI_DSI_DCS_SHORT_WRITE, 0, 0,
		sizeof(display_off), display_off},
};

static void hx8394d_panel_on(struct mmp_panel *panel, int status)
{
	if (status) {
		mmp_panel_dsi_ulps_set_on(panel, 0);
		mmp_panel_dsi_tx_cmd_array(panel, hx8394d_display_on_cmds,
			ARRAY_SIZE(hx8394d_display_on_cmds));
	} else {
		mmp_panel_dsi_tx_cmd_array(panel, hx8394d_display_off_cmds,
			ARRAY_SIZE(hx8394d_display_off_cmds));
		mmp_panel_dsi_ulps_set_on(panel, 1);
	}
}

#ifdef CONFIG_OF
static void hx8394d_panel_power(struct mmp_panel *panel, int skip_on, int on)
{
	static struct regulator *lcd_iovdd, *lcd_avdd;
	int lcd_rst_n, ret = 0;

	lcd_rst_n = of_get_named_gpio(panel->dev->of_node, "rst_gpio", 0);
	if (lcd_rst_n < 0) {
		pr_err("%s: of_get_named_gpio failed\n", __func__);
		return;
	}

	if (gpio_request(lcd_rst_n, "lcd reset gpio")) {
		pr_err("gpio %d request failed\n", lcd_rst_n);
		return;
	}

	/* FIXME: LCD_IOVDD, 1.8V from buck2 */
	if (panel->is_iovdd && (!lcd_iovdd)) {
		lcd_iovdd = regulator_get(panel->dev, "iovdd");
		if (IS_ERR(lcd_iovdd)) {
			pr_err("%s regulator get error!\n", __func__);
			ret = -EIO;
			goto regu_lcd_iovdd;
		}
	}
	if (panel->is_avdd && (!lcd_avdd)) {
		lcd_avdd = regulator_get(panel->dev, "avdd");
		if (IS_ERR(lcd_avdd)) {
			pr_err("%s regulator get error!\n", __func__);
			ret = -EIO;
			goto regu_lcd_iovdd;
		}
	}
	if (on) {
		if (panel->is_avdd && lcd_avdd) {
			regulator_set_voltage(lcd_avdd, 2800000, 2800000);
			ret = regulator_enable(lcd_avdd);
			if (ret < 0)
				goto regu_lcd_iovdd;
		}

		if (panel->is_iovdd && lcd_iovdd) {
			regulator_set_voltage(lcd_iovdd, 1800000, 1800000);
			ret = regulator_enable(lcd_iovdd);
			if (ret < 0)
				goto regu_lcd_iovdd;
		}
		usleep_range(10000, 12000);
		if (!skip_on) {
			gpio_direction_output(lcd_rst_n, 1);
			usleep_range(10000, 11000);
			gpio_direction_output(lcd_rst_n, 0);
			usleep_range(10000, 12000);
			gpio_direction_output(lcd_rst_n, 1);
			usleep_range(10000, 12000);
		}
	} else {
		/* set panel reset */
		gpio_direction_output(lcd_rst_n, 0);

		/* disable LCD_IOVDD 1.8v */
		if (panel->is_iovdd && lcd_iovdd)
			regulator_disable(lcd_iovdd);
		if (panel->is_avdd && lcd_avdd)
			regulator_disable(lcd_avdd);
	}

regu_lcd_iovdd:
	gpio_free(lcd_rst_n);
	if (ret < 0) {
		lcd_iovdd = NULL;
		lcd_avdd = NULL;
	}
}
#else
static void hx8394d_panel_power(struct mmp_panel *panel, int skip_on, int on) {}
#endif

static void hx8394d_set_status(struct mmp_panel *panel, int status)
{
	struct hx8394d_plat_data *plat = panel->plat_data;
	int skip_on = (status == MMP_ON_REDUCED);

	if (status_is_on(status)) {
		/* power on */
		if (plat->plat_onoff)
			plat->plat_onoff(1);
		else
			hx8394d_panel_power(panel, skip_on, 1);
		if (!skip_on)
			hx8394d_panel_on(panel, 1);
	} else if (status_is_off(status)) {
		hx8394d_panel_on(panel, 0);
		/* power off */
		if (plat->plat_onoff)
			plat->plat_onoff(0);
		else
			hx8394d_panel_power(panel, 0, 0);
	} else
		dev_warn(panel->dev, "set status %s not supported\n",
					status_name(status));
}

static void hx8394d_esd_onoff(struct mmp_panel *panel, int status)
{
	struct hx8394d_plat_data *plat = panel->plat_data;

	if (status) {
		if (plat->esd_enable)
			esd_start(&panel->esd);
	} else {
		if (plat->esd_enable)
			esd_stop(&panel->esd);
	}
}

static void hx8394d_esd_recover(struct mmp_panel *panel)
{
	struct mmp_path *path = mmp_get_path(panel->plat_path_name);
	static int count = 1;
	/*
	 * FIXME: skip the first esd_recover
	 * since the first esd check will fail.
	 */
	if (count++ > 1)
		esd_panel_recover(path);
}

/* for panel hx8394d */
static u8 hx8394d_pkt_size_cmd1[] = {0x01};
static u8 read_id_0xhx8394d1[] = {0xD9};
static struct mmp_dsi_cmd_desc hx8394d_read_id_cmds1[] = {
	{MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE, 1, 1,
		sizeof(hx8394d_pkt_size_cmd1),
		hx8394d_pkt_size_cmd1},
	{MIPI_DSI_DCS_READ, 1, 1,
		sizeof(read_id_0xhx8394d1), read_id_0xhx8394d1},
};

static u8 hx8394d_pkt_size_cmd2[] = {0x02};
static u8 read_id_0xhx8394d2[] = {0x09};
static struct mmp_dsi_cmd_desc hx8394d_read_id_cmds2[] = {
	{MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE, 1, 1,
		sizeof(hx8394d_pkt_size_cmd2),
		hx8394d_pkt_size_cmd2},
	{MIPI_DSI_DCS_READ, 1, 1,
		sizeof(read_id_0xhx8394d2), read_id_0xhx8394d2},
};

static int hx8394d_get_status(struct mmp_panel *panel)
{
	struct mmp_dsi_buf dbuf;
	u32 read_id = 0;
	int ret;

	ret = mmp_panel_dsi_rx_cmd_array(panel, &dbuf,
		hx8394d_read_id_cmds1,
		ARRAY_SIZE(hx8394d_read_id_cmds1));
	if (ret < 0) {
		pr_err("[ERROR] hx8394d_get_status id_cmds1 DSI receive failure!\n");
		return 1;
	}

	read_id |= dbuf.data[0];
	ret = mmp_panel_dsi_rx_cmd_array(panel, &dbuf,
		hx8394d_read_id_cmds2,
		ARRAY_SIZE(hx8394d_read_id_cmds2));
	if (ret < 0) {
		pr_err("[ERROR] hx8394d_get_status id_cmds2 DSI receive failure!\n");
		return 1;
	}

	if ((read_id != 0x80) || (dbuf.data[0] != 0x80) ||
			(dbuf.data[1] != 0x73)) {
		pr_err("[ERROR] panel status is 0x%x\n", read_id);
		return 1;
	} else {
		pr_debug("panel status is 0x%x\n", read_id);
	}

	return 0;
}
static struct mmp_mode mmp_modes_hx8394d[] = {
	[0] = {
		.pixclock_freq = 75513600,
		.refresh = 60,
		.xres = 720,
		.yres = 1280,
		.real_xres = 720,
		.real_yres = 1280,
		.hsync_len = 20,
		.left_margin = 110,
		.right_margin = 110,
		.vsync_len = 4,
		.upper_margin = 12,
		.lower_margin = 15,
		.invert_pixclock = 0,
		.pix_fmt_out = PIXFMT_BGR888PACK,
		.hsync_invert = 0,
		.vsync_invert = 0,
		.height = 110,
		.width = 62,
	},
};

static int hx8394d_get_modelist(struct mmp_panel *panel,
		struct mmp_mode **modelist)
{
	*modelist = mmp_modes_hx8394d;
	return 1;
}

static struct mmp_panel panel_hx8394d = {
	.name = "hx8394d",
	.panel_type = PANELTYPE_DSI_VIDEO,
	.is_iovdd = 0,
	.is_avdd = 0,
	.get_modelist = hx8394d_get_modelist,
	.set_status = hx8394d_set_status,
	.get_status = hx8394d_get_status,
	.panel_esd_recover = hx8394d_esd_recover,
	.esd_set_onoff = hx8394d_esd_onoff,
};

static int hx8394d_bl_update_status(struct backlight_device *bl)
{
	struct hx8394d_plat_data *data = dev_get_drvdata(&bl->dev);
	struct mmp_panel *panel = data->panel;
	int level;

	if (bl->props.fb_blank == FB_BLANK_UNBLANK &&
			bl->props.power == FB_BLANK_UNBLANK)
		level = bl->props.brightness;
	else
		level = 0;

	/* If there is backlight function of board, use it */
	if (data && data->plat_set_backlight) {
		data->plat_set_backlight(panel, level);
		return 0;
	}

	if (panel && panel->set_brightness)
		panel->set_brightness(panel, level);

	return 0;
}

static int hx8394d_bl_get_brightness(struct backlight_device *bl)
{
	if (bl->props.fb_blank == FB_BLANK_UNBLANK &&
			bl->props.power == FB_BLANK_UNBLANK)
		return bl->props.brightness;

	return 0;
}

static DEFINE_SPINLOCK(bl_lock);
static void hx8394d_panel_set_bl(struct mmp_panel *panel, int intensity)
{
	int gpio_bl, bl_level, p_num;
	unsigned long flags;
	/*
	 * FIXME
	 * the initial value of bl_level_last is the
	 * uboot backlight level, it should be aligned.
	 */
	static int bl_level_last = 17;

	gpio_bl = of_get_named_gpio(panel->dev->of_node, "bl_gpio", 0);
	if (gpio_bl < 0) {
		pr_err("%s: of_get_named_gpio failed\n", __func__);
		return;
	}

	if (gpio_request(gpio_bl, "lcd backlight")) {
		pr_err("gpio %d request failed\n", gpio_bl);
		return;
	}

	/*
	 * Brightness is controlled by a series of pulses
	 * generated by gpio. It has 32 leves and level 1
	 * is the brightest. Pull low for 3ms makes
	 * backlight shutdown
	 */
	bl_level = (100 - intensity) * 32 / 100 + 1;

	if (bl_level == bl_level_last)
		goto set_bl_return;

	if (bl_level == 33) {
		/* shutdown backlight */
		gpio_direction_output(gpio_bl, 0);
		goto set_bl_return;
	}

	if (bl_level > bl_level_last)
		p_num = bl_level - bl_level_last;
	else
		p_num = bl_level + 32 - bl_level_last;

	while (p_num--) {
		spin_lock_irqsave(&bl_lock, flags);
		gpio_direction_output(gpio_bl, 0);
		udelay(1);
		gpio_direction_output(gpio_bl, 1);
		spin_unlock_irqrestore(&bl_lock, flags);
		udelay(1);
	}

set_bl_return:
	if (bl_level == 33)
		bl_level_last = 0;
	else
		bl_level_last = bl_level;
	gpio_free(gpio_bl);
}
static const struct backlight_ops hx8394d_bl_ops = {
	.get_brightness = hx8394d_bl_get_brightness,
	.update_status  = hx8394d_bl_update_status,
};

static int hx8394d_probe(struct platform_device *pdev)
{
	struct mmp_mach_panel_info *mi;
	struct hx8394d_plat_data *plat_data;
	struct device_node *np = pdev->dev.of_node;
	const char *path_name;
	struct backlight_properties props;
	struct backlight_device *bl;
	int ret;
	u32 esd_enable;

	plat_data = kzalloc(sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF)) {
		ret = of_property_read_string(np, "marvell,path-name",
				&path_name);
		if (ret < 0) {
			kfree(plat_data);
			return ret;
		}
		panel_hx8394d.plat_path_name = path_name;

		if (of_property_read_string(np, "panel_name", &plat_data->name))
			return -EINVAL;

		if (of_find_property(np, "iovdd-supply", NULL))
			panel_hx8394d.is_iovdd = 1;

		if (of_find_property(np, "avdd-supply", NULL))
			panel_hx8394d.is_avdd = 1;
		if (of_property_read_u32(np, "panel_esd", &esd_enable))
			plat_data->esd_enable = 0;

		plat_data->esd_enable = esd_enable;
		if (of_get_named_gpio(np, "bl_gpio", 0) < 0)
			pr_debug("%s: get bl_gpio failed\n", __func__);
		else
			plat_data->plat_set_backlight = hx8394d_panel_set_bl;

	} else {
		/* get configs from platform data */
		mi = pdev->dev.platform_data;
		if (mi == NULL) {
			dev_err(&pdev->dev, "no platform data defined\n");
			kfree(plat_data);
			return -EINVAL;
		}
		plat_data->plat_onoff = mi->plat_set_onoff;
		panel_hx8394d.plat_path_name = mi->plat_path_name;
		plat_data->plat_set_backlight = mi->plat_set_backlight;
		plat_data->esd_enable = mi->esd_enable;
	}

	plat_data->panel = &panel_hx8394d;
	panel_hx8394d.plat_data = plat_data;
	panel_hx8394d.dev = &pdev->dev;
	mmp_register_panel(&panel_hx8394d);

	if (plat_data->esd_enable)
		esd_init(&panel_hx8394d);

	/*
	 * if no panel or plat associate backlight control,
	 * don't register backlight device here.
	 */
	if (!panel_hx8394d.set_brightness && !plat_data->plat_set_backlight)
		return 0;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.max_brightness = 100;
	props.type = BACKLIGHT_RAW;

	bl = backlight_device_register("lcd-bl", &pdev->dev, plat_data,
			&hx8394d_bl_ops, &props);
	if (IS_ERR(bl)) {
		ret = PTR_ERR(bl);
		dev_err(&pdev->dev, "failed to register lcd-backlight\n");
		return ret;
	}

	bl->props.fb_blank = FB_BLANK_UNBLANK;
	bl->props.power = FB_BLANK_UNBLANK;
	bl->props.brightness = 40;

	return 0;
}

static int hx8394d_remove(struct platform_device *dev)
{
	mmp_unregister_panel(&panel_hx8394d);
	kfree(panel_hx8394d.plat_data);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mmp_hx8394d_dt_match[] = {
	{ .compatible = "marvell,mmp-hx8394d_jt" },
	{},
};
#endif

static struct platform_driver hx8394d_driver = {
	.driver		= {
		.name	= "mmp-hx8394d_jt",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mmp_hx8394d_dt_match),
	},
	.probe		= hx8394d_probe,
	.remove		= hx8394d_remove,
};

static int hx8394d_init(void)
{
	return platform_driver_register(&hx8394d_driver);
}
static void hx8394d_exit(void)
{
	platform_driver_unregister(&hx8394d_driver);
}
module_init(hx8394d_init);
module_exit(hx8394d_exit);

MODULE_AUTHOR("Yonghai Huang <huangyh@marvell.com>");
MODULE_DESCRIPTION("Panel driver for MIPI panel HX8394d");
MODULE_LICENSE("GPL");
