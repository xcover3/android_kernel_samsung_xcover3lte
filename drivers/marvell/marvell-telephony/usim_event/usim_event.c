/******************************************************************************
*(C) Copyright 2011 Marvell International Ltd.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as published
    by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/edge_wakeup_mmp.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <stddef.h>

enum {
	GE_DEBUG_DEBUG = 1U << 0,
	GE_DEBUG_INFO = 1U << 1,
	GE_DEBUG_ERROR = 1U << 2,
};

#define usim_event_debug(mask, x...) \
	do { \
		if (usim_debug_mask & mask) \
			pr_info(x); \
	} while (0)

static uint32_t usim_debug_mask = GE_DEBUG_INFO | GE_DEBUG_ERROR;
module_param_named(debug_mask, usim_debug_mask, uint, S_IWUSR | S_IRUGO);

static uint32_t usim_wakeup_timeout_in_ms = 3000;
module_param_named(wakeup_timeout_in_ms, usim_wakeup_timeout_in_ms, uint,
		   S_IWUSR | S_IRUGO);

static uint32_t usim_delay_time_in_jiffies = HZ / 4;
module_param_named(delay_time_in_jiffies, usim_delay_time_in_jiffies, uint,
		   S_IWUSR | S_IRUGO);

static struct class *usim_event_class;

struct usim_event_device {
	char name[16];

	struct device *dev;
	int state;

	int gpio;
	int irq;

	struct delayed_work work;
	struct workqueue_struct *wq;

	spinlock_t lock;
};

#define USIM_DEVICE_NUM 3
static struct usim_event_device *guedevice[USIM_DEVICE_NUM];

static void report_usim_event(struct usim_event_device *uedev, int state)
{
	char name_buf[50];
	char *env[3];

	snprintf(name_buf, sizeof(name_buf), "USIM_NAME=%s", uedev->name);

	env[0] = name_buf;
	if (strcmp("usimTray", uedev->name) == 0)
		env[1] = state ? "USIM_EVENT=trayPlugin" :
			"USIM_EVENT=trayPlugout";
	else
		env[1] = state ? "USIM_EVENT=plugin" : "USIM_EVENT=plugout";
	env[2] = NULL;

	kobject_uevent_env(&uedev->dev->kobj, KOBJ_CHANGE, env);
	usim_event_debug(GE_DEBUG_INFO, "%s: usim uevent [%s %s] is sent\n",
			 __func__, env[0], env[1]);
}

static void usim_event_work(struct work_struct *work)
{
	struct usim_event_device *uedev =
	    container_of(to_delayed_work(work), struct usim_event_device, work);
	int state = !!gpio_get_value(uedev->gpio);

	if (state != uedev->state) {
		uedev->state = state;
		report_usim_event(uedev, state);
	}
}

static void usim_event_wakeup(int gpio, void *data)
{
	unsigned long dev_num = (unsigned long)data;
	pm_wakeup_event(guedevice[dev_num]->dev, usim_wakeup_timeout_in_ms);
}

irqreturn_t usim_event_handler(int irq, void *dev_id)
{
	struct usim_event_device *uedev = (struct usim_event_device *)dev_id;
	unsigned long flags = 0;
	spin_lock_irqsave(&uedev->lock, flags);
	queue_delayed_work(uedev->wq, &uedev->work, usim_delay_time_in_jiffies);
	spin_unlock_irqrestore(&uedev->lock, flags);

	usim_event_debug(GE_DEBUG_INFO,
	"%s: gpio event irq received. irq[%d]\n", __func__, irq);
	return IRQ_HANDLED;
}

static ssize_t usim_send_event(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct usim_event_device *uedev =
	    (struct usim_event_device *)dev_get_drvdata(dev);

	unsigned long state;
	int ret = 0;

	ret = kstrtoul(buf, 10, &state);
	report_usim_event(uedev, (int)state);
	if (ret)
		return ret;
	else
		return count;
}

static ssize_t usim_show_state(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usim_event_device *uedev =
	    (struct usim_event_device *)dev_get_drvdata(dev);

	int len;
	len = sprintf(buf, "%d\n", uedev->state);
	return len;
}

static DEVICE_ATTR(send_event, 0222, NULL, usim_send_event);
static DEVICE_ATTR(state, 0444, usim_show_state, NULL);
static struct device_attribute *usim_event_attr[] = {
	&dev_attr_send_event,
	&dev_attr_state,
	NULL
};

static int usim_event_create_sys_device(struct device *dev)
{
	int ret = 0;
	struct device_attribute **attr = usim_event_attr;

	for (; *attr; ++attr) {
		ret = device_create_file(dev, *attr);
		if (ret)
			break;
	}

	if (ret) {
		for (--attr; attr >= usim_event_attr; --attr)
			device_remove_file(dev, *attr);
	}
	return 0;
}

static int usim_event_remove_sys_device(struct device *dev)
{
	struct device_attribute **attr = usim_event_attr;

	for (; *attr; ++attr)
		device_remove_file(dev, *attr);

	return 0;
}

static int init_device(struct platform_device *pdev, int dev_num)
{
	int ret = -1;
	unsigned long tmp;
	struct usim_event_device *uedevice = guedevice[dev_num];

	if (dev_num == USIM_DEVICE_NUM - 1)
		snprintf(uedevice->name, sizeof(uedevice->name) - 1,
			"usimTray");
	else
		snprintf(uedevice->name, sizeof(uedevice->name) - 1,
			"usim%d", dev_num);
	spin_lock_init(&uedevice->lock);

	uedevice->dev = device_create(usim_event_class, NULL,
				   MKDEV(0, dev_num), NULL, uedevice->name);

	if (IS_ERR(uedevice->dev)) {
		ret = PTR_ERR(uedevice->dev);
		goto out;
	}

	dev_set_drvdata(uedevice->dev, uedevice);

	of_property_read_u32(pdev->dev.of_node,
			"edge-wakeup-gpio", &uedevice->gpio);

	if (uedevice->gpio >= 0) {
		tmp = dev_num;
		ret = request_mfp_edge_wakeup(uedevice->gpio,
					      usim_event_wakeup,
					      (void *)tmp, &pdev->dev);
		if (ret) {
			dev_err(uedevice->dev, "failed to request edge wakeup.\n");
			goto edge_wakeup;
		}
	}

	uedevice->state = !!gpio_get_value(uedevice->gpio);

	ret = usim_event_create_sys_device(uedevice->dev);
	if (ret < 0) {
		usim_event_debug(GE_DEBUG_ERROR,
				 "%s: create sys device failed!\n", __func__);
		goto destroy_device;
	}
	kobject_uevent(&(uedevice->dev->kobj), KOBJ_ADD);
	ret = gpio_request(uedevice->gpio, uedevice->name);

	gpio_direction_input(uedevice->gpio);

	uedevice->irq = gpio_to_irq(uedevice->gpio);
	ret =
	    request_irq(uedevice->irq, usim_event_handler,
			IRQF_DISABLED | IRQF_TRIGGER_RISING |
			IRQF_TRIGGER_FALLING | IRQF_NO_SUSPEND, uedevice->name,
			uedevice);
	if (ret < 0) {
		usim_event_debug(GE_DEBUG_ERROR, "%s: request irq failed!\n",
				 __func__);
		goto free_gpio;
	}

	INIT_DELAYED_WORK(&uedevice->work, usim_event_work);
	uedevice->wq = create_workqueue(uedevice->name);
	if (uedevice->wq == NULL) {
		usim_event_debug(GE_DEBUG_ERROR,
				 "%s:Can't create work queue!\n", __func__);
		ret = -ENOMEM;
		goto free_irq;
	}

	ret = 0;
	goto out;

free_irq:
	free_irq(uedevice->irq, uedevice);
free_gpio:
	gpio_free(uedevice->gpio);
destroy_device:
	device_destroy(usim_event_class, MKDEV(0, dev_num));
edge_wakeup:
	if (uedevice->gpio >= 0)
		remove_mfp_edge_wakeup(uedevice->gpio);
out:
	return ret;
}

static void deinit_device(int dev_num)
{
	struct usim_event_device *uedevice = guedevice[dev_num];
	if (uedevice->wq != NULL)
		destroy_workqueue(uedevice->wq);
	free_irq(uedevice->irq, uedevice);
	if (uedevice->gpio >= 0)
		remove_mfp_edge_wakeup(uedevice->gpio);
	gpio_free(uedevice->gpio);
	usim_event_remove_sys_device(uedevice->dev);
	device_destroy(usim_event_class, MKDEV(0, dev_num));
}

static int usim_event_init(struct platform_device *pdev)
{
	int ret;
	int dev_num = 0;

	if (of_device_is_compatible(pdev->dev.of_node, "marvell,usim1")) {
		dev_num = 0;
		usim_event_debug(GE_DEBUG_INFO, "%s: usim1\n",
			 __func__);
	} else if (of_device_is_compatible(pdev->dev.of_node,
				"marvell,usim2")) {
		dev_num = 1;
		usim_event_debug(GE_DEBUG_INFO, "%s: usim2\n",
			 __func__);
	} else if (of_device_is_compatible(pdev->dev.of_node,
				"marvell,usimTray")) {
		dev_num = 2;
		usim_event_debug(GE_DEBUG_INFO, "%s: usimTray\n",
			 __func__);
	} else {
		usim_event_debug(GE_DEBUG_ERROR, "%s: unknown device\n",
			 __func__);
		return -1;
	}
	if (!usim_event_class)
		usim_event_class = class_create(THIS_MODULE, "usim_event");


	if (IS_ERR(usim_event_class))
		return PTR_ERR(usim_event_class);
	pr_info("usim_event_init XXXXX\n");

	guedevice[dev_num] = kzalloc(sizeof(struct usim_event_device),
		GFP_KERNEL);

	ret = init_device(pdev, dev_num);
	if (ret < 0)
		goto deinit;

	return 0;

deinit:
	deinit_device(dev_num);
	class_destroy(usim_event_class);

	return -1;
}

static int usim_event_exit(struct platform_device *pdev)
{
	int i;
	for (i = 0; i < USIM_DEVICE_NUM; i++)
		deinit_device(i);
	class_destroy(usim_event_class);
	for (i = 0; i < USIM_DEVICE_NUM; i++)
		if (guedevice[i] > 0)
			kfree(guedevice[i]);

	return 0;
}

static const struct of_device_id usim_of_match[] = {
	{ .compatible = "marvell,usim1",},
	{ .compatible = "marvell,usim2",},
	{ .compatible = "marvell,usimTray",},
	{},
};
MODULE_DEVICE_TABLE(of, usim_of_match);

static struct platform_driver usim_driver = {
	.probe = usim_event_init,
	.remove = usim_event_exit,
	.driver	= {
		.name	= "usim1",
		.owner	= THIS_MODULE,
		.of_match_table = usim_of_match,
	},
};

module_platform_driver(usim_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell USIM event notify");
