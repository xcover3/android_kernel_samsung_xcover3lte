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
#include <linux/netdevice.h>	/* dev_kfree_skb_any */
#include <linux/ip.h>
#include <linux/debugfs.h>
#include <linux/platform_device.h>

#include "common_datastub.h"
#include "data_path.h"
#include "psd_data_channel.h"
#include "tel_trace.h"
#include "debugfs.h"

#define PSDATASTUB_IOC_MAGIC 'P'
#define PSDATASTUB_GCFDATA _IOW(PSDATASTUB_IOC_MAGIC, 1, int)
#define PSDATASTUB_TOGGLE_DATA_ENABLE_DISABLE _IOW(PSDATASTUB_IOC_MAGIC, 2, int)

#define NETWORK_EMBMS_CID    0xFF

#define IPV4_ACK_LENGTH_LIMIT 96
#define IPV6_ACK_LENGTH_LIMIT 128

struct pduhdr {
	__be16 length;
	__u8 offset;
	__u8 reserved;
	int cid;
} __packed;

struct data_path *psd_dp;

static int embms_cid = 7;

static struct psd_user __rcu *psd_users[MAX_CID_NUM];

static u32 ack_opt = true;
static u32 data_drop = true;
static u32 ndev_fc;

static bool data_enabled = true;

int psd_register(const struct psd_user *user, int cid)
{
	return cmpxchg(&psd_users[cid], NULL, user) == NULL ? 0 : -EEXIST;
}
EXPORT_SYMBOL(psd_register);


int psd_unregister(const struct psd_user *user, int cid)
{
	int ret;
	ret =  cmpxchg(&psd_users[cid], user, NULL) == user ? 0 : -ENOENT;
	if (ret == 0)
		synchronize_net();
	return ret;
}
EXPORT_SYMBOL(psd_unregister);

void set_embms_cid(int cid)
{
	embms_cid = cid;
}
EXPORT_SYMBOL(set_embms_cid);

unsigned short psd_select_queue(struct sk_buff *skb)
{
	struct iphdr *iph;
	bool is_ack;

	if (!ack_opt)
		return PSD_QUEUE_HIGH;

	iph = ip_hdr(skb);
	is_ack = iph &&
		((iph->version == 4 && skb->len <= IPV4_ACK_LENGTH_LIMIT) ||
		(iph->version == 6 && skb->len <= IPV6_ACK_LENGTH_LIMIT));
	return is_ack ? PSD_QUEUE_HIGH : PSD_QUEUE_DEFAULT;
}
EXPORT_SYMBOL(psd_select_queue);

