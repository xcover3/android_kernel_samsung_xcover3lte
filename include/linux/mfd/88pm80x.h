/*
 * Marvell 88PM80x Interface
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Qiao Zhou <zhouqiao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM80X_H
#define __LINUX_MFD_88PM80X_H

#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/regmap.h>
#include <linux/atomic.h>
#include <linux/switch.h>

enum {
	CHIP_INVALID = 0,
	CHIP_PM800,
	CHIP_PM805,
	CHIP_PM822,
	CHIP_PM86X,
	CHIP_MAX,
};

enum {
	PM800_ID_BUCK1 = 0,
	PM800_ID_BUCK1A = 0,
	PM800_ID_BUCK2,
	PM800_ID_BUCK3,
	PM800_ID_BUCK4,
	PM800_ID_BUCK5,
	PM800_ID_BUCK6,
	PM800_ID_BUCK1B,

	PM800_ID_LDO1 = 7,
	PM800_ID_LDO2,
	PM800_ID_LDO3,
	PM800_ID_LDO4,
	PM800_ID_LDO5,
	PM800_ID_LDO6,
	PM800_ID_LDO7,
	PM800_ID_LDO8,
	PM800_ID_LDO9,
	PM800_ID_LDO10,
	PM800_ID_LDO11,
	PM800_ID_LDO12,
	PM800_ID_LDO13,
	PM800_ID_LDO14 = 20,
	PM800_ID_VOUTSW = 21,
	PM800_ID_LDO15 = 21,
	PM800_ID_LDO16,
	PM800_ID_LDO17,
	PM800_ID_LDO18,
	PM800_ID_LDO19,
	PM800_ID_LDO20,

	PM800_ID_RG_MAX = 24,
	PM822_ID_RG_MAX = 20,
	PM86X_ID_RG_MAX = 27,
};
#define PM800_MAX_REGULATOR	PM800_ID_RG_MAX	/* 5 Bucks, 19 LDOs */
#define PM800_NUM_BUCK (5)	/*5 Bucks */
#define PM800_NUM_LDO (19)	/*19 Bucks */

/* page 0 basic: slave adder 0x60 */

#define PM800_STATUS_1			(0x01)
#define PM800_ONKEY_STS1		(1 << 0)
#define PM800_EXTON_STS1		(1 << 1)
#define PM800_CHG_STS1			(1 << 2)
#define PM800_BAT_STS1			(1 << 3)
#define PM800_VBUS_STS1			(1 << 4)
#define PM800_LDO_PGOOD_STS1	(1 << 5)
#define PM800_BUCK_PGOOD_STS1	(1 << 6)

#define PM800_STATUS_2			(0x02)
#define PM800_RTC_ALARM_STS2	(1 << 0)

/* Wakeup Registers */
#define PM800_WAKEUP1		(0x0D)

#define PM800_WAKEUP2		(0x0E)
#define PM800_WAKEUP2_INV_INT		(1 << 0)
#define PM800_WAKEUP2_INT_CLEAR		(1 << 1)
#define PM800_WAKEUP2_INT_RC		(0 << 1)
#define PM800_WAKEUP2_INT_WC		(1 << 1)
#define PM800_WAKEUP2_INT_MASK		(1 << 2)

#define PM800_POWER_UP_LOG	(0x10)

/* Referance and low power registers */
#define PM800_LOW_POWER1		(0x20)
#define PM800_LOW_POWER2		(0x21)
#define PM800_LOW_POWER_CONFIG3	(0x22)
#define PM800_LOW_POWER_CONFIG4	(0x23)

/* GPIO register */
#define PM800_GPIO_0_1_CNTRL		(0x30)
#define PM800_GPIO0_VAL				(1 << 0)
#define PM800_GPIO0_GPIO_MODE(x)	(x << 1)
#define PM800_GPIO1_VAL				(1 << 4)
#define PM800_GPIO1_GPIO_MODE(x)	(x << 5)

#define PM800_GPIO_2_3_CNTRL		(0x31)
#define PM800_GPIO2_VAL				(1 << 0)
#define PM800_GPIO2_GPIO_MODE(x)	(x << 1)
#define PM800_GPIO3_VAL				(1 << 4)
#define PM800_GPIO3_GPIO_MODE(x)	(x << 5)
#define PM800_GPIO3_MODE_MASK		0x1F
#define PM800_GPIO3_HEADSET_MODE	PM800_GPIO3_GPIO_MODE(6)

