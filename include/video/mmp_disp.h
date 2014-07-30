/*
 * linux/include/video/mmp_disp.h
 * Header file for Marvell MMP Display Controller
 *
 * Copyright (C) 2012 Marvell Technology Group Ltd.
 * Authors: Zhou Zhu <zzhu3@marvell.com>
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

#ifndef _MMP_DISP_H_
#define _MMP_DISP_H_

enum {
	PIXFMT_UYVY = 0,
	PIXFMT_VYUY,
	PIXFMT_YUYV,
	PIXFMT_YUV422P,
	PIXFMT_YVU422P,
	PIXFMT_YUV420P = 0x6,
	PIXFMT_YVU420P,
	PIXFMT_YUV420SP = 0xA,
	PIXFMT_YVU420SP,
	PIXFMT_RGB565 = 0x100,
	PIXFMT_BGR565,
	PIXFMT_RGB1555,
	PIXFMT_BGR1555,
	PIXFMT_RGB888PACK,
	PIXFMT_BGR888PACK,
	PIXFMT_RGB888UNPACK,
	PIXFMT_BGR888UNPACK,
	PIXFMT_RGBA888,
	PIXFMT_BGRA888,

	/* for output usage */
	PIXFMT_RGB666PACK,
	PIXFMT_BGR666PACK,
	PIXFMT_RGB666UNPACK,
	PIXFMT_BGR666UNPACK,

	PIXFMT_PSEUDOCOLOR = 0x200,
};

/* parameters used by path/overlay */
/* overlay related para: win/addr */
struct mmp_win {
	/* position/size of window */
	unsigned short	xsrc;
	unsigned short	ysrc;
	unsigned short	xdst;
	unsigned short	ydst;
	unsigned short	xpos;
	unsigned short	ypos;
	unsigned short	left_crop;
	unsigned short	right_crop;
	unsigned short	up_crop;
	unsigned short	bottom_crop;
	int	pix_fmt;
	/*
	 * pitch[0]: graphics/video layer line length or y pitch
	 * pitch[1]/pitch[2]: video u/v pitch if non-zero
	 */
	unsigned int	pitch[3];
};

struct mmp_addr {
	/* phys address */
	unsigned int	phys[6];
};

struct mmp_gamma {
#define GAMMA_ENABLE	(1 << 0)
#define GAMMA_DUMP	(1 << 1)
	unsigned int	flag;
#define GAMMA_TABLE_LEN	256
	char		table[GAMMA_TABLE_LEN];
};

struct mmp_colorkey_alpha {
#define FB_DISABLE_COLORKEY_MODE		0x0
#define FB_ENABLE_Y_COLORKEY_MODE		0x1
#define FB_ENABLE_U_COLORKEY_MODE		0x2
#define FB_ENABLE_RGB_COLORKEY_MODE		0x3
#define FB_ENABLE_V_COLORKEY_MODE		0x4
#define FB_ENABLE_R_COLORKEY_MODE		0x5
#define FB_ENABLE_G_COLORKEY_MODE		0x6
#define FB_ENABLE_B_COLORKEY_MODE		0x7
	unsigned int mode;
#define FB_VID_PATH_ALPHA			0x0
#define FB_GRA_PATH_ALPHA			0x1
#define FB_CONFIG_ALPHA				0x2
	unsigned int alphapath;
	unsigned int config;
	unsigned int y_coloralpha;
	unsigned int u_coloralpha;
	unsigned int v_coloralpha;
};

