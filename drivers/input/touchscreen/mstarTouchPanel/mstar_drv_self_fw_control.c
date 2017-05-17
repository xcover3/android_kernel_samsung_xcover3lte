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
 * @file    mstar_drv_self_fw_control.c
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#include "mstar_drv_self_fw_control.h"
#include "mstar_drv_utility_adaption.h"
#include "mstar_drv_platform_porting_layer.h"

#if defined(CONFIG_ENABLE_CHIP_MSG21XXA) || defined(CONFIG_ENABLE_CHIP_MSG22XX)

static u8 _gTpVendorCode[3] = {0};

static u8 _gDwIicInfoData[1024];
/* used for MSG22XX */
static u8 _gOneDimenFwData[MSG22XX_FIRMWARE_MAIN_BLOCK_SIZE * 1024 +
	MSG22XX_FIRMWARE_INFO_BLOCK_SIZE] = {0};

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
static u32 _gGestureWakeupValue[2] = {0};
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

u8 _gIsDisableFinagerTouch = 0;

u8 g_ChipType = 0;
u8 g_DemoModePacket[DEMO_MODE_PACKET_LENGTH] = {0};

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
struct FirmwareInfo_t g_FirmwareInfo;

u8 g_LogModePacket[DEBUG_MODE_PACKET_LENGTH] = {0};
u16 g_FirmwareMode = FIRMWARE_MODE_DEMO_MODE;
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP

#if defined(CONFIG_ENABLE_GESTURE_DEBUG_MODE)
u8 _gGestureWakeupPacket[GESTURE_DEBUG_MODE_PACKET_LENGTH] = {0};
#elif defined(CONFIG_ENABLE_GESTURE_INFORMATION_MODE)
u8 _gGestureWakeupPacket[GESTURE_WAKEUP_INFORMATION_PACKET_LENGTH] = {0};
#else
#if defined(CONFIG_ENABLE_CHIP_MSG21XXA)
u8 _gGestureWakeupPacket[DEMO_MODE_PACKET_LENGTH] = {0};
#elif defined(CONFIG_ENABLE_CHIP_MSG22XX)
u8 _gGestureWakeupPacket[GESTURE_WAKEUP_PACKET_LENGTH] = {0};
#endif

#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
u8 g_GestureDebugFlag = 0x00;
u8 g_GestureDebugMode = 0x00;
u8 g_LogGestureDebug[GESTURE_DEBUG_MODE_PACKET_LENGTH] = {0};
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
u32 g_LogGestureInfor[GESTURE_WAKEUP_INFORMATION_PACKET_LENGTH] = {0};
#endif /* CONFIG_ENABLE_GESTURE_INFORMATION_MODE */

u32 g_GestureWakeupMode[2] = {0xFFFFFFFF, 0xFFFFFFFF};
u8 g_GestureWakeupFlag = 0;
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
static void _DrvFwCtrlCoordinate(u8 *pRawData, u32 *pTranX, u32 *pTranY);
#endif /* CONFIG_ENABLE_GESTURE_INFORMATION_MODE */
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

static void _DrvFwCtrlEraseEmemC32(void)
{
	DBG("*** %s() ***\n", __func__);

	/* Erase  all */

	/* Enter gpio mode */
	RegSet16BitValue(0x161E, 0xBEAF);

	/* Before gpio mode, set the control pin as the orginal status */
	RegSet16BitValue(0x1608, 0x0000);
	RegSetLByteValue(0x160E, 0x10);
	mdelay(10);

	/* ptrim = 1, h'04[2] */
	RegSetLByteValue(0x1608, 0x04);
	RegSetLByteValue(0x160E, 0x10);
	mdelay(10);

	/* ptm = 6, h'04[12:14] = b'110 */
	RegSetLByteValue(0x1609, 0x60);
	RegSetLByteValue(0x160E, 0x10);

	/* pmasi = 1, h'04[6] */
	RegSetLByteValue(0x1608, 0x44);
	/* pce = 1, h'04[11] */
	RegSetLByteValue(0x1609, 0x68);
	/* perase = 1, h'04[7] */
	RegSetLByteValue(0x1608, 0xC4);
	/* pnvstr = 1, h'04[5] */
	RegSetLByteValue(0x1608, 0xE4);
	/* pwe = 1, h'04[9] */
	RegSetLByteValue(0x1609, 0x6A);
	/* trigger gpio load */
	RegSetLByteValue(0x160E, 0x10);
}

static void _DrvFwCtrlEraseEmemC33(enum EmemType_e eEmemType)
{
	DBG("*** %s() ***\n", __func__);

	/* Stop mcu */
	RegSet16BitValue(0x0FE6, 0x0001);

	/* Disable watchdog */
	RegSetLByteValue(0x3C60, 0x55);
	RegSetLByteValue(0x3C61, 0xAA);

	/* Set PROGRAM password */
	RegSetLByteValue(0x161A, 0xBA);
	RegSetLByteValue(0x161B, 0xAB);

	/* Clear pce */
	RegSetLByteValue(0x1618, 0x80);

	if (eEmemType == EMEM_ALL)
		RegSetLByteValue(0x1608, 0x10); /* mark */

	RegSetLByteValue(0x1618, 0x40);
	mdelay(10);

	RegSetLByteValue(0x1618, 0x80);

	/* erase trigger */
	if (eEmemType == EMEM_MAIN)
		RegSetLByteValue(0x160E, 0x04); /* erase main */
	else
		RegSetLByteValue(0x160E, 0x08); /* erase all block */
}

static void _DrvFwCtrlMsg22xxGetTpVendorCode(u8 *pTpVendorCode)
{
	DBG("*** %s() ***\n", __func__);

	if (g_ChipType == CHIP_TYPE_MSG22XX) {
		u16 nRegData1, nRegData2;

		DrvPlatformLyrTouchDeviceResetHw();

		DbBusEnterSerialDebugMode();
		DbBusStopMCU();
		DbBusIICUseBus();
		DbBusIICReshape();
		mdelay(100);

		/* Stop mcu */
		RegSetLByteValue(0x0FE6, 0x01);

		/* Stop watchdog */
		RegSet16BitValue(0x3C60, 0xAA55);

		/* RIU password */
		RegSet16BitValue(0x161A, 0xABBA);

		/* Clear pce */
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x80));

		/*
		 * Set start address for tp vendor code on info block
		 * (Actually, start reading from 0xC1E8)
		 */
		RegSet16BitValue(0x1600, 0xC1E9);

		/* Enable burst mode */
		/*
		 * RegSet16BitValue(0x160C,
		 * (RegGet16BitValue(0x160C) | 0x01));
		 */

		/* Set pce */
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x40));

		RegSetLByteValue(0x160E, 0x01);

		nRegData1 = RegGet16BitValue(0x1604);
		nRegData2 = RegGet16BitValue(0x1606);

		pTpVendorCode[0] = ((nRegData1 >> 8) & 0xFF);
		pTpVendorCode[1] = (nRegData2 & 0xFF);
		pTpVendorCode[2] = ((nRegData2 >> 8) & 0xFF);

		DBG("pTpVendorCode[0] = 0x%x, %c\n",
				pTpVendorCode[0],
				pTpVendorCode[0]);
		DBG("pTpVendorCode[1] = 0x%x, %c\n",
				pTpVendorCode[1],
				pTpVendorCode[1]);
		DBG("pTpVendorCode[2] = 0x%x, %c\n",
				pTpVendorCode[2],
				pTpVendorCode[2]);

		/* Clear burst mode */
		/*
		 * RegSet16BitValue(0x160C,
		 * RegGet16BitValue(0x160C) & (~0x01));
		 */

		RegSet16BitValue(0x1600, 0x0000);

		/* Clear RIU password */
		RegSet16BitValue(0x161A, 0x0000);

		DbBusIICNotUseBus();
		DbBusNotStopMCU();
		DbBusExitSerialDebugMode();

		DrvPlatformLyrTouchDeviceResetHw();
	}
}

static u32 _DrvFwCtrlMsg22xxGetFirmwareCrcByHardware(enum EmemType_e eEmemType)
{
	u16 nCrcDown = 0;
	u32 nRetVal = 0;

	DBG("*** %s() eEmemType = %d ***\n", __func__, eEmemType);

	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();
	mdelay(100);

	/* RIU password */
	RegSet16BitValue(0x161A, 0xABBA);

	/* Set PCE high */
	RegSetLByteValue(0x1618, 0x40);

	if (eEmemType == EMEM_MAIN) {
		/* Set start address and end address for main block */
		RegSet16BitValue(0x1600, 0x0000);
		RegSet16BitValue(0x1640, 0xBFF8);
	} else if (eEmemType == EMEM_INFO) {
		/* Set start address and end address for info block */
		RegSet16BitValue(0x1600, 0xC000);
		RegSet16BitValue(0x1640, 0xC1F8);
	}

	/* CRC reset */
	RegSet16BitValue(0x164E, 0x0001);

	RegSet16BitValue(0x164E, 0x0000);

	/* Trigger CRC check */
	RegSetLByteValue(0x160E, 0x20);
	mdelay(10);

	nCrcDown = RegGet16BitValue(0x164E);

	while (nCrcDown != 2) {
		DBG("Wait CRC down\n");
		mdelay(10);
		nCrcDown = RegGet16BitValue(0x164E);
	}

	nRetVal = RegGet16BitValue(0x1652);
	nRetVal = (nRetVal << 16) | RegGet16BitValue(0x1650);

	DBG("Hardware CRC = 0x%x\n", nRetVal);

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	return nRetVal;
}

static void _DrvFwCtrlMsg22xxConvertFwDataTwoDimenToOneDimen(
		u8 szTwoDimenFwData[][1024], u8 *pOneDimenFwData)
{
	u32 i, j;

	DBG("*** %s() ***\n", __func__);

	for (i = 0; i < (MSG22XX_FIRMWARE_MAIN_BLOCK_SIZE+1); i++) {
		if (i < MSG22XX_FIRMWARE_MAIN_BLOCK_SIZE) {
			for (j = 0; j < 1024; j++)
				pOneDimenFwData[i*1024+j] =
					szTwoDimenFwData[i][j];
		} else {
			for (j = 0; j < 512; j++)
				pOneDimenFwData[i*1024+j] =
					szTwoDimenFwData[i][j];
		}
	}
}

static s32 _DrvFwCtrlParsePacket(u8 *pPacket, u16 nLength,
		struct TouchInfo_t *pInfo)
{
	u8 nCheckSum = 0;
	u32 nDeltaX = 0, nDeltaY = 0;
	u32 nX = 0;
	u32 nY = 0;
#ifdef CONFIG_SWAP_X_Y
	u32 nTempX;
	u32 nTempY;
#endif
	u32 i = 0;
#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
	u8 nCheckSumIndex = 0;
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

