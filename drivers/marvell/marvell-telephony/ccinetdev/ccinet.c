/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (C) Copyright 2006 Marvell International Ltd.
 * All Rights Reserved
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>

#include <linux/uaccess.h>
#include <linux/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/percpu.h>
#include <linux/cdev.h>
#include <linux/platform_device.h>
#include <linux/kthread.h>
#include <linux/debugfs.h>

#include "common_datastub.h"
#include "data_channel_kernel.h"
#include "psd_data_channel.h"
#include "debugfs.h"

#define MAX_CID_NUM    8
#define NETWORK_EMBMS_CID    0xFF

#define SIOCUSECCINET    SIOCDEVPRIVATE
#define SIOCRELEASECCINET (SIOCDEVPRIVATE+1)

#define CCINET_IOCTL_SET_V6_CID (SIOCDEVPRIVATE+4)

#define CCINET_IOCTL_SET_EMBMS_CID (SIOCDEVPRIVATE+5)

struct ccinet_priv {
	unsigned char is_used;
	unsigned char sim_id;
	unsigned char cid;
	int v6_cid;
	spinlock_t lock;	/* use to protect critical session */
	struct net_device_stats net_stats; /* status of the network device */
};
static struct net_device *net_devices[MAX_CID_NUM];

static int main_cid[MAX_CID_NUM];

static int embms_cid = 7;

#if  1
#define DPRINT(fmt, args ...)     printk(fmt, ## args)
#define DBGMSG(fmt, args ...)     pr_debug("CIN: " fmt, ## args)
#define ENTER()         pr_debug("CIN: ENTER %s\n", __func__)
#define LEAVE()         pr_debug("CIN: LEAVE %s\n", __func__)
#define FUNC_EXIT()     pr_debug("CIN: EXIT %s\n", __func__)
#else
#define DPRINT(fmt, args ...)     pr_info("CIN: " fmt, ## args)
#define DBGMSG(fmt, args ...)     do {} while (0)
#define ENTER()                 do {} while (0)
#define LEAVE()                 do {} while (0)
#define FUNC_EXIT()                     do {} while (0)
#endif

static u32 checksum_enable = true;

/**********************************************************/
/* Network Operations */
/**********************************************************/
static int ccinet_open(struct net_device *netdev)
{
	ENTER();

	/* netif_carrier_on(netdev); */
	netif_start_queue(netdev);
	LEAVE();
	return 0;
}

static int ccinet_stop(struct net_device *netdev)
{
	struct ccinet_priv *devobj = netdev_priv(netdev);
	ENTER();
	netif_stop_queue(netdev);
	/* netif_carrier_off(netdev); */
	memset(&devobj->net_stats, 0, sizeof(devobj->net_stats));
	if (devobj->v6_cid > -1) {
		main_cid[devobj->v6_cid] = -1;
		devobj->v6_cid = -1;
	}

	LEAVE();
	return 0;
}

static netdev_tx_t ccinet_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct ccinet_priv *devobj = netdev_priv(netdev);
	int cid = devobj->cid;
	struct iphdr *ip_header = (struct iphdr *)ip_hdr(skb);
	int ret;

	if (ip_header->version == 6) {
		if (devobj->v6_cid > -1)
			cid = devobj->v6_cid;
	}
	netdev->trans_start = jiffies;

	if (skb->tstamp.tv64 == 0)
		__net_timestamp(skb);

	ret = sendPSDData(cid | devobj->sim_id << 31, skb);
	if (ret == PSD_DATA_SEND_BUSY) {
		return NETDEV_TX_BUSY;
	} else if (ret == PSD_DATA_SEND_DROP) {
		devobj->net_stats.tx_dropped++;
	} else {
		devobj->net_stats.tx_packets++;
		devobj->net_stats.tx_bytes += skb->len;
	}
	return NETDEV_TX_OK;

}

void ccinet_fc_cb(bool is_throttle)
{
	int i;

	void (*cb)(struct net_device *dev);

	DBGMSG("ccinet_fc_cb: is_throttle=%d\n", is_throttle);

	if (is_throttle)
		cb = &netif_stop_queue;
	else
		cb = &netif_wake_queue;

	for (i = 0; i < MAX_CID_NUM; i++)
		cb(net_devices[i]);
}

static void ccinet_tx_timeout(struct net_device *netdev)
{
	struct ccinet_priv *devobj = netdev_priv(netdev);

	pr_warn("%s: on %s\n", __func__, netdev->name);
	devobj->net_stats.tx_errors++;
	/* netif_wake_queue(netdev); */   /* Resume tx */
	return;
}

static struct net_device_stats *ccinet_get_stats(struct net_device *dev)
{
	struct ccinet_priv *devobj;

