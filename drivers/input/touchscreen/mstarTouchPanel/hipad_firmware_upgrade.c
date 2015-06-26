#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/wakelock.h>
#include <linux/mutex.h>
#include <linux/i2c.h>

#include "hipad_firmware_upgrade.h"

static struct firmware_miscdevice *fw_mdev_list[MDEV_NUMS];

static int fw_mdev_open(struct inode *inode, struct file *file)
{
	int minor = iminor(inode);
	struct firmware_miscdevice *dev = fw_mdev_list[minor];

	fw_mdev_debug("minor = %d, start\n", minor);

	if (!dev) {
		fw_mdev_err("dev = NULL\n");
		return -ENOENT;
	}

	dev->fw_count = 0;
	dev->is_prepare = 1;
	file->private_data = dev;

	fw_mdev_debug("minor = %d, end\n", minor);
	return 0;
}

static int fw_mdev_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	fw_mdev_debug("\n");
	return 0;
}

static ssize_t fw_mdev_read(struct file *file, char __user *buff,
	size_t size, loff_t *offset)
{
	return -EIO;
}

static ssize_t fw_mdev_write(struct file *file, const char __user *buff,
	size_t size, loff_t *offset)
{
	struct firmware_miscdevice *mdev = file->private_data;

	fw_mdev_debug("size = %d\n", size);

	if (mdev == NULL || mdev->firmware_upgrade == NULL) {
		fw_mdev_err("firemware_upgrade == NULL\n");
		return -EINVAL;
	}

	mutex_lock(&mdev->lock);
	wake_lock(&mdev->wake_lock);

	mdev->firmware_upgrade(mdev, buff, size);

	wake_unlock(&mdev->wake_lock);
	mutex_unlock(&mdev->lock);

	return size;
}

static ssize_t fw_mdev_attr_firmware_id_show(struct device *device,
	struct device_attribute *attr, char *buff)
{
	struct firmware_miscdevice *mdev = dev_get_drvdata(device);
	int size;

	if (!mdev) {
		fw_mdev_err("mdev is null\n");
		return -EINVAL;
	}

	mutex_lock(&mdev->lock);
	wake_lock(&mdev->wake_lock);
	if (!mdev->get_firmware_id || !mdev->client) {
		fw_mdev_err("!mdev->get_firmware_id||!mdev->client\n");
		wake_unlock(&mdev->wake_lock);
		mutex_unlock(&mdev->lock);
		return -EINVAL;
	}

	size = mdev->get_firmware_id(mdev, buff, PAGE_SIZE);

	wake_unlock(&mdev->wake_lock);
	mutex_unlock(&mdev->lock);

	return size;
}

static ssize_t fw_mdev_attr_firmware_size_store(struct device *device,
	struct device_attribute *attr, const char *buff, size_t size)
{
	unsigned long val;
	struct firmware_miscdevice *mdev = dev_get_drvdata(device);

	if (!mdev) {
		fw_mdev_err("mdev =  NULL\n");
		return -EINVAL;
	}

	mdev->fw_size = kstrtoul(buff, 10, &val);

	return size;
}

static ssize_t fw_mdev_attr_firmware_size_show(struct device *device,
	struct device_attribute *attr, char *buff)
{
	struct firmware_miscdevice *mdev = dev_get_drvdata(device);

	if (!mdev) {
		fw_mdev_err("mdev =  NULL\n");
		return -EINVAL;
	}

	return sprintf(buff, "%d", mdev->fw_size);
}

static struct device_attribute fw_mdev_device_attrs[] = {
	__ATTR(firmware_id, S_IRUSR, fw_mdev_attr_firmware_id_show, NULL),
	__ATTR(firmware_size, S_IRUSR|S_IWUSR, fw_mdev_attr_firmware_size_show,
		fw_mdev_attr_firmware_size_store),
};

int fw_mdev_master_recv_from_i2c(struct i2c_client *client, short addr,
	void *buff, size_t size)
{
	int ret;
	struct i2c_msg msg = {
		.addr = addr,
		.flags = (client->flags & I2C_M_TEN) | I2C_M_RD,
		.len = size,
		.buf = buff
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret == 1)
		return size;

	return likely(ret < 0) ? ret : -EFAULT;
}
EXPORT_SYMBOL_GPL(fw_mdev_master_recv_from_i2c);