	DBG("*** %s() ***\n", __func__);

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
	if (g_FirmwareMode == FIRMWARE_MODE_DEMO_MODE)
		nCheckSumIndex = 7;
	else if (g_FirmwareMode == FIRMWARE_MODE_DEBUG_MODE ||
			g_FirmwareMode == FIRMWARE_MODE_RAW_DATA_MODE)
		nCheckSumIndex = 31;

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (g_GestureWakeupFlag == 1)
		nCheckSumIndex = nLength-1;
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
	nCheckSum = DrvCommonCalculateCheckSum(&pPacket[0], nCheckSumIndex);
	DBG("check sum : [%x] == [%x]?\n",
			pPacket[nCheckSumIndex], nCheckSum);
#else
	nCheckSum = DrvCommonCalculateCheckSum(&pPacket[0], (nLength-1));
	DBG("check ksum : [%x] == [%x]?\n",
			pPacket[nLength-1], nCheckSum);
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (g_GestureWakeupFlag == 1) {
		u8 nWakeupMode = 0;
		u8 bIsCorrectFormat = 0;

		DBG("received raw data from touch panel as following:\n");
		for (i = 0; i < 6; i++)
			DBG("pPacket[%d]=%x ", i, pPacket[i]);

		DBG("\n");

		if (g_ChipType == CHIP_TYPE_MSG22XX &&
				pPacket[0] == 0xA7 &&
				pPacket[1] == 0x00 &&
				pPacket[2] == 0x06 &&
				pPacket[3] == PACKET_TYPE_GESTURE_WAKEUP) {
			nWakeupMode = pPacket[4];
			bIsCorrectFormat = 1;
		}
#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
		else if (g_ChipType == CHIP_TYPE_MSG22XX &&
				pPacket[0] == 0xA7 &&
				pPacket[1] == 0x00 &&
				pPacket[2] == 0x80 &&
				pPacket[3] == PACKET_TYPE_GESTURE_DEBUG) {
			u32 a = 0;

			nWakeupMode = pPacket[4];
			bIsCorrectFormat = 1;

			for (a = 0; a < 0x80; a++)
				g_LogGestureDebug[a] = pPacket[a];

			if (!(pPacket[5] >> 7)) {/* LCM Light Flag = 0 */
				nWakeupMode = 0xFE;
				DBG("gesture debug mode LCM flag = 0\n");
			}
		}
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
		else if (g_ChipType == CHIP_TYPE_MSG22XX &&
				pPacket[0] == 0xA7 &&
				pPacket[1] == 0x00 &&
				pPacket[2] == 0x80 &&
				pPacket[3] == PACKET_TYPE_GESTURE_INFORMATION) {
			u32 a = 0;
			u32 nTmpCount = 0;

			nWakeupMode = pPacket[4];
			bIsCorrectFormat = 1;

			for (a = 0; a < 6; a++) {/* header */
				g_LogGestureInfor[nTmpCount] = pPacket[a];
				nTmpCount++;
			}

			/* parse packet to coordinate */
			for (a = 6; a < 126; a = a+3) {
				u32 nTranX = 0;
				u32 nTranY = 0;

				_DrvFwCtrlCoordinate(&pPacket[a],
						&nTranX, &nTranY);
				g_LogGestureInfor[nTmpCount] = nTranX;
				nTmpCount++;
				g_LogGestureInfor[nTmpCount] = nTranY;
				nTmpCount++;
			}

			g_LogGestureInfor[nTmpCount] = pPacket[126]; /* Dummy */
			nTmpCount++;
			/* checksum */
			g_LogGestureInfor[nTmpCount] = pPacket[127];
			nTmpCount++;
			DBG("gesture information mode Count = %d\n", nTmpCount);
		}
#endif /* CONFIG_ENABLE_GESTURE_INFORMATION_MODE */
		else if (g_ChipType == CHIP_TYPE_MSG21XXA &&
				pPacket[0] == 0x52 &&
				pPacket[1] == 0xFF &&
				pPacket[2] == 0xFF &&
				pPacket[3] == 0xFF &&
				pPacket[4] == 0xFF &&
				pPacket[6] == 0xFF) {
			nWakeupMode = pPacket[5];
			bIsCorrectFormat = 1;
		}

		if (bIsCorrectFormat) {
			DBG("nWakeupMode = 0x%x\n", nWakeupMode);

			switch (nWakeupMode) {
			case 0x58:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_DOUBLE_CLICK_FLAG;

				DBG("Light up screen by DOUBLE_CLICK ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x60:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_UP_DIRECT_FLAG;

				DBG("Light up screen by UP_DIRECT ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x61:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_DOWN_DIRECT_FLAG;

				DBG("Light up screen by DOWN_DIRECT",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x62:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_LEFT_DIRECT_FLAG;

				DBG("Light up screen by LEFT_DIRECT ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x63:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RIGHT_DIRECT_FLAG;

				DBG("Light up screen by RIGHT_DIRECT ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x64:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_m_CHARACTER_FLAG;

				DBG("Light up screen by m_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x65:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_W_CHARACTER_FLAG;

				DBG("Light up screen by W_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x66:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_C_CHARACTER_FLAG;

				DBG("Light up screen by C_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x67:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_e_CHARACTER_FLAG;

				DBG("Light up screen by e_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x68:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_V_CHARACTER_FLAG;

				DBG("Light up screen by V_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x69:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_O_CHARACTER_FLAG;

				DBG("Light up screen by O_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x6A:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_S_CHARACTER_FLAG;

				DBG("Light up screen by S_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x6B:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_Z_CHARACTER_FLAG;

				DBG("Light up screen by Z_CHARACTER ",
						"gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x6C:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE1_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE1_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x6D:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE2_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE2_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x6E:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE3_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE3_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
#ifdef CONFIG_SUPPORT_64_TYPES_GESTURE_WAKEUP_MODE
			case 0x6F:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE4_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE4_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x70:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE5_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE5_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x71:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE6_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE6_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x72:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE7_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE7_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x73:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE8_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE8_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x74:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE9_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE9_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x75:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE10_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE10_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x76:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE11_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE11_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x77:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE12_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE12_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x78:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE13_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE13_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x79:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE14_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE14_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x7A:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE15_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE15_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x7B:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE16_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE16_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x7C:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE17_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE17_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x7D:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE18_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE18_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x7E:
				_gGestureWakeupValue[0] =
					GESTURE_WAKEUP_MODE_RESERVE19_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE19_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x7F:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE20_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE20_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x80:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE21_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE21_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x81:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE22_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE22_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x82:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE23_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE23_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x83:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE24_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE24_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x84:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE25_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE25_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x85:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE26_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE26_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x86:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE27_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE27_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x87:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE28_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE28_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x88:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE29_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE29_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x89:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE30_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE30_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x8A:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE31_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE31_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x8B:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE32_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE32_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x8C:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE33_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE33_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x8D:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE34_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE34_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x8E:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE35_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE35_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x8F:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE36_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE36_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x90:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE37_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE37_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x91:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE38_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE38_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x92:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE39_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE39_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x93:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE40_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE40_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x94:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE41_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE41_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x95:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE42_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE42_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x96:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE43_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE43_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x97:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE44_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE44_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x98:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE45_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE45_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x99:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE46_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE46_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x9A:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE47_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE47_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x9B:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE48_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE48_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x9C:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE49_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE49_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x9D:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE50_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE50_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
			case 0x9E:
				_gGestureWakeupValue[1] =
					GESTURE_WAKEUP_MODE_RESERVE51_FLAG;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_RESERVE51_FLAG ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
#endif
#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
			case 0xFF:
				_gGestureWakeupValue[1] = 0xFF;

				DBG("Light up screen by ",
				    "GESTURE_WAKEUP_MODE_FAIL ",
				    "gesture wakeup.\n");

				input_report_key(g_InputDevice, KEY_POWER, 1);
				input_sync(g_InputDevice);
				input_report_key(g_InputDevice, KEY_POWER, 0);
				input_sync(g_InputDevice);
				break;
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */
			default:
				_gGestureWakeupValue[0] = 0;
				_gGestureWakeupValue[1] = 0;
				DBG("Un-supported gesture wakeup mode. ",
				    "Please check your device driver code.\n");
				break;
			}

			DBG("_gGestureWakeupValue[0] = 0x%x\n",
					_gGestureWakeupValue[0]);
			DBG("_gGestureWakeupValue[1] = 0x%x\n",
					_gGestureWakeupValue[1]);
		} else {
			DBG("gesture wakeup packet format is incorrect.\n");
		}

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
		/*
		 * Notify android application to retrieve log data mode
		 * packet from device driver by sysfs.
		 */
		if (g_GestureKObj != NULL &&
				pPacket[3] == PACKET_TYPE_GESTURE_DEBUG) {
			char *pEnvp[2];
			s32 nRetVal = 0;

			pEnvp[0] = "STATUS=GET_GESTURE_DEBUG";
			pEnvp[1] = NULL;

			nRetVal = kobject_uevent_env(g_GestureKObj,
					KOBJ_CHANGE, pEnvp);
			DBG("kobject_uevent_env() nRetVal = %d\n", nRetVal);

		}
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

		return -1;
	}
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

	DBG("received raw data from touch panel as following:\n");
	for (i = 0; i < 8; i++)
		DBG("pPacket[%d]=%x ", i, pPacket[i]);
	DBG("\n");

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
	/* check the checksum of packet */
	if ((pPacket[nCheckSumIndex] == nCheckSum) && (pPacket[0] == 0x52)) {
#else
		/* check the checksum of packet */
		if ((pPacket[nLength-1] == nCheckSum) && (pPacket[0] == 0x52)) {
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */
			/* parse the packet to coordinate */
			nX = (((pPacket[1] & 0xF0) << 4) | pPacket[2]);
			nY = (((pPacket[1] & 0x0F) << 8) | pPacket[3]);

			nDeltaX = (((pPacket[4] & 0xF0) << 4) | pPacket[5]);
			nDeltaY = (((pPacket[4] & 0x0F) << 8) | pPacket[6]);

			DBG("[x,y]=[%d,%d]\n", nX, nY);
			DBG("[delta_x,delta_y]=[%d,%d]\n", nDeltaX, nDeltaY);

#ifdef CONFIG_SWAP_X_Y
			nTempY = nX;
			nTempX = nY;
			nX = nTempX;
			nY = nTempY;

			nTempY = nDeltaX;
			nTempX = nDeltaY;
			nDeltaX = nTempX;
			nDeltaY = nTempY;
#endif

#ifdef CONFIG_REVERSE_X
			nX = 2047 - nX;
			nDeltaX = 4095 - nDeltaX;
#endif

#ifdef CONFIG_REVERSE_Y
			nY = 2047 - nY;
			nDeltaY = 4095 - nDeltaY;
#endif

			/*
			 * pPacket[0]: id,
			 * pPacket[1]~pPacket[3]: the first point abs,
			 * pPacket[4]~pPacket[6]: the relative distance between
			 * the first point abs and the second point abs
			 * when pPacket[1]~pPacket[4], pPacket[6] is 0xFF,
			 * keyevent, pPacket[5] to judge which key press.
			 * pPacket[1]~pPacket[6] all are 0xFF, release touch
			 */
			if ((pPacket[1] == 0xFF) && (pPacket[2] == 0xFF) &&
					(pPacket[3] == 0xFF) &&
					(pPacket[4] == 0xFF) &&
					(pPacket[6] == 0xFF)) {
				pInfo->tPoint[0].nX = 0;/* final X coordinate */
				pInfo->tPoint[0].nY = 0;/* final Y coordinate */
				/*
				 * pPacket[5] is key value
				 * 0x00 is key up, 0xff is touch screen up
				 */
				if ((pPacket[5] != 0x00) &&
						(pPacket[5] != 0xFF)) {
					DBG("touch key down pPacket[5]=%d\n",
							pPacket[5]);

					pInfo->nFingerNum = 1;
					pInfo->nTouchKeyCode = pPacket[5];
					pInfo->nTouchKeyMode = 1;

#ifdef CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE
					pInfo->nFingerNum = 1;
					pInfo->nTouchKeyCode = 0;
					pInfo->nTouchKeyMode = 0;

					/* TOUCH_KEY_HOME */
					if (pPacket[5] == 4) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[1][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[1][1];
					/* TOUCH_KEY_MENU */
					} else if (pPacket[5] == 1) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[0][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[0][1];
					/* TOUCH_KEY_BACK */
					} else if (pPacket[5] == 2) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[2][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[2][1];
					/* TOUCH_KEY_SEARCH  */
					} else if (pPacket[5] == 8) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[3][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[3][1];
					} else {
						DBG("multi-key is pressed.\n");
						return -1;
					}
#endif /* CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE */
				} else {   /* key up or touch up */
					DBG("touch end\n");
					pInfo->nFingerNum = 0; /* touch end */
					pInfo->nTouchKeyCode = 0;
					pInfo->nTouchKeyMode = 0;
				}
			} else {
				/* Touch on screen... */
				pInfo->nTouchKeyMode = 0;

				if (
#ifdef CONFIG_REVERSE_X
						(nDeltaX == 4095)
#else
						(nDeltaX == 0)
#endif
						&&
#ifdef CONFIG_REVERSE_Y
						(nDeltaY == 4095)
#else
						(nDeltaY == 0)
#endif
				  ) {   /* one touch point */
					pInfo->nFingerNum = 1; /* one touch */
					pInfo->tPoint[0].nX =
						(nX * TOUCH_SCREEN_X_MAX) /
						TPD_WIDTH;
					pInfo->tPoint[0].nY =
						(nY * TOUCH_SCREEN_Y_MAX) /
						TPD_HEIGHT;
					DBG("[%s]: [x,y]=[%d,%d]\n", __func__,
							nX, nY);
					DBG("[%s]: point[x,y]=[%d,%d]\n",
							__func__,
							pInfo->tPoint[0].nX,
							pInfo->tPoint[0].nY);
				} else {   /* two touch points */
					u32 nX2, nY2;

					pInfo->nFingerNum = 2; /* two touch */
					/* Finger 1 */
					pInfo->tPoint[0].nX =
						(nX * TOUCH_SCREEN_X_MAX) /
						TPD_WIDTH;
					pInfo->tPoint[0].nY =
						(nY * TOUCH_SCREEN_Y_MAX) /
						TPD_HEIGHT;
					DBG("[%s]: point1[x,y]=[%d,%d]\n",
							__func__,
							pInfo->tPoint[0].nX,
							pInfo->tPoint[0].nY);
					/* Finger 2 */
					/*
					 * transform the unsigned
					 * value to signed value
					 */
					if (nDeltaX > 2048)
						nDeltaX -= 4096;

					if (nDeltaY > 2048)
						nDeltaY -= 4096;

					nX2 = (u32)(nX + nDeltaX);
					nY2 = (u32)(nY + nDeltaY);

					pInfo->tPoint[1].nX =
						(nX2 * TOUCH_SCREEN_X_MAX) /
						TPD_WIDTH;
					pInfo->tPoint[1].nY =
						(nY2 * TOUCH_SCREEN_Y_MAX) /
						TPD_HEIGHT;
					DBG("[%s]: point2[x,y]=[%d,%d]\n",
							__func__,
							pInfo->tPoint[1].nX,
							pInfo->tPoint[1].nY);
				}
			}
		}
#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
		else if (pPacket[nCheckSumIndex] == nCheckSum &&
				pPacket[0] == 0x62) {
			nX = ((pPacket[1] << 8) | pPacket[2]);  /* Position_X */
			nY = ((pPacket[3] << 8) | pPacket[4]);  /* Position_Y */
			/* Distance_X */
			nDeltaX = ((pPacket[13] << 8) | pPacket[14]);
			/* Distance_Y */
			nDeltaY = ((pPacket[15] << 8) | pPacket[16]);

			DBG("[x,y]=[%d,%d]\n", nX, nY);
			DBG("[delta_x,delta_y]=[%d,%d]\n", nDeltaX, nDeltaY);

#ifdef CONFIG_SWAP_X_Y
			nTempY = nX;
			nTempX = nY;
			nX = nTempX;
			nY = nTempY;

			nTempY = nDeltaX;
			nTempX = nDeltaY;
			nDeltaX = nTempX;
			nDeltaY = nTempY;
#endif

#ifdef CONFIG_REVERSE_X
			nX = 2047 - nX;
			nDeltaX = 4095 - nDeltaX;
#endif

#ifdef CONFIG_REVERSE_Y
			nY = 2047 - nY;
			nDeltaY = 4095 - nDeltaY;
#endif

			/*
			 * pPacket[0]:id,
			 * pPacket[1]~pPacket[4]:the first point abs,
			 * pPacket[13]~pPacket[16]:the relative distance
			 * between the first point abs and the second point
			 * abs when pPacket[1]~pPacket[7] is 0xFF, keyevent,
			 * pPacket[8] to judge which key press.
			 * pPacket[1]~pPacket[8] all are 0xFF, release touch
			 */
			if ((pPacket[1] == 0xFF) && (pPacket[2] == 0xFF) &&
					(pPacket[3] == 0xFF) &&
					(pPacket[4] == 0xFF) &&
					(pPacket[5] == 0xFF) &&
					(pPacket[6] == 0xFF) &&
					(pPacket[7] == 0xFF)) {
				pInfo->tPoint[0].nX = 0;/* final X coordinate */
				pInfo->tPoint[0].nY = 0;/* final Y coordinate */

				/* pPacket[8] is key value */
				/* 0x00 is key up, 0xff is touch screen up */
				if ((pPacket[8] != 0x00) &&
						(pPacket[8] != 0xFF)) {
					DBG("touch key down pPacket[8]=%d\n",
							pPacket[8]);
					pInfo->nFingerNum = 1;
					pInfo->nTouchKeyCode = pPacket[8];
					pInfo->nTouchKeyMode = 1;

#ifdef CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE
					pInfo->nFingerNum = 1;
					pInfo->nTouchKeyCode = 0;
					pInfo->nTouchKeyMode = 0;

					/* TOUCH_KEY_HOME */
					if (pPacket[8] == 4) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[1][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[1][1];
					/* TOUCH_KEY_MENU */
					} else if (pPacket[8] == 1) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[0][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[0][1];
					/* TOUCH_KEY_BACK */
					} else if (pPacket[8] == 2) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[2][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[2][1];
					/* TOUCH_KEY_SEARCH  */
					} else if (pPacket[8] == 8) {
						pInfo->tPoint[0].nX = g_TpVirtualKeyDimLocal[3][0];
						pInfo->tPoint[0].nY = g_TpVirtualKeyDimLocal[3][1];
					} else {
						DBG("multi-key is pressed.\n");
						return -1;
					}
#endif /* CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE */
				} else {   /* key up or touch up */
					DBG("touch end\n");
					pInfo->nFingerNum = 0; /* touch end */
					pInfo->nTouchKeyCode = 0;
					pInfo->nTouchKeyMode = 0;
				}
			} else {
				/* Touch on screen... */
				pInfo->nTouchKeyMode = 0;

				/* if ((nDeltaX == 0) && (nDeltaY == 0)) */
				if (
#ifdef CONFIG_REVERSE_X
						(nDeltaX == 4095)
#else
						(nDeltaX == 0)
#endif
						&&
#ifdef CONFIG_REVERSE_Y
						(nDeltaY == 4095)
#else
						(nDeltaY == 0)
#endif
				   ) {   /* one touch point */
					pInfo->nFingerNum = 1; /* one touch */
					pInfo->tPoint[0].nX =
						(nX * TOUCH_SCREEN_X_MAX) /
						TPD_WIDTH;
					pInfo->tPoint[0].nY =
						(nY * TOUCH_SCREEN_Y_MAX) /
						TPD_HEIGHT;
					DBG("[%s]: [x,y]=[%d,%d]\n",
							__func__, nX, nY);
					DBG("[%s]: point[x,y]=[%d,%d]\n",
							__func__,
							pInfo->tPoint[0].nX,
							pInfo->tPoint[0].nY);
				} else {   /* two touch points */
					u32 nX2, nY2;

					pInfo->nFingerNum = 2; /* two touch */
					/* Finger 1 */
					pInfo->tPoint[0].nX =
						(nX * TOUCH_SCREEN_X_MAX) /
						TPD_WIDTH;
					pInfo->tPoint[0].nY =
						(nY * TOUCH_SCREEN_Y_MAX) /
						TPD_HEIGHT;
					DBG("[%s]: point1[x,y]=[%d,%d]\n",
							__func__,
							pInfo->tPoint[0].nX,
							pInfo->tPoint[0].nY);
					/*
					 * Finger 2
					 * transform the unsigned value
					 * to signed value
					 */
					if (nDeltaX > 2048)
						nDeltaX -= 4096;

					if (nDeltaY > 2048)
						nDeltaY -= 4096;

					nX2 = (u32)(nX + nDeltaX);
					nY2 = (u32)(nY + nDeltaY);

					pInfo->tPoint[1].nX =
						(nX2 * TOUCH_SCREEN_X_MAX) /
						TPD_WIDTH;
					pInfo->tPoint[1].nY =
						(nY2 * TOUCH_SCREEN_Y_MAX) /
						TPD_HEIGHT;
					DBG("[%s]: point2[x,y]=[%d,%d]\n",
							__func__,
							pInfo->tPoint[1].nX,
							pInfo->tPoint[1].nY);
				}

				/*
				 * Notify android application to retrieve log
				 * data mode packet from device driver by sysfs.
				 */
				if (g_TouchKObj != NULL) {
					char *pEnvp[2];
					s32 nRetVal = 0;

					pEnvp[0] = "STATUS=GET_PACKET";
					pEnvp[1] = NULL;

					nRetVal = kobject_uevent_env(
							g_TouchKObj,
							KOBJ_CHANGE,
							pEnvp);
					DBG("kobject_uevent_env() ",
					    "nRetVal = %d\n", nRetVal);
				}
			}
		} else {
			if (pPacket[nCheckSumIndex] != nCheckSum) {
				DBG("WRONG CHECKSUM\n");
				return -1;
			}

			if (g_FirmwareMode == FIRMWARE_MODE_DEMO_MODE &&
					pPacket[0] != 0x52) {
				DBG("WRONG DEMO MODE HEADER\n");
				return -1;
			} else if (g_FirmwareMode ==
					FIRMWARE_MODE_DEBUG_MODE &&
					pPacket[0] != 0x62) {
				DBG("WRONG DEBUG MODE HEADER\n");
				return -1;
			} else if (g_FirmwareMode ==
					FIRMWARE_MODE_RAW_DATA_MODE &&
					pPacket[0] != 0x62) {
				DBG("WRONG RAW DATA MODE HEADER\n");
				return -1;
			}
		}
#else
	else {
		DBG("pPacket[0]=0x%x, pPacket[7]=0x%x, nCheckSum=0x%x\n",
				pPacket[0], pPacket[7], nCheckSum);

		if (pPacket[nLength-1] != nCheckSum) {
			DBG("WRONG CHECKSUM\n");
			return -1;
		}

		if (pPacket[0] != 0x52) {
			DBG("WRONG DEMO MODE HEADER\n");
			return -1;
		}
	}
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

	return 0;
}

static void _DrvFwCtrlStoreFirmwareData(u8 *pBuf, u32 nSize)
{
	u32 nCount = nSize / 1024;
	u32 nRemainder = nSize % 1024;
	u32 i;

	DBG("*** %s() ***\n", __func__);

	if (nCount > 0) {
		for (i = 0; i < nCount; i++) {
			memcpy(g_FwData[g_FwDataCount], pBuf+(i*1024), 1024);
			g_FwDataCount++;
		}

		/* Handle special firmware size like MSG22XX(48.5KB) */
		if (nRemainder > 0) {
			DBG("nRemainder = %d\n", nRemainder);

			memcpy(g_FwData[g_FwDataCount],
					pBuf+(i*1024),
					nRemainder);
			g_FwDataCount++;
		}
	} else {
		if (nSize > 0) {
			memcpy(g_FwData[g_FwDataCount], pBuf, nSize);
			g_FwDataCount++;
		}
	}

	DBG("*** g_FwDataCount = %d ***\n", g_FwDataCount);

	if (pBuf != NULL)
		DBG("*** buf[0] = %c ***\n", pBuf[0]);
}

static u16 _DrvFwCtrlMsg21xxaGetSwId(enum EmemType_e eEmemType)
{
	u16 nRetVal = 0;
	u16 nRegData = 0;
	u8 szDbBusTxData[5] = {0};
	u8 szDbBusRxData[4] = {0};

	DBG("*** %s() eEmemType = %d ***\n", __func__, eEmemType);

	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();
	mdelay(100);

	/* Stop mcu */
	RegSetLByteValue(0x0FE6, 0x01); /* bank:mheg5, addr:h0073 */

	/* Stop watchdog */
	RegSet16BitValue(0x3C60, 0xAA55); /* bank:reg_PIU_MISC_0, addr:h0030 */

	/* cmd */
	RegSet16BitValue(0x3CE4, 0xA4AB); /* bank:reg_PIU_MISC_0, addr:h0072 */

	/* TP SW reset */
	RegSet16BitValue(0x1E04, 0x7d60); /* bank:chip, addr:h0002 */
	RegSet16BitValue(0x1E04, 0x829F);

	/* Start mcu */
	RegSetLByteValue(0x0FE6, 0x00); /* bank:mheg5, addr:h0073 */

	mdelay(100);

	/* Polling 0x3CE4 is 0x5B58 */
	do {
		/* bank:reg_PIU_MISC_0, addr:h0072 */
		nRegData = RegGet16BitValue(0x3CE4);
	} while (nRegData != 0x5B58);

	szDbBusTxData[0] = 0x72;
	if (eEmemType == EMEM_MAIN) {/* Read SW ID from main block */
		szDbBusTxData[1] = 0x7F;
		szDbBusTxData[2] = 0x55;
	} else if (eEmemType == EMEM_INFO) {/* Read SW ID from info block */
		szDbBusTxData[1] = 0x83;
		szDbBusTxData[2] = 0x00;
	}

	szDbBusTxData[3] = 0x00;
	szDbBusTxData[4] = 0x04;

	IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 5);
	IicReadData(SLAVE_I2C_ID_DWI2C, &szDbBusRxData[0], 4);

	DBG("szDbBusRxData[0,1,2,3] = 0x%x,0x%x,0x%x,0x%x\n",
			szDbBusRxData[0], szDbBusRxData[1],
			szDbBusRxData[2], szDbBusRxData[3]);

	if ((szDbBusRxData[0] >= 0x30 && szDbBusRxData[0] <= 0x39) &&
			(szDbBusRxData[1] >= 0x30 &&
			 szDbBusRxData[1] <= 0x39) &&
			(szDbBusRxData[2] >= 0x31 &&
			 szDbBusRxData[2] <= 0x39))
		nRetVal = (szDbBusRxData[0] - 0x30) * 100 +
			(szDbBusRxData[1] - 0x30) * 10 +
			(szDbBusRxData[2] - 0x30);

	DBG("SW ID = 0x%x\n", nRetVal);

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	return nRetVal;
}

static u16 _DrvFwCtrlMsg22xxGetSwId(enum EmemType_e eEmemType)
{
	u16 nRetVal = 0;
	u16 nRegData1 = 0;

	DBG("*** %s() eEmemType = %d ***\n", __func__, eEmemType);

	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();
	mdelay(100);

	/* Stop mcu */
	RegSetLByteValue(0x0FE6, 0x01);

	/* Stop watchdog */
	RegSet16BitValue(0x3C60, 0xAA55);

	/* RIU password */
	RegSet16BitValue(0x161A, 0xABBA);

	/* Clear pce */
	RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x80));

	if (eEmemType == EMEM_MAIN) /* Read SW ID from main block */
		/* Set start address for main block SW ID */
		RegSet16BitValue(0x1600, 0xBFF4);
	else if (eEmemType == EMEM_INFO) /* Read SW ID from info block */
		/* Set start address for info block SW ID */
		RegSet16BitValue(0x1600, 0xC1EC);

	/*
	   Ex. SW ID in Main Block :
	   Major low byte at address 0xBFF4
	   Major high byte at address 0xBFF5

	   SW ID in Info Block :
	   Major low byte at address 0xC1EC
	   Major high byte at address 0xC1ED
	 */

	/* Enable burst mode */
	/* RegSet16BitValue(0x160C, (RegGet16BitValue(0x160C) | 0x01)); */

	/* Set pce */
	RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x40));

	RegSetLByteValue(0x160E, 0x01);

	nRegData1 = RegGet16BitValue(0x1604);
	/*    nRegData2 = RegGet16BitValue(0x1606); */

	nRetVal = ((nRegData1 >> 8) & 0xFF) << 8;
	nRetVal |= (nRegData1 & 0xFF);

	/* Clear burst mode */
	/* RegSet16BitValue(0x160C, RegGet16BitValue(0x160C) & (~0x01)); */

	RegSet16BitValue(0x1600, 0x0000);

	/* Clear RIU password */
	RegSet16BitValue(0x161A, 0x0000);

	DBG("SW ID = 0x%x\n", nRetVal);

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	return nRetVal;
}

static void _DrvFwCtrlReadInfoC33(void)
{
	u8 szDbBusTxData[5] = {0};
	u16 nRegData = 0;

	DBG("*** %s() ***\n", __func__);

	mdelay(300);

	/* Stop Watchdog */
	RegSetLByteValue(0x3C60, 0x55);
	RegSetLByteValue(0x3C61, 0xAA);

	RegSet16BitValue(0x3CE4, 0xA4AB);

	RegSet16BitValue(0x1E04, 0x7d60);

	/* TP SW reset */
	RegSet16BitValue(0x1E04, 0x829F);
	mdelay(10);

	szDbBusTxData[0] = 0x10;
	szDbBusTxData[1] = 0x0F;
	szDbBusTxData[2] = 0xE6;
	szDbBusTxData[3] = 0x00;
	IicWriteData(SLAVE_I2C_ID_DBBUS, szDbBusTxData, 4);
	mdelay(100);

	do {
		nRegData = RegGet16BitValue(0x3CE4);
	} while (nRegData != 0x5B58);


	szDbBusTxData[0] = 0x72;
	szDbBusTxData[1] = 0x80;
	szDbBusTxData[2] = 0x00;
	szDbBusTxData[3] = 0x04; /* read 1024 bytes */
	szDbBusTxData[4] = 0x00;

	IicWriteData(SLAVE_I2C_ID_DWI2C, szDbBusTxData, 5);

	mdelay(50);

	/* Receive info data */
	IicReadData(SLAVE_I2C_ID_DWI2C, &_gDwIicInfoData[0], 1024);
}

static s32 _DrvFwCtrlUpdateFirmwareC32(u8 szFwData[][1024],
		enum EmemType_e eEmemType)
{
	u32 i, j;
	u32 nCrcMain, nCrcMainTp;
	u32 nCrcInfo, nCrcInfoTp;
	u32 nCrcTemp;
	u16 nRegData = 0;

	DBG("*** %s() ***\n", __func__);

	nCrcMain = 0xffffffff;
	nCrcInfo = 0xffffffff;

	/* Erase  all */
	_DrvFwCtrlEraseEmemC32();
	mdelay(1000);

	DrvPlatformLyrTouchDeviceResetHw();

	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();
	mdelay(100);

	/* Reset watch dog */
	RegSetLByteValue(0x3C60, 0x55);
	RegSetLByteValue(0x3C61, 0xAA);

	/* Program */

	/* Polling 0x3CE4 is 0x1C70 */
	do {
		nRegData = RegGet16BitValue(0x3CE4);
	} while (nRegData != 0x1C70);

	RegSet16BitValue(0x3CE4, 0xE38F);  /* for all-blocks */

	/* Polling 0x3CE4 is 0x2F43 */
	do {
		nRegData = RegGet16BitValue(0x3CE4);
	} while (nRegData != 0x2F43);

	/* Calculate CRC 32 */
	DrvCommonCrcInitTable();

	for (i = 0; i < 33; i++) { /* total  33 KB : 2 byte per R/W */
		if (i < 32) { /* emem_main */
			if (i == 31) {
				szFwData[i][1014] = 0x5A;
				szFwData[i][1015] = 0xA5;

				for (j = 0; j < 1016; j++)
					nCrcMain = DrvCommonCrcGetValue(
							szFwData[i][j],
							nCrcMain);

				nCrcTemp = nCrcMain;
				nCrcTemp = nCrcTemp ^ 0xffffffff;

				DBG("nCrcTemp=%x\n", nCrcTemp);

				for (j = 0; j < 4; j++) {
					szFwData[i][1023-j] =
						nCrcTemp >> (8 * j) & 0xFF;

					DBG("((nCrcTemp>>(8*%d)) & 0xFF)=%x\n",
							j,
							nCrcTemp>>(8*j)&0xFF);
					DBG("Update main clock crc32 into bin ",
					    "buffer szFwData[%d][%d]=%x\n",
					    i, (1020+j), szFwData[i][1020+j]);
				}
			} else {
				for (j = 0; j < 1024; j++)
					nCrcMain = DrvCommonCrcGetValue(
							szFwData[i][j],
							nCrcMain);
			}
		} else { /* emem_info */
			for (j = 0; j < 1024; j++)
				nCrcInfo = DrvCommonCrcGetValue(
						szFwData[i][j],
						nCrcInfo);
		}

		IicWriteData(SLAVE_I2C_ID_DWI2C, szFwData[i], 1024);

		/* Polling 0x3CE4 is 0xD0BC */
		do {
			nRegData = RegGet16BitValue(0x3CE4);
		} while (nRegData != 0xD0BC);

		RegSet16BitValue(0x3CE4, 0x2F43);
	}

	/* Write file done */
	RegSet16BitValue(0x3CE4, 0x1380);

	mdelay(10);
	/* Polling 0x3CE4 is 0x9432 */
	do {
		nRegData = RegGet16BitValue(0x3CE4);
	} while (nRegData != 0x9432);

	nCrcMain = nCrcMain ^ 0xffffffff;
	nCrcInfo = nCrcInfo ^ 0xffffffff;

	/* CRC Main from TP */
	nCrcMainTp = RegGet16BitValue(0x3C80);
	nCrcMainTp = (nCrcMainTp << 16) | RegGet16BitValue(0x3C82);

	/* CRC Info from TP */
	nCrcInfoTp = RegGet16BitValue(0x3CA0);
	nCrcInfoTp = (nCrcInfoTp << 16) | RegGet16BitValue(0x3CA2);

	DBG("nCrcMain=0x%x, nCrcInfo=0x%x, nCrcMainTp=0x%x, nCrcInfoTp=0x%x\n",
			nCrcMain, nCrcInfo, nCrcMainTp, nCrcInfoTp);

	g_FwDataCount = 0; /* Reset g_FwDataCount to 0 after update firmware */

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	DrvPlatformLyrTouchDeviceResetHw();


	if ((nCrcMainTp != nCrcMain) || (nCrcInfoTp != nCrcInfo)) {
		DBG("Update FAILED\n");
		return -1;
	}

	DBG("Update SUCCESS\n");

	return 0;
}

static s32 _DrvFwCtrlUpdateFirmwareC33(u8 szFwData[][1024],
		enum EmemType_e eEmemType)
{
	u8 szLifeCounter[2];
	u32 i, j;
	u32 nCrcMain, nCrcMainTp;
	u32 nCrcInfo, nCrcInfoTp;
	u32 nCrcTemp;
	u16 nRegData = 0;

	DBG("*** %s() ***\n", __func__);

	nCrcMain = 0xffffffff;
	nCrcInfo = 0xffffffff;


	_DrvFwCtrlReadInfoC33();

	if (_gDwIicInfoData[0] == 'M' && _gDwIicInfoData[1] == 'S' &&
			_gDwIicInfoData[2] == 'T' &&
			_gDwIicInfoData[3] == 'A' &&
			_gDwIicInfoData[4] == 'R' &&
			_gDwIicInfoData[5] == 'T' &&
			_gDwIicInfoData[6] == 'P' &&
			_gDwIicInfoData[7] == 'C') {
		_gDwIicInfoData[8] = szFwData[32][8];
		_gDwIicInfoData[9] = szFwData[32][9];
		_gDwIicInfoData[10] = szFwData[32][10];
		_gDwIicInfoData[11] = szFwData[32][11];
		/* updata life counter */
		szLifeCounter[1] =
			((_gDwIicInfoData[13]<<8 | _gDwIicInfoData[12])+1)>>8 &
			0xFF;
		szLifeCounter[0] =
			((_gDwIicInfoData[13]<<8 | _gDwIicInfoData[12])+1) &
			0xFF;
		_gDwIicInfoData[12] = szLifeCounter[0];
		_gDwIicInfoData[13] = szLifeCounter[1];

		RegSet16BitValue(0x3CE4, 0x78C5);
		RegSet16BitValue(0x1E04, 0x7d60);
		/* TP SW reset */
		RegSet16BitValue(0x1E04, 0x829F);

		mdelay(50);

		/* Polling 0x3CE4 is 0x2F43 */
		do {
			nRegData = RegGet16BitValue(0x3CE4);
		} while (nRegData != 0x2F43);

		/* Transmit lk info data */

		IicWriteData(SLAVE_I2C_ID_DWI2C, &_gDwIicInfoData[0], 1024);

		/* Polling 0x3CE4 is 0xD0BC */
		do {
			nRegData = RegGet16BitValue(0x3CE4);
		} while (nRegData != 0xD0BC);
	}

	/* erase main */
	_DrvFwCtrlEraseEmemC33(EMEM_MAIN);
	mdelay(1000);

	DrvPlatformLyrTouchDeviceResetHw();

	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();
	mdelay(100);

	/* Program */

	/* Polling 0x3CE4 is 0x1C70 */
	if ((eEmemType == EMEM_ALL) || (eEmemType == EMEM_MAIN)) {
		do {
			nRegData = RegGet16BitValue(0x3CE4);
		} while (nRegData != 0x1C70);
	}

	switch (eEmemType) {
	case EMEM_ALL:
		RegSet16BitValue(0x3CE4, 0xE38F);  /* for all blocks */
		break;
	case EMEM_MAIN:
		RegSet16BitValue(0x3CE4, 0x7731);  /* for main block */
		break;
	case EMEM_INFO:
		RegSet16BitValue(0x3CE4, 0x7731);  /* for info block */

		RegSetLByteValue(0x0FE6, 0x01);

		RegSetLByteValue(0x3CE4, 0xC5);
		RegSetLByteValue(0x3CE5, 0x78);

		RegSetLByteValue(0x1E04, 0x9F);
		RegSetLByteValue(0x1E05, 0x82);

		RegSetLByteValue(0x0FE6, 0x00);
		mdelay(100);
		break;
	}

	/* Polling 0x3CE4 is 0x2F43 */
	do {
		nRegData = RegGet16BitValue(0x3CE4);
	} while (nRegData != 0x2F43);

	/* Calculate CRC 32 */
	DrvCommonCrcInitTable();

	for (i = 0; i < 33; i++) { /* total 33 KB : 2 byte per R/W */
		if (eEmemType == EMEM_INFO)
			i = 32;

		if (i < 32) { /* emem_main */
			if (i == 31) {
				szFwData[i][1014] = 0x5A;
				szFwData[i][1015] = 0xA5;

				for (j = 0; j < 1016; j++)
					nCrcMain = DrvCommonCrcGetValue(
							szFwData[i][j],
							nCrcMain);

				nCrcTemp = nCrcMain;
				nCrcTemp = nCrcTemp ^ 0xffffffff;

				DBG("nCrcTemp=%x\n", nCrcTemp);

				for (j = 0; j < 4; j++) {
					szFwData[i][1023-j] =
						nCrcTemp>>(8*j) & 0xFF;

					DBG("((nCrcTemp>>(8*%d)) & 0xFF)=%x\n",
							j,
							nCrcTemp>>(8*j)&0xFF);
					DBG("Update main clock crc32 into bin ",
					    "buffer szFwData[%d][%d]=%x\n",
					    i, (1020+j), szFwData[i][1020+j]);
				}
			} else {
				for (j = 0; j < 1024; j++)
					nCrcMain = DrvCommonCrcGetValue(
							szFwData[i][j],
							nCrcMain);
			}
		} else { /* emem_info */
			for (j = 0; j < 1024; j++)
				nCrcInfo = DrvCommonCrcGetValue(
						_gDwIicInfoData[j],
						nCrcInfo);

			if (eEmemType == EMEM_MAIN)
				break;
		}

		IicWriteData(SLAVE_I2C_ID_DWI2C, szFwData[i], 1024);

		/* Polling 0x3CE4 is 0xD0BC */
		do {
			nRegData = RegGet16BitValue(0x3CE4);
		} while (nRegData != 0xD0BC);

		RegSet16BitValue(0x3CE4, 0x2F43);
	}

	if ((eEmemType == EMEM_ALL) || (eEmemType == EMEM_MAIN))
		/* write file done and check crc */
		RegSet16BitValue(0x3CE4, 0x1380);

	mdelay(10);

	if ((eEmemType == EMEM_ALL) || (eEmemType == EMEM_MAIN)) {
		/* Polling 0x3CE4 is 0x9432 */
		do {
			nRegData = RegGet16BitValue(0x3CE4);
		} while (nRegData != 0x9432);
	}

	nCrcMain = nCrcMain ^ 0xffffffff;
	nCrcInfo = nCrcInfo ^ 0xffffffff;

	if ((eEmemType == EMEM_ALL) || (eEmemType == EMEM_MAIN)) {
		/* CRC Main from TP */
		nCrcMainTp = RegGet16BitValue(0x3C80);
		nCrcMainTp = (nCrcMainTp << 16) | RegGet16BitValue(0x3C82);

		/* CRC Info from TP */
		nCrcInfoTp = RegGet16BitValue(0x3CA0);
		nCrcInfoTp = (nCrcInfoTp << 16) | RegGet16BitValue(0x3CA2);
	}

	DBG("nCrcMain=0x%x, nCrcInfo=0x%x, nCrcMainTp=0x%x, nCrcInfoTp=0x%x\n",
			nCrcMain, nCrcInfo, nCrcMainTp, nCrcInfoTp);

	g_FwDataCount = 0; /* Reset g_FwDataCount to 0 after update firmware */

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	DrvPlatformLyrTouchDeviceResetHw();


	if ((eEmemType == EMEM_ALL) || (eEmemType == EMEM_MAIN)) {
		if ((nCrcMainTp != nCrcMain) || (nCrcInfoTp != nCrcInfo)) {
			DBG("Update FAILED\n");
			return -1;
		}
	}

	DBG("Update SUCCESS\n");

	return 0;
}

static s32 _DrvFwCtrlMsg22xxUpdateFirmware(u8 szFwData[][1024],
		enum EmemType_e eEmemType)
{
	u32 i, index;
	u32 nCrcMain, nCrcMainTp;
	u32 nCrcInfo = 0, nCrcInfoTp = 0;
	u32 nRemainSize, nBlockSize, nSize;
	u16 nRegData = 0;

	u8 szDbBusTxData[1024] = {0};
	u32 nSizePerWrite = 1021;

	DBG("*** %s() ***\n", __func__);


	_DrvFwCtrlMsg22xxConvertFwDataTwoDimenToOneDimen(szFwData,
			_gOneDimenFwData);

	DrvPlatformLyrTouchDeviceResetHw();

	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();

	DBG("Erase start\n");

	/* Stop mcu */
	RegSet16BitValue(0x0FE6, 0x0001);

	/* Disable watchdog */
	RegSetLByteValue(0x3C60, 0x55);
	RegSetLByteValue(0x3C61, 0xAA);

	/* Set PROGRAM password */
	RegSetLByteValue(0x161A, 0xBA);
	RegSetLByteValue(0x161B, 0xAB);

	if (eEmemType == EMEM_ALL) { /* 48KB + 512Byte */
		DBG("Erase all block\n");

		/* Clear pce */
		RegSetLByteValue(0x1618, 0x80);
		mdelay(100);

		/* Chip erase */
		RegSet16BitValue(0x160E, BIT3);

		DBG("Wait erase done flag\n");

		do { /* Wait erase done flag */
			nRegData = RegGet16BitValue(0x1610); /* Memory status */
			mdelay(50);
		} while ((nRegData & BIT1) != BIT1);
	} else if (eEmemType == EMEM_MAIN) { /* 48KB (32+8+8) */
		DBG("Erase main block\n");

		for (i = 0; i < 3; i++) {
			/* Clear pce */
			RegSetLByteValue(0x1618, 0x80);
			mdelay(10);

			if (i == 0)
				RegSet16BitValue(0x1600, 0x0000);
			else if (i == 1)
				RegSet16BitValue(0x1600, 0x8000);
			else if (i == 2)
				RegSet16BitValue(0x1600, 0xA000);

			/* Sector erase */
			RegSet16BitValue(0x160E,
					(RegGet16BitValue(0x160E) | BIT2));

			DBG("Wait erase done flag\n");

			do { /* Wait erase done flag */
				/* Memory status */
				nRegData = RegGet16BitValue(0x1610);
				mdelay(50);
			} while ((nRegData & BIT1) != BIT1);
		}
	} else if (eEmemType == EMEM_INFO) {/* 512Byte */
		DBG("Erase info block\n");

		/* Clear pce */
		RegSetLByteValue(0x1618, 0x80);
		mdelay(10);

		RegSet16BitValue(0x1600, 0xC000);

		/* Sector erase */
		RegSet16BitValue(0x160E, (RegGet16BitValue(0x160E) | BIT2));

		DBG("Wait erase done flag\n");

		do { /* Wait erase done flag */
			nRegData = RegGet16BitValue(0x1610); /* Memory status */
			mdelay(50);
		} while ((nRegData & BIT1) != BIT1);
	}

	DBG("Erase end\n");

	/* Hold reset pin before program */
	RegSetLByteValue(0x1E06, 0x00);

	/* Program */

	if (eEmemType == EMEM_ALL || eEmemType == EMEM_MAIN) { /* 48KB */
		DBG("Program main block start\n");

		/* Program main block */
		RegSet16BitValue(0x161A, 0xABBA);
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x80));

		/* Set start address of main block */
		RegSet16BitValue(0x1600, 0x0000);
		/* Enable burst mode */
		RegSet16BitValue(0x160C, (RegGet16BitValue(0x160C) | 0x01));

		/* Program start */
		szDbBusTxData[0] = 0x10;
		szDbBusTxData[1] = 0x16;
		szDbBusTxData[2] = 0x02;

		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 3);

		szDbBusTxData[0] = 0x20;

		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 1);

		nRemainSize = MSG22XX_FIRMWARE_MAIN_BLOCK_SIZE * 1024;/* 48KB */
		index = 0;

		while (nRemainSize > 0) {
			if (nRemainSize > nSizePerWrite)
				nBlockSize = nSizePerWrite;
			else
				nBlockSize = nRemainSize;

			szDbBusTxData[0] = 0x10;
			szDbBusTxData[1] = 0x16;
			szDbBusTxData[2] = 0x02;

			nSize = 3;

			for (i = 0; i < nBlockSize; i++) {
				szDbBusTxData[3+i] =
					_gOneDimenFwData[index*nSizePerWrite+i];
				nSize++;
			}

			index++;

			IicWriteData(SLAVE_I2C_ID_DBBUS,
					&szDbBusTxData[0], nSize);

			nRemainSize = nRemainSize - nBlockSize;
		}

		/* Program end */
		szDbBusTxData[0] = 0x21;

		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 1);

		nRegData = RegGet16BitValue(0x160C);
		RegSet16BitValue(0x160C, nRegData & (~0x01));

		DBG("Wait main block write done flag\n");

		/* Polling 0x1610 is 0x0002 */
		do {
			nRegData = RegGet16BitValue(0x1610);
			nRegData = nRegData & BIT1;
			mdelay(10);

		} while (nRegData != BIT1); /* Wait write done flag */

		DBG("Program main block end\n");
	}

	if (eEmemType == EMEM_ALL || eEmemType == EMEM_INFO) { /* 512 Byte */
		DBG("Program info block start\n");

		/* Program info block */
		RegSet16BitValue(0x161A, 0xABBA);
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x80));

		/* Set start address of info block */
		RegSet16BitValue(0x1600, 0xC000);
		/* Enable burst mode */
		RegSet16BitValue(0x160C, (RegGet16BitValue(0x160C) | 0x01));

		/* Program start */
		szDbBusTxData[0] = 0x10;
		szDbBusTxData[1] = 0x16;
		szDbBusTxData[2] = 0x02;

		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 3);

		szDbBusTxData[0] = 0x20;

		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 1);

		nRemainSize = MSG22XX_FIRMWARE_INFO_BLOCK_SIZE; /* 512Byte */
		index = 0;

		while (nRemainSize > 0) {
			if (nRemainSize > nSizePerWrite)
				nBlockSize = nSizePerWrite;
			else
				nBlockSize = nRemainSize;

			szDbBusTxData[0] = 0x10;
			szDbBusTxData[1] = 0x16;
			szDbBusTxData[2] = 0x02;

			nSize = 3;

			for (i = 0; i < nBlockSize; i++) {
				szDbBusTxData[3+i] =
					_gOneDimenFwData[
					MSG22XX_FIRMWARE_MAIN_BLOCK_SIZE*1024 +
					index*nSizePerWrite + i];
				nSize++;
			}

			index++;

			IicWriteData(SLAVE_I2C_ID_DBBUS,
					&szDbBusTxData[0], nSize);

			nRemainSize = nRemainSize - nBlockSize;
		}

		/* Program end */
		szDbBusTxData[0] = 0x21;

		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 1);