struct mmp_alpha {
#define ALPHA_PN_GRA_AND_PN_VID		(1 << 0)
#define ALPHA_PN_GRA_AND_TV_GRA		(1 << 1)
#define ALPHA_PN_GRA_AND_TV_VID		(1 << 2)
#define ALPHA_PN_VID_AND_TV_GRA		(1 << 3)
#define ALPHA_PN_VID_AND_TV_VID		(1 << 4)
#define ALPHA_TV_GRA_AND_TV_VID		(1 << 5)
	unsigned int alphapath;
#define ALPHA_PATH_PN_PATH_ALPHA		(1 << 0)
#define ALPHA_PATH_TV_PATH_ALPHA		(1 << 1)
#define ALPHA_PATH_VID_PATH_ALPHA	(1 << 2)
#define ALPHA_PATH_GRA_PATH_ALPHA	(1 << 3)
	unsigned int config;
};

#ifdef __KERNEL__
#include <linux/kthread.h>
#include <linux/device.h>

static inline int pixfmt_to_stride(int pix_fmt)
{
	switch (pix_fmt) {
	case PIXFMT_RGB565:
	case PIXFMT_BGR565:
	case PIXFMT_RGB1555:
	case PIXFMT_BGR1555:
	case PIXFMT_UYVY:
	case PIXFMT_VYUY:
	case PIXFMT_YUYV:
		return 2;
	case PIXFMT_RGB888UNPACK:
	case PIXFMT_BGR888UNPACK:
	case PIXFMT_RGBA888:
	case PIXFMT_BGRA888:
		return 4;
	case PIXFMT_RGB888PACK:
	case PIXFMT_BGR888PACK:
		return 3;
	case PIXFMT_YUV422P:
	case PIXFMT_YVU422P:
	case PIXFMT_YUV420P:
	case PIXFMT_YVU420P:
	case PIXFMT_YUV420SP:
	case PIXFMT_YVU420SP:
	case PIXFMT_PSEUDOCOLOR:
		return 1;
	default:
		return 0;
	}
}

/* path related para: mode */
struct mmp_mode {
	const char *name;
	unsigned int refresh;
	unsigned int xres;
	unsigned int yres;
	unsigned int left_margin;
	unsigned int right_margin;
	unsigned int upper_margin;
	unsigned int lower_margin;
	unsigned int hsync_len;
	unsigned int vsync_len;
	unsigned int hsync_invert;
	unsigned int vsync_invert;
	unsigned int invert_pixclock;
	unsigned int pixclock_freq;
	int pix_fmt_out;
};

/* main structures */
struct mmp_path;
struct mmp_overlay;
struct mmp_panel;

/* status types */
enum {
	/* status of what object at */
	MMP_OFF = 0,
	MMP_ON,
	MMP_ON_REDUCED,
	MMP_OFF_DMA,
	MMP_ON_DMA,
	MMP_RESET,
};

static inline const char *status_name(int status)
{
	static const char names[][50] = {
		"off",
		"on",
		"on in reduced sequence",
		"off dma only",
		"on dma only",
		"reset",
	};
	return names[status];
}

static inline int status_is_on(int status)
{
	return	status == MMP_ON ||
		status == MMP_ON_REDUCED ||
		status == MMP_ON_DMA;
}

static inline int status_is_off(int status)
{
	return	status == MMP_OFF ||
		status == MMP_OFF_DMA;
}

struct mmp_overlay_ops {
	/* should be provided by driver */
	void (*set_status)(struct mmp_overlay *overlay, int status);
	void (*set_win)(struct mmp_overlay *overlay, struct mmp_win *win);
	int (*set_addr)(struct mmp_overlay *overlay, struct mmp_addr *addr);
	int (*set_colorkey_alpha)(struct mmp_overlay *overlay,
				struct mmp_colorkey_alpha *ca);
	int (*set_alpha)(struct mmp_overlay *overlay,
				struct mmp_alpha *pa);
	void (*set_vsmooth_en)(struct mmp_overlay *overlay, int en);
};

/* overlay describes a z-order indexed slot in each path. */
struct mmp_overlay {
	int id;
	const char *name;
	struct mmp_path *path;

	/* overlay info: private data */
	struct mmp_addr addr;
	struct mmp_win win;

	/* state */
	int status;
	struct mutex access_ok;
	atomic_t on_count;

