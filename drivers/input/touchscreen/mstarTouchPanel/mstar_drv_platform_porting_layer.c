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
 * @file    mstar_drv_platform_porting_layer.c
 *
 * @brief   This file defines the interface of touch screen
 *
 */

#include "mstar_drv_platform_porting_layer.h"
#include "mstar_drv_ic_fw_porting_layer.h"
#include "mstar_drv_platform_interface.h"

struct mutex g_Mutex;
static struct work_struct _gFingerTouchWork;

#ifdef CONFIG_TP_HAVE_KEY
const int g_TpVirtualKey[] = {
	TOUCH_KEY_MENU,
	TOUCH_KEY_HOME,
	TOUCH_KEY_BACK,
	TOUCH_KEY_SEARCH
};

#ifdef CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE
#define BUTTON_W (100)
#define BUTTON_H (100)

const int g_TpVirtualKeyDimLocal[MAX_KEY_NUM][4] = {
	{80, 1300, 135, 100},
	{180, 1300, 135, 100},
	{300, 1300, 135, 100},
	{420, 1300, 135, 100}
};
#ifdef VIRTUAL_KEY
#define VIRT_KEYS(x...)  __stringify(x)
static ssize_t virtual_keys_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	return sprintf(buf,
			VIRT_KEYS(EV_KEY) ":" VIRT_KEYS(KEY_MENU)
			":80:1300:135:100" ":"
			VIRT_KEYS(EV_KEY) ":" VIRT_KEYS(KEY_BACK)
			":300:1300:135:100" ":"
			VIRT_KEYS(EV_KEY) ":" VIRT_KEYS(KEY_HOMEPAGE)
			":180:1300:135:100\n");
}

static struct kobj_attribute virtual_keys_attr = {
	.attr = {
		.name = "virtualkeys.msg2238",
		.mode = S_IRUGO,
	},
	.show = &virtual_keys_show,
};

static struct attribute *props_attrs[] = {
	&virtual_keys_attr.attr,
	NULL
};

static struct attribute_group props_attr_group = {
	.attrs = props_attrs,
};

static int msg_set_virtual_key(struct input_dev *input_dev)
{
	struct kobject *props_kobj;
	int ret = 0;

	props_kobj = kobject_create_and_add("board_properties", NULL);
	if (props_kobj)
		ret = sysfs_create_group(props_kobj, &props_attr_group);
	if (!props_kobj || ret)
		pr_err("failed to create board_properties\n");

	return 0;
}
#endif
#endif /*CONFIG_ENABLE_REPORT_KEY_WITH_COORDINATE */
#endif /*CONFIG_TP_HAVE_KEY */

struct input_dev *g_InputDevice = NULL;
static int _gIrq = -1;

/*
 * read data through I2C then report data to input
 * sub-system when interrupt occurred
 */
static void _DrvPlatformLyrFingerTouchDoWork(struct work_struct *pWork)
{
	DBG("*** %s() ***\n", __func__);

	DrvIcFwLyrHandleFingerTouch(NULL, 0);
	enable_irq(_gIrq);
}

/* The interrupt service routine will be triggered when interrupt occurred */
static irqreturn_t _DrvPlatformLyrFingerTouchInterruptHandler(s32 nIrq,
		void *pDeviceId)
{
	DBG("*** %s() ***\n", __func__);

	disable_irq_nosync(_gIrq);
	schedule_work(&_gFingerTouchWork);

	return IRQ_HANDLED;
}

#ifdef CONFIG_ENABLE_REGULATOR_POWER_ON
void DrvPlatformLyrTouchDeviceRegulatorPowerOn(void)
{
	s32 nRetVal = 0;

	DBG("*** %s() ***\n", __func__);
	/*
	 * For specific SPRD BB chip(ex. SC7715) or QCOM BB chip(ex. MSM8610),
	 * need to enable this function call for correctly power on Touch IC.
	 */
	nRetVal = regulator_set_voltage(g_ReguVdd, 2800000, 2800000);

	if (nRetVal)
		DBG("Could not set to 2800mv.\n");

	if (regulator_enable(g_ReguVdd))
		DBG("Could not regulator_enable.\n");

	mdelay(20);
}