		nRegData = RegGet16BitValue(0x160C);
		RegSet16BitValue(0x160C, nRegData & (~0x01));

		DBG("Wait info block write done flag\n");

		/* Polling 0x1610 is 0x0002 */
		do {
			nRegData = RegGet16BitValue(0x1610);
			nRegData = nRegData & BIT1;
			mdelay(10);

		} while (nRegData != BIT1); /* Wait write done flag */

		DBG("Program info block end\n");
	}

	if (eEmemType == EMEM_ALL || eEmemType == EMEM_MAIN) {
		/* Get CRC 32 from updated firmware bin file */
		nCrcMain  = _gOneDimenFwData[0xBFFF] << 24;
		nCrcMain |= _gOneDimenFwData[0xBFFE] << 16;
		nCrcMain |= _gOneDimenFwData[0xBFFD] << 8;
		nCrcMain |= _gOneDimenFwData[0xBFFC];

		/* CRC Main from TP */
		DBG("Get Main CRC from TP\n");

		nCrcMainTp = _DrvFwCtrlMsg22xxGetFirmwareCrcByHardware(
				EMEM_MAIN);

		DBG("nCrcMain=0x%x, nCrcMainTp=0x%x\n", nCrcMain, nCrcMainTp);
	}

	if (eEmemType == EMEM_ALL || eEmemType == EMEM_INFO) {
		nCrcInfo  = _gOneDimenFwData[0xC1FF] << 24;
		nCrcInfo |= _gOneDimenFwData[0xC1FE] << 16;
		nCrcInfo |= _gOneDimenFwData[0xC1FD] << 8;
		nCrcInfo |= _gOneDimenFwData[0xC1FC];

		/* CRC Info from TP */
		DBG("Get Info CRC from TP\n");

		nCrcInfoTp = _DrvFwCtrlMsg22xxGetFirmwareCrcByHardware(
				EMEM_INFO);

		DBG("nCrcInfo=0x%x, nCrcInfoTp=0x%x\n", nCrcInfo, nCrcInfoTp);
	}

	g_FwDataCount = 0; /* Reset g_FwDataCount to 0 after update firmware */

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	DrvPlatformLyrTouchDeviceResetHw();


	if (eEmemType == EMEM_ALL) {
		if ((nCrcMainTp != nCrcMain) || (nCrcInfoTp != nCrcInfo)) {
			DBG("Update FAILED\n");

			return -1;
		}
	} else if (eEmemType == EMEM_MAIN) {
		if (nCrcMainTp != nCrcMain) {
			DBG("Update FAILED\n");

			return -1;
		}
	} else if (eEmemType == EMEM_INFO) {
		if (nCrcInfoTp != nCrcInfo) {
			DBG("Update FAILED\n");

			return -1;
		}
	}

	DBG("Update SUCCESS\n");

	return 0;
}

