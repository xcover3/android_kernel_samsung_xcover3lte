/*
 * include/linux/platform_data/pxa_sdhci.h
 *
 * Copyright 2010 Marvell
 *	Zhangfei Gao <zhangfei.gao@marvell.com>
 *
 * PXA Platform - SDHCI platform data definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _PXA_SDHCI_H_
#define _PXA_SDHCI_H_

/*
 * sdhci_pxa_dtr_data: sdhc data transfer rate table
 * @timing: the specification used timing
 * @preset_rate: the clock could set by the SOC
 * @src_rate: send to the clock subsystem
 * related to APMU on SOC
 * @tx_delay: use this value for TX DDLL
 * @rx_delay: use this value for RX DDLL
 * @rx_sdclk_sel0: select RX DDLL clock source
 * @rx_sdclk_sel1: select RX DDLL or internal clock
 */
struct sdhci_pxa_dtr_data {
	unsigned char timing;
	unsigned long preset_rate;
	unsigned long src_rate;
	unsigned int tx_delay;
	unsigned int rx_delay;
	unsigned char rx_sdclk_sel0;
	unsigned char rx_sdclk_sel1;
};

struct sdhci_regdata {
	u32 TX_DELAY_MASK;
	u32 RX_TUNING_DLY_INC_MASK;
	u32 RX_TUNING_DLY_INC_SHIFT;
	u32 RX_SDCLK_DELAY_MASK;
	u32 SD_RX_TUNE_MAX;
	u32 MMC1_PAD_2V5;
	u32 PAD_POWERDOWNn;
	u32 APBC_ASFAR;
};

#include <linux/pm_qos.h>

/*
 * struct pxa_sdhci_platdata() - Platform device data for PXA SDHCI
 * @flags: flags for platform requirement
 * @clk_delay_cycles:
 *	mmp2: each step is roughly 100ps, 5bits width
 *	pxa910: each step is 1ns, 4bits width
 * @clk_delay_sel: select clk_delay, used on pxa910
 *	0: choose feedback clk
 *	1: choose feedback clk + delay value
 *	2: choose internal clk
 * @clk_delay_enable: enable clk_delay or not, used on pxa910
 * @ext_cd_gpio: gpio pin used for external CD line
 * @ext_cd_gpio_invert: invert values for external CD gpio line
 * @max_speed: the maximum speed supported
 * @host_caps: Standard MMC host capabilities bit field.
 * @quirks: quirks of platfrom
 * @quirks2: quirks2 of platfrom
 * @pm_caps: pm_caps of platfrom
 */
struct sdhci_pxa_platdata {
	unsigned int	flags;
	unsigned int	clk_delay_cycles;
	unsigned int	clk_delay_sel;
	bool		clk_delay_enable;
	unsigned int	max_speed;
	u32		host_caps;
	u32		host_caps2;
	unsigned int    host_caps_disable;
	unsigned int    host_caps2_disable;
	unsigned int	quirks;
	unsigned int	quirks2;
	unsigned int	pm_caps;
	struct sdhci_pxa_dtr_data *dtr_data;
	struct  pm_qos_request  qos_idle;
	u32	lpm_qos;
	const struct sdhci_regdata *regdata;
};

struct sdhci_pxa {
	u8	clk_enable;
	u8	power_mode;
	struct clk	*clk;
	struct clk	*axi_clk;
};
#endif /* _PXA_SDHCI_H_ */