#define PM800_GPIO_4_CNTRL			(0x32)
#define PM800_GPIO4_VAL				(1 << 0)
#define PM800_GPIO4_GPIO_MODE(x)	(x << 1)

#define PM800_HEADSET_CNTRL		(0x38)
#define PM800_HEADSET_DET_EN		(1 << 7)
#define PM800_HSDET_SLP			(1 << 1)
/* PWM register */
#define PM800_PWM1		(0x40)
#define PM800_PWM2		(0x41)
#define PM800_PWM3		(0x42)
#define PM800_PWM4		(0x43)

/* Oscillator control */
#define PM800_OSC_CNTRL1		(0x50)
#define PM800_OSC_CNTRL2		(0x51)
#define PM800_OSC_CNTRL3		(0x52)
#define PM800_OSC_CNTRL4		(0x53)
#define PM800_OSC_CNTRL5		(0x54)
#define PM800_OSC_CNTRL6		(0x55)
#define PM800_OSC_CNTRL7		(0x56)
#define PM800_OSC_CNTRL8		(0x57)
#define PM800_OSC_CNTRL9		(0x58)
#define PM800_OSC_CNTRL11		(0x5a)
#define PM800_OSC_CNTRL12		(0x5b)
#define PM800_OSC_CNTRL13		(0x5c)

/* RTC Registers */
#define PM800_RTC_CONTROL		(0xD0)
#define PM800_RTC_MISC1			(0xE1)
#define PM800_RTC_MISC2			(0xE2)
#define PM800_RTC_MISC3			(0xE3)
#define PM800_RTC_MISC4			(0xE4)
#define PM800_RTC_MISC5			(0xE7)
/* bit definitions of RTC Register 1 (0xD0) */
#define PM800_ALARM1_EN			(1 << 0)
#define PM800_ALARM_WAKEUP		(1 << 4)
#define PM800_ALARM			(1 << 5)
#define PM800_RTC1_USE_XO		(1 << 7)

/* Regulator Control Registers: BUCK1,BUCK5,LDO1 have DVC */

/* buck registers */
#define PM800_BUCK1_SLEEP	(0x30)

#define PM800_AUDIO_MODE_CONFIG1	(0x38)
#define PM800_BUCK3		(0x41)
#define PM800_BUCK4		(0x42)
#define PM800_BUCK4_1		(0x43)
#define PM800_BUCK4_2		(0x44)
#define PM800_BUCK4_3		(0x45)
#define PM800_BUCK5		(0x46)

/* for 88pm860 */
#define PM800_BUCK1A		(0x3C)
#define PM800_BUCK1A_1		(0x3D)
#define PM800_BUCK1A_2		(0x3E)
#define PM800_BUCK1A_3		(0x3F)

#define PM800_BUCK6		(0x4A)
#define PM800_BUCK1B		(0x4B)
#define PM800_BUCK1B_1		(0x4C)
#define PM800_BUCK1B_2		(0x4D)
#define PM800_BUCK1B_3		(0x4E)
#define PM800_BUCK_EN		(0x50)
#define PM800_LDO1_8_EN1		(0x51)
#define PM800_LDO9_16_EN1		(0x52)
#define PM800_LDO17_19_EN1		(0x53)

#define PM800_BUCK1_MISC	(0x8E)

/* BUCK Sleep Mode Register 1: BUCK[1..4] */
#define PM800_BUCK_SLP1		(0x5A)
#define PM800_BUCK1_SLP1_SHIFT	0
#define PM800_BUCK1_SLP1_MASK	(0x3 << PM800_BUCK1_SLP1_SHIFT)
#define PM800_BUCK2_SLP1_SHIFT	2
#define PM800_BUCK2_SLP1_MASK	(0x3 << PM800_BUCK2_SLP1_SHIFT)
#define PM800_BUCK_SLP2		(0x5B)

#define PM800_BUCK_SLP_PWR_OFF	0x0	/* power off */
#define PM800_BUCK_SLP_PWR_ACT1	0x1	/* auto & VBUCK_SET_SLP */
#define PM800_BUCK_SLP_PWR_LOW	0x2	/* VBUCK_SET_SLP */
#define PM800_BUCK_SLP_PWR_ACT2	0x3	/* auto & VBUCK_SET */