static s32 _DrvFwCtrlUpdateFirmwareCash(u8 szFwData[][1024])
{
	DBG("*** %s() ***\n", __func__);

	DBG("g_ChipType = 0x%x\n", g_ChipType);

	if (g_ChipType == CHIP_TYPE_MSG21XXA) {
		u8 nChipVersion = 0;

		DrvPlatformLyrTouchDeviceResetHw();

		/* Erase TP Flash first */
		DbBusEnterSerialDebugMode();
		DbBusStopMCU();
		DbBusIICUseBus();
		DbBusIICReshape();
		mdelay(100);

		/* Stop MCU */
		RegSetLByteValue(0x0FE6, 0x01);

		/* Disable watchdog */
		RegSet16BitValue(0x3C60, 0xAA55);

		/* Difference between C2 and C3 */
		/* c2:MSG2133(1) c32:MSG2133A(2) c33:MSG2138A(2) */
		/* check ic type */
		/* nChipType = RegGet16BitValue(0x1ECC) & 0xFF; */

		/* check ic version */
		nChipVersion = RegGet16BitValue(0x3CEA) & 0xFF;

		DBG("chip version = 0x%x\n", nChipVersion);

		if (nChipVersion == 3)
			return _DrvFwCtrlUpdateFirmwareC33(szFwData, EMEM_MAIN);
		else
			return _DrvFwCtrlUpdateFirmwareC32(szFwData, EMEM_ALL);
	} else if (g_ChipType == CHIP_TYPE_MSG22XX) { /* (0x7A) */
		_DrvFwCtrlMsg22xxGetTpVendorCode(_gTpVendorCode);

		/*
		 * for specific TP vendor which store some important
		 * information in info block, only update firmware for
		 * main block, do not update firmware for info block.
		 */
		if (_gTpVendorCode[0] == 'C' && _gTpVendorCode[1] == 'N' &&
				_gTpVendorCode[2] == 'T')
			return _DrvFwCtrlMsg22xxUpdateFirmware(szFwData,
					EMEM_MAIN);
		else
			return _DrvFwCtrlMsg22xxUpdateFirmware(szFwData,
					EMEM_ALL);
	} else { /* CHIP_TYPE_MSG21XX (0x01) */
		DBG("Can not update firmware. Catch-2 is no need ",
				"to be maintained now.\n");
		/* Reset g_FwDataCount to 0 after update firmware */
		g_FwDataCount = 0;

		return -1;
	}
}

