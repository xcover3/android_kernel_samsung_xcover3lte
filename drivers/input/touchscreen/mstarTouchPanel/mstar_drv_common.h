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
 * @file    mstar_drv_common.h
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#ifndef __MSTAR_DRV_COMMON_H__
#define __MSTAR_DRV_COMMON_H__

/*--------------------------------------------------------------------------*/
/* INCLUDE FILE                                                             */
/*--------------------------------------------------------------------------*/

#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include <linux/firmware.h>
#include <linux/platform_device.h>

#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/cdev.h>
#include <linux/wakelock.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

/*--------------------------------------------------------------------------*/
/* TOUCH DEVICE DRIVER RELEASE VERSION                                      */
/*--------------------------------------------------------------------------*/

#define DEVICE_DRIVER_RELEASE_VERSION   ("2.5.0.0")


/*--------------------------------------------------------------------------*/
/* COMPILE OPTION DEFINITION                                                */
/*--------------------------------------------------------------------------*/

/*
 * Note.
 * The below compile option is used to enable the specific device driver code
 * handling for distinct touch ic.
 * Please enable the compile option depends on the touch ic that customer
 * project are using and disable the others.
 */
/*#define CONFIG_ENABLE_CHIP_MSG21XXA */
#define CONFIG_ENABLE_CHIP_MSG22XX
/*#define CONFIG_ENABLE_CHIP_MSG26XXM */

/*
 * Note.
 * The below compile option is used to enable the specific device driver code
 * handling to make sure main board can supply power to touch ic for some
 * specific BB chip of MTK(EX. MT6582)/SPRD(EX. SC7715)/QCOM(EX. MSM8610).
 * By default, this compile option is disabled.
 */
#define CONFIG_ENABLE_REGULATOR_POWER_ON

/*
 * Note.
 * The below compile option is used to enable the output log mechanism while
 * touch device driver is running.
 * If this compile option is not defined, the function for output log will
 * be disabled.
 * By default, this compile option is enabled.
 */
/*#define CONFIG_ENABLE_TOUCH_DRIVER_DEBUG */

/*
 * Note.
 * The below compile option is used to enable the specific device driver code
 * handling when touch panel support virtual key(EX. Menu, Home, Back, Search).
 * If this compile option is not defined, the function for virtual key handling
 * will be disabled.
 * By default, this compile option is enabled.
 */
#define CONFIG_TP_HAVE_KEY

/*
 * Note.
 * Since specific MTK BB chip report virtual key touch by using coordinate
 * instead of key code, the below compile option is used to enable the code
 * handling for reporting key with coordinate.
 * This compile option is used for MTK platform only.
 * By default, this compile option is disabled.
 */
#define CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE
#ifdef CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE
#define VIRTUAL_KEY
#endif

/*
 * Note.
 * The below compile option is used to enable debug mode data log for firmware.
 * Please make sure firmware support debug mode data log first, then you can
 * enable this compile option.
 * Else, please disbale this compile option.
 * By default, this compile option is disabled.
 */
/*#define CONFIG_ENABLE_FIRMWARE_DATA_LOG */

/*#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG */
#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG

/*
 * Note.
 * The below compile option is used to enable segment read debug mode finger
 * touch data for MSG26XXM only.
 * Since I2C transaction length limitation for some specific MTK BB
 * chip(EX. MT6589/MT6572/...) or QCOM BB chip, the debug mode finger touch
 * data of MSG26XXM can not be retrieved by one time I2C read operation.
 * So we need to retrieve the complete finger touch data by segment read.
 * By default, this compile option is enabled.
 */
#define CONFIG_ENABLE_SEGMENT_READ_FINGER_TOUCH_DATA

#endif /*CONFIG_ENABLE_FIRMWARE_DATA_LOG */
/*#endif CONFIG_ENABLE_FIRMWARE_DATA_LOG */

/*
 * Note.
 * The below compile option is used to enable gesture wakeup.
 * By default, this compile option is disabled.
 */
