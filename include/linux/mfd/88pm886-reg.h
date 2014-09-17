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
#define PM886_CHG_DET			(1 << 2)

#define PM886_MISC_CONFIG1		(0x14)
#define PM886_LONKEY_RST		(1 << 3)

/* gpio */
#define PM886_GPIO_CTRL1		(0x30)
#define PM886_GPIO0_VAL_MSK		(0x1 << 0)
#define PM886_GPIO0_MODE_MSK		(0x7 << 1)
#define PM886_GPIO1_VAL_MSK		(0x1 << 4)
#define PM886_GPIO1_MODE_MSK		(0x7 << 5)
#define PM886_GPIO1_SET_DVC		(0x2 << 5)

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
#define PM886_BUCK1_VOUT	(0xa5)
#define PM886_BUCK1_1_VOUT	(0xa6)
#define PM886_BUCK1_2_VOUT	(0xa7)
#define PM886_BUCK1_3_VOUT	(0xa8)
#define PM886_BUCK1_4_VOUT	(0x9a)
#define PM886_BUCK1_5_VOUT	(0x9b)
#define PM886_BUCK1_6_VOUT	(0x9c)
#define PM886_BUCK1_7_VOUT	(0x9d)

/*
 * buck sleep mode control registers:
 * 00-disable,
 * 01/10-sleep voltage,
 * 11-active voltage
 */
#define PM886_BUCK1_SLP_CTRL	(0xa2)
#define PM886_BUCK2_SLP_CTRL	(0xb0)
#define PM886_BUCK3_SLP_CTRL	(0xbe)
#define PM886_BUCK4_SLP_CTRL	(0xcc)
#define PM886_BUCK5_SLP_CTRL	(0xda)

/*
 * ldo sleep mode control registers:
 * 00-disable,
 * 01/10-sleep voltage,
 * 11-active voltage
 */
#define PM886_LDO1_SLP_CTRL	(0x21)
#define PM886_LDO2_SLP_CTRL	(0x27)
#define PM886_LDO3_SLP_CTRL	(0x2d)
#define PM886_LDO4_SLP_CTRL	(0x33)
#define PM886_LDO5_SLP_CTRL	(0x39)
#define PM886_LDO6_SLP_CTRL	(0x3f)
#define PM886_LDO7_SLP_CTRL	(0x45)
#define PM886_LDO8_SLP_CTRL	(0x4b)
#define PM886_LDO9_SLP_CTRL	(0x51)
#define PM886_LDO10_SLP_CTRL	(0x57)
#define PM886_LDO11_SLP_CTRL	(0x5d)
#define PM886_LDO12_SLP_CTRL	(0x63)
#define PM886_LDO13_SLP_CTRL	(0x69)
#define PM886_LDO14_SLP_CTRL	(0x6f)
#define PM886_LDO15_SLP_CTRL	(0x75)
#define PM886_LDO16_SLP_CTRL	(0x7b)

/*-------------------------------------------------------------------------*/

/*--gpadc page:------------------------------------------------------------*/

#define PM886_GPADC_CONFIG2		(0x2)
#define PM886_GPADC0_MEAS_EN		(1 << 2)
#define PM886_GPADC1_MEAS_EN		(1 << 3)
#define PM886_GPADC2_MEAS_EN		(1 << 4)
#define PM886_GPADC3_MEAS_EN		(1 << 5)

#define PM886_GPADC0_LOW_TH		(0x20)
#define PM886_GPADC1_LOW_TH		(0x21)
#define PM886_GPADC2_LOW_TH		(0x22)
#define PM886_GPADC3_LOW_TH		(0x23)

#define PM886_GPADC0_UPP_TH		(0x30)
#define PM886_GPADC1_UPP_TH		(0x31)
#define PM886_GPADC2_UPP_TH		(0x32)
#define PM886_GPADC3_UPP_TH		(0x33)

#define PM886_VBUS_MEAS1		(0x4A)
#define PM886_GPADC0_MEAS1		(0x54)
#define PM886_GPADC1_MEAS1		(0x56)
#define PM886_GPADC2_MEAS1		(0x58)
#define PM886_GPADC3_MEAS1		(0x5A)


/*--charger page:------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/

/*--test page:-------------------------------------------------------------*/

/*-------------------------------------------------------------------------*/
#endif