static s32 _DrvFwCtrlUpdateFirmwareBySdCard(const char *pFilePath)
{
	s32 nRetVal = 0;
	struct file *pfile = NULL;
	struct inode *inode;
	s32 fsize = 0;
	u8 *pbt_buf = NULL;
	mm_segment_t old_fs;
	loff_t pos;
	u16 eSwId = 0x0000;
	u16 eVendorId = 0x0000;

	DBG("*** %s() ***\n", __func__);

	pfile = filp_open(pFilePath, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		DBG("Error occured while opening file %s.\n", pFilePath);
		return -1;
	}

	inode = pfile->f_dentry->d_inode;
	fsize = inode->i_size;

	DBG("fsize = %d\n", fsize);

	if (fsize <= 0) {
		filp_close(pfile, NULL);
		return -1;
	}

	/* read firmware */
	pbt_buf = kmalloc(fsize, GFP_KERNEL);

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	pos = 0;
	vfs_read(pfile, pbt_buf, fsize, &pos);

	filp_close(pfile, NULL);
	set_fs(old_fs);

	_DrvFwCtrlStoreFirmwareData(pbt_buf, fsize);

	kfree(pbt_buf);

	DrvPlatformLyrDisableFingerTouchReport();

	DrvPlatformLyrTouchDeviceResetHw();

	if (g_ChipType == CHIP_TYPE_MSG21XXA) {
		eVendorId = g_FwData[31][0x34F] << 8 | g_FwData[31][0x34E];
		eSwId = _DrvFwCtrlMsg21xxaGetSwId(EMEM_MAIN);
	} else if (g_ChipType == CHIP_TYPE_MSG22XX) {
		eVendorId = g_FwData[47][1013] << 8 | g_FwData[47][1012];
		eSwId = _DrvFwCtrlMsg22xxGetSwId(EMEM_MAIN);
	}

	DBG("eVendorId = 0x%x, eSwId = 0x%x\n", eVendorId, eSwId);

	if (eSwId == eVendorId)	{
		/* 33KB && 48.5KB*/
		if ((g_ChipType == CHIP_TYPE_MSG21XXA && fsize == 33792) ||
			(g_ChipType == CHIP_TYPE_MSG22XX && fsize == 49664)) {
			nRetVal = _DrvFwCtrlUpdateFirmwareCash(g_FwData);
		} else {
			DBG("The file size of the update firmware bin file is ",
					"not supported, fsize = %d\n", fsize);
			nRetVal = -1;
		}
	} else {
		DBG("The vendor id of the update firmware bin file is ",
				"different from the vendor id on e-flash.\n");
		nRetVal = -1;
	}

	g_FwDataCount = 0; /* Reset g_FwDataCount to 0 after update firmware */

	DrvPlatformLyrEnableFingerTouchReport();

	return nRetVal;
}

