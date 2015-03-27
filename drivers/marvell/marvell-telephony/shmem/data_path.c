/*
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

#include <linux/sched.h>
#include <linux/device.h>
#include <linux/pm_wakeup.h>
#include <linux/version.h>
#include <linux/export.h>
#include <linux/netdevice.h>	/* dev_kfree_skb_any */
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/ctype.h>
#include <linux/ratelimit.h>
#include <linux/pxa9xx_acipc.h>
#include "shm_share.h"
#include "acipcd.h"
#include "shm.h"
#include "msocket.h"
#include "data_path.h"
#include "tel_trace.h"
#include "debugfs.h"
#include "pxa_cp_load.h"


static struct wakeup_source dp_rx_wakeup;

static struct data_path data_path = {
	.name = "psd",
};

static struct dentry *tel_debugfs_root_dir;
static struct dentry *dp_debugfs_root_dir;

enum data_path_state {
	dp_state_idle,
	dp_state_opening,
	dp_state_opened,
	dp_state_closing,
};

/*
 * as we do tx/rx in interrupt context, we should avoid lock up the box
 */
#define MAX_TX_SHOTS 32
#define MAX_RX_SHOTS 32

/*
 * max tx q length
 */
#define MAX_TX_Q_LEN 2048

/*
 * tx schedule delay
 */
#define TX_SCHED_DELAY 2

/*
 * tx q schedule length
 */
#define TX_MIN_SCHED_LEN 0

static inline void tx_q_enqueue(struct data_path *dp, struct sk_buff *skb,
				enum data_path_priority prio)
{
	dp->stat.tx_q_bytes += skb->len;
	dp->stat.tx_q_packets[prio]++;
	skb_llist_enqueue(&dp->tx_q[prio], skb);
}

static inline void tx_q_queue_head(struct data_path *dp, struct sk_buff *skb,
				enum data_path_priority prio)
{
	skb_llist_queue_head(&dp->tx_q[prio], skb);
}

static inline struct sk_buff *tx_q_peek(struct data_path *dp, int *prio)
{
	struct sk_buff *skb = NULL;
	int i;

	for (i = 0; i < dp_priority_cnt; i++) {
		skb = skb_llist_peek(&dp->tx_q[i]);
		if (skb) {
			if (prio)
				*prio = i;
			break;
		}
	}

	return skb;
}

static inline struct sk_buff *tx_q_dequeue(struct data_path *dp, int *prio)
{
	struct sk_buff *skb = NULL;
	int i;

	for (i = 0; i < dp_priority_cnt; i++) {
		skb = skb_llist_dequeue(&dp->tx_q[i]);
		if (skb) {
			if (prio)
				*prio = i;
			break;
		}
	}

	return skb;
}

static inline void tx_q_init(struct data_path *dp)
{
	int i;
	for (i = 0; i < dp_priority_cnt; i++)
		skb_llist_init(&dp->tx_q[i]);
}

static inline void tx_q_clean(struct data_path *dp)
{
	int i;
	for (i = 0; i < dp_priority_cnt; i++)
		skb_llist_clean(&dp->tx_q[i]);
}

static inline bool has_enough_free_tx_slot(const struct data_path *dp,
	int free_slots, int prio)
{
	return free_slots > dp->tx_wm[prio];
}

static inline int tx_q_avail_length(struct data_path *dp, int free_slots)
{
	int len = 0;
	int i;

	for (i = 0; i < dp_priority_cnt; i++)
		if (has_enough_free_tx_slot(dp, free_slots, i))
			len += skb_llist_len(&dp->tx_q[i]);

	return len;
}

/* notify cp psd that new packet available in the socket buffer */
static inline void acipc_notify_psd_packet_sent(void)
{
	acipc_event_set(ACIPC_SHM_PSD_PACKET_NOTIFY);
}

/* notify cp psd that cp can continue transmission */
static inline void acipc_notify_cp_psd_tx_resume(void)
{
	pr_warn_ratelimited(
		"MSOCK: acipc_notify_cp_psd_tx_resume!!!\n");
	acipc_event_set(ACIPC_RINGBUF_PSD_TX_RESUME);
}

/*notify cp psd that ap transmission is stopped, please resume me later */
static inline void acipc_notify_ap_psd_tx_stopped(void)
{
	pr_warn_ratelimited(
		"MSOCK: acipc_notify_ap_psd_tx_stopped!!!\n");
	acipc_event_set(ACIPC_RINGBUF_PSD_TX_STOP);
}

