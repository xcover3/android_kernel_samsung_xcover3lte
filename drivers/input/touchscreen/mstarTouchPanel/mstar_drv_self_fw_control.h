/*
 * Copyright (c) 2006-2014 MStar Semiconductor, Inc.
 * All rights reserved.

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
 * @file    mstar_drv_self_fw_control.h
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#ifndef __MSTAR_DRV_SELF_FW_CONTROL_H__
#define __MSTAR_DRV_SELF_FW_CONTROL_H__

/*--------------------------------------------------------------------------*/
/* INCLUDE FILE                                                             */
/*--------------------------------------------------------------------------*/

#include "mstar_drv_common.h"

#if defined(CONFIG_ENABLE_CHIP_MSG21XXA) || defined(CONFIG_ENABLE_CHIP_MSG22XX)

/*--------------------------------------------------------------------------*/
/* COMPILE OPTION DEFINITION                                                */
/*--------------------------------------------------------------------------*/

/* #define CONFIG_SWAP_X_Y */

/* #define CONFIG_REVERSE_X */
/* #define CONFIG_REVERSE_Y */

/*--------------------------------------------------------------------------*/
/* PREPROCESSOR CONSTANT DEFINITION                                         */
/*--------------------------------------------------------------------------*/

#define DEMO_MODE_PACKET_LENGTH		(8)
#define MAX_TOUCH_NUM			(2)

#define MSG21XXA_FIRMWARE_MAIN_BLOCK_SIZE (32) /* 32K */
#define MSG21XXA_FIRMWARE_INFO_BLOCK_SIZE (1)  /* 1K */
/* 33K */
#define MSG21XXA_FIRMWARE_WHOLE_SIZE \
	(MSG21XXA_FIRMWARE_MAIN_BLOCK_SIZE + MSG21XXA_FIRMWARE_INFO_BLOCK_SIZE)

#define MSG22XX_FIRMWARE_MAIN_BLOCK_SIZE (48)  /* 48K */
#define MSG22XX_FIRMWARE_INFO_BLOCK_SIZE (512) /* 512Byte */


#define FIRMWARE_MODE_DEMO_MODE      (0x00)
#define FIRMWARE_MODE_DEBUG_MODE     (0x01)
#define FIRMWARE_MODE_RAW_DATA_MODE  (0x02)

#define DEBUG_MODE_PACKET_LENGTH    (128)

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
#define FIRMWARE_GESTURE_INFORMATION_MODE_A	(0x00)
#define FIRMWARE_GESTURE_INFORMATION_MODE_B	(0x01)
#define FIRMWARE_GESTURE_INFORMATION_MODE_C	(0x02)
#endif /* CONFIG_ENABLE_GESTURE_INFORMATION_MODE */
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

/*--------------------------------------------------------------------------*/
/* DATA TYPE DEFINITION                                                     */
/*--------------------------------------------------------------------------*/

struct TouchPoint_t {
	u16 nX;
	u16 nY;
};

struct TouchInfo_t {
	u8 nTouchKeyMode;
	u8 nTouchKeyCode;
	u8 nFingerNum;
	struct TouchPoint_t tPoint[MAX_TOUCH_NUM];
};

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG

struct FirmwareInfo_t {
	u8 nFirmwareMode;
	u8 nLogModePacketHeader;
	u16 nLogModePacketLength;
	u8 nIsCanChangeFirmwareMode;
};

#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

/*--------------------------------------------------------------------------*/
/* GLOBAL FUNCTION DECLARATION                                              */
/*--------------------------------------------------------------------------*/

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
extern void DrvFwCtrlOpenGestureWakeup(u32 *pMode);
extern void DrvFwCtrlCloseGestureWakeup(void);

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
extern void DrvFwCtrlOpenGestureDebugMode(u8 nGestureFlag);
extern void DrvFwCtrlCloseGestureDebugMode(void);
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
extern u16 DrvFwCtrlChangeFirmwareMode(u16 nMode);
extern void DrvFwCtrlGetFirmwareInfo(struct FirmwareInfo_t *pInfo);
extern void DrvFwCtrlRestoreFirmwareModeToLogDataMode(void);
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

extern void DrvFwCtrlOptimizeCurrentConsumption(void);
extern u8 DrvFwCtrlGetChipType(void);
extern void DrvFwCtrlGetCustomerFirmwareVersion(u16 *pMajor,
		u16 *pMinor, u8 **ppVersion);
extern void DrvFwCtrlGetPlatformFirmwareVersion(u8 **ppVersion);
extern void DrvFwCtrlHandleFingerTouch(void);
extern s32 DrvFwCtrlUpdateFirmware(u8 szFwData[][1024],
		enum EmemType_e eEmemType);
extern s32 DrvFwCtrlUpdateFirmwareBySdCard(const char *pFilePath);

extern u8 g_FwData[MAX_UPDATE_FIRMWARE_BUFFER_SIZE][1024];
extern u32 g_FwDataCount;
extern struct mutex g_Mutex;

#ifdef CONFIG_TP_HAVE_KEY
extern const int g_TpVirtualKey[];

#ifdef CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE
extern const int g_TpVirtualKeyDimLocal[][4];
#endif /* CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE */
#endif /* CONFIG_TP_HAVE_KEY */

#endif /* CONFIG_ENABLE_CHIP_MSG21XXA || CONFIG_ENABLE_CHIP_MSG22XX */

#endif  /* __MSTAR_DRV_SELF_FW_CONTROL_H__ */