void DrvFwCtrlOptimizeCurrentConsumption(void)
{
	u32 i;
	u8 szDbBusTxData[27] = {0};

	DBG("g_ChipType = 0x%x\n", g_ChipType);

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (g_GestureWakeupFlag == 1)
		return;

#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

	if (g_ChipType == CHIP_TYPE_MSG22XX) {
		DBG("*** %s() ***\n", __func__);

		DrvPlatformLyrTouchDeviceResetHw();

		DbBusEnterSerialDebugMode();
		DbBusStopMCU();
		DbBusIICUseBus();
		DbBusIICReshape();

		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x80));

		/* Enable burst mode */
		RegSet16BitValue(0x160C, (RegGet16BitValue(0x160C) | 0x01));

		szDbBusTxData[0] = 0x10;
		szDbBusTxData[1] = 0x11;
		szDbBusTxData[2] = 0xA0; /* bank:0x11, addr:h0050 */

		for (i = 0; i < 24; i++)
			szDbBusTxData[i+3] = 0x11;

		/* write 0x1111 for reg 0x1150~0x115B */
		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 3+24);

		szDbBusTxData[0] = 0x10;
		szDbBusTxData[1] = 0x11;
		szDbBusTxData[2] = 0xB8; /* bank:0x11, addr:h005C */

		for (i = 0; i < 6; i++)
			szDbBusTxData[i+3] = 0xFF;

		/* Write 0xFF for reg 0x115C~0x115E */
		IicWriteData(SLAVE_I2C_ID_DBBUS, &szDbBusTxData[0], 3+6);

		/* Clear burst mode */
		RegSet16BitValue(0x160C, RegGet16BitValue(0x160C) & (~0x01));

		DbBusIICNotUseBus();
		DbBusNotStopMCU();
		DbBusExitSerialDebugMode();
	}
}

u8 DrvFwCtrlGetChipType(void)
{
	u8 nChipType = 0;

	DBG("*** %s() ***\n", __func__);

	DrvPlatformLyrTouchDeviceResetHw();

	/* Erase TP Flash first */
	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();
	mdelay(100);

	/* Stop MCU */
	RegSetLByteValue(0x0FE6, 0x01);

	/* Disable watchdog */
	RegSet16BitValue(0x3C60, 0xAA55);

	/* Difference between C2 and C3 */
	/* c2:MSG2133(1) c32:MSG2133A(2) c33:MSG2138A(2) */
	/* check ic type */
	nChipType = RegGet16BitValue(0x1ECC) & 0xFF;

	if (nChipType != CHIP_TYPE_MSG21XX &&   /* (0x01)  */
			nChipType != CHIP_TYPE_MSG21XXA &&  /* (0x02)  */
			nChipType != CHIP_TYPE_MSG26XXM &&  /* (0x03)  */
			nChipType != CHIP_TYPE_MSG22XX)     /* (0x7A)  */
		nChipType = 0;

	DBG("*** Chip Type = 0x%x ***\n", nChipType);

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	DrvPlatformLyrTouchDeviceResetHw();

	return nChipType;
}

void DrvFwCtrlGetCustomerFirmwareVersion(u16 *pMajor, u16 *pMinor,
		u8 **ppVersion)
{
	DBG("*** %s() ***\n", __func__);

	if (g_ChipType == CHIP_TYPE_MSG21XXA ||
			g_ChipType == CHIP_TYPE_MSG21XX) {
		u8 szDbBusTxData[3] = {0};
		u8 szDbBusRxData[4] = {0};

		szDbBusTxData[0] = 0x53;
		szDbBusTxData[1] = 0x00;

		if (g_ChipType == CHIP_TYPE_MSG21XXA)
			szDbBusTxData[2] = 0x2A;
		else if (g_ChipType == CHIP_TYPE_MSG21XX)
			szDbBusTxData[2] = 0x74;
		else
			szDbBusTxData[2] = 0x2A;

		mutex_lock(&g_Mutex);

		DrvPlatformLyrTouchDeviceResetHw();

		IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 3);
		IicReadData(SLAVE_I2C_ID_DWI2C, &szDbBusRxData[0], 4);

		mutex_unlock(&g_Mutex);

		*pMajor = (szDbBusRxData[1]<<8) + szDbBusRxData[0];
		*pMinor = (szDbBusRxData[3]<<8) + szDbBusRxData[2];
	} else if (g_ChipType == CHIP_TYPE_MSG22XX) {
		u16 nRegData1, nRegData2;

		mutex_lock(&g_Mutex);

		DrvPlatformLyrTouchDeviceResetHw();

		DbBusEnterSerialDebugMode();
		DbBusStopMCU();
		DbBusIICUseBus();
		DbBusIICReshape();
		mdelay(100);

		/* Stop mcu */
		RegSetLByteValue(0x0FE6, 0x01);

		/* Stop watchdog */
		RegSet16BitValue(0x3C60, 0xAA55);

		/* RIU password */
		RegSet16BitValue(0x161A, 0xABBA);

		/* Clear pce */
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x80));

		/*
		 * Set start address for customer
		 * firmware version on main block
		 */
		RegSet16BitValue(0x1600, 0xBFF4);

		/* Enable burst mode */
		/*
		 * RegSet16BitValue(0x160C, (RegGet16BitValue(0x160C) | 0x01));
		 */

		/* Set pce */
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x40));

		RegSetLByteValue(0x160E, 0x01);

		nRegData1 = RegGet16BitValue(0x1604);
		nRegData2 = RegGet16BitValue(0x1606);

		*pMajor = (((nRegData1 >> 8) & 0xFF) << 8) + (nRegData1 & 0xFF);
		*pMinor = (((nRegData2 >> 8) & 0xFF) << 8) + (nRegData2 & 0xFF);

		/* Clear burst mode */
		/*
		 * RegSet16BitValue(0x160C, RegGet16BitValue(0x160C) & (~0x01));
		 */

		RegSet16BitValue(0x1600, 0x0000);

		/* Clear RIU password */
		RegSet16BitValue(0x161A, 0x0000);

		DbBusIICNotUseBus();
		DbBusNotStopMCU();
		DbBusExitSerialDebugMode();

		DrvPlatformLyrTouchDeviceResetHw();

		mutex_unlock(&g_Mutex);
	}

	DBG("*** major = %d ***\n", *pMajor);
	DBG("*** minor = %d ***\n", *pMinor);

	if (*ppVersion == NULL)
		*ppVersion = kzalloc(sizeof(u8)*6, GFP_KERNEL);

	sprintf(*ppVersion, "%03d%03d", *pMajor, *pMinor);
}

