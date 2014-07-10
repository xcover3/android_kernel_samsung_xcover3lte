#ifndef _B52_API_H
#define _B52_API_H

#include <linux/videodev2.h>

#define NR_METERING_WIN_WEIGHT 13
#define B52_NR_PIPELINE_MAX 2

struct b52_regval {
	__u32	reg;
	__u32	val;
};

struct b52_data_node {
	__u32	size;
	void	*buffer;
};

struct b52isp_profile {
	unsigned int profile_id;
	void	*arg;
};

struct b52isp_win {
	__s32 left;
	__s32 top;
	__s32 right;
	__s32 bottom;
};

struct b52isp_expo_metering {
	unsigned int mode;
	struct b52isp_win stat_win;
	struct v4l2_rect center_win;
	unsigned int win_weight[NR_METERING_WIN_WEIGHT];
};

struct b52isp_awb_gain {
	int write;
	unsigned int b;
	unsigned int gb;
	unsigned int gr;
	unsigned int r;
};

struct memory_sensor {
	char	name[32];
};

enum adv_dns_type {
	ADV_DNS_NONE = 0,
	ADV_DNS_Y,
	ADV_DNS_UV,
	ADV_DNS_YUV,
	ADV_DNS_MAX,
};

struct b52isp_adv_dns {
	enum adv_dns_type type;
	unsigned int times;
};

enum type_aeag {
	TYPE_3A_UNLOCK,
	TYPE_3A_LOCKED,
};

enum type_combo {
	TYPE_3D_COMBO,
	TYPE_HS_COMBO,
	TYPE_HDR_COMBO,
};

struct b52isp_path_arg {
	enum type_aeag	aeag;
	enum type_combo	combo;
	__u16		nr_frame;
	__u16		ratio_1_2;
	__u16		ratio_1_3;
	__u16		linear_yuv;
};

struct b52isp_anti_shake_arg {
	__u16		block_size;
	int			enable;
};

enum v4l2_priv_colorfx {
	V4L2_PRIV_COLORFX_NONE          = 0,
	V4L2_PRIV_COLORFX_MONO_CHROME   = 1,
	V4L2_PRIV_COLORFX_NEGATIVE      = 2,
	V4L2_PRIV_COLORFX_SEPIA         = 3,
	V4L2_PRIV_COLORFX_SKETCH        = 4,
	V4L2_PRIV_COLORFX_WATER_COLOR   = 5,
	V4L2_PRIV_COLORFX_INK           = 6,
	V4L2_PRIV_COLORFX_CARTOON       = 7,
	V4L2_PRIV_COLORFX_COLOR_INK     = 8,
	V4L2_PRIV_COLORFX_AQUA          = 9,
	V4L2_PRIV_COLORFX_BLACK_BOARD   = 10,
	V4L2_PRIV_COLORFX_WHITE_BOARD   = 11,
	V4L2_PRIV_COLORFX_POSTER        = 12,
	V4L2_PRIV_COLORFX_SOLARIZATION  = 13,
	V4L2_PRIV_COLORFX_MAX,
};

#define CID_AF_SNAPSHOT	1
#define CID_AF_CONTINUOUS	2

/*
 * the auto frame rate control.
 * if set it to enable, the frame rate will drop to increase exposure.
 */
#define CID_AUTO_FRAME_RATE_DISABLE   0
#define CID_AUTO_FRAME_RATE_ENABLE    1

/*
 * the range for auto frame rate
 */
#define CID_AFR_MIN_FPS_MIN   5
#define CID_AFR_MIN_FPS_MAX   30

/*
 * save and restore min fps for auto frame rate
 */
#define CID_AFR_SAVE_MIN_FPS      0
#define CID_AFR_RESTORE_MIN_FPS   1

/*
 * If enable AF 5x5 windown, the focus is based on central part.
 */
#define CID_AF_5X5_WIN_DISABLE    0
#define CID_AF_5X5_WIN_ENABLE     1