void DrvPlatformLyrTouchDeviceRegulatorPowerOff(void)
{
	DBG("*** %s() ***\n", __func__);

	regulator_disable(g_ReguVdd);
}
#endif /*CONFIG_ENABLE_REGULATOR_POWER_ON */

void DrvPlatformLyrTouchDevicePowerOn(void)
{
	DBG("*** %s() ***\n", __func__);

	gpio_direction_output(MS_TS_MSG_IC_GPIO_RST, 1);
	udelay(100);
	gpio_set_value(MS_TS_MSG_IC_GPIO_RST, 0);
	mdelay(50);
	gpio_set_value(MS_TS_MSG_IC_GPIO_RST, 1);
	mdelay(100);
}

void DrvPlatformLyrTouchDevicePowerOff(void)
{
	DBG("*** %s() ***\n", __func__);

	DrvIcFwLyrOptimizeCurrentConsumption();

	gpio_set_value(MS_TS_MSG_IC_GPIO_RST, 0);
}

void DrvPlatformLyrTouchDeviceResetHw(void)
{
	DBG("*** %s() ***\n", __func__);

	gpio_direction_output(MS_TS_MSG_IC_GPIO_RST, 1);
	gpio_set_value(MS_TS_MSG_IC_GPIO_RST, 0);
	mdelay(100);
	gpio_set_value(MS_TS_MSG_IC_GPIO_RST, 1);
	mdelay(100);
}

void DrvPlatformLyrDisableFingerTouchReport(void)
{
	DBG("*** %s() ***\n", __func__);
	disable_irq(_gIrq);
}

void DrvPlatformLyrEnableFingerTouchReport(void)
{
	DBG("*** %s() ***\n", __func__);
	enable_irq(_gIrq);
}

void DrvPlatformLyrFingerTouchPressed(s32 nX, s32 nY, s32 nPressure, s32 nId)
{
	DBG("*** %s() ***\n", __func__);
	DBG("point touch pressed\n");

	input_report_key(g_InputDevice, BTN_TOUCH, 1);
#if defined(CONFIG_ENABLE_CHIP_MSG26XXM)
	input_report_abs(g_InputDevice, ABS_MT_TRACKING_ID, nId);
#endif /*CONFIG_ENABLE_CHIP_MSG26XXM */
	input_report_abs(g_InputDevice, ABS_MT_TOUCH_MAJOR, 1);
	input_report_abs(g_InputDevice, ABS_MT_WIDTH_MAJOR, 1);
	input_report_abs(g_InputDevice, ABS_MT_POSITION_X, nX);
	input_report_abs(g_InputDevice, ABS_MT_POSITION_Y, nY);

	input_mt_sync(g_InputDevice);

}

void DrvPlatformLyrFingerTouchReleased(s32 nX, s32 nY)
{
	DBG("*** %s() ***\n", __func__);
	DBG("point touch released\n");

	input_report_key(g_InputDevice, BTN_TOUCH, 0);
	input_mt_sync(g_InputDevice);

}

#ifdef CONFIG_OF
void DrvPlatformLyrDts(struct i2c_client *pClient)
{
	struct device_node *np = pClient->dev.of_node;

	of_property_read_u32(np, "mstar,abs-x-max", &TOUCH_SCREEN_X_MAX);
	of_property_read_u32(np, "mstar,abs-y-max", &TOUCH_SCREEN_Y_MAX);
	MS_TS_MSG_IC_GPIO_INT = of_get_named_gpio(np, "mstar,irq-gpios", 0);
	MS_TS_MSG_IC_GPIO_RST = of_get_named_gpio(np, "mstar,reset-gpios", 0);
}
#endif

