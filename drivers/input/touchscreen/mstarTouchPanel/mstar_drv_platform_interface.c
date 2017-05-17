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
 * @file    mstar_drv_platform_interface.c
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#include "mstar_drv_platform_interface.h"
#include "mstar_drv_main.h"
#include "mstar_drv_ic_fw_porting_layer.h"
#include "mstar_drv_platform_porting_layer.h"

#ifdef	CONFIG_HIPAD_FIRMWARE_UPGRADE
#include "hipad_firmware_upgrade.h"
#endif

#ifdef CONFIG_PM_RUNTIME
static u8 bSuspendRuned;
static int msg_ts_suspend(struct device *dev)
{
	DBG("*** %s() ***\n", __func__);

	DBG("suspend bSuspendRuned=%d\n", bSuspendRuned);
	bSuspendRuned = 1;

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	if (g_GestureWakeupMode[0] != 0x00000000 ||
			g_GestureWakeupMode[1] != 0x00000000) {
		DrvIcFwLyrOpenGestureWakeup(&g_GestureWakeupMode[0]);
		return 0;
	}
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */

	/* Send touch end for clearing point touch */
	DrvPlatformLyrFingerTouchReleased(0, 0);
	input_sync(g_InputDevice);

	DrvPlatformLyrDisableFingerTouchReport();
	DrvPlatformLyrTouchDevicePowerOff();
#ifdef CONFIG_ENABLE_REGULATOR_POWER_ON
	DrvPlatformLyrTouchDeviceRegulatorPowerOff();
#endif
	return 0;
}

static int msg_ts_resume(struct device *dev)
{
	DBG("*** %s() ***\n", __func__);

	DBG("resume bSuspendRuned=%d\n", bSuspendRuned);
	if (bSuspendRuned == 0)
		return 0;
	bSuspendRuned = 0;

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
#ifdef CONFIG_ENABLE_GESTURE_DEBUG_MODE
	if (g_GestureDebugMode == 1)
		DrvIcFwLyrCloseGestureDebugMode();
#endif /* CONFIG_ENABLE_GESTURE_DEBUG_MODE */

	if (g_GestureWakeupFlag == 1)
		DrvIcFwLyrCloseGestureWakeup();
	else
		DrvPlatformLyrEnableFingerTouchReport();
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */
#ifdef CONFIG_ENABLE_REGULATOR_POWER_ON
	DrvPlatformLyrTouchDeviceRegulatorPowerOn();
#endif
	DrvPlatformLyrTouchDevicePowerOn();

#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
	/*
	 * Mark this function call for avoiding device driver
	 * may spend longer time to resume from suspend state.
	 */
	DrvIcFwLyrRestoreFirmwareModeToLogDataMode();
#endif /* CONFIG_ENABLE_FIRMWARE_DATA_LOG */

#ifndef CONFIG_ENABLE_GESTURE_WAKEUP
	DrvPlatformLyrEnableFingerTouchReport();
#endif /* CONFIG_ENABLE_GESTURE_WAKEUP */
	return 0;
}

const struct dev_pm_ops msg_ts_dev_pmops = {
	SET_RUNTIME_PM_OPS(msg_ts_suspend, msg_ts_resume, NULL)
};
#endif

#ifdef CONFIG_HIPAD_FIRMWARE_UPGRADE
struct firmware_miscdevice mdev;
static int msg2238_get_firmware_id(struct firmware_miscdevice *mdev,
		char *buff, size_t size)
{
	u16 major = 0, minor = 0;

	msg_get_firmware_id(&major, &minor);

	fw_mdev_debug("msg2238_get_firmware_id: major = %d, minor = %d\n",
			major, minor);

	return snprintf(buff, size, "%03d.%03d\n", major, minor);
}

static int msg2238_create_fw_mdev(struct firmware_miscdevice *mdev)
{
	int err;

	mdev->name = "HUA-MSG22XX";
	mdev->dev.name = "HUA-MSG22XX";

	mdev->firmware_upgrade = msg_firmware_upgrade;
	mdev->get_firmware_id = msg2238_get_firmware_id;
	mdev->read_data = fw_mdev_read_data;
	mdev->write_data = fw_mdev_write_data;
	err = fw_mdev_register(mdev);
	if (err)
		fw_mdev_err("fw_mdev_register failre\n");

	set_client_to_mdev(mdev, g_I2cClient);

	return err;
}

static void msg2238_destroy_fw_mdev(struct firmware_miscdevice *mdev)
{
	fw_mdev_unregister(mdev);
}
#endif


/* probe function is used for matching and initializing input device */
s32 /*__devinit*/ MsDrvInterfaceTouchDeviceProbe(struct i2c_client *pClient,
		const struct i2c_device_id *pDeviceId)
{
	s32 nRetVal = 0;

	DBG("*** %s() ***\n", __func__);

#ifdef CONFIG_OF
	DrvPlatformLyrDts(pClient);
#endif

	DrvPlatformLyrInputDeviceInitialize(pClient);

	DrvPlatformLyrTouchDeviceRequestGPIO();

#ifdef CONFIG_ENABLE_REGULATOR_POWER_ON
	DrvPlatformLyrTouchDeviceRegulatorPowerOn();
#endif /* CONFIG_ENABLE_REGULATOR_POWER_ON */

	DrvPlatformLyrTouchDevicePowerOn();

	nRetVal = DrvMainTouchDeviceInitialize();
	if (nRetVal == -ENODEV) {
		DrvPlatformLyrTouchDeviceRemove(pClient);
		return nRetVal;
	}

	DrvPlatformLyrTouchDeviceRegisterFingerTouchInterruptHandler();

	DrvPlatformLyrTouchDeviceRegisterEarlySuspend();

#ifdef CONFIG_HIPAD_FIRMWARE_UPGRADE
	nRetVal = msg2238_create_fw_mdev(&mdev);
	if (nRetVal)
		fw_mdev_err("msg2238_create_fw_mdev ERROR = %d\n", nRetVal);
#endif

	DBG("*** MStar touch driver registered ***\n");

	return nRetVal;
}

/*
 * remove function is triggered when the input
 * device is removed from input sub-system
 */
s32 /*__devexit*/ MsDrvInterfaceTouchDeviceRemove(struct i2c_client *pClient)
{
	DBG("*** %s() ***\n", __func__);
#ifdef CONFIG_HIPAD_FIRMWARE_UPGRADE
	msg2238_destroy_fw_mdev(&mdev);
#endif

	return DrvPlatformLyrTouchDeviceRemove(pClient);
}

void MsDrvInterfaceTouchDeviceSetIicDataRate(struct i2c_client *pClient,
		u32 nIicDataRate)
{
	DBG("*** %s() ***\n", __func__);

	DrvPlatformLyrSetIicDataRate(pClient, nIicDataRate);
}
