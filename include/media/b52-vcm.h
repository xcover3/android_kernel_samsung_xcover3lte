#ifndef _VCM_SUBDEV_H_
#define _VCM_SUBDEV_H_

#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <media/b52socisp/host_isd.h>
#include <media/b52-sensor.h>
#include <uapi/media/b52_api.h>
struct vcm_data {
	struct v4l2_device *v4l2_dev;
	struct isp_host_subdev *hsd;
};

struct vcm_ctrls {
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_ctrl *select_type;
};
struct vcm_ops {
	int (*init)(struct v4l2_subdev *);
	int (*s_register)(struct v4l2_subdev *, u16 value);
	int (*g_register)(struct v4l2_subdev *, u16 *value);
};
struct vcm_type {
	const char *name;
	enum b52_vcm_type type;
	struct b52_sensor_i2c_attr *attr;
	u16 pos_reg_msb;
	u16 pos_reg_lsb;
	/*suppose this property belongs to some module */
	struct b52_sensor_regs id;
	struct b52_sensor_regs init;
	struct vcm_ops  *ops;
};
struct b52_vcm_ops {
	int (*g_vcm_info)(struct v4l2_subdev *, struct vcm_type **);
};
struct vcm_subdev {
	struct v4l2_subdev	subdev;
	struct device		*dev;
	struct media_pad	pad;
	/* Controls */
	struct vcm_ctrls vcm_ctrl;
	struct vcm_type *b52_vcm_type[10];
	struct vcm_type *current_type;
	struct b52_vcm_ops ops;
};

#define to_b52_vcm(subdev) container_of((subdev), struct vcm_subdev, subdev);
#define b52_vcm_call(s, f, args...) \
	(!(s) ? -ENODEV : (((s)->ops.f) ? \
	(s)->ops.f((&(s)->subdev) , ##args)   \
	: -ENOIOCTLCMD))
int vcm_subdev_create(struct device *parent,
		const char *name, int id, void *pdata);

#endif