#ifdef CONFIG_ENABLE_PROXIMITY_DETECTION

static ssize_t firmware_psensor_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	DBG("%s\n", __func__);
	if (g_EnableTpProximity)
		return sprintf(buf, "on\n");
	else
		return sprintf(buf, "off\n");
}

static ssize_t firmware_psensor_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	DBG("%s buf = %s g_EnableTpProximity = %d\n",
			__func__, buf, g_EnableTpProximity);
	if (*buf == '0') {
		in_call = 0;
		DrvPlatformLyrTpPsEnable(0);
	} else {
		in_call = 1;
		DrvPlatformLyrTpPsEnable(1);
	}

	return size;
}

static DEVICE_ATTR(active, 0777, firmware_psensor_show, firmware_psensor_store);

#endif /*CONFIG_ENABLE_PROXIMITY_DETECTION */
s32 DrvPlatformLyrInputDeviceInitialize(struct i2c_client *pClient)
{
	s32 nRetVal = 0;

	DBG("*** %s() ***\n", __func__);

	mutex_init(&g_Mutex);

	/* allocate an input device */
	g_InputDevice = input_allocate_device();
	if (g_InputDevice == NULL) {
		DBG("*** input device allocation failed ***\n");
		return -ENOMEM;
	}

	g_InputDevice->name = pClient->name;
	g_InputDevice->phys = "I2C";
	g_InputDevice->dev.parent = &pClient->dev;
	g_InputDevice->id.bustype = BUS_I2C;

	/* set the supported event type for input device */
	set_bit(EV_ABS, g_InputDevice->evbit);
	set_bit(EV_SYN, g_InputDevice->evbit);
	set_bit(EV_KEY, g_InputDevice->evbit);
	set_bit(BTN_TOUCH, g_InputDevice->keybit);
	set_bit(INPUT_PROP_DIRECT, g_InputDevice->propbit);

#ifdef CONFIG_TP_HAVE_KEY
	{
		u32 i;
		for (i = 0; i < MAX_KEY_NUM; i++)
			input_set_capability(g_InputDevice, EV_KEY,
					g_TpVirtualKey[i]);
	}
#endif

#ifdef CONFIG_ENABLE_GESTURE_WAKEUP
	input_set_capability(g_InputDevice, EV_KEY, KEY_POWER);
	input_set_capability(g_InputDevice, EV_KEY, KEY_UP);
	input_set_capability(g_InputDevice, EV_KEY, KEY_DOWN);
	input_set_capability(g_InputDevice, EV_KEY, KEY_LEFT);
	input_set_capability(g_InputDevice, EV_KEY, KEY_RIGHT);
	input_set_capability(g_InputDevice, EV_KEY, KEY_W);
	input_set_capability(g_InputDevice, EV_KEY, KEY_Z);
	input_set_capability(g_InputDevice, EV_KEY, KEY_V);
	input_set_capability(g_InputDevice, EV_KEY, KEY_O);
	input_set_capability(g_InputDevice, EV_KEY, KEY_M);
	input_set_capability(g_InputDevice, EV_KEY, KEY_C);
	input_set_capability(g_InputDevice, EV_KEY, KEY_E);
	input_set_capability(g_InputDevice, EV_KEY, KEY_S);
#endif /*CONFIG_ENABLE_GESTURE_WAKEUP */

#if defined(CONFIG_ENABLE_CHIP_MSG26XXM)
	input_set_abs_params(g_InputDevice, ABS_MT_TRACKING_ID, 0, 255, 0, 0);
#endif /*CONFIG_ENABLE_CHIP_MSG26XXM */
	input_set_abs_params(g_InputDevice, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(g_InputDevice, ABS_MT_WIDTH_MAJOR, 0, 15, 0, 0);
	input_set_abs_params(g_InputDevice, ABS_MT_POSITION_X,
			TOUCH_SCREEN_X_MIN, TOUCH_SCREEN_X_MAX, 0, 0);
	input_set_abs_params(g_InputDevice, ABS_MT_POSITION_Y,
			TOUCH_SCREEN_Y_MIN, TOUCH_SCREEN_Y_MAX, 0, 0);

	/* register the input device to input sub-system */
	nRetVal = input_register_device(g_InputDevice);
	if (nRetVal < 0)
		DBG("*** Unable to register touch input device ***\n");
#ifdef VIRTUAL_KEY
	msg_set_virtual_key(NULL);
#endif

	return nRetVal;
}

s32 DrvPlatformLyrTouchDeviceRequestGPIO(void)
{
	s32 nRetVal = 0;

	DBG("*** %s() ***\n", __func__);

	nRetVal = gpio_request(MS_TS_MSG_IC_GPIO_RST, "ts_rst_pin");
	if (nRetVal < 0)
		DBG("*** Failed to request GPIO %d, error %d ***\n",
				MS_TS_MSG_IC_GPIO_RST, nRetVal);

	nRetVal = gpio_request(MS_TS_MSG_IC_GPIO_INT, "ts_irq_pin");

	if (nRetVal < 0)
		DBG("*** Failed to request GPIO %d, error %d ***\n",
				MS_TS_MSG_IC_GPIO_INT, nRetVal);

	return nRetVal;
}

s32 DrvPlatformLyrTouchDeviceRegisterFingerTouchInterruptHandler(void)
{
	s32 nRetVal = 0;

	DBG("*** %s() ***\n", __func__);

	if (DrvIcFwLyrIsRegisterFingerTouchInterruptHandler()) {
		/* initialize the finger touch work queue */
		INIT_WORK(&_gFingerTouchWork, _DrvPlatformLyrFingerTouchDoWork);

		_gIrq = gpio_to_irq(MS_TS_MSG_IC_GPIO_INT);

		/* request an irq and register the isr */
		nRetVal = request_irq(_gIrq /*MS_TS_MSG_IC_GPIO_INT*/,
				_DrvPlatformLyrFingerTouchInterruptHandler,
				IRQF_TRIGGER_RISING,
				/* | IRQF_NO_SUSPEND */
				/* IRQF_TRIGGER_FALLING */
				"msg2xxx", NULL);

		if (nRetVal != 0)
			DBG("*** Unable to claim irq %d; error %d ***\n",
					MS_TS_MSG_IC_GPIO_INT, nRetVal);
	}

	return nRetVal;
}

void DrvPlatformLyrTouchDeviceRegisterEarlySuspend(void)
{
	DBG("*** %s() ***\n", __func__);

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&g_I2cClient->dev);
	pm_runtime_forbid(&g_I2cClient->dev);
#endif

}

/*
 * remove function is triggered when the input device
 * is removed from input sub-system
 */
s32 DrvPlatformLyrTouchDeviceRemove(struct i2c_client *pClient)
{
	DBG("*** %s() ***\n", __func__);

	free_irq(_gIrq, g_InputDevice);
	gpio_free(MS_TS_MSG_IC_GPIO_INT);
	gpio_free(MS_TS_MSG_IC_GPIO_RST);
	input_unregister_device(g_InputDevice);
#ifdef CONFIG_ENABLE_FIRMWARE_DATA_LOG
	kset_unregister(g_TouchKSet);
	kobject_put(g_TouchKObj);
#endif /*CONFIG_ENABLE_FIRMWARE_DATA_LOG */
#ifdef CONFIG_PM_RUNTIME
	pm_runtime_disable(&pClient->dev);
#endif

	return 0;
}

void DrvPlatformLyrSetIicDataRate(struct i2c_client *pClient, u32 nIicDataRate)
{
	DBG("*** %s() nIicDataRate = %d ***\n", __func__, nIicDataRate);
}