#define B52_IDI_NAME		"b52isd-IDI"
#define B52_PATH_YUV_1_NAME	"b52isd-Pipeline#1"
#define B52_PATH_YUV_2_NAME	"b52isd-Pipeline#2"
#define B52_PATH_RAW_1_NAME	"b52isd-DataDump#1"
#define B52_PATH_RAW_2_NAME	"b52isd-DataDump#2"
#define B52_PATH_M2M_1_NAME	"b52isd-MemorySensor#1"
#define B52_PATH_M2M_2_NAME	"b52isd-MemorySensor#2"
#define B52_PATH_COMBINE_NAME	"b52isd-combine"
#define B52_OUTPUT_A_NAME	"b52isd-AXI1-write1"
#define B52_OUTPUT_B_NAME	"b52isd-AXI1-write2"
#define B52_OUTPUT_C_NAME	"b52isd-AXI2-write1"
#define B52_OUTPUT_D_NAME	"b52isd-AXI2-write2"
#define B52_OUTPUT_E_NAME	"b52isd-AXI3-write1"
#define B52_OUTPUT_F_NAME	"b52isd-AXI3-write2"
#define B52_INPUT_A_NAME	"b52isd-AXI1-read1"
#define B52_INPUT_B_NAME	"b52isd-AXI2-read1"
#define B52_INPUT_C_NAME	"b52isd-AXI3-read1"

/* the specific controls */
#define V4L2_CID_PRIVATE_AF_MODE \
	(V4L2_CID_CAMERA_CLASS_BASE + 0x1000)
#define V4L2_CID_PRIVATE_COLORFX \
	(V4L2_CID_CAMERA_CLASS_BASE + 0x1001)
#define V4L2_CID_PRIVATE_AUTO_FRAME_RATE \
	(V4L2_CID_CAMERA_CLASS_BASE + 0x1002)
#define V4L2_CID_PRIVATE_AFR_MIN_FPS \
	(V4L2_CID_CAMERA_CLASS_BASE + 0x1003)
#define V4L2_CID_PRIVATE_AFR_SR_MIN_FPS \
	(V4L2_CID_CAMERA_CLASS_BASE + 0x1004)
#define V4L2_CID_PRIVATE_AF_5X5_WIN \
	(V4L2_CID_CAMERA_CLASS_BASE + 0x1005)


#define V4L2_PLANE_SIGNATURE_PIPELINE_META	\
	v4l2_fourcc('M', 'E', 'T', 'A')
#define V4L2_PLANE_SIGNATURE_PIPELINE_INFO	\
	v4l2_fourcc('P', 'P', 'I', 'F')



#define VIDIOC_PRIVATE_B52ISP_TOPOLOGY_SNAPSHOT \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 0, struct b52isp_profile)
#define VIDIOC_PRIVATE_B52ISP_CONFIG_AF_WINDONW \
	_IOW('V', BASE_VIDIOC_PRIVATE + 1, struct v4l2_rect)
#define VIDIOC_PRIVATE_B52ISP_CONFIG_EXPO_METERING_MODE \
	_IOW('V', BASE_VIDIOC_PRIVATE + 2, struct b52isp_expo_metering)
#define VIDIOC_PRIVATE_B52ISP_CONFIG_EXPO_METERING_ROI \
	_IOW('V', BASE_VIDIOC_PRIVATE + 3, struct b52isp_win)
#define VIDIOC_PRIVATE_B52ISP_DOWNLOAD_CTDATA \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 4, struct b52_data_node)
#define VIDIOC_PRIVATE_B52ISP_UPLOAD_CTDATA \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 5, struct b52_data_node)
#define VIDIOC_PRIVATE_B52ISP_CONFIG_AWB_GAIN \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 6, struct b52isp_awb_gain)
#define VIDIOC_PRIVATE_B52ISP_CONFIG_MEMORY_SENSOR \
	_IOW('V', BASE_VIDIOC_PRIVATE + 7, struct memory_sensor)
#define VIDIOC_PRIVATE_B52ISP_CONFIG_ADV_DNS \
	_IOW('V', BASE_VIDIOC_PRIVATE + 8, struct b52isp_adv_dns)
#define VIDIOC_PRIVATE_B52ISP_SET_PATH_ARG \
	_IOW('V', BASE_VIDIOC_PRIVATE + 9, struct b52isp_path_arg)
#define VIDIOC_PRIVATE_B52ISP_ANTI_SHAKE\
	_IOW('V', BASE_VIDIOC_PRIVATE + 10, struct b52isp_anti_shake_arg)
#endif /* _B52_API_H */