static void data_path_tx_func(unsigned long arg)
{
	struct data_path *dp = (struct data_path *)arg;
	struct shm_rbctl *rbctl = dp->rbctl;
	struct shm_skctl *skctl = rbctl->skctl_va;
	struct shm_psd_skhdr *skhdr;
	struct sk_buff *packet;
	int slot = 0;
	int pending_slot;
	int free_slots;
	int prio;
	int remain_bytes;
	int used_bytes;
	int consumed_slot = 0;
	int consumed_packets = 0;
	int start_q_len;
	int max_tx_shots = dp->max_tx_shots;

	pending_slot = -1;
	remain_bytes = rbctl->tx_skbuf_size - sizeof(struct shm_psd_skhdr);
	used_bytes = 0;

	start_q_len = tx_q_length(dp);

	dp->stat.tx_sched_cnt++;

	while (consumed_slot < max_tx_shots) {
		if (!cp_is_synced) {
			tx_q_clean(dp);
			break;
		}

		free_slots = shm_free_tx_skbuf(rbctl);
		if (free_slots == 0) {
			/*
			 * notify cp only if we still have packets in queue
			 * otherwise, simply break
			 * also check current fc status, if tx_stopped is
			 * already sent to cp, do not try to interrupt cp again
			 * it is useless, and just make cp busier
			 * BTW:
			 * this may have race condition here, but as cp side
			 * have a watermark for resume interrupt,
			 * we can assume it is safe
			 */
			if (tx_q_length(dp) && !rbctl->is_ap_xmit_stopped) {
				shm_notify_ap_tx_stopped(rbctl);
				acipc_notify_ap_psd_tx_stopped();
			}
			break;
		} else if (free_slots == 1 && pending_slot != -1) {
			/*
			 * the only left slot is our pending slot
			 * check if we still have enough space in this
			 * pending slot
			 */
			packet = tx_q_peek(dp, NULL);
			if (!packet)
				break;

			/* packet is too large, notify cp and break */
			if (padded_size(packet->len) > remain_bytes &&
				!rbctl->is_ap_xmit_stopped) {
				shm_notify_ap_tx_stopped(rbctl);
				acipc_notify_ap_psd_tx_stopped();
				break;
			}
		}

		packet = tx_q_dequeue(dp, &prio);

		if (!packet)
			break;

		/* push to ring buffer */

		/* we have one slot pending */
		if (pending_slot != -1) {
			/*
			 * the packet is too large for the pending slot
			 * send out the pending slot firstly
			 */
			if (padded_size(packet->len) > remain_bytes) {
				shm_flush_dcache(rbctl,
						SHM_PACKET_PTR(rbctl->tx_va,
							pending_slot,
							rbctl->tx_skbuf_size),
						used_bytes + sizeof(struct shm_psd_skhdr));
				skctl->ap_wptr = pending_slot;
				pending_slot = -1;
				consumed_slot++;
				dp->stat.tx_slots++;
				dp->stat.tx_free_bytes += remain_bytes;
				dp->stat.tx_used_bytes += used_bytes;
			} else
				slot = pending_slot;
		}

		/*
		 * each priority has one hard limit to guarantee higher priority
		 * packet is not affected by lower priority packet
		 * if we reach this limit, we can only send higher priority
		 * packets
		 * but in the other hand, if this packet can be filled into our
		 * pending slot, allow it anyway
		 */
		if (!has_enough_free_tx_slot(dp, free_slots, prio) &&
			((pending_slot == -1) || !dp->enable_piggyback)) {
			/* push back the packets and schedule delayed tx */
			tx_q_queue_head(dp, packet, prio);
			__data_path_schedule_tx(dp, true);
			dp->stat.tx_force_sched_cnt++;
			break;
		}

		/* get a new slot from ring buffer */
		if (pending_slot == -1) {
			slot = shm_get_next_tx_slot(dp->rbctl, skctl->ap_wptr);

			remain_bytes =
				rbctl->tx_skbuf_size
				- sizeof(struct shm_psd_skhdr);
			used_bytes = 0;

			pending_slot = slot;
		}

		consumed_packets++;

		dp->stat.tx_packets[prio]++;
		dp->stat.tx_bytes += packet->len;

		skhdr = (struct shm_psd_skhdr *)
			SHM_PACKET_PTR(rbctl->tx_va,
				slot,
				rbctl->tx_skbuf_size);

		/* we are sure our remains is enough for current packet */
		skhdr->length = used_bytes + padded_size(packet->len);
		memcpy((unsigned char *)(skhdr + 1) + used_bytes,
			packet->data, packet->len);

		used_bytes += padded_size(packet->len);
		remain_bytes -= padded_size(packet->len);

		trace_psd_xmit(packet, slot);

		dp->stat.tx_packets_delay[prio] +=
			ktime_to_ns(net_timedelta(skb_get_ktime(packet)));

		dev_kfree_skb_any(packet);
	}

	/* send out the pending slot */
	if (pending_slot != -1) {
		shm_flush_dcache(rbctl, SHM_PACKET_PTR(rbctl->tx_va,
				pending_slot,
				rbctl->tx_skbuf_size),
			used_bytes + sizeof(struct shm_psd_skhdr));
		skctl->ap_wptr = pending_slot;
		pending_slot = -1;
		consumed_slot++;
		dp->stat.tx_slots++;
		dp->stat.tx_free_bytes += remain_bytes;
		dp->stat.tx_used_bytes += used_bytes;
	}

	if (consumed_slot > 0) {
		trace_psd_xmit_irq(consumed_slot);
		acipc_notify_psd_packet_sent();
		dp->stat.tx_interrupts++;
		dp->stat.tx_sched_q_len += start_q_len;
	}

	if (consumed_slot >= max_tx_shots) {
		data_path_schedule_tx(dp);
		dp->stat.tx_resched_cnt++;
	}

	/*
	 * ring buffer is stopped, just notify upper layer
	 * do not need to check is_tx_stopped here, as we need to handle
	 * following situation:
	 * a new on-demand PDP is activated after tx_stop is called
	 */
	if (rbctl->is_ap_xmit_stopped) {
		if (!dp->is_tx_stopped)
			pr_err("%s tx stop\n", __func__);

		dp->is_tx_stopped = true;

		/* notify upper layer tx stopped */
		if (dp->cbs->tx_stop)
			dp->cbs->tx_stop();

		/* reschedule tx to polling the ring buffer */
		if (tx_q_length(dp))
			__data_path_schedule_tx(dp, true);
	}

	/*
	 * ring buffer is resumed and the remain packets
	 * in queue is also sent out
	 */
	if (!rbctl->is_ap_xmit_stopped && dp->is_tx_stopped
		&& tx_q_length(dp) == 0) {
		pr_err("%s tx resume\n", __func__);

		/* notify upper layer tx resumed */
		if (dp->cbs->tx_resume)
			dp->cbs->tx_resume();

		dp->is_tx_stopped = false;
	}
}

