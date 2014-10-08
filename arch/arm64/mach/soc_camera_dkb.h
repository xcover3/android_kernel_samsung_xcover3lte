/*
 *  Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */
#ifndef __SOC_CAMERA_DKB_H__
#define __SOC_CAMERA_DKB_H__

#include <media/soc_camera.h>
#include <media/mrvl-camera.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>


#ifdef CONFIG_SOC_CAMERA_S5K8AA
#define S5K8AA_CCIC_PORT 1
extern struct soc_camera_desc soc_camera_desc_0;
#endif

#ifdef CONFIG_SOC_CAMERA_OV5640_ECS
#define OV5640_PWDN_PIN_1L88 80
#define OV5640_RESET_PIN_1L88 67
#define OV5640_PWDN_PIN_1088 80
#define OV5640_RESET_PIN_1088 81
#define OV5640_PWDN_PIN_1U88 70
#define OV5640_RESET_PIN_1U88 69
#define OV5640_CCIC_PORT 0
#define GPIO_TORCH_EN 12 /* For pxa1L88 */
#define GPIO_FLASH_EN 18 /* For pxa1L88 */
extern struct soc_camera_desc soc_camera_desc_1;
#endif

#if defined(CONFIG_SOC_CAMERA_SP0A20_ECS) && !defined(CONFIG_MACH_SOC_CAMERA_L7)
#define SP0A20_PWDN_PIN 68
#define SP0A20_RESET_PIN 69
#define SP0A20_CCIC_PORT 1
extern struct soc_camera_desc soc_camera_desc_2;
#endif

#endif /* End of CONFIG_MACH_SOC_CAMERA_DKB */

/* this camera can support on L7, we recommend AE move this in the future */

#ifdef CONFIG_SOC_CAMERA_SP1628
#define SP1628_PWDN_PIN 68
#define SP1628_RESET_PIN 69
#define SP1628_CCIC_PORT 1
extern struct soc_camera_desc soc_camera_desc_2;
#endif


