/*
 * Marvell 88PM886 registers
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *  Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM886_REG_H
#define __LINUX_MFD_88PM886_REG_H
/*
 * This file is just used for the common registers,
 * which are shared by sub-clients
 */

/*--base page:--------------------------------------------------------------*/
#define PM886_STATUS1			(0x1)

#define PM886_MISC_CONFIG1		(0x14)
#define PM886_LONKEY_RST		(1 << 3)

/* gpio */
#define PM886_GPIO_CTRL1		(0x30)
#define PM886_GPIO0_VAL_MSK		(0x1 << 0)
#define PM886_GPIO0_MODE_MSK		(0x7 << 1)
#define PM886_GPIO1_VAL_MSK		(0x1 << 4)
#define PM886_GPIO1_MODE_MSK		(0x7 << 5)

#define PM886_GPIO_CTRL2		(0x31)
#define PM886_GPIO2_VAL_MSK		(0x1 << 0)
#define PM886_GPIO2_MODE_MSK		(0x7 << 1)

#define PM886_GPIO_CTRL3		(0x32)

#define PM886_GPIO_CTRL4		(0x33)
#define PM886_GPIO5V_1_VAL_MSK		(0x1 << 0)
#define PM886_GPIO5V_1_MODE_MSK		(0x7 << 1)
#define PM886_GPIO5V_2_VAL_MSK		(0x1 << 4)
#define PM886_GPIO5V_2_MODE_MSK		(0x7 << 5)

#define PM886_RTC_ALARM_CTRL1		(0xd0)
#define PM886_ALARM_WAKEUP		(1 << 4)
#define PM886_USE_XO			(1 << 7)

#define PM886_AON_CTRL2			(0xe2)
#define PM886_AON_CTRL3			(0xe3)
#define PM886_AON_CTRL4			(0xe4)

/* 0xea, 0xeb, 0xec, 0xed are reserved by RTC */
#define PM886_RTC_SPARE6		(0xef)
/*-------------------------------------------------------------------------*/

/*--power page:------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/

/*--gpadc page:------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/

/*--test page:-------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/
#endif