static void data_path_rx_func(unsigned long arg)
{
	struct data_path *dp = (struct data_path *)arg;
	struct shm_rbctl *rbctl = dp->rbctl;
	struct shm_skctl *skctl = rbctl->skctl_va;
	struct shm_psd_skhdr *skhdr;
	int slot;
	int count;
	enum data_path_result result;
	int i;
	int max_rx_shots = dp->max_rx_shots;

	dp->stat.rx_sched_cnt++;

	for (i = 0; i < max_rx_shots; i++) {
		if (!cp_is_synced) {
			/* if not sync, just return */
			break;
		}

		/* process share memory socket buffer flow control */
		if (rbctl->is_cp_xmit_stopped
		    && shm_has_enough_free_rx_skbuf(rbctl)) {
			shm_notify_cp_tx_resume(rbctl);
			acipc_notify_cp_psd_tx_resume();
		}

		if (shm_is_recv_empty(rbctl))
			break;

		slot = shm_get_next_rx_slot(rbctl, skctl->ap_rptr);

		skhdr =
		    (struct shm_psd_skhdr *)SHM_PACKET_PTR(rbctl->rx_va, slot,
							   rbctl->
							   rx_skbuf_size);

		shm_invalidate_dcache(rbctl, skhdr, rbctl->rx_skbuf_size);

		count = skhdr->length + sizeof(*skhdr);

		if (count > rbctl->rx_skbuf_size) {
			pr_err(
				 "%s: slot = %d, count = %d\n", __func__, slot,
				 count);
			goto error_length;
		}

		trace_psd_recv(slot);

		dp->stat.rx_slots++;
		dp->stat.rx_bytes += count - sizeof(*skhdr);
		dp->stat.rx_used_bytes += count;
		dp->stat.rx_free_bytes += rbctl->rx_skbuf_size - count;

		if (dp->cbs && dp->cbs->data_rx)
			result =
			    dp->cbs->data_rx((unsigned char *)(skhdr + 1),
					     skhdr->length);
		else
			result = dp_success;

		/*
		 * upper layer decide to keep the packet as pending
		 * and we need to return now
		 */
		if (result == dp_rx_keep_pending) {
			pr_err("%s: packet is pending\n", __func__);
			break;
		}
error_length:
		skctl->ap_rptr = slot;
	}

	if (i == max_rx_shots) {
		dp->stat.rx_sched_cnt++;
		data_path_schedule_rx(dp);
	}
}

