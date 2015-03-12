#ifndef _FLASH_SUBDEV_H_
#define _FLASH_SUBDEV_H_

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/b52socisp/host_isd.h>
struct flash_data {
	struct v4l2_device *v4l2_dev;
	struct isp_host_subdev *hsd;
};
struct flash_ctrls {
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_ctrl *select_type;
};
struct flash_ops {
	int (*init)(struct v4l2_subdev *);
	int (*s_flash)(struct v4l2_subdev *, u8 on);
	int (*s_torch)(struct v4l2_subdev *, u8 on);
};
struct flash_type {
	const char *name;
	struct flash_ops *ops;
};
struct flash_subdev {
	char			*name;
	struct v4l2_subdev	subdev;
	struct device		*dev;
	struct media_pad	pad;
	/* Controls */
	struct flash_ctrls flash_ctrl;

	enum v4l2_flash_led_mode led_mode;
	enum v4l2_flash_strobe_source strobe_source;
	u32 timeout;
	u32 flash_current;
	u32 torch_current;
	int flash_status;
	struct flash_type b52_flash_type[10];
	struct flash_type *current_type;
};

int flash_subdev_create(struct device *parent,
		const char *name, int id, void *pdata);

#endif