ssize_t fw_mdev_read_data(struct firmware_miscdevice *mdev, u8 addr,
	void *buff, size_t size)
{
	struct i2c_client *client = get_client_from_mdev(mdev);
	return fw_mdev_master_recv_from_i2c(client, addr, buff, size);
}
EXPORT_SYMBOL_GPL(fw_mdev_read_data);

int fw_mdev_master_send_to_i2c(struct i2c_client *client, short addr,
	const void *buff, size_t size)
{
	int ret;
	struct i2c_msg msg = {
		.addr = addr,
		.flags = client->flags & I2C_M_TEN,
		.len = size,
		.buf = (__u8 *)buff
	};

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret == 1)
		return size;

	return likely(ret < 0) ? ret : -EFAULT;
}
EXPORT_SYMBOL_GPL(fw_mdev_master_send_to_i2c);

ssize_t fw_mdev_write_data(struct firmware_miscdevice *mdev, u8 addr,
	void *buff, size_t size)
{
	struct i2c_client *client = get_client_from_mdev(mdev);
	return fw_mdev_master_send_to_i2c(client, addr, buff, size);
}
EXPORT_SYMBOL_GPL(fw_mdev_write_data);


static const struct file_operations fw_mdev_fops = {
	.owner		= THIS_MODULE,
	.write		= fw_mdev_write,
	.read		= fw_mdev_read,
	.open		= fw_mdev_open,
	.release	= fw_mdev_release,
};

static int fw_misc_find_minor(void)
{
	int minor;

	for (minor = 0; minor < ARRAY_SIZE(fw_mdev_list); minor++) {
		if (fw_mdev_list[minor] == NULL)
			return minor;
	}

	return -EBUSY;
}

int fw_mdev_register(struct firmware_miscdevice *mdev)
{
	int ret, i;
	int minor;

	if (!mdev || !mdev->firmware_upgrade) {
		fw_mdev_err("Implement firmware_upgrade()\n");
		return -EINVAL;
	}

	minor = fw_misc_find_minor();
	mdev->dev.fops = &fw_mdev_fops;
	ret = misc_register(&mdev->dev);
	if (ret < 0) {
		fw_mdev_err("ret = %d\n", ret);
		return ret;
	}

	mutex_init(&mdev->lock);
	wake_lock_init(&mdev->wake_lock,
		WAKE_LOCK_SUSPEND, mdev->name);
	mdev->minor = minor;
	fw_mdev_list[mdev->minor] = mdev;

	dev_set_drvdata(mdev->dev.this_device, mdev);

	for (i = 0; i < ARRAY_SIZE(fw_mdev_device_attrs); i++) {
		ret = device_create_file(mdev->dev.this_device,
				&fw_mdev_device_attrs[i]);
		if (ret)
			goto err_create_file;
	}

	return ret;

err_create_file:
	while (--i >= 0)
		device_remove_file(mdev->dev.this_device,
				&fw_mdev_device_attrs[i]);

	fw_mdev_list[mdev->minor] = NULL;
	wake_lock_destroy(&mdev->wake_lock);
	mutex_destroy(&mdev->lock);
	misc_deregister(&mdev->dev);
	return ret;
}
EXPORT_SYMBOL(fw_mdev_register);

void fw_mdev_unregister(struct firmware_miscdevice *mdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fw_mdev_device_attrs); i++) {
		device_remove_file(mdev->dev.this_device,
				&fw_mdev_device_attrs[i]);
	}

	fw_mdev_list[mdev->minor] = NULL;
	wake_lock_destroy(&mdev->wake_lock);
	mutex_destroy(&mdev->lock);
	misc_deregister(&mdev->dev);
}
EXPORT_SYMBOL(fw_mdev_unregister);

static int __init fw_mdev_init(void)
{
	pr_info("%s: enter\n", __func__);
	return 0;
}
static void __exit fw_mdev_exit(void)
{
	pr_info(KERN_INFO "%s: exit\n", __func__);
}

module_init(fw_mdev_init);
module_exit(fw_mdev_exit);

MODULE_DESCRIPTION("Hipad Touchscreen Firmware Upgrade");
MODULE_AUTHOR("Liao Ye<cjok.liao@gmail.com>");
MODULE_LICENSE("GPL");
