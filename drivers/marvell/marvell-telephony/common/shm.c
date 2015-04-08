/*
    shm.c Created on: Aug 2, 2010, Jinhua Huang <jhhuang@marvell.com>

    Marvell PXA9XX ACIPC-MSOCKET driver for Linux
    Copyright (C) 2010 Marvell International Ltd.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <linux/types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include "shm.h"
#include "shm_map.h"
#include "debugfs.h"

static int debugfs_show_ks_info(struct seq_file *s, void *data)
{
	struct shm_rbctl *rbctl = s->private;
	int ret = 0;

	ret += seq_printf(s, "skctl_pa\t: 0x%p\n", (void *)rbctl->skctl_pa);
	ret += seq_printf(s, "skctl_va\t: 0x%p\n", rbctl->skctl_va);

	return ret;
}

static int debugfs_show_tx_buf_info(struct seq_file *s, void *data)
{
	struct shm_rbctl *rbctl = s->private;
	int ret = 0;

	ret += seq_printf(s, "tx_pa\t\t: 0x%p\n", (void *)rbctl->tx_pa);
	ret += seq_printf(s, "tx_va\t\t: 0x%p\n", rbctl->tx_va);
	ret += seq_printf(s, "tx_total_size\t: %d\n",
		rbctl->tx_total_size);
	ret += seq_printf(s, "tx_skbuf_size\t: %d\n",
		rbctl->tx_skbuf_size);
	ret += seq_printf(s, "tx_skbuf_num\t: %d\n",
		rbctl->tx_skbuf_num);
	ret += seq_printf(s, "tx_skbuf_low_wm\t: %d\n",
		rbctl->tx_skbuf_low_wm);

	return ret;
}

static int debugfs_show_rx_buf_info(struct seq_file *s, void *data)
{
	struct shm_rbctl *rbctl = s->private;
	int ret = 0;

	ret += seq_printf(s, "rx_pa\t\t: 0x%p\n", (void *)rbctl->rx_pa);
	ret += seq_printf(s, "rx_va\t\t: 0x%p\n", rbctl->rx_va);
	ret += seq_printf(s, "rx_total_size\t: %d\n",
		rbctl->rx_total_size);
	ret += seq_printf(s, "rx_skbuf_size\t: %d\n",
		rbctl->rx_skbuf_size);
	ret += seq_printf(s, "rx_skbuf_num\t: %d\n",
		rbctl->rx_skbuf_num);
	ret += seq_printf(s, "rx_skbuf_low_wm\t: %d\n",
		rbctl->rx_skbuf_low_wm);

	return ret;
}

static int debugfs_show_fc_stat(struct seq_file *s, void *data)
{
	struct shm_rbctl *rbctl = s->private;
	int ret = 0;

	ret += seq_printf(s, "is_ap_xmit_stopped\t: %s\n",
		rbctl->is_ap_xmit_stopped ? "Y" : "N");
	ret += seq_printf(s, "is_cp_xmit_stopped\t: %s\n",
		rbctl->is_cp_xmit_stopped ? "Y" : "N");

	return ret;
}

static int debugfs_show_stat(struct seq_file *s, void *data)
{
	struct shm_rbctl *rbctl = s->private;
	int ret = 0;

	ret += seq_printf(s, "ap_stopped_num\t: %lu\n",
		rbctl->ap_stopped_num);
	ret += seq_printf(s, "ap_resumed_num\t: %lu\n",
		rbctl->ap_resumed_num);
	ret += seq_printf(s, "cp_stopped_num\t: %lu\n",
		rbctl->cp_stopped_num);
	ret += seq_printf(s, "cp_resumed_num\t: %lu\n",
		rbctl->cp_resumed_num);

	return ret;
}

TEL_DEBUG_ENTRY(ks_info);
TEL_DEBUG_ENTRY(tx_buf_info);
TEL_DEBUG_ENTRY(rx_buf_info);
TEL_DEBUG_ENTRY(fc_stat);
TEL_DEBUG_ENTRY(stat);

static int shm_rb_debugfs_init(struct shm_rbctl *rbctl,
	struct dentry *parent)
{
	struct dentry *ksdir;

	rbctl->rbdir = debugfs_create_dir(rbctl->name ? rbctl->name : "shm",
		parent);
	if (IS_ERR_OR_NULL(rbctl->rbdir))
		return -ENOMEM;

	if (IS_ERR_OR_NULL(debugfs_create_file("ks_info", S_IRUGO,
				rbctl->rbdir, rbctl, &fops_ks_info)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_file("tx_buf_info", S_IRUGO,
				rbctl->rbdir, rbctl, &fops_tx_buf_info)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_file("rx_buf_info", S_IRUGO,
				rbctl->rbdir, rbctl, &fops_rx_buf_info)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_file("fc_stat", S_IRUGO,
				rbctl->rbdir, rbctl, &fops_fc_stat)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_file("stat", S_IRUGO,
				rbctl->rbdir, rbctl, &fops_stat)))
		goto error;

	ksdir = debugfs_create_dir("key_section", rbctl->rbdir);
	if (IS_ERR_OR_NULL(ksdir))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_int(
				"ap_wptr", S_IRUGO | S_IWUSR, ksdir,
				(int *)
				&rbctl->skctl_va->ap_wptr)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_int(
				"cp_rptr", S_IRUGO | S_IWUSR, ksdir,
				(int *)
				&rbctl->skctl_va->cp_rptr)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_int(
				"ap_rptr", S_IRUGO | S_IWUSR, ksdir,
				(int *)
				&rbctl->skctl_va->ap_rptr)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_int(
				"cp_wptr", S_IRUGO | S_IWUSR, ksdir,
				(int *)
				&rbctl->skctl_va->cp_wptr)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_uint(
				"ap_port_fc", S_IRUGO | S_IWUSR, ksdir,
				(unsigned int *)
				&rbctl->skctl_va->ap_port_fc)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_uint(
				"cp_port_fc", S_IRUGO | S_IWUSR, ksdir,
				(unsigned int *)
				&rbctl->skctl_va->cp_port_fc)))
		goto error;

	return 0;

error:
	debugfs_remove_recursive(rbctl->rbdir);
	rbctl->rbdir = NULL;
	return -1;
}

static int shm_rb_debugfs_exit(struct shm_rbctl *rbctl)
{
	debugfs_remove_recursive(rbctl->rbdir);
	rbctl->rbdir = NULL;
	return 0;
}

void shm_rb_data_init(struct shm_rbctl *rbctl)
{
	rbctl->is_ap_xmit_stopped = false;
	rbctl->is_cp_xmit_stopped = false;

	rbctl->ap_stopped_num = 0;
	rbctl->ap_resumed_num = 0;
	rbctl->cp_stopped_num = 0;
	rbctl->cp_resumed_num = 0;
}

static inline void shm_rb_dump(struct shm_rbctl *rbctl)
{
	pr_info(
		"ring buffer %s:\n"
		"\tskctl_pa: 0x%08lx, skctl_va: 0x%p\n"
		"\ttx_pa: 0x%08lx, tx_va: 0x%p\n"
		"\ttx_total_size: 0x%08x, tx_skbuf_size: 0x%08x\n"
		"\ttx_skbuf_num: 0x%08x, tx_skbuf_low_wm: 0x%08x\n"
		"\trx_pa: 0x%08lx, rx_va: 0x%p\n"
		"\trx_total_size: 0x%08x, rx_skbuf_size: 0x%08x\n"
		"\trx_skbuf_num: 0x%08x, rx_skbuf_low_wm: 0x%08x\n",
		rbctl->name ? rbctl->name : "shm",
		rbctl->skctl_pa, rbctl->skctl_va,
		rbctl->tx_pa, rbctl->tx_va,
		rbctl->tx_total_size, rbctl->tx_skbuf_size,
		rbctl->tx_skbuf_num, rbctl->tx_skbuf_low_wm,
		rbctl->rx_pa, rbctl->rx_va,
		rbctl->rx_total_size, rbctl->rx_skbuf_size,
		rbctl->rx_skbuf_num, rbctl->rx_skbuf_low_wm
	);
}

int shm_rb_init(struct shm_rbctl *rbctl, struct dentry *parent)
{
	mutex_lock(&rbctl->va_lock);
	/* map to non-cache virtual address */
	rbctl->skctl_va =
	    shm_map(rbctl->skctl_pa, sizeof(struct shm_skctl));
	if (!rbctl->skctl_va)
		goto exit1;

	/* map ring buffer to cacheable memeory, if it is DDR */
	if (pfn_valid(__phys_to_pfn(rbctl->tx_pa))) {
		rbctl->tx_cacheable = true;
		rbctl->tx_va = phys_to_virt(rbctl->tx_pa);
	} else {
		rbctl->tx_cacheable = false;
		rbctl->tx_va = shm_map(rbctl->tx_pa, rbctl->tx_total_size);
	}

	if (!rbctl->tx_va)
		goto exit2;

	if (pfn_valid(__phys_to_pfn(rbctl->rx_pa))) {
		rbctl->rx_cacheable = true;
		rbctl->rx_va = phys_to_virt(rbctl->rx_pa);
	} else {
		rbctl->rx_cacheable = false;
		rbctl->rx_va = shm_map(rbctl->rx_pa, rbctl->rx_total_size);
	}

	if (!rbctl->rx_va)
		goto exit3;

	mutex_unlock(&rbctl->va_lock);
	shm_rb_data_init(rbctl);
	shm_rb_dump(rbctl);

	if (shm_rb_debugfs_init(rbctl, parent) < 0)
		goto exit4;

	return 0;

