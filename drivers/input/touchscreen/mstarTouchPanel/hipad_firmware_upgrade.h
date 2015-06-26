#ifndef _TS_FIRMWARE_H_
#define _TS_FIRMWARE_H_

#define FW_MDEV_DEBUG
#ifdef FW_MDEV_DEBUG
#define fw_mdev_debug(fmt, arg...)		\
	pr_err(KERN_ERR "[fm_mdev]%s@%d " fmt, __func__, __LINE__, ##arg)
#define fw_mdev_err(fmt, arg...)		\
	pr_err(KERN_ERR "[fm_mdev]%s@%d " fmt, __func__, __LINE__, ##arg)
#else
#define fw_mdev_debug(fmt, arg...)	do {} while (0)
#define fw_mdev_err(fmt, arg...)	do {} while (0)
#endif

#define MDEV_NUMS	32

struct firmware_miscdevice {
	void *private_data;

	int minor;
	struct miscdevice dev;
	const char *name;
	int fw_count;
	int fw_size;
	int is_prepare;
	struct i2c_client *client;

	struct wake_lock wake_lock;
	struct mutex lock;

	int (*firmware_upgrade)(struct firmware_miscdevice *mdev,
		const char __user *fw_buf, int size);
	int (*get_firmware_id)(struct firmware_miscdevice *mdev,
		char *buff, size_t size);

	ssize_t (*read_data)(struct firmware_miscdevice *mdev,
		u8 addr, void *buff, size_t size);
	ssize_t (*write_data)(struct firmware_miscdevice *mdev,
		u8 addr, void *buff, size_t size);
};

int fw_mdev_register(struct firmware_miscdevice *mdev);
void fw_mdev_unregister(struct firmware_miscdevice *mdev);
ssize_t fw_mdev_write_data(struct firmware_miscdevice *mdev,
	u8 addr, void *buff, size_t size);
ssize_t fw_mdev_read_data(struct firmware_miscdevice *mdev,
	u8 addr, void *buff, size_t size);

static inline void set_client_to_mdev(struct firmware_miscdevice *mdev,
	struct i2c_client *client)
{
	mdev->client = client;
}

static inline struct i2c_client *
	get_client_from_mdev(struct firmware_miscdevice *mdev)
{
	return mdev->client;
}

#endif
