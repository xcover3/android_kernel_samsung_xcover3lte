/*
    acipcd.h Created on: Aug 3, 2010, Jinhua Huang <jhhuang@marvell.com>

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

#ifndef _ACIPCD_H_
#define _ACIPCD_H_

#include <linux/types.h>
#include <linux/version.h>
#include <linux/ratelimit.h>

#include <linux/pxa9xx_acipc.h>

/*notify cp that ap will reset cp to let cp exit WFI state */
static inline void acipc_notify_reset_cp_request(void)
{
	pr_warn(
		"MSOCK: acipc_notify_reset_cp_request!!!\n");
	acipc_event_set(ACIPC_MODEM_DDR_UPDATE_REQ);
}

extern int acipc_init(u32 lpm_qos);
extern void acipc_exit(void);
extern struct wakeup_source acipc_wakeup;
extern void acipc_reset_cp_request(void);
extern void acipc_ap_block_cpuidle_axi(bool block);
#endif /* _ACIPCD_H_ */