void DrvFwCtrlGetPlatformFirmwareVersion(u8 **ppVersion)
{
	u32 i;
	u16 nRegData1, nRegData2;
	u8 szDbBusRxData[12] = {0};

	DBG("*** %s() ***\n", __func__);

	mutex_lock(&g_Mutex);

	DrvPlatformLyrTouchDeviceResetHw();

	DbBusEnterSerialDebugMode();
	DbBusStopMCU();
	DbBusIICUseBus();
	DbBusIICReshape();
	mdelay(100);

	/* Only MSG22XX support platform firmware version */
	if (g_ChipType == CHIP_TYPE_MSG22XX) {
		/* Stop mcu */
		RegSetLByteValue(0x0FE6, 0x01);

		/* Stop watchdog */
		RegSet16BitValue(0x3C60, 0xAA55);

		/* RIU password */
		RegSet16BitValue(0x161A, 0xABBA);

		/* Clear pce */
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x80));

		/*
		 * Set start address for platform firmware version on info
		 * block(Actually, start reading from 0xC1F0)
		 */
		RegSet16BitValue(0x1600, 0xC1F2);

		/* Enable burst mode */
		RegSet16BitValue(0x160C, (RegGet16BitValue(0x160C) | 0x01));

		/* Set pce */
		RegSet16BitValue(0x1618, (RegGet16BitValue(0x1618) | 0x40));

		for (i = 0; i < 3; i++) {
			RegSetLByteValue(0x160E, 0x01);

			nRegData1 = RegGet16BitValue(0x1604);
			nRegData2 = RegGet16BitValue(0x1606);

			szDbBusRxData[i*4+0] = (nRegData1 & 0xFF);
			szDbBusRxData[i*4+1] = ((nRegData1 >> 8) & 0xFF);

			szDbBusRxData[i*4+2] = (nRegData2 & 0xFF);
			szDbBusRxData[i*4+3] = ((nRegData2 >> 8) & 0xFF);
		}

		/* Clear burst mode */
		RegSet16BitValue(0x160C, RegGet16BitValue(0x160C) & (~0x01));

		RegSet16BitValue(0x1600, 0x0000);

		/* Clear RIU password */
		RegSet16BitValue(0x161A, 0x0000);

		if (*ppVersion == NULL)
			*ppVersion = kzalloc(sizeof(u8)*10, GFP_KERNEL);

		sprintf(*ppVersion, "%c%c%c%c%c%c%c%c%c%c",
				szDbBusRxData[2], szDbBusRxData[3],
				szDbBusRxData[4], szDbBusRxData[5],
				szDbBusRxData[6], szDbBusRxData[7],
				szDbBusRxData[8], szDbBusRxData[9],
				szDbBusRxData[10], szDbBusRxData[11]);
	} else {
		if (*ppVersion == NULL)
			*ppVersion = kzalloc(sizeof(u8)*10, GFP_KERNEL);

		sprintf(*ppVersion, "%s", "N/A");
	}

	DbBusIICNotUseBus();
	DbBusNotStopMCU();
	DbBusExitSerialDebugMode();

	DrvPlatformLyrTouchDeviceResetHw();

	mutex_unlock(&g_Mutex);

	DBG("*** platform firmware version = %s ***\n", *ppVersion);
}

s32 DrvFwCtrlUpdateFirmware(u8 szFwData[][1024], enum EmemType_e eEmemType)
{
	DBG("*** %s() ***\n", __func__);

	return _DrvFwCtrlUpdateFirmwareCash(szFwData);
}

s32 DrvFwCtrlUpdateFirmwareBySdCard(const char *pFilePath)
{
	s32 nRetVal = -1;

	DBG("*** %s() ***\n", __func__);

	if (g_ChipType == CHIP_TYPE_MSG21XXA ||
			g_ChipType == CHIP_TYPE_MSG22XX)
		nRetVal = _DrvFwCtrlUpdateFirmwareBySdCard(pFilePath);
	else
		DBG("This chip type (%d) does not support update firmware ",
				"by sd card\n", g_ChipType);

	return nRetVal;
}

void DrvFwCtrlHandleFingerTouch(void)
{
	struct TouchInfo_t tInfo;
	u32 i;
	u8 nTouchKeyCode = 0;
	static u32 nLastKeyCode;
	u8 *pPacket = NULL;
	u16 nReportPacketLength = 0;

	if (_gIsDisableFinagerTouch == 1) {
		DBG("Skip finger touch for handling get firmware info ",
				"or change firmware mode\n");
		return;
	}

	mutex_lock(&g_Mutex);

	memset(&tInfo, 0x0, sizeof(tInfo));

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
	if (g_FirmwareMode == FIRMWARE_MODE_DEMO_MODE) {
		DBG("FIRMWARE_MODE_DEMO_MODE\n");

		nReportPacketLength = DEMO_MODE_PACKET_LENGTH;
		pPacket = g_DemoModePacket;
	} else if (g_FirmwareMode == FIRMWARE_MODE_DEBUG_MODE) {
		DBG("FIRMWARE_MODE_DEBUG_MODE\n");

		if (g_FirmwareInfo.nLogModePacketHeader != 0x62) {
			DBG("WRONG DEBUG MODE HEADER : 0x%x\n",
					g_FirmwareInfo.nLogModePacketHeader);
			goto End;
		}

		nReportPacketLength = g_FirmwareInfo.nLogModePacketLength;
		pPacket = g_LogModePacket;
	} else if (g_FirmwareMode == FIRMWARE_MODE_RAW_DATA_MODE) {
		DBG("FIRMWARE_MODE_RAW_DATA_MODE\n");

		if (g_FirmwareInfo.nLogModePacketHeader != 0x62) {
			DBG("WRONG RAW DATA MODE HEADER : 0x%x\n",
					g_FirmwareInfo.nLogModePacketHeader);
			goto End;
		}

		nReportPacketLength = g_FirmwareInfo.nLogModePacketLength;
		pPacket = g_LogModePacket;
	} else {
		DBG("WRONG FIRMWARE MODE : 0x%x\n", g_FirmwareMode);
		goto End;
	}
#else
	DBG("FIRMWARE_MODE_DEMO_MODE\n");

	nReportPacketLength = DEMO_MODE_PACKET_LENGTH;
	pPacket = g_DemoModePacket;
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
	if (g_GestureDebugMode == 1 && g_GestureWakeupFlag == 1) {
		DBG("Set gesture debug mode packet length, g_ChipType=%d\n",
				g_ChipType);

		if (g_ChipType == CHIP_TYPE_MSG22XX) {
			nReportPacketLength = GESTURE_DEBUG_MODE_PACKET_LENGTH;
			pPacket = _gGestureWakeupPacket;
		} else {
			DBG("This chip type does not support gesture ",
					"debug mode.\n");
			goto End;
		}
	} else if (g_GestureWakeupFlag == 1) {
		DBG("Set gesture wakeup packet length, g_ChipType=%d\n",
				g_ChipType);

		if (g_ChipType == CHIP_TYPE_MSG22XX) {
#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
			nReportPacketLength =
				GESTURE_WAKEUP_INFORMATION_PACKET_LENGTH;
#else
			nReportPacketLength = GESTURE_WAKEUP_PACKET_LENGTH;
#endif /* CONFIG_ENABLE_GESTURE_INFORMATION_MODE */
			pPacket = _gGestureWakeupPacket;
		} else if (g_ChipType == CHIP_TYPE_MSG21XXA) {
			nReportPacketLength = DEMO_MODE_PACKET_LENGTH;
			pPacket = _gGestureWakeupPacket;
		} else {
			DBG("This chip type does not support ",
					"gesture wakeup.\n");
			goto End;
		}
	}

#else

	if (g_GestureWakeupFlag == 1) {
		DBG("Set gesture wakeup packet length, g_ChipType=%d\n",
				g_ChipType);

		if (g_ChipType == CHIP_TYPE_MSG22XX) {
#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
			nReportPacketLength =
				GESTURE_WAKEUP_INFORMATION_PACKET_LENGTH;
#else
			nReportPacketLength = GESTURE_WAKEUP_PACKET_LENGTH;
#endif /* CONFIG_ENABLE_GESTURE_INFORMATION_MODE */

			pPacket = _gGestureWakeupPacket;
		} else if (g_ChipType == CHIP_TYPE_MSG21XXA) {
			nReportPacketLength = DEMO_MODE_PACKET_LENGTH;
			pPacket = _gGestureWakeupPacket;
		} else {
			DBG("This chip type does not support ",
					"gesture wakeup.\n");
			goto End;
		}
	}
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (g_GestureWakeupFlag == 1) {
		u32 i = 0, rc;

		while (i < 5) {
			mdelay(50);

			rc = IicReadData(SLAVE_I2C_ID_DWI2C, &pPacket[0],
					nReportPacketLength);

			if (rc > 0)
				break;

			i++;
		}
	} else
		IicReadData(SLAVE_I2C_ID_DWI2C, &pPacket[0],
				nReportPacketLength);
#else
	IicReadData(SLAVE_I2C_ID_DWI2C, &pPacket[0], nReportPacketLength);
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP    */

	if (0 == _DrvFwCtrlParsePacket(pPacket, nReportPacketLength, &tInfo)) {
		if ((tInfo.nFingerNum) == 0) { /* touch end */
			if (nLastKeyCode != 0) {
				DBG("key touch released\n");

				input_report_key(g_InputDevice, BTN_TOUCH, 0);
				input_report_key(g_InputDevice,
						nLastKeyCode, 0);

				input_sync(g_InputDevice);

				nLastKeyCode = 0; /* clear key status.. */
			} else {
				DrvPlatformLyrFingerTouchReleased(0, 0);

				input_sync(g_InputDevice);
			}
		} else { /* touch on screen */
			if (tInfo.nTouchKeyCode != 0) {
#ifdef CONFIG_TP_HAVE_KEY
				/* TOUCH_KEY_HOME */
				if (tInfo.nTouchKeyCode == 4)
					nTouchKeyCode = g_TpVirtualKey[1];
				/* TOUCH_KEY_MENU */
				else if (tInfo.nTouchKeyCode == 1)
					nTouchKeyCode = g_TpVirtualKey[0];
				/* TOUCH_KEY_BACK */
				else if (tInfo.nTouchKeyCode == 2)
					nTouchKeyCode = g_TpVirtualKey[2];
				/* TOUCH_KEY_SEARCH  */
				else if (tInfo.nTouchKeyCode == 8)
					nTouchKeyCode = g_TpVirtualKey[3];

				if (nLastKeyCode != nTouchKeyCode) {
					DBG("key touch pressed\n");
					DBG("nTouchKeyCode = %d, ",
							"nLastKeyCode = %d\n",
							nTouchKeyCode,
							nLastKeyCode);

					nLastKeyCode = nTouchKeyCode;

					input_report_key(g_InputDevice,
							BTN_TOUCH, 1);
					input_report_key(g_InputDevice,
							nTouchKeyCode, 1);

					input_sync(g_InputDevice);
				}
#endif /* CONFIG_TP_HAVE_KEY */
			} else {
				DBG("tInfo->nFingerNum = %d...............\n",
						tInfo.nFingerNum);

				for (i = 0; i < tInfo.nFingerNum; i++)
					DrvPlatformLyrFingerTouchPressed(
							tInfo.tPoint[i].nX,
							tInfo.tPoint[i].nY,
							0, 0);

				input_sync(g_InputDevice);
			}
		}
	}

#if defined(CONFIG_ENABLE_FIRMWARE_DATA_LOG) || \
	defined(CONFIG_ENABLE_GESTURE_WAKEUP)
End:
#endif
	mutex_unlock(&g_Mutex);
}

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP

void DrvFwCtrlOpenGestureWakeup(u32 *pMode)
{
	u8 szDbBusTxData[4] = {0};
	u32 i = 0;
	s32 rc;

	DBG("*** %s() ***\n", __func__);

	DBG("wakeup mode 0 = 0x%x\n", pMode[0]);
	DBG("wakeup mode 1 = 0x%x\n", pMode[1]);

#ifdef CONFIG_SUPPORT_64_TYPES_GESTURE_WAKEUP_MODE
	szDbBusTxData[0] = 0x59;
	szDbBusTxData[1] = 0x00;
	szDbBusTxData[2] = ((pMode[1] & 0xFF000000) >> 24);
	szDbBusTxData[3] = ((pMode[1] & 0x00FF0000) >> 16);

	while (i < 5) {
		rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 4);
		udelay(1000); /* delay 1ms */

		if (rc > 0) {
			DBG("Enable gesture wakeup index 0 success\n");
			break;
		}

		mdelay(10);
		i++;
	}

	if (i == 5)
		DBG("Enable gesture wakeup index 0 failed\n");

	szDbBusTxData[0] = 0x59;
	szDbBusTxData[1] = 0x01;
	szDbBusTxData[2] = ((pMode[1] & 0x0000FF00) >> 8);
	szDbBusTxData[3] = ((pMode[1] & 0x000000FF) >> 0);

	while (i < 5) {
		rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 4);
		udelay(1000); /* delay 1ms  */

		if (rc > 0) {
			DBG("Enable gesture wakeup index 1 success\n");
			break;
		}

		mdelay(10);
		i++;
	}

	if (i == 5)
		DBG("Enable gesture wakeup index 1 failed\n");

	szDbBusTxData[0] = 0x59;
	szDbBusTxData[1] = 0x02;
	szDbBusTxData[2] = ((pMode[0] & 0xFF000000) >> 24);
	szDbBusTxData[3] = ((pMode[0] & 0x00FF0000) >> 16);

	while (i < 5) {
		rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 4);
		udelay(1000); /* delay 1ms */

		if (rc > 0) {
			DBG("Enable gesture wakeup index 2 success\n");
			break;
		}

		mdelay(10);
		i++;
	}

	if (i == 5)
		DBG("Enable gesture wakeup index 2 failed\n");

	szDbBusTxData[0] = 0x59;
	szDbBusTxData[1] = 0x03;
	szDbBusTxData[2] = ((pMode[0] & 0x0000FF00) >> 8);
	szDbBusTxData[3] = ((pMode[0] & 0x000000FF) >> 0);

	while (i < 5) {
		rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 4);
		udelay(1000); /* delay 1ms */

		if (rc > 0) {
			DBG("Enable gesture wakeup index 3 success\n");
			break;
		}

		mdelay(10);
		i++;
	}

	if (i == 5)
		DBG("Enable gesture wakeup index 3 failed\n");

	g_GestureWakeupFlag = 1; /* gesture wakeup is enabled */

#else

	szDbBusTxData[0] = 0x58;
	szDbBusTxData[1] = ((pMode[0] & 0x0000FF00) >> 8);
	szDbBusTxData[2] = ((pMode[0] & 0x000000FF) >> 0);

	while (i < 5) {
		rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 3);

		if (rc > 0) {
			DBG("Enable gesture wakeup success\n");
			break;
		}

		mdelay(10);
		i++;
	}

	if (i == 5)
		DBG("Enable gesture wakeup failed\n");

	g_GestureWakeupFlag = 1; /* gesture wakeup is enabled */
#endif /* CONFIG_SUPPORT_64_TYPES_GESTURE_WAKEUP_MODE */
}

void DrvFwCtrlCloseGestureWakeup(void)
{
	DBG("*** %s() ***\n", __func__);

	g_GestureWakeupFlag = 0; /* gesture wakeup is disabled */
}

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
void DrvFwCtrlOpenGestureDebugMode(u8 nGestureFlag)
{
	u8 szDbBusTxData[3] = {0};
	s32 rc;

	DBG("*** %s() ***\n", __func__);

	DBG("Gesture Flag = 0x%x\n", nGestureFlag);

	szDbBusTxData[0] = 0x30;
	szDbBusTxData[1] = 0x01;
	szDbBusTxData[2] = nGestureFlag;

	rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 3);
	if (rc < 0)
		DBG("Enable gesture debug mode failed\n");
	else
		DBG("Enable gesture debug mode success\n");

	g_GestureDebugMode = 1; /* gesture debug mode is enabled */
}

void DrvFwCtrlCloseGestureDebugMode(void)
{
	u8 szDbBusTxData[3] = {0};
	s32 rc;

	DBG("*** %s() ***\n", __func__);

	szDbBusTxData[0] = 0x30;
	szDbBusTxData[1] = 0x00;
	szDbBusTxData[2] = 0x00;

	rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 3);
	if (rc < 0)
		DBG("Disable gesture debug mode failed\n");
	else
		DBG("Disable gesture debug mode success\n");

	g_GestureDebugMode = 0; /* gesture debug mode is disabled */
}
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
static void _DrvFwCtrlCoordinate(u8 *pRawData, u32 *pTranX, u32 *pTranY)
{
	u32 nX;
	u32 nY;
#ifdef CONFIG_SWAP_X_Y
	u32 nTempX;
	u32 nTempY;
#endif
	/* parse the packet to coordinate */
	nX = (((pRawData[0] & 0xF0) << 4) | pRawData[1]);
	nY = (((pRawData[0] & 0x0F) << 8) | pRawData[2]);

	DBG("[x,y]=[%d,%d]\n", nX, nY);

#ifdef CONFIG_SWAP_X_Y
	nTempY = nX;
	nTempX = nY;
	nX = nTempX;
	nY = nTempY;
#endif

#ifdef CONFIG_REVERSE_X
	nX = 2047 - nX;
#endif

#ifdef CONFIG_REVERSE_Y
	nY = 2047 - nY;
#endif

	/*
	 * pRawData[0]~nRawData[2] : the point abs,
	 * pRawData[0]~nRawData[2] all are 0xFF, release touch
	 */
	if ((pRawData[0] == 0xFF) && (pRawData[1] == 0xFF) &&
			(pRawData[2] == 0xFF)) {
		*pTranX = 0; /* final X coordinate */
		*pTranY = 0; /* final Y coordinate */
	} else {
		/* one touch point */
		*pTranX = (nX * TOUCH_SCREEN_X_MAX) / TPD_WIDTH;
		*pTranY = (nY * TOUCH_SCREEN_Y_MAX) / TPD_HEIGHT;
		DBG("[%s]: [x,y]=[%d,%d]\n", __func__, nX, nY);
		DBG("[%s]: point[x,y]=[%d,%d]\n", __func__, *pTranX, *pTranY);
	}
}
#endif /* CONFIG_ENABLE_GESTURE_INFORMATION_MODE */

#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

/* -------------------------------------------------------------------------- */

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG

u16 DrvFwCtrlChangeFirmwareMode(u16 nMode)
{
	u8 szDbBusTxData[2] = {0};
	u32 i = 0;
	s32 rc;

	DBG("*** %s() *** nMode = 0x%x\n", __func__, nMode);

	/*
	 * Disable finger touch ISR handling temporarily for device driver
	 * can send change firmware mode i2c command to firmware.
	 */
	_gIsDisableFinagerTouch = 1;

	szDbBusTxData[0] = 0x02;
	szDbBusTxData[1] = (u8)nMode;


	mutex_lock(&g_Mutex);

	while (i < 5) {
		rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 2);
		if (rc > 0) {
			DBG("Change firmware mode success\n");
			break;
		}

		mdelay(10);
		i++;
	}

	if (i == 5)
		DBG("Change firmware mode failed, rc = %d\n", rc);

	mutex_unlock(&g_Mutex);

	_gIsDisableFinagerTouch = 0;

	return nMode;
}

void DrvFwCtrlGetFirmwareInfo(struct FirmwareInfo_t *pInfo)
{
	u8 szDbBusTxData[1] = {0};
	u8 szDbBusRxData[8] = {0};
	u32 i = 0;
	s32 rc;

	DBG("*** %s() ***\n", __func__);

	/*
	 * Disable finger touch ISR handling temporarily for device driver
	 * can send get firmware info i2c command to firmware.
	 */
	_gIsDisableFinagerTouch = 1;

	szDbBusTxData[0] = 0x01;

	mutex_lock(&g_Mutex);

	while (i < 5) {
		rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 1);
		if (rc > 0)
			DBG("Get firmware info IicWriteData() success\n");

		mdelay(20);
		rc = IicReadData(SLAVE_I2C_ID_DWI2C, &szDbBusRxData[0], 8);
		if (rc > 0) {
			DBG("Get firmware info IicReadData() success\n");
			break;
		}

		mdelay(10);
		i++;
	}

	if (i == 5)
		DBG("Get firmware info failed, rc = %d\n", rc);

	mutex_unlock(&g_Mutex);

	if ((szDbBusRxData[1] & 0x80) == 0x80)
		pInfo->nIsCanChangeFirmwareMode = 0;
	else
		pInfo->nIsCanChangeFirmwareMode = 1;

	pInfo->nFirmwareMode = szDbBusRxData[1] & 0x7F;
	pInfo->nLogModePacketHeader = szDbBusRxData[2];
	pInfo->nLogModePacketLength = (szDbBusRxData[3]<<8) + szDbBusRxData[4];

	DBG("pInfo->nFirmwareMode=0x%x, pInfo->nLogModePacketHeader=0x%x, ",
			"pInfo->nLogModePacketLength=%d, ",
			"pInfo->nIsCanChangeFirmwareMode=%d\n",
			pInfo->nFirmwareMode, pInfo->nLogModePacketHeader,
			pInfo->nLogModePacketLength,
			pInfo->nIsCanChangeFirmwareMode);

	_gIsDisableFinagerTouch = 0;
}

void DrvFwCtrlRestoreFirmwareModeToLogDataMode(void)
{
	DBG("*** %s() g_IsSwitchModeByAPK = %d ***\n", __func__,
			g_IsSwitchModeByAPK);

	if (g_IsSwitchModeByAPK == 1) {
		struct FirmwareInfo_t tInfo;

		memset(&tInfo, 0x0, sizeof(FirmwareInfo_t));

		DrvFwCtrlGetFirmwareInfo(&tInfo);

		DBG("g_FirmwareMode = 0x%x, tInfo.nFirmwareMode = 0x%x\n",
				g_FirmwareMode, tInfo.nFirmwareMode);

		/*
		 * Since reset_hw() will reset the firmware mode to demo mode,
		 * we must reset the firmware mode again after reset_hw().
		 */
		if (g_FirmwareMode == FIRMWARE_MODE_DEBUG_MODE &&
				FIRMWARE_MODE_DEBUG_MODE != tInfo.nFirmwareMode)
			g_FirmwareMode = DrvFwCtrlChangeFirmwareMode(
					FIRMWARE_MODE_DEBUG_MODE);
		else if (g_FirmwareMode == FIRMWARE_MODE_RAW_DATA_MODE &&
				FIRMWARE_MODE_RAW_DATA_MODE !=
				tInfo.nFirmwareMode)
			g_FirmwareMode = DrvFwCtrlChangeFirmwareMode(
					FIRMWARE_MODE_RAW_DATA_MODE);
		else
			DBG("firmware mode is not restored\n");
	}
}
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */


#ifdef CONFIG_ENABLE_PROXIMITY_DETECTION

s32 DrvFwCtrlEnableProximity(void)
{
	u8 szDbBusTxData[4] = {0};
	s32 rc;

	DBG("*** %s() ***\n", __func__);

	szDbBusTxData[0] = 0x52;
	szDbBusTxData[1] = 0x00;

	if (g_ChipType == CHIP_TYPE_MSG21XX)
		szDbBusTxData[2] = 0x62;
	else if (g_ChipType == CHIP_TYPE_MSG21XXA ||
			g_ChipType == CHIP_TYPE_MSG22XX)
		szDbBusTxData[2] = 0x4a;
	else {
		DBG("*** Un-recognized chip type = 0x%x ***\n", g_ChipType);
		return -1;
	}

	szDbBusTxData[3] = 0xa0;

	mutex_lock(&g_Mutex);
	rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 4);
	mutex_unlock(&g_Mutex);

	if (rc > 0) {
		g_EnableTpProximity = 1;
		DBG("Enable proximity detection success\n");
	} else {
		g_EnableTpProximity = 0;
		DBG("Enable proximity detection failed\n");
	}

	return rc;
}

s32 DrvFwCtrlDisableProximity(void)
{
	u8 szDbBusTxData[4] = {0};
	s32 rc;

	DBG("*** %s() ***\n", __func__);

	szDbBusTxData[0] = 0x52;
	szDbBusTxData[1] = 0x00;

	if (g_ChipType == CHIP_TYPE_MSG21XX)
		szDbBusTxData[2] = 0x62;
	else if (g_ChipType == CHIP_TYPE_MSG21XXA ||
			g_ChipType == CHIP_TYPE_MSG22XX)
		szDbBusTxData[2] = 0x4a;
	else {
		DBG("*** Un-recognized chip type = 0x%x ***\n", g_ChipType);
		return -1;
	}

	szDbBusTxData[3] = 0xa1;

	mutex_lock(&g_Mutex);
	rc = IicWriteData(SLAVE_I2C_ID_DWI2C, &szDbBusTxData[0], 4);
	mutex_unlock(&g_Mutex);

	if (rc > 0)
		DBG("Disable proximity detection success\n");
	else
		DBG("Disable proximity detection failed\n");

	g_EnableTpProximity = 0;
	g_FaceClosingTp = 0;

	return rc;
}

#endif /* CONFIG_ENABLE_PROXIMITY_DETECTION */
#endif /* CONFIG_ENABLE_CHIP_MSG21XXA || CONFIG_ENABLE_CHIP_MSG22XX */