/* BUCK Sleep Mode Register 2: BUCK5 */
#define PM800_BUCK_SLP2		(0x5B)
#define PM800_BUCK5_SLP2_SHIFT	0
#define PM800_BUCK5_SLP2_MASK	(0x3 << PM800_BUCK5_SLP2_SHIFT)

#define PM800_LDO1_1		(0x08)
#define PM800_LDO1_2		(0x09)
#define PM800_LDO1_3		(0x0a)
#define PM800_LDO2		(0x0b)
#define PM800_LDO3		(0x0c)
#define PM800_LDO4		(0x0d)
#define PM800_LDO5		(0x0e)
#define PM800_LDO6		(0x0f)
#define PM800_LDO7		(0x10)
#define PM800_LDO8		(0x11)
#define PM800_LDO9		(0x12)
#define PM800_LDO10		(0x13)
#define PM800_LDO11		(0x14)
#define PM800_LDO12		(0x15)
#define PM800_LDO13		(0x16)
#define PM800_LDO14		(0x17)
#define PM800_LDO15		(0x18)
#define PM800_LDO16		(0x19)
#define PM800_LDO17		(0x1a)
#define PM800_LDO18		(0x1b)
#define PM800_LDO19		(0x1c)

#define PM800_AUDIO_MODE	(0x38)

/*88pm822 registers*/
#define PM822_BUCK1		(0x3C)
#define PM822_BUCK2		(0x40)
#define PM822_BUCK3		(0x41)
#define PM822_BUCK3_1		(0x42)
#define PM822_BUCK3_2		(0x43)
#define PM822_BUCK3_3		(0x44)
#define PM822_BUCK4		(0x45)
#define PM822_BUCK5		(0x46)

#define PM822_NOUSE		(0xFF)
#define PM822_LDO2_AUDIO	(0x0A)
#define PM822_VOUTSW		(0xFF)	/* fake register */


/* BUCK Sleep Mode Register 1: BUCK[1..4] */
#define PM800_BUCK_SLP1		(0x5A)
#define PM800_BUCK1_SLP1_SHIFT	0
#define PM800_BUCK1_SLP1_MASK	(0x3 << PM800_BUCK1_SLP1_SHIFT)

/* page 2 GPADC: slave adder 0x02 */
#define PM800_GPADC_MEAS_EN1		(0x01)
#define PM800_MEAS_EN1_VBAT         (1 << 2)
#define PM800_GPADC_MEAS_EN2		(0x02)
#define PM800_MEAS_EN2_RFTMP        (1 << 0)
#define PM800_MEAS_GP0_EN			(1 << 2)
#define PM800_MEAS_GP1_EN			(1 << 3)
#define PM800_MEAS_GP2_EN			(1 << 4)
#define PM800_MEAS_GP3_EN			(1 << 5)
#define PM800_MEAS_GP4_EN			(1 << 6)

#define PM800_GPADC_MISC_CONFIG1	(0x05)
#define PM800_GPADC_MISC_CONFIG2	(0x06)
#define PM800_GPADC_MISC_GPFSM_EN	(1 << 0)
#define PM800_GPADC_SLOW_MODE(x)	(x << 3)

#define PM800_GPADC_MISC_CONFIG3		(0x09)
#define PM800_GPADC_MISC_CONFIG4		(0x0A)
#define PM800_GPADC_BIAS1			(0x0B)
#define PM800_GPADC_BIAS2			(0x0C)
#define PM800_GPADC_BIAS3			(0x0D)
#define PM800_GPADC_BIAS4			(0x0E)

#define GP_BIAS_SET(x)				(x << 0)
#define GP_PREBIAS(x)				(x << 4)

#define PM800_GPADC_PREBIAS1			(0x0F)
#define PM800_GPADC0_GP_PREBIAS_TIME(x)	(x << 0)
#define PM800_GPADC_PREBIAS2			(0x10)