/*#define CONFIG_ENABLE_GESTURE_WAKEUP */

/*#ifdef CONFIG_ENABLE_GESTURE_WAKEUP */
#ifdef CONFIG_ENABLE_GESTURE_WAKEUP

/*
 * Note.
 * The below compile option is used to enable device driver to support at most
 * 64 types of gesture wakeup mode.
 * If the below compile option is not enabled, device driver can only support
 * at most 16 types of gesture wakeup mode.
 * By the way, 64 types of gesture wakeup mode is ready for MSG22XX only.
 * But, 64 types of gesture wakeup mode for MSG21XXA is not supported.
 * Besides, 64 types of gesture wakeup mode for MSG26XXM is not ready yet.
 * By default, this compile option is disabled.
 */
/*#define CONFIG_SUPPORT_64_TYPES_GESTURE_WAKEUP_MODE */

/*
 * Note.
 * The below compile option is used to enable gesture debug mode.
 * By default, this compile option is disabled.
 */
/*#define CONFIG_ENABLE_GESTURE_DEBUG_MODE */

/*
 * Note.
 * The below compile option is used to enable gesture trajectory display mode.
 * By default, this compile option is disabled.
 */
/*#define CONFIG_ENABLE_GESTURE_INFORMATION_MODE */

#endif /*CONFIG_ENABLE_GESTURE_WAKEUP */
/*#endif CONFIG_ENABLE_GESTURE_WAKEUP */


/*
 * Note.
 * The below compile option is used to enable hipad firmware upgrade detection.
 * Be careful, hipad firmware upgrade detection is not ready yet.
 * By default, this compile option is disabled.
 */
/* #define CONFIG_HIPAD_FIRMWARE_UPGRADE */



/*--------------------------------------------------------------------------*/
/* PREPROCESSOR CONSTANT DEFINITION                                         */
/*--------------------------------------------------------------------------*/

#define u8   unsigned char
#define u16  unsigned short
#define u32  unsigned int
#define s8   signed char
#define s16  signed short
#define s32  signed int


#if defined(CONFIG_ENABLE_CHIP_MSG21XXA) || \
	defined(CONFIG_ENABLE_CHIP_MSG26XXM)
/*0x62 // for MSG21XX/MSG21XXA/MSG26XXM */
#define SLAVE_I2C_ID_DBBUS         (0xC4>>1)
#elif defined(CONFIG_ENABLE_CHIP_MSG22XX)
#define SLAVE_I2C_ID_DBBUS         (0xB2>>1) /*0x59 // for MSG22XX */
#endif
#define SLAVE_I2C_ID_DWI2C         (0x4C>>1) /*0x26  */


#define CHIP_TYPE_MSG21XX   (0x01) /* EX. MSG2133 */
/*
 * EX. MSG2133A/MSG2138A(Besides, use version to distinguish MSG2133A/MSG2138A,
 * you may refer to _DrvFwCtrlUpdateFirmwareCash())
 */
#define CHIP_TYPE_MSG21XXA  (0x02)
#define CHIP_TYPE_MSG26XXM  (0x03) /* EX. MSG2633M */
#define CHIP_TYPE_MSG22XX   (0x7A) /* EX. MSG2238/MSG2256 */

#define PACKET_TYPE_TOOTH_PATTERN	(0x20)
#define PACKET_TYPE_GESTURE_WAKEUP	(0x50)
#define PACKET_TYPE_GESTURE_DEBUG	(0x51)
#define PACKET_TYPE_GESTURE_INFORMATION	(0x52)

#define TOUCH_SCREEN_X_MIN   (0)
#define TOUCH_SCREEN_Y_MIN   (0)
/*
 * Note.
 * Please change the below touch screen resolution according to the touch panel
 * that you are using.
 */
extern u32 TOUCH_SCREEN_X_MAX;
extern u32 TOUCH_SCREEN_Y_MAX;
/*
 * Note.
 * Please do not change the below setting.
 */