static void tx_sched_timeout(unsigned long data)
{
	struct data_path *dp = (struct data_path *)data;

	if (dp && atomic_read(&dp->state) == dp_state_opened)
		tasklet_schedule(&dp->tx_tl);
}

/*
 * force_delay: delay the schedule forcibly, for the high watermark case
 */
void __data_path_schedule_tx(struct data_path *dp, bool force_delay)
{
	if (dp && atomic_read(&dp->state) == dp_state_opened) {
		int free_slots = shm_free_tx_skbuf(dp->rbctl);
		int len = tx_q_avail_length(dp, free_slots);

		/*
		 * ok, we have enough packet in queue, fire the work immediately
		 */
		if (!force_delay && len > dp->tx_q_min_sched_len) {
			tasklet_schedule(&dp->tx_tl);
			del_timer(&dp->tx_sched_timer);
		} else {
			if (!timer_pending(&dp->tx_sched_timer)) {
				unsigned long expires = jiffies +
					msecs_to_jiffies
					(dp->tx_sched_delay_in_ms);
				mod_timer(&dp->tx_sched_timer, expires);
			}
		}
	}
}

void data_path_schedule_tx(struct data_path *dp)
{
	__data_path_schedule_tx(dp, false);
}
EXPORT_SYMBOL(data_path_schedule_tx);

void data_path_schedule_rx(struct data_path *dp)
{
	if (dp && atomic_read(&dp->state) == dp_state_opened)
		tasklet_schedule(&dp->rx_tl);
}
EXPORT_SYMBOL(data_path_schedule_rx);

int data_path_max_payload(struct data_path *dp)
{
	struct shm_rbctl *rbctl = dp->rbctl;
	return rbctl->tx_skbuf_size - sizeof(struct shm_psd_skhdr);
}
EXPORT_SYMBOL(data_path_max_payload);

enum data_path_result data_path_xmit(struct data_path *dp,
				     struct sk_buff *skb,
				     enum data_path_priority prio)
{
	enum data_path_result ret = dp_not_open;

	if (dp && atomic_read(&dp->state) == dp_state_opened) {
		tx_q_enqueue(dp, skb, prio);
		data_path_schedule_tx(dp);
		ret = dp_success;
	}

	return ret;
}
EXPORT_SYMBOL(data_path_xmit);

static void data_path_broadcast_msg(int proc)
{
	struct data_path *dp = &data_path;

	if (atomic_read(&dp->state) == dp_state_opened) {
		if (proc == MsocketLinkdownProcId) {
			/* make sure tx/rx tasklet is stopped */
			tasklet_disable(&dp->tx_tl);
			/*
			 * tx tasklet is completely stopped
			 * purge the skb list
			 */
			tx_q_clean(dp);
			tasklet_enable(&dp->tx_tl);

			tasklet_disable(&dp->rx_tl);
			tasklet_enable(&dp->rx_tl);

			if (dp->cbs && dp->cbs->link_down)
				dp->cbs->link_down();
		} else if (proc == MsocketLinkupProcId) {
			/*
			 * Now both AP and CP will not send packet
			 * to ring buffer or receive packet from ring
			 * buffer, so cleanup any packet in ring buffer
			 * and initialize some key data structure to
			 * the beginning state otherwise user space
			 * process and CP may occur error
			 */
			shm_rb_data_init(dp->rbctl);
			if (dp->cbs && dp->cbs->link_up)
				dp->cbs->link_up();
		}
	}
}

static int debugfs_show_tx_q(struct seq_file *s, void *data)
{
	struct data_path *dp = s->private;
	int ret = 0;
	int i;

	ret += seq_puts(s, "len :");
	for (i = 0; i < dp_priority_cnt; i++)
		ret += seq_printf(s, " %d",
			skb_llist_len(&dp->tx_q[i]));
	ret += seq_puts(s, "\n");

	return ret;
}