	struct mmp_overlay_ops *ops;
};

/* panel type */
enum {
	PANELTYPE_ACTIVE = 0,
	PANELTYPE_SMART,
	PANELTYPE_TV,
	PANELTYPE_DSI_CMD,
	PANELTYPE_DSI_VIDEO,
};

struct dsi_esd {
	struct delayed_work work;
	u8 dsi_reset_state;
	int (*esd_check)(struct mmp_panel *panel);
	void (*esd_recover)(struct mmp_panel *panel);
};

struct mmp_panel {
	/* use node to register to list */
	struct list_head node;
	const char *name;
	/* path name used to connect to proper path configed */
	const char *plat_path_name;
	struct device *dev;
	int panel_type;
	struct dsi_esd esd;
	void *plat_data;
	int (*get_modelist)(struct mmp_panel *panel,
			struct mmp_mode **modelist);
	void (*set_mode)(struct mmp_panel *panel,
			struct mmp_mode *mode);
	void (*set_status)(struct mmp_panel *panel,
			int status);
	void (*panel_start)(struct mmp_panel *panel,
			int status);
	void (*panel_esd_recover)(struct mmp_panel *panel);
	int (*get_status)(struct mmp_panel *panel);
	void (*esd_set_onoff)(struct mmp_panel *panel,
		int status);
};

struct mmp_path_ops {
	int (*check_status)(struct mmp_path *path);
	struct mmp_overlay *(*get_overlay)(struct mmp_path *path,
			int overlay_id);
	int (*get_modelist)(struct mmp_path *path,
			struct mmp_mode **modelist);

	/* follow ops should be provided by driver */
	void (*set_mode)(struct mmp_path *path, struct mmp_mode *mode);
	void (*set_onoff)(struct mmp_path *path, int status);
	int (*wait_vsync)(struct mmp_path *path);
	int (*set_irq)(struct mmp_path *path, int on);
	int (*ctrl_safe)(struct mmp_path *path);
	int (*set_gamma)(struct mmp_path *path, int flag, char *table);
	/* todo: add query */
};

/* path output types */
enum {
	PATH_OUT_PARALLEL,
	PATH_OUT_DSI,
	PATH_OUT_HDMI,
};

struct mmp_dsi_cmd_desc {
	u8 data_type;
	u8 lp;      /*command tx through low power mode or high-speed mode */
	unsigned int delay;  /* time to delay */
	unsigned int length; /* cmds length */
	u8 *data;
};

struct mmp_dsi_buf {
	u8 data_type;
	/* DSI maximum packet data buffer */
	#define DSI_MAX_DATA_BYTES	256
	u8 data[DSI_MAX_DATA_BYTES];
	unsigned int length; /* cmds length */
};

/* for panel to dsi */
struct mmp_dsi_port {
	const char *name;
	/* use node to register to list */
	struct list_head node;
	/* functions */
	int (*tx_cmds)(struct mmp_dsi_port *dsi_port,
			struct mmp_dsi_cmd_desc cmds[], int count);
	int (*rx_cmds)(struct mmp_dsi_port *dsi_port, struct mmp_dsi_buf *dbuf,
		struct mmp_dsi_cmd_desc cmds[], int count);
	void (*ulps_set_on)(struct mmp_dsi_port *dsi_port, int status);
};

enum {
	/* get dsi requested min path clock rate */
	DSI_SYNC_CLKREQ,
	/* get margin ratio which might be impacted by dsi lane number */
	DSI_SYNC_RATIO,
	/* get dsi mode setting */
	DSI_SYNC_MASTER_MODE,
};

struct mmp_dsi_setting {
	u32 lanes:3;
	u32 burst_mode:2;
	u32 master_mode:1;
	u32 lpm_line_en:1;
	u32 lpm_frame_en:1;
	u32 last_line_turn:1;
	u32 hex_slot_en:1;
	u32 all_slot_en:1;
	u32 hbp_en:1;
	u32 hact_en:1;
	u32 hfp_en:1;
	u32 hex_en:1;
	u32 hlp_en:1;
	u32 hsa_en:1;
	u32 hse_en:1;
	u32 eotp_en:1;
};