#define TPD_WIDTH   (2048)
#define TPD_HEIGHT  (2048)


#define BIT0  (1<<0)
#define BIT1  (1<<1)
#define BIT2  (1<<2)
#define BIT3  (1<<3)
#define BIT4  (1<<4)
#define BIT5  (1<<5)
#define BIT6  (1<<6)
#define BIT7  (1<<7)
#define BIT8  (1<<8)
#define BIT9  (1<<9)
#define BIT10 (1<<10)
#define BIT11 (1<<11)
#define BIT12 (1<<12)
#define BIT13 (1<<13)
#define BIT14 (1<<14)
#define BIT15 (1<<15)


#define MAX_DEBUG_REGISTER_NUM     (10)
/*
 * 130KB. The size shall be large enough for stored any kind firmware size of
 * MSG21XXA(33KB)/MSG22XX(48.5KB)/MSG26XXM(40KB)/MSG28XX(130KB).
 */
#define MAX_UPDATE_FIRMWARE_BUFFER_SIZE    (130)
/*
 * Please change the value depends on the I2C transaction limitation for the
 * platform that you are using.
 */
#define MAX_I2C_TRANSACTION_LENGTH_LIMIT      (250)
/* It is a fixed value and shall not be modified. */
#define MAX_TOUCH_IC_REGISTER_BANK_SIZE       (256)

#define PROCFS_AUTHORITY (0666)

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
#define GESTURE_WAKEUP_MODE_DOUBLE_CLICK_FLAG     0x00000001
#define GESTURE_WAKEUP_MODE_UP_DIRECT_FLAG        0x00000002
#define GESTURE_WAKEUP_MODE_DOWN_DIRECT_FLAG      0x00000004
#define GESTURE_WAKEUP_MODE_LEFT_DIRECT_FLAG      0x00000008
#define GESTURE_WAKEUP_MODE_RIGHT_DIRECT_FLAG     0x00000010
#define GESTURE_WAKEUP_MODE_m_CHARACTER_FLAG      0x00000020
#define GESTURE_WAKEUP_MODE_W_CHARACTER_FLAG      0x00000040
#define GESTURE_WAKEUP_MODE_C_CHARACTER_FLAG      0x00000080
#define GESTURE_WAKEUP_MODE_e_CHARACTER_FLAG      0x00000100
#define GESTURE_WAKEUP_MODE_V_CHARACTER_FLAG      0x00000200
#define GESTURE_WAKEUP_MODE_O_CHARACTER_FLAG      0x00000400
#define GESTURE_WAKEUP_MODE_S_CHARACTER_FLAG      0x00000800
#define GESTURE_WAKEUP_MODE_Z_CHARACTER_FLAG      0x00001000
#define GESTURE_WAKEUP_MODE_RESERVE1_FLAG         0x00002000
#define GESTURE_WAKEUP_MODE_RESERVE2_FLAG         0x00004000
#define GESTURE_WAKEUP_MODE_RESERVE3_FLAG         0X00008000

#ifdef CONFIG_SUPPORT_64_TYPES_GESTURE_WAKEUP_MODE
#define GESTURE_WAKEUP_MODE_RESERVE4_FLAG         0x00010000
#define GESTURE_WAKEUP_MODE_RESERVE5_FLAG         0x00020000
#define GESTURE_WAKEUP_MODE_RESERVE6_FLAG         0x00040000
#define GESTURE_WAKEUP_MODE_RESERVE7_FLAG         0x00080000
#define GESTURE_WAKEUP_MODE_RESERVE8_FLAG         0x00100000
#define GESTURE_WAKEUP_MODE_RESERVE9_FLAG         0x00200000
#define GESTURE_WAKEUP_MODE_RESERVE10_FLAG        0x00400000
#define GESTURE_WAKEUP_MODE_RESERVE11_FLAG        0x00800000
#define GESTURE_WAKEUP_MODE_RESERVE12_FLAG        0x01000000
#define GESTURE_WAKEUP_MODE_RESERVE13_FLAG        0x02000000
#define GESTURE_WAKEUP_MODE_RESERVE14_FLAG        0x04000000
#define GESTURE_WAKEUP_MODE_RESERVE15_FLAG        0x08000000
#define GESTURE_WAKEUP_MODE_RESERVE16_FLAG        0x10000000
#define GESTURE_WAKEUP_MODE_RESERVE17_FLAG        0x20000000
#define GESTURE_WAKEUP_MODE_RESERVE18_FLAG        0x40000000
#define GESTURE_WAKEUP_MODE_RESERVE19_FLAG        0x80000000