#define PM800_GP_BIAS_ENA1				(0x14)
#define PM800_GPADC_GP_BIAS_EN0			(1 << 0)
#define PM800_GPADC_GP_BIAS_EN1			(1 << 1)
#define PM800_GPADC_GP_BIAS_EN2			(1 << 2)
#define PM800_GPADC_GP_BIAS_EN3			(1 << 3)

#define PM800_GP_BIAS_OUT1		(0x15)
#define PM800_BIAS_OUT_GP0		(1 << 0)
#define PM800_BIAS_OUT_GP1		(1 << 1)
#define PM800_BIAS_OUT_GP2		(1 << 2)
#define PM800_BIAS_OUT_GP3		(1 << 3)

#define PM800_GPADC0_LOW_TH		0x20
#define PM800_GPADC1_LOW_TH		0x21
#define PM800_GPADC2_LOW_TH		0x22
#define PM800_GPADC3_LOW_TH		0x23
#define PM800_GPADC4_LOW_TH		0x24

#define PM800_GPADC0_UPP_TH		0x30
#define PM800_GPADC1_UPP_TH		0x31
#define PM800_GPADC2_UPP_TH		0x32
#define PM800_GPADC3_UPP_TH		0x33
#define PM800_GPADC4_UPP_TH		0x34

#define PM800_VBBAT_MEAS1		0x40
#define PM800_VBBAT_MEAS2		0x41
#define PM800_VBAT_MEAS1		0x42
#define PM800_VBAT_MEAS2		0x43
#define PM800_VSYS_MEAS1		0x44
#define PM800_VSYS_MEAS2		0x45
#define PM800_VCHG_MEAS1		0x46
#define PM800_VCHG_MEAS2		0x47
#define PM800_TINT_MEAS1		0x50
#define PM800_TINT_MEAS2		0x51
#define PM800_PMOD_MEAS1		0x52
#define PM800_PMOD_MEAS2		0x53

/* for 88pm822 */
#define PM800_MISC_EN1			0x54

#define PM800_GPADC0_MEAS1		0x54
#define PM800_GPADC0_MEAS2		0x55
#define PM800_GPADC1_MEAS1		0x56
#define PM800_GPADC1_MEAS2		0x57
#define PM800_GPADC2_MEAS1		0x58
#define PM800_GPADC2_MEAS2		0x59
#define PM800_GPADC3_MEAS1		0x5A
#define PM800_GPADC3_MEAS2		0x5B
#define PM800_GPADC4_MEAS1		0x5C
#define PM800_GPADC4_MEAS2		0x5D

#define PM800_GPADC4_AVG1		0xA8
#define PM800_GPADC4_AVG2		0xA9

/* 88PM805 Registers */
#define PM805_MAIN_POWERUP		(0x01)
#define PM805_INT_STATUS0		(0x02)	/* for ena/dis all interrupts */

#define PM805_STATUS0_INT_CLEAR		(1 << 0)
#define PM805_STATUS0_INT_RC		(0 << 0)
#define PM805_STATUS0_INT_WC		(1 << 0)
#define PM805_STATUS0_INV_INT		(1 << 1)
#define PM800_STATUS0_INT_MASK		(1 << 2)

#define PM805_INT_STATUS1		(0x03)

#define PM805_INT1_HP1_SHRT		(1 << 0)
#define PM805_INT1_HP2_SHRT		(1 << 1)
#define PM805_INT1_MIC_CONFLICT		(1 << 2)
#define PM805_INT1_CLIP_FAULT		(1 << 3)
#define PM805_INT1_LDO_OFF			(1 << 4)
#define PM805_INT1_SRC_DPLL_LOCK	(1 << 5)

#define PM805_INT_STATUS2		(0x04)

#define PM805_INT2_MIC_DET			(1 << 0)
#define PM805_INT2_SHRT_BTN_DET		(1 << 1)
#define PM805_INT2_VOLM_BTN_DET		(1 << 2)
#define PM805_INT2_VOLP_BTN_DET		(1 << 3)
#define PM805_INT2_RAW_PLL_FAULT	(1 << 4)
#define PM805_INT2_FINE_PLL_FAULT	(1 << 5)

#define PM805_INT_MASK1			(0x05)
#define PM805_INT_MASK2			(0x06)
#define PM805_SHRT_BTN_DET		(1 << 1)

/* number of status and int reg in a row */
#define PM805_INT_REG_NUM		(2)

