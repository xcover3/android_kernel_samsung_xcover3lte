/*
 *
 * Copyright (c) 2006-2014 MStar Semiconductor, Inc.
 * All rights reserved.
 *
 * Unless otherwise stipulated in writing, any and all information contained
 * herein regardless in any format shall remain the sole proprietary of
 * MStar Semiconductor Inc. and be kept in strict confidence
 * (??MStar Confidential Information??) by the recipient.
 * Any unauthorized act including without limitation unauthorized disclosure,
 * copying, use, reproduction, sale, distribution, modification, disassembling,
 * reverse engineering and compiling of the contents of MStar Confidential
 * Information is unlawful and strictly prohibited. MStar hereby reserves the
 * rights to any and all damages, losses, costs and expenses resulting
 * therefrom.
 */

/**
 *
 * @file    mstar_drv_platform_porting_layer.h
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#ifndef __MSTAR_DRV_PLATFORM_PORTING_LAYER_H__
#define __MSTAR_DRV_PLATFORM_PORTING_LAYER_H__

/*--------------------------------------------------------------------------*/
/* INCLUDE FILE                                                             */
/*--------------------------------------------------------------------------*/

#include "mstar_drv_common.h"

/*--------------------------------------------------------------------------*/
/* PREPROCESSOR CONSTANT DEFINITION                                         */
/*--------------------------------------------------------------------------*/

/*
 * Note.
 * Please change the below GPIO pin setting to follow the platform that you
 * are using(EX. MediaTek, Spreadtrum, Qualcomm).
 */

/*
 * TODO : Please FAE colleague to confirm with customer device driver engineer
 * about the value of RST and INT GPIO setting
 */
/* #define MS_TS_MSG_IC_GPIO_RST   GPIO_TOUCH_RESET //53 //35 */
/* #define MS_TS_MSG_IC_GPIO_INT   GPIO_TOUCH_IRQ   //52 //37 */
extern u32 MS_TS_MSG_IC_GPIO_RST;
extern u32 MS_TS_MSG_IC_GPIO_INT;

#ifdef CONFIG_TP_HAVE_KEY
#define TOUCH_KEY_MENU (139) /* 229 */
#define TOUCH_KEY_HOME (172) /* 102 */
#define TOUCH_KEY_BACK (158)
#define TOUCH_KEY_SEARCH (217)

#define MAX_KEY_NUM (4)
#endif /* CONFIG_TP_HAVE_KEY */

/*--------------------------------------------------------------------------*/
/* GLOBAL FUNCTION DECLARATION                                              */
/*--------------------------------------------------------------------------*/
#ifdef CONFIG_OF
extern void DrvPlatformLyrDts(struct i2c_client *pClient);
#endif
extern void DrvPlatformLyrDisableFingerTouchReport(void);
extern void DrvPlatformLyrEnableFingerTouchReport(void);
extern void DrvPlatformLyrFingerTouchPressed(s32 nX, s32 nY, s32 nPressure,
		s32 nId);
extern void DrvPlatformLyrFingerTouchReleased(s32 nX, s32 nY);
extern s32 DrvPlatformLyrInputDeviceInitialize(struct i2c_client *pClient);
extern void DrvPlatformLyrSetIicDataRate(struct i2c_client *pClient,
		u32 nIicDataRate);
extern void DrvPlatformLyrTouchDevicePowerOff(void);
extern void DrvPlatformLyrTouchDevicePowerOn(void);
#ifdef CONFIG_ENABLE_REGULATOR_POWER_ON
extern void DrvPlatformLyrTouchDeviceRegulatorPowerOn(void);
extern void DrvPlatformLyrTouchDeviceRegulatorPowerOff(void);

extern struct regulator *g_ReguVdd;

#endif /* CONFIG_ENABLE_REGULATOR_POWER_ON */
extern void DrvPlatformLyrTouchDeviceRegisterEarlySuspend(void);
extern s32 DrvPlatformLyrTouchDeviceRegisterFingerTouchInterruptHandler(void);
extern s32 DrvPlatformLyrTouchDeviceRemove(struct i2c_client *pClient);
extern s32 DrvPlatformLyrTouchDeviceRequestGPIO(void);
extern void DrvPlatformLyrTouchDeviceResetHw(void);

#endif  /* __MSTAR_DRV_PLATFORM_PORTING_LAYER_H__ */