static int debugfs_show_stat(struct seq_file *s, void *data)
{
	struct data_path *dp = s->private;
	int ret = 0;
	int i;

	ret += seq_printf(s, "rx_bytes\t: %lu\n",
		(unsigned long)dp->stat.rx_bytes);
	ret += seq_printf(s, "rx_packets\t: %lu\n",
		(unsigned long)dp->stat.rx_packets);
	ret += seq_printf(s, "rx_slots\t: %lu\n",
		(unsigned long)dp->stat.rx_slots);
	ret += seq_printf(s, "rx_interrupts\t: %lu\n",
		(unsigned long)dp->stat.rx_interrupts);
	ret += seq_printf(s, "rx_used_bytes\t: %lu\n",
		(unsigned long)dp->stat.rx_used_bytes);
	ret += seq_printf(s, "rx_free_bytes\t: %lu\n",
		(unsigned long)dp->stat.rx_free_bytes);
	ret += seq_printf(s, "rx_sched_cnt\t: %lu\n",
		(unsigned long)dp->stat.rx_sched_cnt);
	ret += seq_printf(s, "rx_resched_cnt\t: %lu\n",
		(unsigned long)dp->stat.rx_resched_cnt);

	ret += seq_printf(s, "tx_bytes\t: %lu\n",
		(unsigned long)dp->stat.tx_bytes);

	ret += seq_puts(s, "tx_packets\t:");
	for (i = 0; i < dp_priority_cnt; ++i)
		ret += seq_printf(s, " %lu",
			(unsigned long)dp->stat.tx_packets[i]);
	ret += seq_puts(s, "\n");

	ret += seq_puts(s, "tx_packets_delay\t:");
	for (i = 0; i < dp_priority_cnt; ++i)
		ret += seq_printf(s, " %llu",
			(unsigned long long)dp->stat.tx_packets_delay[i]);
	ret += seq_puts(s, "\n");

	ret += seq_printf(s, "tx_q_bytes\t: %lu\n",
		(unsigned long)dp->stat.tx_q_bytes);

	ret += seq_puts(s, "tx_q_packets\t:");
	for (i = 0; i < dp_priority_cnt; ++i)
		ret += seq_printf(s, " %lu",
			(unsigned long)dp->stat.tx_q_packets[i]);
	ret += seq_puts(s, "\n");

	ret += seq_printf(s, "tx_slots\t: %lu\n",
		(unsigned long)dp->stat.tx_slots);
	ret += seq_printf(s, "tx_interrupts\t: %lu\n",
		(unsigned long)dp->stat.tx_interrupts);
	ret += seq_printf(s, "tx_used_bytes\t: %lu\n",
		(unsigned long)dp->stat.tx_used_bytes);
	ret += seq_printf(s, "tx_free_bytes\t: %lu\n",
		(unsigned long)dp->stat.tx_free_bytes);
	ret += seq_printf(s, "tx_sched_cnt\t: %lu\n",
		(unsigned long)dp->stat.tx_sched_cnt);
	ret += seq_printf(s, "tx_resched_cnt\t: %lu\n",
		(unsigned long)dp->stat.tx_resched_cnt);
	ret += seq_printf(s, "tx_force_sched_cnt\t: %lu\n",
		(unsigned long)dp->stat.tx_force_sched_cnt);
	ret += seq_printf(s, "tx_sched_q_len\t: %lu\n",
		(unsigned long)dp->stat.tx_sched_q_len);

	return ret;
}

TEL_DEBUG_ENTRY(tx_q);
TEL_DEBUG_ENTRY(stat);

static ssize_t read_wm(struct file *file, char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct data_path *dp = file->private_data;
	char buf[dp_priority_cnt * 8 + 1];
	int *wm = dp->tx_wm;
	char *p, *pend;
	int i;

	p = buf;
	pend = buf + sizeof(buf) - 1;
	p[0] = '\0';

	for (i = 0; i < dp_priority_cnt; ++i)
		p += snprintf(p, pend - p, "%d ", wm[i]);

	buf[strlen(buf) - 1] = '\n';

	return simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
}

static ssize_t write_wm(struct file *file, const char __user *user_buf,
	size_t count, loff_t *ppos)
{
	struct data_path *dp = file->private_data;
	char buf[dp_priority_cnt * 8 + 1];
	int *wm = dp->tx_wm;
	char *p, *tok;
	const char delim[] = " \t";
	int i;
	int tmp;

	if (count > sizeof(buf) - 1) {
		pr_err("%s: user_buf is too large(%zu), expect less than %zu\n",
			__func__, count, sizeof(buf) - 1);
		return -EFAULT;
	}
	if (copy_from_user(buf, user_buf, count))
		return -EFAULT;

	buf[count] = '\0';

	p = buf;

	for (i = 0; i < dp_priority_cnt; ++i) {
		while (p && *p && isspace(*p))
			++p;

		tok = strsep(&p, delim);
		if (!tok)
			break;

		if (kstrtoint(tok, 10, &tmp) < 0) {
			pr_err("%s: set wm[%d] error\n", __func__, i);
			break;
		}

		if (tmp >= dp->rbctl->tx_skbuf_num) {
			pr_err("%s: set wm[%d] error, ",
				__func__, i);
			pr_err("val %d exceed tx_skbuf_num %d\n",
				tmp, dp->rbctl->tx_skbuf_num);
			return -EFAULT;
		}

		wm[i] = tmp;
	}

	if (wm[0])
		pr_err("%s: wm[0] is set to non-zero!!\n", __func__);

	for (i = 0; i < dp_priority_cnt - 1; ++i) {
		if (wm[i] > wm[i + 1]) {
			pr_err("%s: wm[%d] is larger than wm[%d], reset wm[%d]\n",
				__func__, i, i + i, i + 1);
			wm[i + 1] = wm[i];
		}
	}
	return count;
}