	devobj = netdev_priv(dev);
	return &devobj->net_stats;
}

/*
 * extra header size need to be reserved to avoid memory copying
 * in RNDIS driver and WiFi driver
 */
#define CCINET_RESERVE_HDR_LEN (192)

static int ccinet_rx(struct net_device *netdev, char *packet, int pktlen)
{

	struct sk_buff *skb;
	struct ccinet_priv *priv = netdev_priv(netdev);
	struct iphdr *ip_header = (struct iphdr *)packet;
	__be16	protocol;

	if (ip_header->version == 4) {
		protocol = htons(ETH_P_IP);
	} else if (ip_header->version == 6) {
		protocol = htons(ETH_P_IPV6);
	} else {
		pr_err("ccinet_rx: invalid ip version: %d\n",
		       ip_header->version);
		priv->net_stats.rx_dropped++;
		goto out;
	}

	skb = dev_alloc_skb(pktlen + CCINET_RESERVE_HDR_LEN);
	if (!skb) {
		pr_notice_ratelimited(
			"ccinet_rx: low on mem - packet dropped\n");

		priv->net_stats.rx_dropped++;

		goto out;

	}
	skb_reserve(skb, CCINET_RESERVE_HDR_LEN);
	memcpy(skb_put(skb, pktlen), packet, pktlen);

	/* Write metadata, and then pass to the receive level */

	skb->dev = netdev;
	skb->protocol = protocol;
	if (!checksum_enable)
		skb->ip_summed = CHECKSUM_UNNECESSARY;	/* don't check it */
	if (netif_rx(skb) == NET_RX_SUCCESS) {
		priv->net_stats.rx_packets++;
		priv->net_stats.rx_bytes += pktlen;
	} else {
		priv->net_stats.rx_dropped++;
		pr_notice_ratelimited(
			"ccinet_rx: packet dropped by netif_rx\n");
		goto out;
	}

	return 0;

out:
	return -1;
}

static int ccinet_ioctl(struct net_device *netdev, struct ifreq *rq, int cmd)
{
	int err = 0;
	struct ccinet_priv *priv = netdev_priv(netdev);
	switch (cmd) {
	case SIOCUSECCINET:
	{
		int sim_id;
		if (copy_from_user(&sim_id, rq->ifr_data, sizeof(sim_id))) {
			err = -EFAULT;
			break;
		}
		spin_lock(&priv->lock);
		if (priv->is_used) {
			netdev_err(netdev,
				"%s: SIM%d has used cid%d, SIM%d request fail\n",
				__func__, (int)priv->sim_id + 1, (int)priv->cid,
				sim_id + 1);
			err = -EFAULT;
		} else {
			priv->sim_id = sim_id;
			priv->is_used = 1;
			netdev_err(netdev, "%s: SIM%d now use cid%d\n",
				__func__, sim_id + 1, (int)priv->cid);
		}
		spin_unlock(&priv->lock);
		break;
	}
	case SIOCRELEASECCINET:
	{
		int sim_id;
		if (copy_from_user(&sim_id, rq->ifr_data, sizeof(sim_id))) {
			err = -EFAULT;
			break;
		}
		spin_lock(&priv->lock);
		if (priv->is_used) {
			if (sim_id != priv->sim_id) {
				spin_unlock(&priv->lock);
				netdev_err(netdev,
					"%s: SIM%d want release cid%d, but it's used by SIM%d\n",
					__func__, sim_id + 1, priv->cid,
					(int)priv->sim_id + 1);
				err = -EFAULT;
			} else {
				priv->sim_id = 0;
				priv->is_used = 0;
				spin_unlock(&priv->lock);
				netdev_err(netdev,
					"%s: SIM%d now release cid%d\n",
					__func__, sim_id + 1, (int)priv->cid);
			}
		} else {
			spin_unlock(&priv->lock);
			netdev_err(netdev,
				"%s: SIM%d want release cid%d, but not used before\n",
				__func__, sim_id + 1, priv->cid);
		}
		break;
	}
	case CCINET_IOCTL_SET_V6_CID:
	{
		int v6_cid = rq->ifr_ifru.ifru_ivalue;
		if (v6_cid < 0 || v6_cid >= MAX_CID_NUM) {
			err = -EINVAL;
			break;
		}
		priv->v6_cid = v6_cid;
		main_cid[v6_cid] = priv->cid;
		break;
	}
	case CCINET_IOCTL_SET_EMBMS_CID:
	{
		embms_cid = rq->ifr_ifru.ifru_ivalue;
		break;
	}
	default:
		err = -EOPNOTSUPP;
		break;
	}
	return err;
}

