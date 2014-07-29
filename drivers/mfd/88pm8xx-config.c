#include "88pm8xx-config.h"

#define PM800_BASE_PAGE  0x0
#define PM800_POWER_PAGE 0x1
#define PM800_GPADC_PAGE 0x2
/*
 * board specfic configurations also parameters from customers,
 * this parameters are passed form board dts file
 */
static void pmic_board_config(struct pm80x_chip *chip, struct device_node *np)
{
	unsigned int page, reg, mask, data;
	const __be32 *values;
	int size, rows, index;

	values = of_get_property(np, "marvell,pmic-board-cfg", &size);
	if (!values) {
		dev_warn(chip->dev, "no valid property for %s\n", np->name);
		return;
	}

	/* number of elements in array */
	size /= sizeof(*values);
	rows = size / 4;
	dev_info(chip->dev, "Proceed PMIC board specific configuration (%d items)\n", rows);
	index = 0;

	while (rows--) {
		page = be32_to_cpup(values + index++);
		reg = be32_to_cpup(values + index++);
		mask = be32_to_cpup(values + index++);
		data = be32_to_cpup(values + index++);
		switch (page) {
		case PM800_BASE_PAGE:
			dev_info(chip->dev, "base [0x%02x] <- 0x%02x,  mask = [0x%2x]\n", reg, data, mask);
			regmap_update_bits(chip->regmap, reg, mask, data);
			break;
		case PM800_POWER_PAGE:
			dev_info(chip->dev, "power [0x%02x] <- 0x%02x,  mask = [0x%2x]\n", reg, data, mask);
			regmap_update_bits(chip->subchip->regmap_power, reg, mask, data);
			break;
		case PM800_GPADC_PAGE:
			dev_info(chip->dev, "gpadc [0x%02x] <- 0x%02x,  mask = [0x%2x]\n", reg, data, mask);
			regmap_update_bits(chip->subchip->regmap_gpadc, reg, mask, data);
			break;
		default:
			dev_warn(chip->dev, "Unsupported page for %d\n", page);
			break;
		}
	}

	return;
}

