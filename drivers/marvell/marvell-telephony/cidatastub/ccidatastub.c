/*

 *(C) Copyright 2007 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All Rights Reserved
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/aio.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>

#include <linux/uaccess.h>
#include <linux/version.h>

#include <linux/platform_device.h>
#include <linux/skbuff.h>
#include "common_datastub.h"
#include "shm_share.h"
#include "data_channel_kernel.h"
#include "msocket.h"

static const char *const ccidatastub_name = "ccidatastub";
int dataSvgHandle;
DATAHANDLELIST *hCsdataList = NULL;

int csdChannelInited;
int imsChannelInited;
DEFINE_SPINLOCK(data_handle_list_lock);

#define DPRINT(fmt, args ...)     pr_info(fmt, ## args)
#define DBGMSG(fmt, args ...)     pr_debug("CIN: " fmt, ## args)
#define ENTER()         pr_debug("CIN: ENTER %s\n", __func__)
#define LEAVE()         pr_debug("CIN: LEAVE %s\n", __func__)
#define FUNC_EXIT()     pr_debug("CIN: EXIT %s\n", __func__)

static void remove_handle_by_cid(DATAHANDLELIST **plist, unsigned char cid)
{
	DATAHANDLELIST *pCurrNode, *pPrevNode;

	pPrevNode = NULL;	/* pPrevNode set to NULL */
	pCurrNode = *plist;	/* pCurrNode set to header */

	ENTER();
	/* search list to find which cid equals */
	while (pCurrNode && pCurrNode->handle.m_cid != cid) {
		pPrevNode = pCurrNode;
		pCurrNode = pCurrNode->next;
	}

	if (pCurrNode) {	/* if found it */
		if (!pPrevNode) {	/* first node */
			*plist = pCurrNode->next;
		} else {	/* in the middle */
			pPrevNode->next = pCurrNode->next;
		}

		/* in any case, free node memory */
		kfree(pCurrNode);
	}
	/* else nothing to do */
	LEAVE();
}

static void add_to_handle_list(DATAHANDLELIST **plist,
			       DATAHANDLELIST *newdrvnode)
{
	/* we add the new node before header */
	newdrvnode->next = *plist;
	*plist = newdrvnode;
}

DATAHANDLELIST *search_handlelist_by_cid(DATAHANDLELIST *pHeader,
					 unsigned char cid)
{
	DATAHANDLELIST *pCurrNode = pHeader;

	while (pCurrNode && pCurrNode->handle.m_cid != cid)
		pCurrNode = pCurrNode->next;

	return pCurrNode;
}

static int ccidatastub_open(struct inode *inode, struct file *filp)
{
	ENTER();
	return 0;
	LEAVE();
}

static long ccidatastub_ioctl(struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	DATAHANDLELIST *pNode, **plist;
	struct datahandle_obj dataHandle;
	unsigned char cid;

	ENTER();
	if (_IOC_TYPE(cmd) != CCIDATASTUB_IOC_MAGIC) {
		DBGMSG("ccidatastub_ioctl: cci magic number is wrong!\n");
		return -ENOTTY;
	}

	DBGMSG("ccidatastub_ioctl,cmd=0x%x\n", cmd);
	switch (cmd) {
	case CCIDATASTUB_DATAHANDLE:
		if (copy_from_user(&dataHandle, (struct datahandle_obj *)arg,
				   sizeof(dataHandle)))
			return -EFAULT;
		DPRINT("CCIDATASTUB_DATAHANDLE: cid =%d, type =%d\n",
		       dataHandle.m_cid, dataHandle.m_connType);
		if (dataHandle.m_connType == CI_DAT_CONNTYPE_PS)
			break;

		spin_lock_irq(&data_handle_list_lock);

		plist = &hCsdataList;

		if (!search_handlelist_by_cid(*plist, dataHandle.m_cid)) {
			pNode = kmalloc(sizeof(*pNode), GFP_ATOMIC);
			if (!pNode) {
				spin_unlock_irq(&data_handle_list_lock);
				return -ENOMEM;
			}
			pNode->handle = dataHandle;
			pNode->next = NULL;
			add_to_handle_list(plist, pNode);
		} else {
			DPRINT("CCIDATASTUB_DATAHANDLE: cid already exist\n");
		}

		spin_unlock_irq(&data_handle_list_lock);

		break;

	case CCIDATASTUB_DATASVGHANDLE:
		dataSvgHandle = arg;
		DBGMSG("ccidatastub_ioctl,dataSvgHandle=0x%x\n", dataSvgHandle);
		break;

	case CCIDATASTUB_CS_CHNOK:
		cid = (unsigned char)arg;
		DPRINT("CCIDATASTUB_CS_CHNOK: cid =%d\n", cid);

		spin_lock_irq(&data_handle_list_lock);
		remove_handle_by_cid(&hCsdataList, cid);
		spin_unlock_irq(&data_handle_list_lock);
		break;
	}
	LEAVE();
	return 0;
}

