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
 * @file    mstar_drv_marvell.c
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

/*
 * adair mstar_drv_sprd.c --- mstar_drv_platform_interface.c
 * --- mstar_drv_platform_porting_layer.c
 * ---mstar_drv_ic_fw_porting_layer.c
 * ---mstar_drv_self_fw_control.c
 */

#include "mstar_drv_platform_interface.h"


#define MSG_TP_IC_NAME "msg2xxx"
/*
 * "msg21xxA" or "msg22xx" or "msg26xxM"
 * Please define the mstar touch ic name based on
 * the mutual-capacitive ic or self capacitive ic
 * that you are using
 */

struct i2c_client *g_I2cClient = NULL;

#ifdef CONFIG_ENABLE_REGULATOR_POWER_ON
struct regulator *g_ReguVdd = NULL;
#endif /* CONFIG_ENABLE_REGULATOR_POWER_ON */

/* probe function is used for matching and initializing input device */
static int /*__devinit*/ touch_driver_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{

	DBG("*** %s ***\n", __func__);

	if (client == NULL) {
		DBG("i2c client is NULL\n");
		return -1;
	}
	g_I2cClient = client;

#ifdef CONFIG_ENABLE_REGULATOR_POWER_ON
	g_ReguVdd = regulator_get(&g_I2cClient->dev, "mstar,v_tsp");
#endif /* CONFIG_ENABLE_REGULATOR_POWER_ON */

	return MsDrvInterfaceTouchDeviceProbe(g_I2cClient, id);
}

/*
 * remove function is triggered when the input
 * device is removed from input sub-system
 */
static int /*__devexit*/ touch_driver_remove(struct i2c_client *client)
{
	DBG("*** %s ***\n", __func__);

	return MsDrvInterfaceTouchDeviceRemove(client);
}

/* The I2C device list is used for matching I2C device and I2C device driver. */
static const struct i2c_device_id touch_device_id[] = {
	{MSG_TP_IC_NAME, 0}, /* SLAVE_I2C_ID_DWI2C */
	{}, /* should not omitted */
};

MODULE_DEVICE_TABLE(i2c, touch_device_id);

#ifdef CONFIG_OF
static const struct of_device_id msg_dts[] = {
	{ .compatible = "mstar,msg2238", },
	{},
};
MODULE_DEVICE_TABLE(of, msg_dts);
#endif

static struct i2c_driver touch_device_driver = {
	.driver = {
		.name = MSG_TP_IC_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table	= of_match_ptr(msg_dts),
#endif
#ifdef CONFIG_PM_RUNTIME
		.pm = &msg_ts_dev_pmops,
#endif
	},
	.probe = touch_driver_probe,
	.remove = touch_driver_remove,
	/*    .remove = __devexit_p(touch_driver_remove), */
	.id_table = touch_device_id,
};

static int __init touch_driver_init(void)
{
	int ret;

	/* register driver */
	ret = i2c_add_driver(&touch_device_driver);
	if (ret < 0) {
		DBG("add touch device driver i2c driver failed.\n");
		return -ENODEV;
	}
	DBG("add touch device driver i2c driver.\n");

	return ret;
}

static void __exit touch_driver_exit(void)
{
	DBG("remove touch device driver i2c driver.\n");

	i2c_del_driver(&touch_device_driver);
}

module_init(touch_driver_init);
module_exit(touch_driver_exit);

MODULE_AUTHOR("ZiLi.Chen <chenzili@hipad.com>");
MODULE_DESCRIPTION("Huamobile MSG22XX TouchScreen Driver");
MODULE_LICENSE("GPL");