struct mmp_dsi {
	struct device *dev;
	struct mutex lock;
	const char *name;
	int status;

	/* use node to register to list */
	struct list_head node;

	/* path name used to connect to proper path configed */
	const char *plat_path_name;

	/* dsi info */
	struct clk *clk;
	void *reg_base;
	struct mmp_mode mode;
	struct completion tx_done;
	u32 version;
	struct mmp_dsi_setting setting;

	/* ops could be added here */
	void (*set_mode)(struct mmp_dsi *dsi, struct mmp_mode *mode);
	void (*set_status)(struct mmp_dsi *dsi, int status);
	unsigned int (*get_sync_val)(struct mmp_dsi *dsi, int val_type);

	/* output dsi port */
	struct mmp_dsi_port dsi_port;
};

struct mmp_vsync_notifier_node {
	/* use node to register to list */
	struct list_head node;
	void (*cb_notify)(void *);
	void *cb_data;
};

struct mmp_vsync {
	atomic_t ready;
	wait_queue_head_t waitqueue;
	void (*handle_irq)(struct mmp_vsync *vsync);
	struct list_head	notifier_list;
};

struct mmp_mach_debug_irq_count {
	int irq_count;
	int dispd_count;
	int vsync_count;
	int vsync_check;
};

/* path is main part of mmp-disp */
struct mmp_path {
	/* use node to register to list */
	struct list_head node;

	/* init data */
	struct device *dev;

	int id;
	const char *name;
	int output_type;
	void *encoder;
	struct mmp_panel *panel;
	void *plat_data;

	/* dynamic use */
	struct mmp_mode mode;
	struct mmp_vsync vsync;

	/* state */
	int open_count;
	int status;
	struct mutex access_ok;
	atomic_t irq_en_ref;

	/* debug */
	struct mmp_mach_debug_irq_count irq_count;

	struct mmp_path_ops ops;

	/* layers */
	int overlay_num;
	struct mmp_overlay overlays[0];
};

extern struct mmp_path *mmp_get_path(const char *name);
static inline void mmp_path_set_mode(struct mmp_path *path,
		struct mmp_mode *mode)
{
	if (path && path->ops.set_mode)
		path->ops.set_mode(path, mode);
}
static inline void mmp_path_set_onoff(struct mmp_path *path, int status)
{
	if (path && path->ops.set_onoff)
		path->ops.set_onoff(path, status);
}
static inline int mmp_path_get_modelist(struct mmp_path *path,
		struct mmp_mode **modelist)
{
	if (path && path->ops.get_modelist)
		return path->ops.get_modelist(path, modelist);
	return 0;
}
static inline int mmp_path_wait_vsync(struct mmp_path *path)
{
	if (path && path->ops.wait_vsync)
		return path->ops.wait_vsync(path);
	return 0;
}
static inline int mmp_path_set_gamma(struct mmp_path *path,
		int flag, char *table)
{
	if (path && path->ops.set_gamma)
		return path->ops.set_gamma(path, flag, table);
	return 0;
}
static inline int mmp_path_set_irq(struct mmp_path *path, int on)
{
	if (path && path->ops.set_irq)
		return path->ops.set_irq(path, on);
	return 0;
}
static inline int mmp_path_get_irq_state(struct mmp_path *path)
{
	if (path)
		return atomic_read(&path->irq_en_ref) > 0;
	return 0;
}
static inline void mmp_path_register_vsync_cb(
		struct mmp_path *path,
		struct mmp_vsync_notifier_node *notifier_node)
{
	if (path)
		list_add_tail(&notifier_node->node,
				&path->vsync.notifier_list);
}