exit4:
	shm_rb_debugfs_exit(rbctl);
	mutex_lock(&rbctl->va_lock);
	if (!rbctl->rx_cacheable)
		shm_unmap(rbctl->rx_pa, rbctl->rx_va);
	rbctl->rx_cacheable = false;
exit3:
	if (!rbctl->tx_cacheable)
		shm_unmap(rbctl->tx_pa, rbctl->tx_va);
	rbctl->tx_cacheable = false;
exit2:
	shm_unmap(rbctl->skctl_pa, rbctl->skctl_va);
exit1:
	mutex_unlock(&rbctl->va_lock);
	return -1;
}
EXPORT_SYMBOL(shm_rb_init);

int shm_rb_exit(struct shm_rbctl *rbctl)
{
	void *skctl_va = rbctl->skctl_va;
	void *tx_va = rbctl->tx_va;
	void *rx_va = rbctl->rx_va;

	shm_rb_debugfs_exit(rbctl);
	/* release memory */
	mutex_lock(&rbctl->va_lock);
	rbctl->skctl_va = NULL;
	rbctl->tx_va = NULL;
	rbctl->rx_va = NULL;
	mutex_unlock(&rbctl->va_lock);
	shm_unmap(rbctl->skctl_pa, skctl_va);
	if (!rbctl->tx_cacheable)
		shm_unmap(rbctl->tx_pa, tx_va);
	rbctl->tx_cacheable = false;
	if (!rbctl->rx_cacheable)
		shm_unmap(rbctl->rx_pa, rx_va);
	rbctl->rx_cacheable = false;

	return 0;
}
EXPORT_SYMBOL(shm_rb_exit);

