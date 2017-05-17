/*
 * Copyright (c) 2006-2014 MStar Semiconductor, Inc.
 * All rights reserved.

 * Unless otherwise stipulated in writing, any and all information contained
 * herein regardless in any format shall remain the sole proprietary of
 * MStar Semiconductor Inc. and be kept in strict confidence
 * (??MStar Confidential Information??) by the recipient.
 * Any unauthorized act including without limitation unauthorized disclosure,
 * copying, use, reproduction, sale,
 * reverse engineering and compiling of the contents of MStar Confidential
 * Information is unlawful and strictly prohibited. MStar hereby reserves the
 * rights to any and all damages, losses, costs and expenses resulting
 * therefrom.
 */

/**
 *
 * @file    mstar_drv_ic_fw_porting_layer.c
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#include "mstar_drv_ic_fw_porting_layer.h"

void DrvIcFwLyrOptimizeCurrentConsumption(void)
{
	DrvFwCtrlOptimizeCurrentConsumption();
}

u8 DrvIcFwLyrGetChipType(void)
{
	return DrvFwCtrlGetChipType();
}

void DrvIcFwLyrGetCustomerFirmwareVersion(u16 *pMajor, u16 *pMinor,
		u8 **ppVersion)
{
	DrvFwCtrlGetCustomerFirmwareVersion(pMajor, pMinor, ppVersion);
}

void DrvIcFwLyrGetPlatformFirmwareVersion(u8 **ppVersion)
{
	DrvFwCtrlGetPlatformFirmwareVersion(ppVersion);
}

s32 DrvIcFwLyrUpdateFirmware(u8 szFwData[][1024], enum EmemType_e eEmemType)
{
	return DrvFwCtrlUpdateFirmware(szFwData, eEmemType);
}

s32 DrvIcFwLyrUpdateFirmwareBySdCard(const char *pFilePath)
{
	return DrvFwCtrlUpdateFirmwareBySdCard(pFilePath);
}

u32 DrvIcFwLyrIsRegisterFingerTouchInterruptHandler(void)
{
	DBG("*** %s() ***\n", __func__);
	/*
	 * Indicate that it is necessary to register interrupt handler
	 * with GPIO INT pin when firmware is running on IC
	 */
	return 1;
}

void DrvIcFwLyrHandleFingerTouch(u8 *pPacket, u16 nLength)
{
	DrvFwCtrlHandleFingerTouch();
}

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP

void DrvIcFwLyrOpenGestureWakeup(u32 *pWakeupMode)
{
	DrvFwCtrlOpenGestureWakeup(pWakeupMode);
}

void DrvIcFwLyrCloseGestureWakeup(void)
{
	DrvFwCtrlCloseGestureWakeup();
}

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
void DrvIcFwLyrOpenGestureDebugMode(u8 nGestureFlag)
{
	DrvFwCtrlOpenGestureDebugMode(nGestureFlag);
}
void DrvIcFwLyrCloseGestureDebugMode(void)
{

	DrvFwCtrlCloseGestureDebugMode();
}
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */


#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG

#if defined(CONFIG_ENABLE_CHIP_MSG26XXM)
u16 DrvIcFwLyrGetFirmwareMode(void)
{
	return DrvFwCtrlGetFirmwareMode();
}
#endif /* CONFIG_ENABLE_CHIP_MSG26XXM */

u16 DrvIcFwLyrChangeFirmwareMode(u16 nMode)
{
	return DrvFwCtrlChangeFirmwareMode(nMode);
}

void DrvIcFwLyrGetFirmwareInfo(struct FirmwareInfo_t *pInfo)
{
	DrvFwCtrlGetFirmwareInfo(pInfo);
}

void DrvIcFwLyrRestoreFirmwareModeToLogDataMode(void)
{
	DrvFwCtrlRestoreFirmwareModeToLogDataMode();
}

#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */


#ifdef CONFIG_ENABLE_ITO_MP_TEST

void DrvIcFwLyrCreateMpTestWorkQueue(void)
{
	DrvMpTestCreateMpTestWorkQueue();
}

void DrvIcFwLyrScheduleMpTestWork(ItoTestMode_e eItoTestMode)
{
	DrvMpTestScheduleMpTestWork(eItoTestMode);
}

s32 DrvIcFwLyrGetMpTestResult(void)
{
	return DrvMpTestGetTestResult();
}

void DrvIcFwLyrGetMpTestFailChannel(ItoTestMode_e eItoTestMode,
		u8 *pFailChannel, u32 *pFailChannelCount)
{
	return DrvMpTestGetTestFailChannel(eItoTestMode, pFailChannel,
			pFailChannelCount);
}

void DrvIcFwLyrGetMpTestDataLog(ItoTestMode_e eItoTestMode, u8 *pDataLog,
		u32 *pLength)
{
	return DrvMpTestGetTestDataLog(eItoTestMode, pDataLog, pLength);
}

#if defined(CONFIG_ENABLE_CHIP_MSG26XXM)
void DrvIcFwLyrGetMpTestScope(TestScopeInfo_t *pInfo)
{
	return DrvMpTestGetTestScope(pInfo);
}
#endif /* CONFIG_ENABLE_CHIP_MSG26XXM */

#endif /* CONFIG_ENABLE_ITO_MP_TEST */

#ifdef CONFIG_ENABLE_SEGMENT_READ_FINGER_TOUCH_DATA

#if defined(CONFIG_ENABLE_CHIP_MSG26XXM)
void DrvIcFwLyrGetTouchPacketAddress(u16 *pDataAddress, u16 *pFlagAddress)
{
	return DrvFwCtrlGetTouchPacketAddress(pDataAddress, pFlagAddress);
}
#endif /* CONFIG_ENABLE_CHIP_MSG26XXM */

#endif /* CONFIG_ENABLE_SEGMENT_READ_FINGER_TOUCH_DATA */
