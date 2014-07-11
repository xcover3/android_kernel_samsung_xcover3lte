#ifndef _MAP_CAMERA_H
#define _MAP_CAMERA_H

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#include <media/b52socisp/b52socisp-pdev.h>

#define ISPSD_PAD_MAX	15
#define GID_ISP_SUBDEV	0xBEEFCAFE

enum isp_gdev_type {
	ISP_GDEV_NONE	= 0,
	ISP_GDEV_BLOCK,
	ISP_GDEV_I2C,
	ISP_GDEV_SUBDEV,	/* Means this device is a host subdev */
};

enum isp_subdev_type {
	ISD_TYPE_NONE	= 0,
	ISD_TYPE_NORMAL,
	ISD_TYPE_DMA_OUT,
	ISD_TYPE_DMA_IN,
	ISD_TYPE_SENSOR,
};

struct isp_subdev {
	struct v4l2_subdev	subdev;
	struct list_head	hook;
	struct media_pad	pads[ISPSD_PAD_MAX];
	__u8			pads_cnt;
	__u8			single;		/* SINGLE/COMBINED? */
	__u8			sd_type;	/* NORMAL / DMA_IN / DMA_OUT */
	__u8			sd_code;	/* unique id code */
	struct list_head	gdev_list;	/* guest device list */
	struct list_head	hdev;

	struct v4l2_mbus_framefmt	fmt_pad[ISPSD_PAD_MAX];
	struct v4l2_rect		crop_pad[ISPSD_PAD_MAX];
	struct v4l2_ctrl_handler	ctrl_handler;

	/* point to the driver specific structure that contains this struct */
	void			*drv_priv;
	struct isp_build	*build;

	struct isp_subdev_ops	*ops;
};

struct isp_subdev_ops {
	/* Called before register to build */
	int	(*add)(struct isp_subdev *ispsd);
	/* Called after remove from build */
	void	(*remove)(struct isp_subdev *ispsd);
	/* Called before add to pipeline */
	int	(*open)(struct isp_subdev *ispsd);
	/* Called after remove from pipeline */
	void	(*close)(struct isp_subdev *ispsd);
};

struct isp_dev_ptr {
	struct list_head	hook;
	void			*ptr;
	enum isp_gdev_type	type;
};

int isp_subdev_add_guest(struct isp_subdev *ispsd,
				void *guest, enum isp_gdev_type type);
static inline struct isp_block *isp_sd2blk(struct isp_subdev *sd)
{
	struct isp_dev_ptr *desc;
	if (!sd->single)
		return NULL;
	desc = list_first_entry(&(sd->gdev_list), struct isp_dev_ptr, hook);
	if (desc->type != ISP_GDEV_BLOCK)
		return NULL;
	return desc->ptr;
};

static inline struct isp_subdev *me_to_ispsd(struct media_entity *me)
{
	struct v4l2_subdev *sd;

	if (media_entity_type(me) == MEDIA_ENT_T_V4L2_SUBDEV)
		sd = media_entity_to_v4l2_subdev(me);
	else
		return NULL;
	if (sd->grp_id == GID_ISP_SUBDEV)
		return v4l2_get_subdev_hostdata(sd);
	else
		return NULL;
};

struct isp_subdev *isp_subdev_find(struct isp_build *build, int sd_code);
int isp_subdev_register(struct isp_subdev *ispsd, struct isp_build *build);
void isp_subdev_unregister(struct isp_subdev *ispsd);
int isp_entity_get_fmt(struct media_entity *me, struct v4l2_subdev_format *fmt);
int isp_link_try_set_fmt(struct media_link *link,
				struct v4l2_mbus_framefmt *fmt, __u32 which);
int isp_entity_try_set_fmt(struct media_entity *me,
				struct v4l2_subdev_format *fmt);
static inline void isp_subdev_reset_format(struct isp_subdev *ispsd)
{
	memset(ispsd->fmt_pad, 0,
		ISPSD_PAD_MAX * sizeof(struct v4l2_mbus_framefmt));
}

struct isp_build {
	struct v4l2_device	v4l2_dev;
	struct media_device	media_dev;
	struct device		*dev;
	struct mutex		graph_mutex;
	const char		*name;

	/* MAP related */
	struct list_head	resrc_pool;
	struct list_head	pipeline_pool;
	int			pipeline_cnt;
	struct list_head	ispsd_list;
	struct list_head	event_pool;

	void			*plat_priv;

	int	(*add_vdev)(struct isp_build *build,
				struct isp_subdev *ispsd);
	void	(*close_vdev)(struct isp_build *build);
};

void isp_build_exit(struct isp_build *mngr);
int isp_build_init(struct isp_build *mngr);
int isp_build_attach_ispsd(struct isp_build *mngr);

/*
 * isp_event is used to provide a method for isp-subdevs to sync action between
 * each other, can be useful for shared IRQ, etc.
 */
struct isp_event;
typedef int __must_check (*event_handler_t)
	(struct isp_subdev *receiver, struct isp_event *event);
struct isp_dispatch_entry {
	struct list_head	hook;
	struct isp_subdev	*ispsd;
	event_handler_t		handler;
};

/*
 * @owenr:	sender of this event
 * @type:	event type, sender must set it
 * @msg:	event message, that sender wants receiver to be aware
 * @hook:	used to mount on the event pool
 * @dispatch entry:	where receiver and its handle is saved
 */
struct isp_event {
	char			name[V4L2_SUBDEV_NAME_SIZE * 2];
	void			*owner;
	u32			type;
	void			*msg;
	struct list_head	hook;
	struct list_head	dispatch_list;
};

int isp_event_register(struct isp_build *build, const char *name);
int isp_event_unregister(struct isp_build *build, const char *name);
struct isp_event *isp_event_find(struct isp_build *build, const char *name);
int isp_event_subscribe(struct isp_event *event, struct isp_subdev *rx_sd,
			event_handler_t handler);
int isp_event_unsubscribe(struct isp_event *event, struct isp_subdev *rx_sd);
int isp_event_dispatch(struct isp_event *event);

#define subdev_has_fn(sd, o, f) ((sd) && (sd->ops) && (sd->ops->o) && \
				(sd->ops->o->f))

#endif
