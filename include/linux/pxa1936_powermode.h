/*
 * linux/include/pxa1936_powermode.h
 *
 * Author:	Xiaoguang Chen <chenxg@marvell.com>
 * Copyright:	(C) 2015 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __PXA1936_POWERMODE_H__
#define __PXA1936_POWERMODE_H__

enum lowpower_state {
	/* core level power modes */
	POWER_MODE_CORE_INTIDLE,	/* C12 */
	POWER_MODE_CORE_EXTIDLE,	/* C13 */
	POWER_MODE_CORE_POWERDOWN,	/* C22 */

	/* MP level power modes */
	POWER_MODE_MP_IDLE_CORE_EXTIDLE,/* M11_C13 */
	POWER_MODE_MP_IDLE_CORE_POWERDOWN,/* M11_C22 */
	POWER_MODE_MP_POWERDOWN_L2_ON,	/* M21 */
	POWER_MODE_MP_POWERDOWN_L2_OFF,	/* M22 */
	POWER_MODE_MP_POWERDOWN = POWER_MODE_MP_POWERDOWN_L2_OFF,

	/* AP subsystem level power modes */
	POWER_MODE_APPS_IDLE,		/* D1P */
	POWER_MODE_APPS_IDLE_DDR,	/* D1PDDR */
	POWER_MODE_APPS_SLEEP,		/* D1PP */
	POWER_MODE_APPS_SLEEP_UDR,	/* D1PPSTDBY */

	/* chip level power modes */
	POWER_MODE_SYS_SLEEP_VCTCXO,	/* D1 */
	POWER_MODE_SYS_SLEEP_VCTCXO_OFF,/* D1 */
	POWER_MODE_SYS_SLEEP = POWER_MODE_SYS_SLEEP_VCTCXO_OFF,
	POWER_MODE_UDR_VCTCXO,	/* D2 */
	POWER_MODE_UDR_VCTCXO_OFF,	/* D2PP */
	POWER_MODE_UDR = POWER_MODE_UDR_VCTCXO_OFF,
	POWER_MODE_MAX = 15,		/* maximum lowpower states */
};

#endif /* __PXA1936_POWERMODE_H__ */