#ifdef CONFIG_COMPAT
static long compat_ccidatastub_ioctl(struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	return ccidatastub_ioctl(filp, cmd, arg);
}
#endif

static const struct file_operations ccidatastub_fops = {
	.open = ccidatastub_open,
	.unlocked_ioctl = ccidatastub_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ccidatastub_ioctl,
#endif
	.owner = THIS_MODULE
};

static struct miscdevice ccidatastub_miscdev = {
	MISC_DYNAMIC_MINOR,
	"ccidatastub",
	&ccidatastub_fops,
};

#ifdef CONFIG_SSIPC_SUPPORT
static int set_csd_init_cfg(void)
{
	struct datahandle_obj dataHandle;
	DATAHANDLELIST *pNode, **plist;
	spin_lock_irq(&data_handle_list_lock);
	plist = &hCsdataList;
	dataHandle.m_connType = CI_DAT_CONNTYPE_CS;
	dataHandle.connectionType = ATCI_LOCAL;
	dataHandle.m_cid = 0x1;

	if (!search_handlelist_by_cid(*plist, dataHandle.m_cid)) {
		pNode = kmalloc(sizeof(*pNode), GFP_ATOMIC);
		if (!pNode) {
			spin_unlock_irq(&data_handle_list_lock);
			return -ENOMEM;
		}
		pNode->handle = dataHandle;
		pNode->next = NULL;
		add_to_handle_list(plist, pNode);
	} else
		DPRINT("%s: cid already exist\n", __func__);

	spin_unlock_irq(&data_handle_list_lock);
	dataSvgHandle = 0x10000008;
	DPRINT("%s done\n", __func__);
	return 0;
}
#endif

static int ccidatastub_ready_cb(struct notifier_block *nb,
			unsigned long action, void *data)
{
	DPRINT("%s executed\n", __func__);

#ifdef CONFIG_SSIPC_SUPPORT
	set_csd_init_cfg();
#endif

	InitCsdChannel();
	InitImsChannel();
	return 0;
}

static struct notifier_block cp_state_notifier = {
	.notifier_call = ccidatastub_ready_cb,
};

static int ccidatastub_probe(struct platform_device *dev)
{
	int ret;

	ENTER();

	ret = misc_register(&ccidatastub_miscdev);
	if (ret)
		pr_err("register misc device error\n");

	register_first_cp_synced(&cp_state_notifier);

	LEAVE();

	return ret;
}

static int ccidatastub_remove(struct platform_device *dev)
{
	ENTER();
	DeInitCsdChannel();
	DeInitImsChannel();
	misc_deregister(&ccidatastub_miscdev);
	LEAVE();
	return 0;
}

static void ccidatastub_dev_release(struct device *dev)
{
	return;
}

static struct platform_device ccidatastub_device = {
	.name = "ccidatastub",
	.id = 0,
	.dev = {
		.release = ccidatastub_dev_release,
	},
};

static struct platform_driver ccidatastub_driver = {
	.probe = ccidatastub_probe,
	.remove = ccidatastub_remove,
	.driver = {
		.name = "ccidatastub",
	}
};

static int __init ccidatastub_init(void)
{
	int ret;

	ret = platform_device_register(&ccidatastub_device);
	if (!ret) {
		ret = platform_driver_register(&ccidatastub_driver);
		if (ret)
			platform_device_unregister(&ccidatastub_device);
	} else {
		DPRINT("Cannot register CCIDATASTUB platform device\n");
	}

	return ret;
}

static void __exit ccidatastub_exit(void)
{
	platform_driver_unregister(&ccidatastub_driver);
	platform_device_unregister(&ccidatastub_device);
}

module_init(ccidatastub_init);
module_exit(ccidatastub_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell CI data stub.");