static int data_rx(char *packet, int len, unsigned char cid)
{
	struct net_device *dev;
	int i;

	/*map embms cid to local value*/
	if (cid == NETWORK_EMBMS_CID)
		cid = embms_cid;
	if (cid >= MAX_CID_NUM)
		return -1;
	i = main_cid[cid] >= 0 ? main_cid[cid] : cid;
	dev = net_devices[i];
	if (!(dev->flags & IFF_UP)) {
		pr_err_ratelimited(
			"%s: netdevice %s not up for cid:%d\n",
			__func__, dev->name, (int)cid);
	}
	ccinet_rx(dev, packet, len);
	return len;
}
/*************************************************************************/
/* Initialization */
/*************************************************************************/

static const struct net_device_ops cci_netdev_ops = {
	.ndo_open		= ccinet_open,
	.ndo_stop		= ccinet_stop,
	.ndo_start_xmit	= ccinet_tx,
	.ndo_tx_timeout		= ccinet_tx_timeout,
	.ndo_get_stats	= ccinet_get_stats,
	.ndo_do_ioctl = ccinet_ioctl
};

static void ccinet_setup(struct net_device *netdev)
{
	ENTER();
	netdev->netdev_ops	= &cci_netdev_ops;
	netdev->type		= ARPHRD_VOID;
	netdev->mtu		= 1500;
	netdev->addr_len	= 0;
	netdev->tx_queue_len	= 1000;
	netdev->flags		= IFF_NOARP;
	netdev->hard_header_len	= 16;
	netdev->priv_flags	&= ~IFF_XMIT_DST_RELEASE;

	LEAVE();
}

static struct dentry *tel_debugfs_root_dir;
static struct dentry *ccinet_debugfs_root_dir;

static int ccinet_debugfs_init(void)
{
	tel_debugfs_root_dir = tel_debugfs_get();
	if (!tel_debugfs_root_dir)
		return -ENOMEM;

	ccinet_debugfs_root_dir = debugfs_create_dir("ccinet", tel_debugfs_root_dir);
	if (IS_ERR_OR_NULL(ccinet_debugfs_root_dir))
		goto putrootfs;

	if (IS_ERR_OR_NULL(debugfs_create_bool("checksum_enable", S_IRUGO | S_IWUSR,
			ccinet_debugfs_root_dir, &checksum_enable)))
		goto error;

	return 0;

error:
	debugfs_remove_recursive(ccinet_debugfs_root_dir);
	ccinet_debugfs_root_dir = NULL;
putrootfs:
	tel_debugfs_put(tel_debugfs_root_dir);
	tel_debugfs_root_dir = NULL;
	return -1;
}

static int ccinet_debugfs_exit(void)
{
	debugfs_remove_recursive(ccinet_debugfs_root_dir);
	ccinet_debugfs_root_dir = NULL;
	tel_debugfs_put(tel_debugfs_root_dir);
	tel_debugfs_root_dir = NULL;
	return 0;
}

static int __init ccinet_init(void)
{
	int i;
	for (i = 0; i < MAX_CID_NUM; i++) {
		char ifname[32];
		struct net_device *dev;
		struct ccinet_priv *priv;
		int ret;

		main_cid[i] = -1;
		sprintf(ifname, "ccinet%d", i);
		dev =
		    alloc_netdev(sizeof(struct ccinet_priv), ifname,
				 ccinet_setup);

		if (!dev) {
			pr_err("%s: alloc_netdev for %s fail\n",
			       __func__, ifname);
			return -ENOMEM;
		}
		ret = register_netdev(dev);
		if (ret) {
			pr_err("%s: register_netdev for %s fail\n",
			       __func__, ifname);
			free_netdev(dev);
			return ret;
		}
		priv = netdev_priv(dev);
		memset(priv, 0, sizeof(struct ccinet_priv));
		spin_lock_init(&priv->lock);
		priv->cid = i;
		priv->v6_cid = -1;
		net_devices[i] = dev;
	}

	registerRxCallBack(PDP_DIRECTIP, data_rx);
	registerPSDFCCallBack(PDP_DIRECTIP, ccinet_fc_cb);
	ccinet_debugfs_init();

	return 0;
};

static void __exit ccinet_exit(void)
{
	int i;
	ccinet_debugfs_exit();
	unregisterRxCallBack(PDP_DIRECTIP);
	for (i = 0; i < MAX_CID_NUM; i++) {
		unregister_netdev(net_devices[i]);
		free_netdev(net_devices[i]);
		net_devices[i] = NULL;
	}
	unregisterPSDFCCallBack(PDP_DIRECTIP);
}

module_init(ccinet_init);
module_exit(ccinet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell CI Network Driver");
