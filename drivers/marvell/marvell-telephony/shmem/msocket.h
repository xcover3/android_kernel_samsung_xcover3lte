/*
    msocket.h Created on: Aug 2, 2010, Jinhua Huang <jhhuang@marvell.com>

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

#ifndef MSOCKET_H_
#define MSOCKET_H_

#include <linux/skbuff.h>
#include "pxa_cp_load_ioctl.h"
#include "shm.h"
#include "util.h"

/* the magic is 8-bit byte, should not use 300 */
/* #define MSOCKET_MAJOR         300   */  /* The major number of the devices */

#define MSOCKET_IOC_MAGIC       0xD0
#define MSOCKET_IOC_BIND	_IO(MSOCKET_IOC_MAGIC, 1)
#define MSOCKET_IOC_UP		_IO(MSOCKET_IOC_MAGIC, 2)
#define MSOCKET_IOC_DOWN	_IO(MSOCKET_IOC_MAGIC, 3)
#define MSOCKET_IOC_PMIC_QUERY	_IOR(MSOCKET_IOC_MAGIC, 4, int)
#define MSOCKET_IOC_CONNECT	_IO(MSOCKET_IOC_MAGIC, 5)
#define MSOCKET_IOC_RESET_CP_REQUEST _IO(MSOCKET_IOC_MAGIC, 6)
#define MSOCKET_IOC_ERRTO	_IO(MSOCKET_IOC_MAGIC, 7)
#define MSOCKET_IOC_RECOVERY	_IO(MSOCKET_IOC_MAGIC, 8)
#define MSOCKET_IOC_NETWORK_MODE_CP_NOTIFY _IOW(MSOCKET_IOC_MAGIC, 9, int)

#define MSOCKET_IOC_MAXNR	9

/* flags for msend/mrecv */
#define MSOCKET_KERNEL		0	/* can be blocked in kernel context */
#define MSOCKET_ATOMIC		1	/* should be atomic in interrupt */

#define DUMP_PORT(x)	(1<<((x)+3))
#define DUMP_TX		(1<<0)
#define DUMP_RX		(1<<1)
#define DUMP_TX_SP (1<<2)
#define DUMP_RX_SP (1<<3)

#define DATA_TX		0
#define DATA_RX		1

extern bool cp_is_synced;
extern struct completion cp_peer_sync;

extern bool m3_is_synced;
extern struct completion m3_peer_sync;

struct rm_m3_addr;
extern int cp_shm_ch_init(const struct cpload_cp_addr *addr, u32 lpm_qos);
extern void cp_shm_ch_deinit(void);
extern int m3_shm_ch_init(const struct rm_m3_addr *addr);
extern void m3_shm_ch_deinit(void);

extern int register_first_cp_synced(struct notifier_block *nb);


extern int msocket(int port);
extern int msocket_with_cb(int port,
		void (*clbk)(struct sk_buff *, void *), void *arg);
extern int mclose(int sock);
extern int msend(int sock, const void *buf, int len, int flags);
extern int mrecv(int sock, void *buf, int len, int flags);
extern int msendskb(int sock, struct sk_buff *skb, int len, int flags);
extern struct sk_buff *mrecvskb(int sock, int len, int flags);
extern void data_dump(const unsigned char *data, unsigned int len, int port,
		      int direction);
extern void msocket_recv_throttled(int sock);
extern void msocket_recv_unthrottled(int sock);
/* designed for future use, not used here */
/*
 * extern void msched_work(struct work_struct *work);
 * extern int msend_skb(int sock, struct sk_buff *skb, int flags);
 * extern int mrecv_skb(int sock, struct sk_buff **pskb, int flags);
*/
DECLARE_BLOCKING_NOTIFIER(cp_link_status);

struct cp_keysection {
#define PMIC_MASTER_FLAG	0x4D415354
	/* PMIC SSP master status setting query */
	volatile unsigned int ap_pcm_master;
	volatile unsigned int cp_pcm_master;
	volatile unsigned int modem_ddrfreq;

	/* DIAG specific info */
	volatile unsigned int diag_header_ptr;
	volatile unsigned int diag_cp_db_ver;
	volatile unsigned int diag_ap_db_ver;

	volatile unsigned int reset_request;
	volatile unsigned int ap_pm_status_request;
	volatile unsigned int profile_number;

	/* dvc voltage table number */
	volatile unsigned int dvc_vol_tbl_num;
	volatile unsigned int dvc_vol_tbl[16];

#define VERSION_MAGIC_FLAG 0x56455253
#define VERSION_NUMBER_FLAG 0x1
	volatile unsigned int version_magic;
	volatile unsigned int version_number;

	volatile unsigned int dfc_dclk_num;
	volatile unsigned int dfc_dclk[16];

	/*L+G or G+L*/
	volatile unsigned int network_mode;

	/* uuid reserved for SSIPC solution */
	volatile unsigned int uuid_high;
	volatile unsigned int uuid_low;

	/* dvc voltage and frequency */
	volatile unsigned int cp_freq[MAX_CP_PPNUM];
	volatile unsigned int cp_vol[MAX_CP_PPNUM];
	volatile unsigned int msa_dvc_vol;
};

extern struct cp_keysection *cpks;
extern struct mutex cpks_lock;
extern struct dentry *cpks_rootdir;

/* check if cp pmic is in master mode */
static inline bool shm_is_cp_pmic_master(struct shm_rbctl *rbctl)
{
	return cpks->cp_pcm_master == PMIC_MASTER_FLAG;
}

#endif /* MSOCKET_H_ */
