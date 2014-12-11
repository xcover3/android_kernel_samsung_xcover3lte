/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (C) Copyright 2012 Marvell International Ltd.
 * All Rights Reserved
 */

#ifndef _DEVFREQ_VPU_H_
#define _DEVFREQ_VPU_H_

#include <linux/notifier.h>

enum msg_type {
	VPU_POWER_OFF = 0,
	VPU_POWER_ON,
	VPU_TARGET_SET,
};

extern void vpu_power_notify(unsigned long status, void *value);

#endif /* _DEVFREQ_VPU_H_ */