void parse_powerup_down_log(struct pm80x_chip *chip)
{
	int up_log, down1_log, down2_log, bit;
	static const char *powerup_name[7] = {
		"ONKEY_WAKEUP    ",
		"CHG_WAKEUP      ",
		"EXTON_WAKEUP    ",
		"RESERVED        ",
		"RTC_ALARM_WAKEUP",
		"FAULT_WAKEUP    ",
		"BAT_WAKEUP      "
	};

	static const char *powerd1_name[8] = {
		"OVER_TEMP ",
		"UV_VSYS1  ",
		"SW_PDOWN  ",
		"FL_ALARM  ",
		"WDT       ",
		"LONG_ONKEY",
		"OV_VSYS   ",
		"RTC_RESET "
	};

	static const char *powerd2_name[5] = {
		"HYB_DONE   ",
		"UV_VSYS2   ",
		"HW_RESET   ",
		"PGOOD_PDOWN",
		"LONKEY_RTC "
	};

	/*power up log*/
	regmap_read(chip->regmap, PM800_POWER_UP_LOG, &up_log);
	dev_info(chip->dev, "powerup log 0x%x: 0x%x\n", PM800_POWER_UP_LOG, up_log);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|     name(power up) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < ARRAY_SIZE(powerup_name); bit++)
		dev_info(chip->dev, "|  %s  |    %x     |\n",
			powerup_name[bit], (up_log >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");
	/*power down log1*/
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG1, &down1_log);
	dev_info(chip->dev, "powerdown log1 0x%x: 0x%x\n", PM800_POWER_DOWN_LOG1, down1_log);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "| name(power down1)  |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < ARRAY_SIZE(powerd1_name); bit++)
		dev_info(chip->dev, "|    %s      |    %x     |\n",
			powerd1_name[bit], (down1_log >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");
	/*power down log2*/
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG2, &down2_log);
	dev_info(chip->dev, "power log2 0x%x: 0x%x\n", PM800_POWER_DOWN_LOG2, down2_log);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|  name(power down2) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < ARRAY_SIZE(powerd2_name); bit++)
		dev_info(chip->dev, "|    %s     |    %x     |\n",
			powerd2_name[bit], (down2_log>> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");
}

int pm800_init_config(struct pm80x_chip *chip, struct device_node *np)
{
	int data;
	if (!chip || !chip->regmap || !chip->subchip
	    || !chip->subchip->regmap_power) {
		pr_err("%s:chip is not availiable!\n", __func__);
		return -EINVAL;
	}

	/*base page:reg 0xd0.7 = 1 32kHZ generated from XO */
	regmap_read(chip->regmap, PM800_RTC_CONTROL, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_RTC_CONTROL, data);

	/* Set internal digital sleep voltage as 1.2V */
	regmap_read(chip->regmap, PM800_LOW_POWER1, &data);
	data &= ~(0xf << 4);
	regmap_write(chip->regmap, PM800_LOW_POWER1, data);
	/*
	 * enable 32Khz-out-3 low jitter XO_LJ = 1 in pm800
	 * enable 32Khz-out-2 low jitter XO_LJ = 1 in pm822
	 * they are the same bit
	 */
	regmap_read(chip->regmap, PM800_LOW_POWER2, &data);
	data |= (1 << 5);
	regmap_write(chip->regmap, PM800_LOW_POWER2, data);

	switch (chip->type) {
	case CHIP_PM800:
		/* enable 32Khz-out-from XO 1, 2, 3 all enabled */
		regmap_write(chip->regmap, PM800_RTC_MISC2, 0x2a);
		break;

	case CHIP_PM822:
		/* select 22pF internal capacitance on XTAL1 and XTAL2*/
		regmap_read(chip->regmap, PM800_RTC_MISC6, &data);
		data |= (0x7 << 4);
		regmap_write(chip->regmap, PM800_RTC_MISC6, data);

		/* Enable 32Khz-out-from XO 1, 2 all enabled */
		regmap_write(chip->regmap, PM800_RTC_MISC2, 0xa);

		/* gps use the LDO13 set the current 170mA  */
		regmap_read(chip->subchip->regmap_power,
					PM822_LDO13_CTRL, &data);
		data &= ~(0x3);
		data |= (0x2);
		regmap_write(chip->subchip->regmap_power,
					PM822_LDO13_CTRL, data);
		/* low power config
		 * 1. base_page 0x21, BK_CKSLP_DIS is gated 1ms after sleep mode entry.
		 * 2. base_page 0x23, REF_SLP_EN reference group enter low power mode.
		 *    REF_UVL_SEL set to be 5.6V
		 * 3. base_page 0x50, 0x55 OSC_CKLCK buck FLL is locked
		 * 4. gpadc_page 0x06, GP_SLEEP_MODE MEANS_OFF scale set to be 8
		 *    MEANS_EN_SLP set to 1, GPADC operates in sleep duty cycle mode.
		 */
		regmap_read(chip->regmap, PM800_LOW_POWER2, &data);
		data |= (1 << 4);
		regmap_write(chip->regmap, PM800_LOW_POWER2, data);

		regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG4, &data);
		data |= (1 << 4);
		data &= ~(0x3 < 2);
		regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG4, data);

		regmap_read(chip->regmap, PM800_OSC_CNTRL1, &data);
		data |= (1 << 0);
		regmap_write(chip->regmap, PM800_OSC_CNTRL1, data);

		regmap_read(chip->regmap, PM800_OSC_CNTRL6, &data);
		data &= ~(1 << 0);
		regmap_write(chip->regmap, PM800_OSC_CNTRL6, data);

		regmap_read(chip->subchip->regmap_gpadc, PM800_GPADC_MISC_CONFIG2, &data);
		data |= (0x7 << 4);
		regmap_write(chip->subchip->regmap_gpadc, PM800_GPADC_MISC_CONFIG2, data);

		/*
		 * enable LDO sleep mode
		 * TODO: GPS and RF module need to test after enable
		 * ldo3 sleep mode may make emmc not work when resume, disable it
		 */
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP1, 0xba);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP2, 0xaa);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP3, 0xaa);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP4, 0x0a);

		break;

	case CHIP_PM86X:
		/* enable buck1 dual phase mode */
		regmap_read(chip->subchip->regmap_power, PM860_BUCK1_MISC,
				&data);
		data |= BUCK1_DUAL_PHASE_SEL;
		regmap_write(chip->subchip->regmap_power, PM860_BUCK1_MISC,
				data);

		/* xo_cap sel bit(4~6)= 100 12pf register:0xe8 */
		regmap_read(chip->regmap, PM860_MISC_RTC3, &data);
		data |= (0x4 << 4);
		regmap_write(chip->regmap, PM860_MISC_RTC3, data);

		regmap_read(chip->regmap, PM80X_CHIP_ID, &data);
		pr_info("88pm860 id: 0x%x\n", data);
		if (data == CHIP_PM860_A0_ID) {
			/* set gpio4 and gpio5 to be DVC mode */
			regmap_read(chip->regmap, PM860_GPIO_4_5_CNTRL, &data);
			data |= PM860_GPIO4_GPIO_MODE(7) | PM860_GPIO5_GPIO_MODE(7);
			regmap_write(chip->regmap, PM860_GPIO_4_5_CNTRL, data);
		} else {
			/* set gpio3 and gpio4 to be DVC mode */
			regmap_read(chip->regmap, PM860_GPIO_2_3_CNTRL, &data);
			data |= PM860_GPIO3_GPIO_MODE(7);
			regmap_write(chip->regmap, PM860_GPIO_2_3_CNTRL, data);

			regmap_read(chip->regmap, PM860_GPIO_4_5_CNTRL, &data);
			data |= PM860_GPIO4_GPIO_MODE(7);
			regmap_write(chip->regmap, PM860_GPIO_4_5_CNTRL, data);
		}

		break;

	default:
		dev_err(chip->dev, "Unknown device type: %d\n", chip->type);
		break;
	}

	/*
	 * block wakeup attempts when VSYS rises above
	 * VSYS_UNDER_RISE_TH1, or power off may fail.
	 * it is set to prevent contimuous attempt to power up
	 * incase the VSYS is above the VSYS_LOW_TH threshold.
	 */
	regmap_read(chip->regmap, PM800_RTC_MISC5, &data);
	data |= 0x1;
	regmap_write(chip->regmap, PM800_RTC_MISC5, data);

	/* enabele LDO and BUCK clock gating in lpm */
	regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG3, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG3, data);
	/*
	 * disable reference group sleep mode
	 * - to reduce power fluctuation in suspend
	 */
	regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG4, &data);
	data &= ~(1 << 7);
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG4, data);

	/* enable voltage change in pmic, POWER_HOLD = 1 */
	regmap_read(chip->regmap, PM800_WAKEUP1, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_WAKEUP1, data);

	/* enable buck sleep mode */
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP1, 0xaa);
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP2, 0x2);

	/*
	 * set buck2 and buck4 driver selection to be full.
	 * this bit is now reserved and default value is 0,
	 * for full drive, set to 1.
	 */
	regmap_read(chip->subchip->regmap_power, 0x7c, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x7c, data);

	regmap_read(chip->subchip->regmap_power, 0x82, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x82, data);

	/* need to config board specific parameters */
	if (np)
		pmic_board_config(chip, np);


	return 0;
}