static const struct file_operations fops_wm = {
	.read =		read_wm,
	.write =	write_wm,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static int dp_debugfs_init(struct data_path *dp)
{
	char buf[256];
	char path[256];

	dp->dentry = debugfs_create_dir(dp->name ? dp->name : "dp",
		dp_debugfs_root_dir);
	if (!dp->dentry)
		return -ENOMEM;

	snprintf(path, sizeof(path), "../../../%s",
		dentry_path_raw(dp->rbctl->rbdir, buf, sizeof(buf)));

	if (IS_ERR_OR_NULL(debugfs_create_symlink(
				"shm", dp->dentry, path)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_file(
				"tx_q", S_IRUGO, dp->dentry,
				dp, &fops_tx_q)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_file(
				"stat", S_IRUGO, dp->dentry,
				dp, &fops_stat)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_file(
				"wm", S_IRUGO | S_IWUSR, dp->dentry,
				dp, &fops_wm)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_bool(
				"enable_piggyback", S_IRUGO | S_IWUSR,
				dp->dentry, &dp->enable_piggyback)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_bool(
				"is_tx_stopped", S_IRUGO | S_IWUSR,
				dp->dentry, &dp->is_tx_stopped)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_u32(
				"tx_q_max_len", S_IRUGO | S_IWUSR,
				dp->dentry, &dp->tx_q_max_len)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_u16(
				"max_tx_shots", S_IRUGO | S_IWUSR,
				dp->dentry, &dp->max_tx_shots)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_u16(
				"max_rx_shots", S_IRUGO | S_IWUSR,
				dp->dentry, &dp->max_rx_shots)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_u16(
				"tx_sched_delay_in_ms", S_IRUGO | S_IWUSR,
				dp->dentry, &dp->tx_sched_delay_in_ms)))
		goto error;

	if (IS_ERR_OR_NULL(debugfs_create_u16(
				"tx_q_min_sched_len", S_IRUGO | S_IWUSR,
				dp->dentry, &dp->tx_q_min_sched_len)))
		goto error;

	return 0;

error:
	debugfs_remove_recursive(dp->dentry);
	dp->dentry = NULL;
	return -1;
}

static int dp_debugfs_exit(struct data_path *dp)
{
	debugfs_remove_recursive(dp->dentry);
	dp->dentry = NULL;
	return 0;
}

struct data_path *data_path_open(struct data_path_callback *cbs)
{
	struct data_path *dp = &data_path;

	if (!cbs) {
		pr_err("%s: cbs is NULL\n", __func__);
		return NULL;
	}

	if (atomic_cmpxchg(&dp->state, dp_state_idle,
			   dp_state_opening) != dp_state_idle) {
		pr_err("%s: path is already opened(state %d)\n",
			 __func__, atomic_read(&dp->state));
		return NULL;
	}

	dp->tx_q_max_len = MAX_TX_Q_LEN;
	dp->is_tx_stopped = false;
	tx_q_init(dp);
	dp->tx_wm[dp_priority_high] = 0;
	dp->tx_wm[dp_priority_default]
		= dp->rbctl->tx_skbuf_num / 10;

	dp->enable_piggyback = true;

	dp->max_tx_shots = MAX_TX_SHOTS;
	dp->max_rx_shots = MAX_RX_SHOTS;

	dp->tx_sched_delay_in_ms = TX_SCHED_DELAY;
	dp->tx_q_min_sched_len = TX_MIN_SCHED_LEN;

	memset(&dp->stat, 0, sizeof(dp->stat));

	dp->cbs = cbs;
	tasklet_init(&dp->tx_tl, data_path_tx_func,
		     (unsigned long)dp);
	tasklet_init(&dp->rx_tl, data_path_rx_func,
		     (unsigned long)dp);

	init_timer(&dp->tx_sched_timer);
	dp->tx_sched_timer.function = tx_sched_timeout;
	dp->tx_sched_timer.data =
		(unsigned long)dp;

	if (dp_debugfs_init(dp) < 0) {
		pr_err("%s: debugfs failed\n", __func__);
		atomic_set(&dp->state, dp_state_idle);
		return NULL;
	}

	atomic_set(&dp->state, dp_state_opened);

	return dp;
}
EXPORT_SYMBOL(data_path_open);

void data_path_close(struct data_path *dp)
{
	if (!dp) {
		pr_err("%s: empty data channel\n", __func__);
		return;
	}

	if (atomic_cmpxchg(&dp->state, dp_state_opened,
			   dp_state_closing) != dp_state_opened) {
		pr_err("%s: path is already opened(state %d)\n",
			 __func__, atomic_read(&dp->state));
		return;
	}

	dp_debugfs_exit(dp);

	dp->tx_q_max_len = 0;
	dp->is_tx_stopped = false;
	tx_q_clean(dp);

	del_timer_sync(&dp->tx_sched_timer);

	tasklet_kill(&dp->tx_tl);
	tasklet_kill(&dp->rx_tl);
	dp->cbs = NULL;

	atomic_set(&dp->state, dp_state_idle);
}
EXPORT_SYMBOL(data_path_close);

void dp_rb_stop_cb(struct shm_rbctl *rbctl)
{
	struct data_path *dp;

	if (!rbctl)
		return;

	shm_rb_stop(rbctl);

	dp = rbctl->priv;

	__pm_wakeup_event(&acipc_wakeup, 5000);
	pr_warn("MSOCK: dp_rb_stop_cb!!!\n");

	if (dp && (atomic_read(&dp->state) == dp_state_opened)) {
		if (dp->cbs && dp->cbs->rx_stop)
			dp->cbs->rx_stop();
		data_path_schedule_rx(dp);
	}
}

void dp_rb_resume_cb(struct shm_rbctl *rbctl)
{
	struct data_path *dp;

	if (!rbctl)
		return;

	shm_rb_resume(rbctl);

	dp = rbctl->priv;

	__pm_wakeup_event(&acipc_wakeup, 2000);
	pr_warn("MSOCK: dp_rb_resume_cb!!!\n");

	if (dp && (atomic_read(&dp->state) == dp_state_opened)) {
		/* do not need to check queue length,
		 * as we need to resume upper layer in tx_func */
		data_path_schedule_tx(dp);
	}
}

void dp_packet_send_cb(struct shm_rbctl *rbctl)
{
	struct data_path *dp;
	struct shm_skctl *skctl;
	static unsigned long last_time = INITIAL_JIFFIES;

	if (!rbctl)
		return;

	dp = rbctl->priv;

	dp->stat.rx_interrupts++;

	/*
	 * hold 2s wakeup source for user space
	 * do not try to hold again if it is already held in last 0.5s
	 */
	if (time_after(jiffies, last_time + HZ / 2)) {
		__pm_wakeup_event(&dp_rx_wakeup, 2000);
		last_time = jiffies;
	}

	skctl = rbctl->skctl_va;
	trace_psd_recv_irq(skctl->cp_wptr);

	data_path_schedule_rx(dp);
}

static size_t dp_get_packet_length(const unsigned char *hdr)
{
	struct shm_psd_skhdr *skhdr = (struct shm_psd_skhdr *)hdr;
	return skhdr->length + sizeof(*skhdr);
}

struct shm_callback dp_shm_cb = {
	.get_packet_length = dp_get_packet_length,
};

struct shm_rbctl psd_rbctl = {
	.name = "cp-psd",
	.cbs = &dp_shm_cb,
	.priv = &data_path,
	.va_lock = __MUTEX_INITIALIZER(psd_rbctl.va_lock),
};

/* cp psd xmit stopped notify interrupt */
static u32 acipc_cb_psd_rb_stop(u32 status)
{
	dp_rb_stop_cb(&psd_rbctl);
	return 0;
}

/* cp psd wakeup ap xmit interrupt */
static u32 acipc_cb_psd_rb_resume(u32 status)
{
	dp_rb_resume_cb(&psd_rbctl);
	return 0;
}

/* psd new packet arrival interrupt */
static u32 acipc_cb_psd_cb(u32 status)
{
	dp_packet_send_cb(&psd_rbctl);
	return 0;
}

static int dp_acipc_init(void)
{
	/* we do not check any return value */
	acipc_event_bind(ACIPC_RINGBUF_PSD_TX_STOP, acipc_cb_psd_rb_stop,
		       ACIPC_CB_NORMAL, NULL);
	acipc_event_bind(ACIPC_RINGBUF_PSD_TX_RESUME, acipc_cb_psd_rb_resume,
		       ACIPC_CB_NORMAL, NULL);
	acipc_event_bind(ACIPC_SHM_PSD_PACKET_NOTIFY, acipc_cb_psd_cb,
		       ACIPC_CB_NORMAL, NULL);

	return 0;
}

/* acipc_exit used to unregister interrupt call-back function */
static void dp_acipc_exit(void)
{
	acipc_event_unbind(ACIPC_SHM_PSD_PACKET_NOTIFY);
	acipc_event_unbind(ACIPC_RINGBUF_PSD_TX_RESUME);
	acipc_event_unbind(ACIPC_RINGBUF_PSD_TX_STOP);
}

static int cp_link_status_notifier_func(struct notifier_block *this,
	unsigned long code, void *cmd)
{
	data_path_broadcast_msg((int)code);
	return 0;
}

static struct notifier_block cp_link_status_notifier = {
	.notifier_call = cp_link_status_notifier_func,
};

#define SHM_PSD_TX_SKBUF_SIZE	2048	/* PSD tx maximum packet size */
#define SHM_PSD_RX_SKBUF_SIZE	16384	/* PSD rx maximum packet size */
static int shm_param_init(const struct cpload_cp_addr *addr)
{
	if (!addr)
		return -1;

	/* psd dedicated ring buffer */
	psd_rbctl.skctl_pa = addr->psd_skctl_pa;

	psd_rbctl.tx_skbuf_size = SHM_PSD_TX_SKBUF_SIZE;
	psd_rbctl.rx_skbuf_size = SHM_PSD_RX_SKBUF_SIZE;

	psd_rbctl.tx_pa = addr->psd_tx_pa;
	psd_rbctl.rx_pa = addr->psd_rx_pa;

	psd_rbctl.tx_total_size = addr->psd_tx_total_size;
	psd_rbctl.rx_total_size = addr->psd_rx_total_size;

	psd_rbctl.tx_skbuf_num =
		psd_rbctl.tx_total_size /
		psd_rbctl.tx_skbuf_size;
	psd_rbctl.rx_skbuf_num =
		psd_rbctl.rx_total_size /
		psd_rbctl.rx_skbuf_size;

	psd_rbctl.tx_skbuf_low_wm =
		(psd_rbctl.tx_skbuf_num + 1) / 4;
	psd_rbctl.rx_skbuf_low_wm =
		(psd_rbctl.rx_skbuf_num + 1) / 4;

	return 0;
}

static int cp_mem_set_notifier_func(struct notifier_block *this,
	unsigned long code, void *cmd)
{
	struct cpload_cp_addr *addr = (struct cpload_cp_addr *)cmd;

	if (!addr->first_boot)
		shm_rb_exit(&psd_rbctl);

	shm_param_init(addr);
	if (shm_rb_init(&psd_rbctl, dp_debugfs_root_dir) < 0)
		pr_err("%s: init psd rbctl failed\n", __func__);

	return 0;
}

static struct notifier_block cp_mem_set_notifier = {
	.notifier_call = cp_mem_set_notifier_func,
};

int data_path_init(void)
{
	struct data_path *dp = &data_path;

	tel_debugfs_root_dir = tel_debugfs_get();
	if (!tel_debugfs_root_dir)
		return -1;

	dp_debugfs_root_dir = debugfs_create_dir("data_path",
		tel_debugfs_root_dir);
	if (IS_ERR_OR_NULL(dp_debugfs_root_dir))
		goto putrootfs;

	wakeup_source_init(&dp_rx_wakeup, "dp_rx_wakeups");

	dp->rbctl = &psd_rbctl;
	atomic_set(&dp->state, dp_state_idle);

	if (register_cp_link_status_notifier(&cp_link_status_notifier) < 0)
		goto exit1;

	if (register_cp_mem_set_notifier(&cp_mem_set_notifier) < 0)
		goto exit2;

	if (dp_acipc_init() < 0)
		goto unregister;

	return 0;

unregister:
	unregister_cp_mem_set_notifier(&cp_mem_set_notifier);
exit2:
	unregister_cp_link_status_notifier(&cp_link_status_notifier);
exit1:
	wakeup_source_trash(&dp_rx_wakeup);

	debugfs_remove(dp_debugfs_root_dir);
	dp_debugfs_root_dir = NULL;

putrootfs:
	tel_debugfs_put(tel_debugfs_root_dir);
	tel_debugfs_root_dir = NULL;

	return -1;
}

void data_path_exit(void)
{
	dp_acipc_exit();

	unregister_cp_mem_set_notifier(&cp_mem_set_notifier);

	unregister_cp_link_status_notifier(&cp_link_status_notifier);

	wakeup_source_trash(&dp_rx_wakeup);

	debugfs_remove(dp_debugfs_root_dir);
	dp_debugfs_root_dir = NULL;
	tel_debugfs_put(tel_debugfs_root_dir);
	tel_debugfs_root_dir = NULL;
}