int psd_data_tx(int cid, struct sk_buff *skb)
{
	struct pduhdr *hdr;
	struct sk_buff *skb2;
	unsigned len;
	unsigned tailpad;
	enum data_path_priority prio;

	if (!data_enabled)
		goto drop;
	/* data path is not open, drop the packet and return */
	if (!psd_dp) {
		pr_err("%s: data path is not open!\n", __func__);
		goto drop;
	}

	len = skb->len;
	if (padded_size(sizeof(*hdr) + len) >
			data_path_max_payload(psd_dp)) {
		pr_err("%s: packet too large %d\n", __func__, len);
		goto drop;
	}

	prio = skb->queue_mapping == PSD_QUEUE_HIGH
			? dp_priority_high
			: dp_priority_default;

	/*
	 * tx_q is full or link is down
	 * allow ack when queue is full
	 */
	if (unlikely((prio != dp_priority_high &&
		data_path_is_tx_q_full(psd_dp)) ||
		 !data_path_is_link_up())) {
		pr_err_ratelimited("%s: tx_q is full or link is down\n", __func__);
		/* tx q is full, should schedule tx immediately */
		if (data_path_is_link_up())
			data_path_schedule_tx(psd_dp);

		if (data_drop) {
			pr_err_ratelimited("%s: drop the packet\n", __func__);
			goto drop;
		} else {
			pr_err_ratelimited("%s: return net busy to upper layer\n",
				__func__);
			return PSD_DATA_SEND_BUSY;
		}
	}

	tailpad = padding_size(sizeof(*hdr) + len);

	if (likely(!skb_cloned(skb))) {
		int headroom = skb_headroom(skb);
		int tailroom = skb_tailroom(skb);

		/* enough room as-is? */
		if (likely(sizeof(*hdr) + tailpad <= headroom + tailroom)) {
			/* do not need to be readjusted */
			if (sizeof(*hdr) <= headroom && tailpad <= tailroom)
				goto fill;

			skb->data = memmove(skb->head + sizeof(*hdr),
					    skb->data, len);
			skb_set_tail_pointer(skb, len);
			goto fill;
		}
	}

	/* create a new skb, with the correct size (and tailpad) */
	skb2 = skb_copy_expand(skb, sizeof(*hdr), tailpad + 1, GFP_ATOMIC);
	if (skb2)
		trace_psd_xmit_skb_realloc(skb, skb2);
	if (unlikely(!skb2))
		return PSD_DATA_SEND_BUSY;
	dev_kfree_skb_any(skb);
	skb = skb2;

	/* fill out the pdu header */
fill:
	hdr = (void *)__skb_push(skb, sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	hdr->length = cpu_to_be16(len);
	hdr->cid = cid;
	memset(skb_put(skb, tailpad), 0, tailpad);

	data_path_xmit(psd_dp, skb, prio);
	return PSD_DATA_SEND_OK;
drop:
	dev_kfree_skb_any(skb);
	return PSD_DATA_SEND_DROP;

}
EXPORT_SYMBOL(psd_data_tx);

enum data_path_result psd_data_rx(unsigned char *data, unsigned int length)
{
	unsigned char *p = data;
	unsigned int remains = length;

	while (remains > 0) {
		struct psd_user *user;
		struct pduhdr	*hdr = (void *)p;
		u32				iplen, offset_len;
		u32				tailpad;
		u32				total_len;
		int				sim_id;

		total_len = be16_to_cpu(hdr->length);
		offset_len = hdr->offset;

		if (unlikely(total_len < offset_len)) {
			pr_err("%s: packet error\n", __func__);
			return dp_rx_packet_error;
		}

		iplen = total_len - offset_len;
		tailpad = padding_size(sizeof(*hdr) + iplen + offset_len);
		sim_id = (hdr->cid >> 31) & 1;
		hdr->cid &= ~(1 << 31);

		if (unlikely(remains < (iplen + offset_len
					+ sizeof(*hdr) + tailpad))) {
			pr_err("%s: packet length error\n", __func__);
			return dp_rx_packet_error;
		}

		/* offset domain data */
		p += sizeof(*hdr);
		remains -= sizeof(*hdr);

		/* ip payload */
		p += offset_len;
		remains -= offset_len;

		if (hdr->cid == NETWORK_EMBMS_CID)
			hdr->cid = embms_cid;
		if (likely(hdr->cid >= 0 && hdr->cid < MAX_CID_NUM)) {
			rcu_read_lock();
			user = rcu_dereference(psd_users[hdr->cid]);
			if (user && user->on_receive)
				user->on_receive(user->priv, p, iplen);
			rcu_read_unlock();
			if (!user)
				pr_err_ratelimited(
					"%s: no psd user for cid:%d\n",
					__func__, hdr->cid);
		} else
			pr_err_ratelimited(
				"%s: invalid cid:%d, simid:%d\n",
				__func__, hdr->cid, sim_id);

		p += iplen + tailpad;
		remains -= iplen + tailpad;
	}
	return dp_success;
}

static void psd_tx_traffic_control(bool is_throttle)
{
	int i;

	if (!ndev_fc)
		return;

	for (i = 0; i < MAX_CID_NUM; ++i) {
		struct psd_user *user;
		rcu_read_lock();
		user = rcu_dereference(psd_users[i]);
		if (user && user->on_throttle)
			user->on_throttle(user->priv, is_throttle);
		rcu_read_unlock();
	}
}

void psd_tx_stop(void)
{
	psd_tx_traffic_control(true);
}

void psd_tx_resume(void)
{
	psd_tx_traffic_control(false);
}

void psd_rx_stop(void)
{
	return;
}

void psd_link_down(void)
{
	return;
}

void psd_link_up(void)
{
	return;
}

struct data_path_callback psd_cbs = {
	.data_rx = psd_data_rx,
	.tx_stop = psd_tx_stop,
	.tx_resume = psd_tx_resume,
	.rx_stop = psd_rx_stop,
	.link_down = psd_link_down,
	.link_up = psd_link_up,
};

static struct dentry *tel_debugfs_root_dir;
static struct dentry *psd_debugfs_root_dir;

static int psd_debugfs_init(void)
{
	char buf[256];
	char path[256];

	tel_debugfs_root_dir = tel_debugfs_get();
	if (!tel_debugfs_root_dir)
		return -ENOMEM;

	psd_debugfs_root_dir = debugfs_create_dir("psd", tel_debugfs_root_dir);
	if (IS_ERR_OR_NULL(psd_debugfs_root_dir))
		goto putrootfs;

	snprintf(path, sizeof(path), "../../%s",
		dentry_path_raw(psd_dp->dentry, buf, sizeof(buf)));

	if (IS_ERR_OR_NULL(debugfs_create_symlink("dp", psd_debugfs_root_dir,
			path)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_bool("ndev_fc", S_IRUGO | S_IWUSR,
			psd_debugfs_root_dir, &ndev_fc)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_bool("data_drop", S_IRUGO | S_IWUSR,
			psd_debugfs_root_dir, &data_drop)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_bool("ack_opt", S_IRUGO | S_IWUSR,
			psd_debugfs_root_dir, &ack_opt)))
		goto error;

	return 0;

error:
	debugfs_remove_recursive(psd_debugfs_root_dir);
	psd_debugfs_root_dir = NULL;
putrootfs:
	tel_debugfs_put(tel_debugfs_root_dir);
	tel_debugfs_root_dir = NULL;
	return -1;
}

static int psd_debugfs_exit(void)
{
	debugfs_remove_recursive(psd_debugfs_root_dir);
	psd_debugfs_root_dir = NULL;
	tel_debugfs_put(tel_debugfs_root_dir);
	tel_debugfs_root_dir = NULL;
	return 0;
}

static int ps_channel_init(struct notifier_block *nb,
			unsigned long action, void *data)
{
	psd_dp = data_path_open(&psd_cbs);
	if (!psd_dp) {
		pr_err("%s: failed to open data path\n", __func__);
		return -1;
	}
	if (psd_debugfs_init() < 0)
		data_path_close(psd_dp);
	return 0;
}

static void ps_channel_exit(void)
{
	psd_debugfs_exit();
	data_path_close(psd_dp);
}

static int psdatastub_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static long psdatastub_ioctl(struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	GCFDATA gcfdata;
	struct sk_buff *gcfbuf;

	if (_IOC_TYPE(cmd) != PSDATASTUB_IOC_MAGIC) {
		pr_debug("%s: cci magic number is wrong!\n", __func__);
		return -ENOTTY;
	}

	pr_debug("%s: cmd=0x%x\n", __func__, cmd);
	switch (cmd) {
	case PSDATASTUB_GCFDATA:	/* For CGSEND and TGSINK */
		if (copy_from_user(&gcfdata, (GCFDATA *) arg, sizeof(GCFDATA)))
			return -EFAULT;
		gcfbuf = alloc_skb(gcfdata.len, GFP_KERNEL);
		if (!gcfbuf)
			return -ENOMEM;
		if (copy_from_user
		    (skb_put(gcfbuf, gcfdata.len), gcfdata.databuf,
		     gcfdata.len)) {
			kfree_skb(gcfbuf);
			return -EFAULT;
		}
		psd_data_tx(gcfdata.cid, gcfbuf);
		break;

	case PSDATASTUB_TOGGLE_DATA_ENABLE_DISABLE:
		if (data_enabled)
			data_enabled = false;
		else
			data_enabled = true;

		pr_info("%s: Toggle Data to %s", __func__, data_enabled ?
			"Enabled" : "Disabled");
		break;

	}
	return 0;
}

#ifdef CONFIG_COMPAT
static int compat_cgfdata_handle(struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	struct GCFDATA32 __user *argp = (void __user *)arg;
	GCFDATA __user *buf;
	compat_uptr_t param_addr;
	int ret = 0;

	buf = compat_alloc_user_space(sizeof(*buf));
	if (!access_ok(VERIFY_WRITE, buf, sizeof(*buf))
	    || !access_ok(VERIFY_WRITE, argp, sizeof(*argp)))
		return -EFAULT;

	if (__copy_in_user(buf, argp, offsetof(struct GCFDATA32, databuf))
	    || __get_user(param_addr, &argp->databuf)
	    || __put_user(compat_ptr(param_addr), &buf->databuf))
		return -EFAULT;

	ret = psdatastub_ioctl(filp, cmd, (unsigned long)buf);
	return ret;
}
static long compat_psdatastub_ioctl(struct file *filp,
			      unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	if (_IOC_TYPE(cmd) != PSDATASTUB_IOC_MAGIC) {
		pr_debug("%s: cci magic number is wrong!\n", __func__);
		return -ENOTTY;
	}
	switch (cmd) {
	case PSDATASTUB_GCFDATA:	/* For CGSEND and TGSINK */
		ret = compat_cgfdata_handle(filp, cmd, arg);
		break;
	default:
		ret = psdatastub_ioctl(filp, cmd, arg);
		break;
	}
	return ret;
}
#endif

static const struct file_operations psdatastub_fops = {
	.open = psdatastub_open,
	.unlocked_ioctl = psdatastub_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_psdatastub_ioctl,
#endif
	.owner = THIS_MODULE
};

static struct miscdevice psdatastub_miscdev = {
	MISC_DYNAMIC_MINOR,
	"psdatastub",
	&psdatastub_fops,
};

static struct notifier_block cp_state_notifier = {
	.notifier_call = ps_channel_init,
};

static int psdatastub_probe(struct platform_device *dev)
{
	int ret;

	ret = misc_register(&psdatastub_miscdev);
	if (ret)
		pr_err("register misc device error\n");

	register_first_cp_synced(&cp_state_notifier);

	return ret;
}

static int psdatastub_remove(struct platform_device *dev)
{
	ps_channel_exit();
	misc_deregister(&psdatastub_miscdev);
	return 0;
}

static void psdatastub_dev_release(struct device *dev)
{
	return;
}

static struct platform_device psdatastub_device = {
	.name = "psdatastub",
	.id = 0,
	.dev = {
		.release = psdatastub_dev_release,
	},
};

static struct platform_driver psdatastub_driver = {
	.probe = psdatastub_probe,
	.remove = psdatastub_remove,
	.driver = {
		.name = "psdatastub",
	}
};

int psdatastub_init(void)
{
	int ret;

	ret = platform_device_register(&psdatastub_device);
	if (!ret) {
		ret = platform_driver_register(&psdatastub_driver);
		if (ret)
			platform_device_unregister(&psdatastub_device);
	} else {
		pr_info("Cannot register CCIDATASTUB platform device\n");
	}

	return ret;
}

void psdatastub_exit(void)
{
	platform_driver_unregister(&psdatastub_driver);
	platform_device_unregister(&psdatastub_device);
}