static inline void mmp_path_unregister_vsync_cb(
		struct mmp_path *path,
		struct mmp_vsync_notifier_node *notifier_node)
{
	if (path) {
		notifier_node->cb_notify = NULL;
		notifier_node->cb_data = NULL;
		list_del(&notifier_node->node);
	}
}
static inline struct mmp_overlay *mmp_path_get_overlay(
		struct mmp_path *path, int overlay_id)
{
	if (path && path->ops.get_overlay)
		return path->ops.get_overlay(path, overlay_id);
	return NULL;
}
static inline int mmp_path_ctrl_safe(struct mmp_path *path)
{
	if (path && path->ops.ctrl_safe)
		return path->ops.ctrl_safe(path);
	return 1;
}
static inline void mmp_overlay_set_status(struct mmp_overlay *overlay,
		int status)
{
	if (overlay && overlay->ops->set_status)
		overlay->ops->set_status(overlay, status);
}
static inline int mmp_overlay_set_colorkey_alpha(struct mmp_overlay *overlay,
		struct mmp_colorkey_alpha *ca)
{
	if (overlay && overlay->ops->set_colorkey_alpha)
		return overlay->ops->set_colorkey_alpha(overlay, ca);
	return 0;
}

static inline void mmp_overlay_vsmooth_en(struct mmp_overlay *overlay,
		int en)
{
	if (overlay && overlay->ops->set_vsmooth_en)
		overlay->ops->set_vsmooth_en(overlay, en);
}

static inline int mmp_overlay_set_path_alpha(struct mmp_overlay *overlay,
		struct mmp_alpha *pa)
{
	if (overlay && overlay->ops->set_alpha)
		return overlay->ops->set_alpha(overlay, pa);
	return 0;
}

static int is_win_changed(struct mmp_win *dst, struct mmp_win *src)
{
	return	!src || !dst
		|| src->xsrc != dst->xsrc
		|| src->ysrc != dst->ysrc
		|| src->xdst != dst->xdst
		|| src->ydst != dst->ydst
		|| src->xpos != dst->xpos
		|| src->ypos != dst->ypos
		|| src->left_crop != dst->left_crop
		|| src->right_crop != dst->right_crop
		|| src->up_crop != dst->up_crop
		|| src->bottom_crop != dst->bottom_crop
		|| src->pix_fmt != dst->pix_fmt
		|| src->pitch[0] != dst->pitch[0]
		|| src->pitch[1] != dst->pitch[1]
		|| src->pitch[2] != dst->pitch[2];
}

static inline void mmp_overlay_set_win(struct mmp_overlay *overlay,
		struct mmp_win *win)
{
	if (overlay && overlay->ops->set_win
		&& is_win_changed(&overlay->win, win)) {
		overlay->win = *win;
		overlay->ops->set_win(overlay, win);
	}
}
static inline int mmp_overlay_set_addr(struct mmp_overlay *overlay,
		struct mmp_addr *addr)
{
	if (overlay && overlay->ops->set_addr)
		return overlay->ops->set_addr(overlay, addr);
	return 0;
}

static inline struct mmp_dsi *mmp_path_to_dsi(struct mmp_path *path)
{
	if (!path || path->output_type != PATH_OUT_DSI)
		return NULL;
	return path->encoder;
}

static inline struct mmp_dsi_port *mmp_panel_to_dsi_port(struct mmp_panel *panel)
{
	struct mmp_path *path = mmp_get_path(panel->plat_path_name);
	struct mmp_dsi *dsi = mmp_path_to_dsi(path);
	if (!dsi)
		return NULL;
	return &dsi->dsi_port;
}

static inline int mmp_panel_dsi_tx_cmd_array(struct mmp_panel *panel,
		struct mmp_dsi_cmd_desc cmds[], int count)
{
	struct mmp_dsi_port *dsi_port = mmp_panel_to_dsi_port(panel);
	if (dsi_port && dsi_port->tx_cmds)
		return dsi_port->tx_cmds(dsi_port, cmds, count);