#define GESTURE_WAKEUP_MODE_RESERVE20_FLAG        0x00000001
#define GESTURE_WAKEUP_MODE_RESERVE21_FLAG        0x00000002
#define GESTURE_WAKEUP_MODE_RESERVE22_FLAG        0x00000004
#define GESTURE_WAKEUP_MODE_RESERVE23_FLAG        0x00000008
#define GESTURE_WAKEUP_MODE_RESERVE24_FLAG        0x00000010
#define GESTURE_WAKEUP_MODE_RESERVE25_FLAG        0x00000020
#define GESTURE_WAKEUP_MODE_RESERVE26_FLAG        0x00000040
#define GESTURE_WAKEUP_MODE_RESERVE27_FLAG        0x00000080
#define GESTURE_WAKEUP_MODE_RESERVE28_FLAG        0x00000100
#define GESTURE_WAKEUP_MODE_RESERVE29_FLAG        0x00000200
#define GESTURE_WAKEUP_MODE_RESERVE30_FLAG        0x00000400
#define GESTURE_WAKEUP_MODE_RESERVE31_FLAG        0x00000800
#define GESTURE_WAKEUP_MODE_RESERVE32_FLAG        0x00001000
#define GESTURE_WAKEUP_MODE_RESERVE33_FLAG        0x00002000
#define GESTURE_WAKEUP_MODE_RESERVE34_FLAG        0x00004000
#define GESTURE_WAKEUP_MODE_RESERVE35_FLAG        0X00008000
#define GESTURE_WAKEUP_MODE_RESERVE36_FLAG        0x00010000
#define GESTURE_WAKEUP_MODE_RESERVE37_FLAG        0x00020000
#define GESTURE_WAKEUP_MODE_RESERVE38_FLAG        0x00040000
#define GESTURE_WAKEUP_MODE_RESERVE39_FLAG        0x00080000
#define GESTURE_WAKEUP_MODE_RESERVE40_FLAG        0x00100000
#define GESTURE_WAKEUP_MODE_RESERVE41_FLAG        0x00200000
#define GESTURE_WAKEUP_MODE_RESERVE42_FLAG        0x00400000
#define GESTURE_WAKEUP_MODE_RESERVE43_FLAG        0x00800000
#define GESTURE_WAKEUP_MODE_RESERVE44_FLAG        0x01000000
#define GESTURE_WAKEUP_MODE_RESERVE45_FLAG        0x02000000
#define GESTURE_WAKEUP_MODE_RESERVE46_FLAG        0x04000000
#define GESTURE_WAKEUP_MODE_RESERVE47_FLAG        0x08000000
#define GESTURE_WAKEUP_MODE_RESERVE48_FLAG        0x10000000
#define GESTURE_WAKEUP_MODE_RESERVE49_FLAG        0x20000000
#define GESTURE_WAKEUP_MODE_RESERVE50_FLAG        0x40000000
#define GESTURE_WAKEUP_MODE_RESERVE51_FLAG        0x80000000
#endif /*CONFIG_SUPPORT_64_TYPES_GESTURE_WAKEUP_MODE */

#define GESTURE_WAKEUP_PACKET_LENGTH    (6)

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
#define GESTURE_DEBUG_MODE_PACKET_LENGTH	(128)
#endif /*CONFIG_ENABLE_GESTURE_DEBUG_MODE */

