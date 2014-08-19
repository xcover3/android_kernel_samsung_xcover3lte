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

#define PM886_RTC_ALARM_CTRL1		(0xd0)
#define PM886_ALARM_WAKEUP		(1 << 4)


/*-------------------------------------------------------------------------*/

/*--power page:------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/

/*--gpadc page:------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/

/*--test page:-------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/
#endif

