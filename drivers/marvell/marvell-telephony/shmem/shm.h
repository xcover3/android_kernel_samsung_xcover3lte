/*
    shm.h Created on: Aug 2, 2010, Jinhua Huang <jhhuang@marvell.com>

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

#ifndef _SHM_H_
#define _SHM_H_

#include <linux/skbuff.h>
#include <linux/mutex.h>
#include <linux/clk/mmpcpdvc.h>
#include "shm_common.h"
#include "pxa_cp_load_ioctl.h"
#include "acipcd.h"
#include "amipcd.h"

enum shm_grp_type {
	shm_grp_cp,
	shm_grp_m3,
	shm_grp_total_cnt
};

enum shm_rb_type {
	shm_rb_main,
	shm_rb_psd,
	shm_rb_diag,
	shm_rb_m3,
	shm_rb_total_cnt
};

struct shm_rbctl {
	/*
	 * name
	 */
	const char *name;

	/*
	 * type
	 */
	enum shm_rb_type rb_type;

	/*
	 * group type
	 */
	enum shm_grp_type grp_type;

	/*
	 * callback function
	 */
	struct shm_callback *cbs;

	/*
	 * private, owned by upper layer
	 */
	void *priv;

	/*
	 * debugfs dir
	 */
	struct dentry *rbdir;

	/*
	 * key section pointer
	 */
	unsigned long     skctl_pa;
	struct shm_skctl *skctl_va;

	/*
	 * TX buffer
	 */
	unsigned long  tx_pa;
	void          *tx_va;
	int            tx_total_size;
	int            tx_skbuf_size;
	int            tx_skbuf_num;
	int            tx_skbuf_low_wm;
	bool           tx_cacheable;

	/*
	 * RX buffer
	 */
	unsigned long  rx_pa;
	void          *rx_va;
	int            rx_total_size;
	int            rx_skbuf_size;
	int            rx_skbuf_num;
	int            rx_skbuf_low_wm;
	bool           rx_cacheable;

	/*
	 * flow control flag
	 *
	 * I'm sure these two global variable will never enter race condition,
	 * so we needn't any exclusive lock mechanism to access them.
	 */
	bool is_ap_xmit_stopped;
	bool is_cp_xmit_stopped;

	/*
	 * statistics for ring buffer flow control interrupts
	 */
	unsigned long ap_stopped_num;
	unsigned long ap_resumed_num;
	unsigned long cp_stopped_num;
	unsigned long cp_resumed_num;
	/*
	 * lock for virtual address mapping
	 */
	struct mutex	va_lock;
};

struct shm_callback {
	size_t (*get_packet_length)(const unsigned char *);
};

/* share memory control block structure */
struct shm_skctl {
	/* up-link control block, AP write, CP read */
	volatile int ap_wptr;
	volatile int cp_rptr;
	volatile unsigned int ap_port_fc;

	/* down-link control block, CP write, AP read */
	volatile int ap_rptr;
	volatile int cp_wptr;
	volatile unsigned int cp_port_fc;
};

extern struct shm_rbctl shm_rbctl[];

/* get the next tx socket buffer slot */
static inline int shm_get_next_tx_slot(struct shm_rbctl *rbctl, int slot)
{
	return __shm_get_next_slot(rbctl->tx_skbuf_num, slot);
}

/* get the next rx socket buffer slot */
static inline int shm_get_next_rx_slot(struct shm_rbctl *rbctl, int slot)
{
	return __shm_get_next_slot(rbctl->rx_skbuf_num, slot);
}

/* check if up-link free socket buffer available */
static inline bool shm_is_xmit_full(struct shm_rbctl *rbctl)
{
	return __shm_is_xmit_full(rbctl->tx_skbuf_num,
		rbctl->skctl_va->ap_wptr,
		rbctl->skctl_va->cp_rptr);
}

/* check if down-link socket buffer data received */
static inline bool shm_is_recv_empty(struct shm_rbctl *rbctl)
{
	return __shm_is_recv_empty(rbctl->skctl_va->cp_wptr,
		rbctl->skctl_va->ap_rptr);
}

/* get free rx skbuf num */
static inline int shm_free_rx_skbuf(struct shm_rbctl *rbctl)
{
	return __shm_free_rx_skbuf(rbctl->skctl_va->ap_rptr,
		rbctl->skctl_va->cp_wptr, rbctl->rx_skbuf_num);
}

/* check if down-link socket buffer has enough free slot */
static inline bool shm_has_enough_free_rx_skbuf(struct shm_rbctl *rbctl)
{
	return shm_free_rx_skbuf(rbctl) > rbctl->rx_skbuf_low_wm;
}

/* get free tx skbuf num */
static inline int shm_free_tx_skbuf(struct shm_rbctl *rbctl)
{
	return __shm_free_tx_skbuf(rbctl->skctl_va->cp_rptr,
		rbctl->skctl_va->ap_wptr, rbctl->tx_skbuf_num);
}

/* update status in ring buffer fc */
static inline u32 shm_rb_stop(struct shm_rbctl *rbctl)
{
	if (!rbctl)
		return 0;

	rbctl->cp_stopped_num++;
	rbctl->is_cp_xmit_stopped = true;

	return 0;
}

static inline u32 shm_rb_resume(struct shm_rbctl *rbctl)
{
	if (!rbctl)
		return 0;

	rbctl->ap_resumed_num++;
	rbctl->is_ap_xmit_stopped = false;

	return 0;
}

static inline void shm_notify_cp_tx_resume(struct shm_rbctl *rbctl)
{
	if (!rbctl)
		return;

	rbctl->cp_resumed_num++;
	rbctl->is_cp_xmit_stopped = false;
}

static inline void shm_notify_ap_tx_stopped(struct shm_rbctl *rbctl)
{
	if (!rbctl)
		return;

	rbctl->ap_stopped_num++;
	rbctl->is_ap_xmit_stopped = true;
}

static inline void shm_flush_dcache(struct shm_rbctl *rbctl,
	void *addr, size_t size)
{
	if (rbctl && rbctl->tx_cacheable)
		__shm_flush_dcache(addr, size);
}

static inline void shm_invalidate_dcache(struct shm_rbctl *rbctl,
	void *addr, size_t size)
{
	if (rbctl && rbctl->rx_cacheable)
		__shm_invalidate_dcache(addr, size);
}

/*
 * functions exported by this module
 */
/* init & exit */
extern int tel_shm_init(enum shm_grp_type grp_type,
	const void *data);
extern void tel_shm_exit(enum shm_grp_type grp_type);
extern int shm_debugfs_init(void);
extern void shm_debugfs_exit(void);
extern void shm_rb_data_init(struct shm_rbctl *rbctl);
extern void shm_lock_init(void);
extern int shm_free_rx_skbuf_safe(struct shm_rbctl *rbctl);
extern int shm_free_tx_skbuf_safe(struct shm_rbctl *rbctl);

/* open & close */
extern struct shm_rbctl *shm_open(enum shm_rb_type rb_type,
				  struct shm_callback *cb, void *priv);
extern void shm_close(struct shm_rbctl *rbctl);

/* xmit and recv */
extern void shm_xmit(struct shm_rbctl *rbctl, struct sk_buff *skb);
extern struct sk_buff *shm_recv(struct shm_rbctl *rbctl);

#endif /* _SHM_H_ */