#ifdef CONFIG_ENABLE_GESTURE_INFORMATION_MODE
#define GESTURE_WAKEUP_INFORMATION_PACKET_LENGTH	(128)
#endif /*CONFIG_ENABLE_GESTURE_INFORMATION_MODE */

#endif /*CONFIG_ENABLE_GESTURE_WAKEUP */


/*--------------------------------------------------------------------------*/
/* PREPROCESSOR MACRO DEFINITION                                            */
/*--------------------------------------------------------------------------*/

#ifdef CONFIG_ENABLE_TOUCH_DRIVER_DEBUG
#define DBG(fmt, arg...) pr_info(fmt, ##arg)
#define DBG(fmt, arg...) printk(fmt, ##arg)
#else
#define DBG(fmt, arg...)
#endif

/*--------------------------------------------------------------------------*/
/* DATA TYPE DEFINITION                                                     */
/*--------------------------------------------------------------------------*/

enum EmemType_e {
	EMEM_ALL = 0,
	EMEM_MAIN,
	EMEM_INFO
};

enum ItoTestMode_e {
	ITO_TEST_MODE_OPEN_TEST = 1,
	ITO_TEST_MODE_SHORT_TEST = 2
};

enum ItoTestResult_e {
	ITO_TEST_OK = 0,
	ITO_TEST_FAIL,
	ITO_TEST_GET_TP_TYPE_ERROR,
	ITO_TEST_UNDEFINED_ERROR,
	ITO_TEST_UNDER_TESTING

};

enum AddressMode_e {
	ADDRESS_MODE_8BIT = 0,
	ADDRESS_MODE_16BIT = 1
};

/*--------------------------------------------------------------------------*/
/* GLOBAL VARIABLE DEFINITION                                               */
/*--------------------------------------------------------------------------*/

extern struct i2c_client *g_I2cClient;
extern u8 g_ChipType;

#ifdef CONFIG_PM_RUNTIME
extern const struct dev_pm_ops msg_ts_dev_pmops;
#endif

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
extern struct FirmwareInfo_t g_FirmwareInfo;
extern u8 g_LogModePacket[DEBUG_MODE_PACKET_LENGTH];
extern u16 g_FirmwareMode;
extern struct kobject *g_TouchKObj;
extern struct kset *g_TouchKSet;
extern u8 g_IsSwitchModeByAPK;
#endif

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
extern u32 g_GestureWakeupMode[2];
extern u8 g_GestureWakeupFlag;

#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
extern u8 g_LogGestureDebug[128];
extern u8 g_GestureDebugFlag;
extern u8 g_GestureDebugMode;

extern struct kobject *g_GestureKObj;
#endif
#endif

extern struct input_dev *g_InputDevice;

#ifdef CONFIG_ENABLE_ITO_MP_TEST
#if defined(CONFIG_ENABLE_CHIP_MSG26XXM)
extern TestScopeInfo_t g_TestScopeInfo;
#endif /* CONFIG_ENABLE_CHIP_MSG26XXM */
#endif /* CONFIG_ENABLE_ITO_MP_TEST */

#ifdef CONFIG_ENABLE_ITO_MP_TEST
static ItoTestMode_e _gItoTestMode;
#endif /* CONFIG_ENABLE_ITO_MP_TEST */

/*--------------------------------------------------------------------------*/
/* GLOBAL FUNCTION DECLARATION                                              */
/*--------------------------------------------------------------------------*/

extern u8 DrvCommonCalculateCheckSum(u8 *pMsg, u32 nLength);
extern u32 DrvCommonConvertCharToHexDigit(char *pCh, u32 nLength);
extern u32 DrvCommonCrcDoReflect(u32 nRef, s8 nCh);
extern u32 DrvCommonCrcGetValue(u32 nText, u32 nPrevCRC);
extern void DrvCommonCrcInitTable(void);

#endif  /* __MSTAR_DRV_COMMON_H__ */