	return 0;
}
static inline int mmp_panel_dsi_rx_cmd_array(struct mmp_panel *panel,
		struct mmp_dsi_buf *dbuf,
		struct mmp_dsi_cmd_desc cmds[], int count)
{
	struct mmp_dsi_port *dsi_port = mmp_panel_to_dsi_port(panel);
	if (dsi_port && dsi_port->rx_cmds)
		return dsi_port->rx_cmds(dsi_port, dbuf, cmds, count);

	return 0;
}

static inline void mmp_panel_dsi_ulps_set_on(struct mmp_panel *panel, int status)
{
	struct mmp_dsi_port *dsi_port = mmp_panel_to_dsi_port(panel);
	if (dsi_port && dsi_port->ulps_set_on)
		dsi_port->ulps_set_on(dsi_port, status);
}

static inline struct mmp_dsi *mmp_dsi_port_to_dsi(struct mmp_dsi_port *dsi_port)
{
	return container_of(dsi_port, struct mmp_dsi, dsi_port);
}

static inline void mmp_register_dsi(struct mmp_dsi *dsi)
{
	struct mmp_path *path = mmp_get_path(dsi->plat_path_name);
	if (path && path->output_type == PATH_OUT_DSI)
		path->encoder = dsi;
}

static inline void mmp_unregister_dsi(struct mmp_dsi *dsi)
{
	struct mmp_path *path = mmp_get_path(dsi->plat_path_name);
	if (path && path->output_type == PATH_OUT_DSI && path->encoder)
		path->encoder = NULL;
}

enum {
	PN_PATH = 0,
	TV_PATH,
	PN2_PATH,
	MAX_PATH,
};

/*
 * driver data is set from each detailed ctrl driver for path usage
 * it defined a common interface that plat driver need to implement
 */
struct mmp_path_info {
	/* driver data, set when registed*/
	const char *name;
	struct device *dev;
	int id;
	int output_type;
	int overlay_num;
	unsigned int *overlay_table;
	struct mmp_overlay_ops *overlay_ops;
	void *plat_data;
};

extern struct mmp_path *mmp_register_path(
		struct mmp_path_info *info);
extern void mmp_unregister_path(struct mmp_path *path);
extern void mmp_register_panel(struct mmp_panel *panel);
extern void mmp_unregister_panel(struct mmp_panel *panel);

enum {
	PN_VID = 0,
	PN_GRA,
	TV_VID,
	TV_GRA,
	PN2_VID,
	PN2_GRA,
	MAX_OVERLAY,
};

/* defintions for platform data */
/* interface for buffer driver */
struct mmp_buffer_driver_mach_info {
	const char	*name;
	const char	*path_name;
	int	overlay_id;
	int	default_pixfmt;
	int     buffer_num;
};

/* interface for controllers driver */
struct mmp_mach_path_config {
	const char *name;
	int overlay_num;
	unsigned int *overlay_table;
	int output_type;
	unsigned int path_config;
	unsigned int link_config;
};

struct mmp_mach_plat_info {
	const char *name;
	const char *clk_name;
	int path_num;
	struct mmp_mach_path_config *paths;
};

/* interface for panel drivers */
struct mmp_mach_panel_info {
	const char *name;
	void (*plat_set_onoff)(int status);
	void (*plat_panel_start)(int status);
	const char *plat_path_name;
	u32 esd_enable;
};

/* DSI burst mode */
enum {
	DSI_BURST_MODE_SYNC_PULSE = 0x0,
	DSI_BURST_MODE_SYNC_EVENT,
	DSI_BURST_MODE_BURST,
};

/* interface for dsi drivers */
struct mmp_mach_dsi_info {
	const char *name;
	int lanes;
	int burst_mode;
	int hbp_en;
	int hfp_en;
	const char *plat_path_name;
};
#endif /* __KERNEL__ */
#endif	/* _MMP_DISP_H_ */