int shm_free_rx_skbuf_safe(struct shm_rbctl *rbctl)
{
	int ret;

	mutex_lock(&rbctl->va_lock);
	if (rbctl->skctl_va)
		ret = shm_free_rx_skbuf(rbctl);
	else
		ret = -1;
	mutex_unlock(&rbctl->va_lock);
	return ret;
}
EXPORT_SYMBOL(shm_free_rx_skbuf_safe);

int shm_free_tx_skbuf_safe(struct shm_rbctl *rbctl)
{
	int ret;

	mutex_lock(&rbctl->va_lock);
	if (rbctl->skctl_va)
		ret = shm_free_tx_skbuf(rbctl);
	else
		ret = -1;
	mutex_unlock(&rbctl->va_lock);
	return ret;
}
EXPORT_SYMBOL(shm_free_tx_skbuf_safe);

/* write packet to share memory socket buffer */
void shm_xmit(struct shm_rbctl *rbctl, struct sk_buff *skb)
{
	struct shm_skctl *skctl = rbctl->skctl_va;

	/*
	 * we always write to the next slot !?
	 * thinking the situation of the first slot in the first accessing
	 */
	int slot = shm_get_next_tx_slot(rbctl, skctl->ap_wptr);
	void *data = SHM_PACKET_PTR(rbctl->tx_va, slot, rbctl->tx_skbuf_size);

	if (!skb) {
		pr_err("shm_xmit skb is null..\n");
		return;
	}
	memcpy(data, skb->data, skb->len);
	shm_flush_dcache(rbctl, data, skb->len);
	skctl->ap_wptr = slot;	/* advance pointer index */
}

/* read packet from share memory socket buffer */
struct sk_buff *shm_recv(struct shm_rbctl *rbctl)
{
	struct shm_skctl *skctl = rbctl->skctl_va;

	/* yes, we always read from the next slot either */
	int slot = shm_get_next_rx_slot(rbctl, skctl->ap_rptr);

	/* get the total packet size first for memory allocate */
	unsigned char *hdr = SHM_PACKET_PTR(rbctl->rx_va, slot,
					    rbctl->rx_skbuf_size);
	size_t count = 0;
	struct sk_buff *skb = NULL;

	shm_invalidate_dcache(rbctl, hdr, rbctl->rx_skbuf_size);

	if (likely(rbctl->cbs && rbctl->cbs->get_packet_length))
		count = rbctl->cbs->get_packet_length(hdr);

	if (unlikely(count > rbctl->rx_skbuf_size || !count)) {
		pr_emerg(
		       "MSOCK: shm_recv: slot = %d, count = %zu\n", slot, count);
		goto error_length;
	}

	skb = alloc_skb(count, GFP_ATOMIC);
	if (!skb)
		return NULL;

	/* write all the packet data including header to sk_buff */
	memcpy(skb_put(skb, count), hdr, count);

error_length:
	/* advance reader pointer */
	skctl->ap_rptr = slot;

	return skb;
}