#define PM805_MIC_DET1			(0x07)
#define PM805_MIC_DET_EN_MIC_DET (1 << 0)
#define PM805_MIC_DET2			(0x08)
#define PM805_MIC_DET_STATUS1	(0x09)

#define PM805_MIC_DET_STATUS3	(0x0A)
#define PM805_AUTO_SEQ_STATUS1	(0x0B)
#define PM805_AUTO_SEQ_STATUS2	(0x0C)

#define PM805_ADC_SETTING1		(0x10)
#define PM805_ADC_SETTING2		(0x11)
#define PM805_ADC_SETTING3		(0x11)
#define PM805_ADC_GAIN1			(0x12)
#define PM805_ADC_GAIN2			(0x13)
#define PM805_DMIC_SETTING		(0x15)
#define PM805_DWS_SETTING		(0x16)
#define PM805_MIC_CONFLICT_STS	(0x17)

#define PM805_PDM_SETTING1		(0x20)
#define PM805_PDM_SETTING2		(0x21)
#define PM805_PDM_SETTING3		(0x22)
#define PM805_PDM_CONTROL1		(0x23)
#define PM805_PDM_CONTROL2		(0x24)
#define PM805_PDM_CONTROL3		(0x25)

#define PM805_HEADPHONE_SETTING			(0x26)
#define PM805_HEADPHONE_GAIN_A2A		(0x27)
#define PM805_HEADPHONE_SHORT_STATE		(0x28)
#define PM805_EARPHONE_SETTING			(0x29)
#define PM805_AUTO_SEQ_SETTING			(0x2A)

struct pm80x_rtc_pdata {
	int		vrtc;
	int		rtc_wakeup;
};

struct pm80x_subchip {
	struct i2c_client *power_page;	/* chip client for power page */
	struct i2c_client *gpadc_page;	/* chip client for gpadc page */
	struct regmap *regmap_power;
	struct regmap *regmap_gpadc;
	unsigned short power_page_addr;	/* power page I2C address */
	unsigned short gpadc_page_addr;	/* gpadc page I2C address */
};

struct pm80x_chip {
	struct pm80x_subchip *subchip;
	struct device *dev;
	struct i2c_client *client;
	struct i2c_client *companion;
	struct regmap *regmap;
	struct regmap_irq_chip *regmap_irq_chip;
	struct regmap_irq_chip_data *irq_data;
	int type;
	int irq;
	int irq_mode;
	unsigned long wu_flag;
	spinlock_t lock;
};

struct pm80x_platform_data {
	struct pm80x_rtc_pdata *rtc;
	struct gpio_switch_platform_data *headset;
	/*
	 * For the regulator not defined, set regulators[not_defined] to be
	 * NULL. num_regulators are the number of regulators supposed to be
	 * initialized. If all regulators are not defined, set num_regulators
	 * to be 0.
	 */
	struct regulator_init_data *regulators[PM800_ID_RG_MAX];
	unsigned int num_regulators;
	int irq_mode;		/* Clear interrupt by read/write(0/1) */
	int batt_det;		/* enable/disable */
	int (*plat_config)(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata);
};

extern const struct dev_pm_ops pm80x_pm_ops;
extern const struct regmap_config pm80x_regmap_config;

static inline int pm80x_request_irq(struct pm80x_chip *pm80x, int irq,
				     irq_handler_t handler, unsigned long flags,
				     const char *name, void *data)
{
	if (!pm80x->irq_data)
		return -EINVAL;
	return request_threaded_irq(regmap_irq_get_virq(pm80x->irq_data, irq),
				    NULL, handler, flags, name, data);
}

static inline void pm80x_free_irq(struct pm80x_chip *pm80x, int irq, void *data)
{
	if (!pm80x->irq_data)
		return;
	free_irq(regmap_irq_get_virq(pm80x->irq_data, irq), data);
}

#ifdef CONFIG_PM
static inline int pm80x_dev_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		set_bit((1 << irq), &chip->wu_flag);

	return 0;
}

static inline int pm80x_dev_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		clear_bit((1 << irq), &chip->wu_flag);

	return 0;
}
#endif

extern int pm80x_init(struct i2c_client *client);
extern int pm80x_deinit(void);
#endif /* __LINUX_MFD_88PM80X_H */
