/*
 * Copyright (C) 2010 Marvell International Ltd.
 *		Zhangfei Gao <zhangfei.gao@marvell.com>
 *		Kevin Wang <dwang4@marvell.com>
 *		Mingwei Wang <mwwang@marvell.com>
 *		Philip Rakity <prakity@marvell.com>
 *		Mark Brown <markb@marvell.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/pm_qos.h>
#include <linux/dma-mapping.h>
#include <linux/pinctrl/consumer.h>
#include <dt-bindings/mmc/pxa_sdhci.h>

#include "sdhci.h"
#include "sdhci-pltfm.h"

#define PXAV3_RPM_DELAY_MS		50
#define SDCLK_SEL			0x100
#define SDCLK_DELAY_SHIFT		9
#define SDCLK_DELAY_MASK		0x1f
#define SDCFG_GEN_PAD_CLK_ON		(1<<6)
#define SDCFG_GEN_PAD_CLK_CNT_MASK	0xFF
#define SDCFG_GEN_PAD_CLK_CNT_SHIFT	24
#define SDCFG_PIO_RDFC			(1<<0)
#define SD_SPI_MODE			0x108
#define SDCE_MISC_INT			(1<<2)
#define SDCE_MISC_INT_EN		(1<<1)
#define RX_SDCLK_DELAY_SHIFT		8
#define RX_SDCLK_SEL0_MASK		0x3
#define RX_SDCLK_SEL1_MASK		0x3
#define RX_SDCLK_SEL0_SHIFT		0
#define RX_SDCLK_SEL1_SHIFT		2
#define PAD_CLK_GATE_MASK		(0x3<<11)
#define TX_DELAY1_SHIFT			16
#define TX_MUX_SEL			(0x1<<31)
#define TX_SEL_BUS_CLK			(0x1<<30)
#define RX_TUNING_CFG_REG		0x11C
#define RX_TUNING_WD_CNT_MASK		0x3F
#define RX_TUNING_WD_CNT_SHIFT		8
#define RX_TUNING_TT_CNT_MASK		0xFF
#define RX_TUNING_TT_CNT_SHIFT		0
#define SD_CE_ATA2_HS200_EN		(1<<10)
#define SD_CE_ATA2_MMC_MODE		(1<<12)

#define SD_CLOCK_BURST_SIZE_SETUP	0x10A
#define SD_CFG_FIFO_PARAM		0x100
#define SD_CE_ATA_1			0x10C
#define SD_CE_ATA_2			0x10E
#define SD_FIFO_PARAM			0x104
#define SD_RX_CFG_REG			0x114
#define SD_TX_CFG_REG			0x118

#define AIB_MMC1_IO_REG         0xD401E81C
#define AKEY_ASFAR	0xbaba
#define AKEY_ASSAR	0xeb10
#define MMC1_PAD_1V8            (0x1 << 2)
#define MMC1_PAD_3V3            (0x0 << 2)
#define MMC1_PAD_MASK           (0x3 << 2)

/* pxav3_regdata_v1: pxa988/pxa1088/pxa1L88 */
static struct sdhci_regdata pxav3_regdata_v1 = {
	.TX_DELAY_MASK = 0x1FF,
	.RX_TUNING_DLY_INC_MASK = 0x1FF,
	.RX_TUNING_DLY_INC_SHIFT = 17,
	.RX_SDCLK_DELAY_MASK = 0x1FF,
	.SD_RX_TUNE_MAX = 0x1FF,
	.MMC1_PAD_2V5 = (0x2 << 2),
	.PAD_POWERDOWNn = (0x1 << 0),
	.APBC_ASFAR = 0xD4015050,
};

/* pxav3_regdata_v2: pxa1U88 */
static struct sdhci_regdata pxav3_regdata_v2 = {
	.TX_DELAY_MASK = 0x3FF,
	.RX_TUNING_DLY_INC_MASK = 0x3FF,
	.RX_TUNING_DLY_INC_SHIFT = 18,
	.RX_SDCLK_DELAY_MASK = 0x3FF,
	.SD_RX_TUNE_MAX = 0x3FF,
	.MMC1_PAD_2V5 = (0x2 << 2),
	.PAD_POWERDOWNn = (0x1 << 0),
	.APBC_ASFAR = 0xD4015050,
};

/* pxav3_regdata_v3: pxa1928 */
static struct sdhci_regdata pxav3_regdata_v3 = {
	.TX_DELAY_MASK = 0x3FF,
	.RX_TUNING_DLY_INC_MASK = 0x3FF,
	.RX_TUNING_DLY_INC_SHIFT = 18,
	.RX_SDCLK_DELAY_MASK = 0x3FF,
	.SD_RX_TUNE_MAX = 0x3FF,
	.MMC1_PAD_2V5 = 0,
	.PAD_POWERDOWNn = 0,
	.APBC_ASFAR = 0xD4015068,
};

#define SD_RX_TUNE_MIN			0
#define SD_RX_TUNE_STEP			1

static const u32 tuning_patten4[16] = {
	0x00ff0fff, 0xccc3ccff, 0xffcc3cc3, 0xeffefffe,
	0xddffdfff, 0xfbfffbff, 0xff7fffbf, 0xefbdf777,
	0xf0fff0ff, 0x3cccfc0f, 0xcfcc33cc, 0xeeffefff,
	0xfdfffdff, 0xffbfffdf, 0xfff7ffbb, 0xde7b7ff7,
};

static const u32 tuning_patten8[32] = {
	0xff00ffff, 0x0000ffff, 0xccccffff, 0xcccc33cc,
	0xcc3333cc, 0xffffcccc, 0xffffeeff, 0xffeeeeff,
	0xffddffff, 0xddddffff, 0xbbffffff, 0xbbffffff,
	0xffffffbb, 0xffffff77, 0x77ff7777, 0xffeeddbb,
	0x00ffffff, 0x00ffffff, 0xccffff00, 0xcc33cccc,
	0x3333cccc, 0xffcccccc, 0xffeeffff, 0xeeeeffff,
	0xddffffff, 0xddffffff, 0xffffffdd, 0xffffffbb,
	0xffffbbbb, 0xffff77ff, 0xff7777ff, 0xeeddbb77,
};

static void pxav3_set_rx_cfg(struct sdhci_host *host,
		struct sdhci_pxa_platdata *pdata)
{
	u32 tmp_reg = 0;
	unsigned char timing = host->mmc->ios.timing;
	struct sdhci_pxa_dtr_data *dtr_data;
	const struct sdhci_regdata *regdata = pdata->regdata;

	if ((!pdata) || (!pdata->dtr_data))
		return;

	if (MMC_TIMING_LEGACY == timing)
		goto exit;

	/* From PXA988, the RX_CFG Reg is changed to 0x114 */
	if (pdata->flags & PXA_FLAG_NEW_RX_CFG_REG) {
		if (timing >= MMC_TIMING_MAX) {
			pr_err("%s: invalid timing %d\n", mmc_hostname(host->mmc), timing);
			return;
		}

		dtr_data = &pdata->dtr_data[timing];

		if (timing != dtr_data->timing)
			return;

		/* set Rx delay */
		if (dtr_data->rx_delay) {
			/*
			 * Only SEL1 = 0b01, SEL0 is meanful and Rx delay value works
			 *  SEL0
			 *   = 0b00: clock from PADd
			 *   = 0b01: inverted clock from PAD
			 *   = 0b10: internal clock
			 *   = 0b11: inverted internal clock
			 */
			tmp_reg |= 0x1 << RX_SDCLK_SEL1_SHIFT;

			tmp_reg |= (dtr_data->rx_delay & regdata->RX_SDCLK_DELAY_MASK)
					<< RX_SDCLK_DELAY_SHIFT;
			tmp_reg |= (dtr_data->rx_sdclk_sel0 & RX_SDCLK_SEL0_MASK)
					<< RX_SDCLK_SEL0_SHIFT;

			if ((MMC_TIMING_UHS_SDR104 != timing)
					&& (MMC_TIMING_MMC_HS200 != timing)) {
				/*
				 * Rx delay works for all speed modes,
				 * but only SDR104 and HS200 need to use it.
				 */
				pr_info("suggest only to set Rx delay for HS200 or SDR104\n");
			}
		} else if (dtr_data->rx_sdclk_sel1 != 0x1) {
			/*
			 * If SEL1 != 0b01, Rx delay value doesn't work and SEL0 is meanless
			 *  SEL1
			 *   = 0b00: Select clock from PAD
			 *   = 0b10/0b11: Select clock from internal clock
			 */
			tmp_reg |= (dtr_data->rx_sdclk_sel1 & RX_SDCLK_SEL1_MASK)
					<< RX_SDCLK_SEL1_SHIFT;
		}
	}
exit:
	sdhci_writel(host, tmp_reg, SD_RX_CFG_REG);
}

static ssize_t rx_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mmc_host =
		container_of(dev, struct mmc_host, class_dev);
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	int ret = 0;
	u32 tmp_reg = 0;

	sdhci_access_constrain(host, 1);
	sdhci_runtime_pm_get(host);

	tmp_reg = sdhci_readl(host, SD_RX_CFG_REG);
	pxa->rx_dly_val = (tmp_reg >> RX_SDCLK_DELAY_SHIFT) & regdata->RX_SDCLK_DELAY_MASK;
	ret = sprintf(buf, "rx delay: 0x%x\t| RX config: 0x%08x\n", pxa->rx_dly_val, tmp_reg);

	sdhci_runtime_pm_put(host);
	sdhci_access_constrain(host, 0);

	return ret;
}
static ssize_t rx_delay_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mmc_host *mmc_host =
		container_of(dev, struct mmc_host, class_dev);
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	u32 tmp_reg = 0;

	if (sscanf(buf, "%d", &pxa->rx_dly_val) != 1)
		return -EINVAL;

	sdhci_access_constrain(host, 1);
	sdhci_runtime_pm_get(host);
	tmp_reg = sdhci_readl(host, SD_RX_CFG_REG);
	/* clock by SDCLK_SEL0, so it is default setting */
	tmp_reg &= ~(RX_SDCLK_SEL1_MASK << RX_SDCLK_SEL1_SHIFT);
	tmp_reg |= 0x1 << RX_SDCLK_SEL1_SHIFT;
	tmp_reg &= ~(regdata->RX_SDCLK_DELAY_MASK << RX_SDCLK_DELAY_SHIFT);
	tmp_reg |= (pxa->rx_dly_val & regdata->RX_SDCLK_DELAY_MASK) << RX_SDCLK_DELAY_SHIFT;
	sdhci_writel(host, tmp_reg, SD_RX_CFG_REG);
	sdhci_runtime_pm_put(host);
	sdhci_access_constrain(host, 0);

	return count;
}

static void pxav3_set_tx_cfg(struct sdhci_host *host,
		struct sdhci_pxa_platdata *pdata)
{
	u32 tmp_reg = 0;
	unsigned char timing = host->mmc->ios.timing;
	struct sdhci_pxa_dtr_data *dtr_data;
	const struct sdhci_regdata *regdata = pdata->regdata;

	if (pdata && pdata->flags & PXA_FLAG_TX_SEL_BUS_CLK) {
		/*
		 * For the hold time at default speed mode or high speed mode
		 * PXAV3 should enable the TX_SEL_BUS_CLK which will select
		 * clock from inverter of internal work clock.
		 * This setting will guarantee the hold time.
		 */
		if (timing <= MMC_TIMING_UHS_SDR25) {
			tmp_reg |= TX_SEL_BUS_CLK;
			sdhci_writel(host, tmp_reg, SD_TX_CFG_REG);
			return;
		}
	}

	if (pdata && pdata->dtr_data) {
		if (timing >= MMC_TIMING_MAX) {
			pr_err("%s: invalid timing %d\n", mmc_hostname(host->mmc), timing);
			return;
		}

		dtr_data = &pdata->dtr_data[timing];

		if (timing != dtr_data->timing)
			goto exit;

		/* set Tx delay */
		if (dtr_data->tx_delay) {
			tmp_reg |= TX_MUX_SEL;
			if ((MMC_TIMING_UHS_SDR104 == timing)
					|| (MMC_TIMING_MMC_HS200 == timing))
				tmp_reg |= (dtr_data->tx_delay & regdata->TX_DELAY_MASK)
						<< TX_DELAY1_SHIFT;
			else
				tmp_reg |= (dtr_data->tx_delay & regdata->TX_DELAY_MASK);
		}
	}
exit:
	sdhci_writel(host, tmp_reg, SD_TX_CFG_REG);
}

static ssize_t tx_delay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mmc_host *mmc_host =
		container_of(dev, struct mmc_host, class_dev);
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	struct mmc_ios *ios = &host->mmc->ios;
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	int ret = 0;
	u32 tmp_reg = 0;

	sdhci_access_constrain(host, 1);
	sdhci_runtime_pm_get(host);
	tmp_reg = sdhci_readl(host, SD_TX_CFG_REG);
	if ((MMC_TIMING_UHS_SDR104 == ios->timing) || (MMC_TIMING_MMC_HS200 == ios->timing))
		pxa->tx_dly_val = (tmp_reg >> TX_DELAY1_SHIFT) & regdata->TX_DELAY_MASK;
	else
		pxa->tx_dly_val = tmp_reg & regdata->TX_DELAY_MASK;

	ret = sprintf(buf, "tx delay: 0x%x\t| TX config: 0x%08x\n", pxa->tx_dly_val, tmp_reg);

	sdhci_runtime_pm_put(host);
	sdhci_access_constrain(host, 0);
	return ret;
}

static ssize_t tx_delay_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct mmc_host *mmc_host =
		container_of(dev, struct mmc_host, class_dev);
	struct sdhci_host *host = mmc_priv(mmc_host);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	struct mmc_ios *ios = &host->mmc->ios;
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	u32 tmp_reg = 0;

	if (ios->timing <= MMC_TIMING_UHS_SDR25) {
		/*
		 * For HS ans DS mode,
		 * PXAV3 can handle the mmc bus timing issue by HW.
		 * So we don't need to set tx delay for these modes.
		 */
		pr_info("Can't set tx delay in HS or DS mode!\n");
		return count;
	}

	if (sscanf(buf, "%d", &pxa->tx_dly_val) != 1)
		return -EINVAL;

	sdhci_access_constrain(host, 1);
	sdhci_runtime_pm_get(host);

	tmp_reg |= TX_MUX_SEL;
	if ((MMC_TIMING_UHS_SDR104 == ios->timing) || (MMC_TIMING_MMC_HS200 == ios->timing))
		tmp_reg |= (pxa->tx_dly_val & regdata->TX_DELAY_MASK) << TX_DELAY1_SHIFT;
	else
		tmp_reg |= (pxa->tx_dly_val & regdata->TX_DELAY_MASK);

	sdhci_writel(host, tmp_reg, SD_TX_CFG_REG);
	sdhci_runtime_pm_put(host);
	sdhci_access_constrain(host, 0);

	return count;
}

static void pxav3_set_clock(struct sdhci_host *host, unsigned int clock)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (clock == 0)
		return;

	pxav3_set_tx_cfg(host, pdata);
	pxav3_set_rx_cfg(host, pdata);

	/*
	 * Configure pin state like drive strength according to bus clock.
	 * Use slow setting when new bus clock < FAST_CLOCK while current >= FAST_CLOCK.
	 * Use fast setting when new bus clock >= FAST_CLOCK while current < FAST_CLOCK.
	 */
#define FAST_CLOCK 100000000
	if (clock < FAST_CLOCK) {
		if ((host->clock >= FAST_CLOCK) && (!IS_ERR(pdata->pin_slow)))
			pinctrl_select_state(pdata->pinctrl, pdata->pin_slow);
	} else {
		if ((host->clock < FAST_CLOCK) && (!IS_ERR(pdata->pin_fast)))
			pinctrl_select_state(pdata->pinctrl, pdata->pin_fast);
	}
}

static unsigned long pxav3_clk_prepare(struct sdhci_host *host,
		unsigned long rate)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	struct sdhci_pxa_dtr_data *dtr_data;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	unsigned char timing = host->mmc->ios.timing;

	unsigned long preset_rate = 0, src_rate = 0;

	if (pdata && pdata->dtr_data) {
		if (timing >= MMC_TIMING_MAX) {
			pr_err("%s: invalid timing %d\n", mmc_hostname(host->mmc), timing);
			return rate;
		}

		dtr_data = &pdata->dtr_data[timing];

		if (timing != dtr_data->timing)
			return rate;

		if ((MMC_TIMING_LEGACY == timing) &&
				(rate != PXA_SDH_DTR_25M))
			preset_rate = rate;
		else
			preset_rate = dtr_data->preset_rate;
		src_rate = dtr_data->src_rate;
		clk_set_rate(pltfm_host->clk, src_rate);

		return preset_rate;
	} else
		return rate;
}

static void pxav3_clk_gate_auto(struct sdhci_host *host, unsigned int ctrl)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	u16 tmp;
	/*
	 * FIXME: according to MMC Spec, bit SDHCI_CTRL_ASYNC_INT
	 * should be used to deliver async interrupt requested by
	 * sdio device rather than auto clock gate. But in some
	 * platforms, such as PXA920/PXA988/PXA986/MMP2/MMP3, the
	 * mmc host controller use this bit to enable/disable auto
	 * clock gate, except PXA1088 platform. So in PXA1088 platform
	 * use the FORCE_CLK_ON bit to always enable the bus clock.
	 * In order to diff the PXA1088 and other platforms, use
	 * SDHCI_QUIRK2_BUS_CLK_GATE_ENABLED for PXA1088.
	 */
	if (pdata) {
		if (pdata->quirks2 & SDHCI_QUIRK2_BUS_CLK_GATE_ENABLED) {
			tmp = sdhci_readw(host, SD_FIFO_PARAM);
			if (ctrl)
				tmp &= ~PAD_CLK_GATE_MASK;
			else
				tmp |= PAD_CLK_GATE_MASK;

			sdhci_writew(host, tmp, SD_FIFO_PARAM);
		} else {
			tmp = sdhci_readw(host, SD_FIFO_PARAM);
			tmp &= ~PAD_CLK_GATE_MASK;
			sdhci_writew(host, tmp, SD_FIFO_PARAM);

			tmp = sdhci_readw(host, SDHCI_HOST_CONTROL2);

			if (ctrl)
				tmp |= SDHCI_CTRL_ASYNC_INT;
			else
				tmp &= ~SDHCI_CTRL_ASYNC_INT;

			sdhci_writew(host, tmp, SDHCI_HOST_CONTROL2);
		}
	}
}

static void pxav3_set_private_registers(struct sdhci_host *host, u8 mask)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (mask != SDHCI_RESET_ALL) {
		/* Return if not Reset All */
		return;
	}

	/*
	 * tune timing of read data/command when crc error happen
	 * no performance impact
	 */
	pxav3_set_tx_cfg(host, pdata);
	pxav3_set_rx_cfg(host, pdata);
}

#define MAX_WAIT_COUNT 74
static void pxav3_gen_init_74_clocks(struct sdhci_host *host, u8 power_mode)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	u16 tmp;
	int count = 0;

	if (pxa->power_mode == MMC_POWER_UP
			&& power_mode == MMC_POWER_ON) {

		dev_dbg(mmc_dev(host->mmc),
				"%s: slot->power_mode = %d,"
				"ios->power_mode = %d\n",
				__func__,
				pxa->power_mode,
				power_mode);

		/* clear the interrupt bit if posted and
		 * set we want notice of when 74 clocks are sent
		 */
		tmp = sdhci_readw(host, SD_CE_ATA_2);
		tmp |= SDCE_MISC_INT | SDCE_MISC_INT_EN;
		sdhci_writew(host, tmp, SD_CE_ATA_2);

		/* start sending the 74 clocks */
		tmp = sdhci_readw(host, SD_CFG_FIFO_PARAM);
		tmp |= SDCFG_GEN_PAD_CLK_ON;
		sdhci_writew(host, tmp, SD_CFG_FIFO_PARAM);

		/* slowest speed is about 100KHz or 10usec per clock */
		while (count++ < MAX_WAIT_COUNT) {
			if (sdhci_readw(host, SD_CE_ATA_2) & SDCE_MISC_INT)
				break;
			udelay(20);
		}

		if (count >= MAX_WAIT_COUNT)
			dev_warn(mmc_dev(host->mmc), "74 clock interrupt not cleared\n");
	}
	pxa->power_mode = power_mode;
}

static int pxav3_select_hs200(struct sdhci_host *host)
{
	u16 reg = 0;

	reg = sdhci_readw(host, SD_CE_ATA_2);
	reg |= SD_CE_ATA2_HS200_EN | SD_CE_ATA2_MMC_MODE;
	sdhci_writew(host, reg, SD_CE_ATA_2);

	return 0;
}

static int pxav3_set_uhs_signaling(struct sdhci_host *host, unsigned int uhs)
{
	u16 ctrl_2;
	u16 fparm;

	/*
	 * Set V18_EN -- UHS modes do not work without this.
	 * does not change signaling voltage
	 */
	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	fparm = sdhci_readw(host, SD_CFG_FIFO_PARAM);

	/* Select Bus Speed Mode for host */
	switch (uhs) {
	case MMC_TIMING_UHS_SDR12:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
		break;
	case MMC_TIMING_UHS_SDR25:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
		break;
	case MMC_TIMING_UHS_SDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_UHS_SDR104:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104 | SDHCI_CTRL_VDD_180;
		/* PIO mode need this */
		fparm |= SDCFG_PIO_RDFC;
		break;
	case MMC_TIMING_UHS_DDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50 | SDHCI_CTRL_VDD_180;
		break;
	case MMC_TIMING_MMC_HS200:
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104 | SDHCI_CTRL_VDD_180;
		fparm |= SDCFG_PIO_RDFC;
		pxav3_select_hs200(host);
		break;
	}

	sdhci_writew(host, fparm, SD_CFG_FIFO_PARAM);
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
	dev_dbg(mmc_dev(host->mmc),
		"%s uhs = %d, ctrl_2 = %04X\n",
		__func__, uhs, ctrl_2);

	return 0;
}

static void set_mmc1_aib(struct sdhci_host *host, int vol)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	u32 tmp;
	void __iomem *aib_mmc1_io;
	void __iomem *apbc_asfar;

	aib_mmc1_io = ioremap(AIB_MMC1_IO_REG, 4);
	apbc_asfar = ioremap(regdata->APBC_ASFAR, 8);

	writel(AKEY_ASFAR, apbc_asfar);
	writel(AKEY_ASSAR, apbc_asfar + 4);
	tmp = readl(aib_mmc1_io);

	/* 0= power down, only set power down when vol = 0 */
	tmp |= regdata->PAD_POWERDOWNn;

	tmp &= ~MMC1_PAD_MASK;
	if (vol >= 2800000)
		tmp |= MMC1_PAD_3V3;
	else if (vol >= 2300000)
		tmp |= regdata->MMC1_PAD_2V5;
	else if (vol >= 1200000)
		tmp |= MMC1_PAD_1V8;
	else if (vol == 0)
		tmp &= ~regdata->PAD_POWERDOWNn;

	writel(AKEY_ASFAR, apbc_asfar);
	writel(AKEY_ASSAR, apbc_asfar + 4);
	writel(tmp, aib_mmc1_io);

	iounmap(apbc_asfar);
	iounmap(aib_mmc1_io);
}

static void pxav3_clr_wakeup_event(struct sdhci_host *host)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (!pdata)
		return;

	if (pdata->clear_wakeup_event)
		pdata->clear_wakeup_event();
}

static void pxav3_signal_vol_change(struct sdhci_host *host, u8 vol)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	unsigned int set = 0;

	if (!pdata || !(pdata->quirks2 & SDHCI_QUIRK2_SET_AIB_MMC))
		return;

	switch (vol) {
	case MMC_SIGNAL_VOLTAGE_330:
		set = 3300000;
		break;
	case MMC_SIGNAL_VOLTAGE_180:
		set = 1800000;
		break;
	case MMC_SIGNAL_VOLTAGE_120:
		set = 1200000;
		break;
	default:
		set = 3300000;
		break;
	}
	set_mmc1_aib(host, set);
}

static void pxav3_access_constrain(struct sdhci_host *host, unsigned int ac)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (!pdata)
		return;
	if (ac)
		pm_qos_update_request(&pdata->qos_idle, pdata->lpm_qos);
	else
		pm_qos_update_request(&pdata->qos_idle, PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
}

static void pxav3_hw_tuning_prepare(struct sdhci_host *host)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	u32 reg;
	u16 ctrl_2;

	if (!host->tuning_wd_cnt ||
			(host->tuning_wd_cnt > RX_TUNING_WD_CNT_MASK))
		host->tuning_wd_cnt = RX_TUNING_WD_CNT_MASK;
	if (!host->tuning_tt_cnt ||
			(host->tuning_tt_cnt > (RX_TUNING_TT_CNT_MASK + 1)))
		host->tuning_tt_cnt = RX_TUNING_TT_CNT_MASK + 1;

	/* reset tuning circuit */
	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	ctrl_2 &= ~(SDHCI_CTRL_EXEC_TUNING | SDHCI_CTRL_TUNED_CLK);
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);

	/* set tuning step */
	reg = sdhci_readl(host, SD_RX_CFG_REG);
	reg &= ~(regdata->RX_TUNING_DLY_INC_MASK << regdata->RX_TUNING_DLY_INC_SHIFT);
	reg |= (((regdata->RX_SDCLK_DELAY_MASK + 1)/host->tuning_tt_cnt) <<
			regdata->RX_TUNING_DLY_INC_SHIFT);
	sdhci_writel(host, reg, SD_RX_CFG_REG);

	/* set total count and pass window count */
	reg = sdhci_readw(host, RX_TUNING_CFG_REG);
	reg &= ~((RX_TUNING_WD_CNT_MASK << RX_TUNING_WD_CNT_SHIFT) |
			(RX_TUNING_TT_CNT_MASK << RX_TUNING_TT_CNT_SHIFT));
	reg |= (host->tuning_wd_cnt << RX_TUNING_WD_CNT_SHIFT) |
			((host->tuning_tt_cnt - 1) << RX_TUNING_TT_CNT_SHIFT);
	sdhci_writew(host, reg, RX_TUNING_CFG_REG);
}

static void pxav3_prepare_tuning(struct sdhci_host *host, u32 val)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	u32 reg;

	/* delay 1ms for card to be ready for next tuning */
	mdelay(1);

	reg = sdhci_readl(host, SD_RX_CFG_REG);
	reg &= ~(regdata->RX_SDCLK_DELAY_MASK << RX_SDCLK_DELAY_SHIFT);
	reg |= (val & regdata->RX_SDCLK_DELAY_MASK) << RX_SDCLK_DELAY_SHIFT;
	reg &= ~(RX_SDCLK_SEL1_MASK << RX_SDCLK_SEL1_SHIFT);
	reg |= (1 << RX_SDCLK_SEL1_SHIFT);
	sdhci_writel(host, reg, SD_RX_CFG_REG);

	dev_dbg(mmc_dev(host->mmc), "tunning with delay 0x%x\n", val);
}

static void pxav3_request_done(struct mmc_request *mrq)
{
	complete(&mrq->completion);
}

#define PXAV3_TUNING_DEBUG 1
static int pxav3_send_tuning_cmd_adma(struct sdhci_host *host,
		u32 opcode, int point, unsigned long flags)
{
	struct mmc_command cmd = {0};
	struct mmc_request mrq = {0};
	struct mmc_data data = {0};
	struct scatterlist sg;
	char *tuning_pattern = host->tuning_pattern;
	int i;

	cmd.opcode = opcode;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	data.blocks = 1;
	data.flags = MMC_DATA_READ;

	/*
	 * We should call mmc_set_data_timeout to get timeout value.
	 * but we can't send mmc_card to that function in private tuning.
	 * so we set the timeout value as 300ms which proved long enough
	 * for most UHS cards.
	 */
	data.timeout_ns = 300000000;
	data.timeout_clks = 0;
	data.sg = &sg;
	data.sg_len = 1;

	/*
	 * UHS-I modes only support 4bit width.
	 * HS200 support 4bit or 8bit width.
	 * 8bit used 128byte test pattern while 4bit used 64byte.
	 */
	if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_8)
		data.blksz = 128;
	else
		data.blksz = 64;

#if (PXAV3_TUNING_DEBUG > 0)
	/*
	 * Although it is not need to clear the buffer, here
	 * still use memset to clear it before Tuning CMD start.
	 *
	 * So that we can use "data comparing" later
	 */
	memset(tuning_pattern, 0, TUNING_PATTERN_SIZE);
#endif
	sg_init_one(&sg, tuning_pattern, data.blksz);

	mrq.cmd = &cmd;
	mrq.cmd->mrq = &mrq;
	mrq.data = &data;
	mrq.data->mrq = &mrq;
	mrq.cmd->data = mrq.data;

	mrq.done = pxav3_request_done;
	init_completion(&(mrq.completion));

	sdhci_access_constrain(host, 1);
	sdhci_runtime_pm_get(host);
	spin_lock_irqsave(&host->lock, flags);
	host->mrq = &mrq;
	sdhci_send_command(host, mrq.cmd);
	spin_unlock_irqrestore(&host->lock, flags);

	wait_for_completion(&mrq.completion);

#if (PXAV3_TUNING_DEBUG > 0)
	/*
	 * whether patten data is saved within memory range expected
	 * TODO: add more debug code if need
	 */
	for (i = data.blksz; i < TUNING_PATTERN_SIZE; i++) {
		if (tuning_pattern[i])
			BUG_ON(1);
	}
#endif

	dev_dbg(mmc_dev(host->mmc), "point: %d, cmd.error: %d, data.error: %d\n",
		point, cmd.error, data.error);
	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	return 0;
}

/*
 * return 0: sucess, >=1: the num of pattern check errors
 */
static int pxav3_tuning_pio_check(struct sdhci_host *host, int point)
{
	u32 rd_patten;
	unsigned int i;
	u32 *tuning_patten;
	int patten_len;
	int err = 0;

	if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
		tuning_patten = (u32 *)tuning_patten8;
		patten_len = ARRAY_SIZE(tuning_patten8);
	} else {
		tuning_patten = (u32 *)tuning_patten4;
		patten_len = ARRAY_SIZE(tuning_patten4);
	}

	/* read all the data from FIFO, avoid error if IC design is not good */
	for (i = 0; i < patten_len; i++) {
		rd_patten = sdhci_readl(host, SDHCI_BUFFER);
		if (rd_patten != tuning_patten[i])
			err++;
	}
	dev_dbg(mmc_dev(host->mmc), "point: %d, error: %d\n", point, err);
	return err;
}

static int pxav3_send_tuning_cmd_pio(struct sdhci_host *host, u32 opcode,
		int point, unsigned long flags)
{
	struct mmc_command cmd = {0};
	struct mmc_request mrq = {NULL};
	int err = 0;

	cmd.opcode = opcode;
	cmd.arg = 0;
	cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;
	cmd.retries = 0;
	cmd.data = NULL;
	cmd.error = 0;

	mrq.cmd = &cmd;
	host->mrq = &mrq;

	if (cmd.opcode == MMC_SEND_TUNING_BLOCK_HS200) {
		if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_8)
			sdhci_writew(host, SDHCI_MAKE_BLKSZ(7, 128),
					SDHCI_BLOCK_SIZE);
		else if (host->mmc->ios.bus_width == MMC_BUS_WIDTH_4)
			sdhci_writew(host, SDHCI_MAKE_BLKSZ(7, 64),
					SDHCI_BLOCK_SIZE);
	} else {
		sdhci_writew(host, SDHCI_MAKE_BLKSZ(7, 64),
				SDHCI_BLOCK_SIZE);
	}

	/*
	 * The tuning block is sent by the card to the host controller.
	 * So we set the TRNS_READ bit in the Transfer Mode register.
	 * This also takes care of setting DMA Enable and Multi Block
	 * Select in the same register to 0.
	 */
	sdhci_writew(host, SDHCI_TRNS_READ, SDHCI_TRANSFER_MODE);

	sdhci_send_command(host, &cmd);

	host->cmd = NULL;
	host->mrq = NULL;

	spin_unlock_irqrestore(&host->lock, flags);
	/* Wait for Buffer Read Ready interrupt */
	wait_event_interruptible_timeout(host->buf_ready_int,
			(host->tuning_done == 1),
			msecs_to_jiffies(50));
	spin_lock_irqsave(&host->lock, flags);

	if (!host->tuning_done) {
		pr_debug("%s: Timeout waiting for Buffer Read Ready interrupt during tuning procedure, resetting CMD and DATA\n",
		       mmc_hostname(host->mmc));
		sdhci_reset(host, SDHCI_RESET_CMD|SDHCI_RESET_DATA);
		err = -EIO;
	} else
		err = pxav3_tuning_pio_check(host, point);

	host->tuning_done = 0;

	return err;
}

static int pxav3_send_tuning_cmd(struct sdhci_host *host, u32 opcode,
		int point, unsigned long flags)
{
	if (host->quirks2 & SDHCI_QUIRK2_TUNING_ADMA_BROKEN)
		return pxav3_send_tuning_cmd_pio(host, opcode, point, flags);
	else
		return pxav3_send_tuning_cmd_adma(host, opcode, point, flags);
}

static int pxav3_executing_tuning(struct sdhci_host *host, u32 opcode)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	const struct sdhci_regdata *regdata = pdata->regdata;
	int min, max, ret;
	int len = 0, avg = 0;
	u32 ier = 0;
	unsigned long flags = 0;

	if (host->quirks2 & SDHCI_QUIRK2_TUNING_ADMA_BROKEN) {
		spin_lock_irqsave(&host->lock, flags);
		ier = sdhci_readl(host, SDHCI_INT_ENABLE);
		sdhci_clear_set_irqs(host, ier, SDHCI_INT_DATA_AVAIL);
	}

	/* find the mininum delay first which can pass tuning */
	min = SD_RX_TUNE_MIN;
	do {
		while (min < regdata->SD_RX_TUNE_MAX) {
			pxav3_prepare_tuning(host, min);
			if (!pxav3_send_tuning_cmd(host, opcode, min, flags))
				break;
			min += SD_RX_TUNE_STEP;
		}

		/* find the maxinum delay which can not pass tuning */
		max = min + SD_RX_TUNE_STEP;
		while (max < regdata->SD_RX_TUNE_MAX) {
			pxav3_prepare_tuning(host, max);
			if (pxav3_send_tuning_cmd(host, opcode, max, flags))
				break;
			max += SD_RX_TUNE_STEP;
		}

		if ((max - min) > len) {
			len = max - min;
			avg = (min + max - 1) / 2;
		}
		pr_info("%s: tuning pass window [%d : %d], len = %d\n",
				mmc_hostname(host->mmc), min, max - 1, max - min);
		min = max + SD_RX_TUNE_STEP;
	} while (min < regdata->SD_RX_TUNE_MAX);

	pxav3_prepare_tuning(host, avg);
	ret = pxav3_send_tuning_cmd(host, opcode, avg, flags);

	if (host->quirks2 & SDHCI_QUIRK2_TUNING_ADMA_BROKEN) {
		sdhci_clear_set_irqs(host, SDHCI_INT_DATA_AVAIL, ier);
		spin_unlock_irqrestore(&host->lock, flags);
	}

	pr_info("%s: tunning %s at %d, pass window length is %d\n",
			mmc_hostname(host->mmc), ret ? "failed" : "passed", avg, len);

	if (!(host->flags & SDHCI_NEEDS_RETUNING) && host->tuning_count &&
			(host->tuning_mode == SDHCI_TUNING_MODE_1)) {
		host->flags |= SDHCI_USING_RETUNING_TIMER;
		mod_timer(&host->tuning_timer, jiffies +
				host->tuning_count * HZ);
	} else {
		host->flags &= ~SDHCI_NEEDS_RETUNING;
		/* Reload the new initial value for timer */
		if (host->tuning_mode == SDHCI_TUNING_MODE_1)
			mod_timer(&host->tuning_timer, jiffies +
					host->tuning_count * HZ);
	}

	return ret;
}

/*
 * remove the caps that supported by the controller but not available
 * for certain platforms.
 */
static void pxav3_host_caps_disable(struct sdhci_host *host)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (pdata->host_caps_disable)
		host->mmc->caps &= ~(pdata->host_caps_disable);
	if (pdata->host_caps2_disable)
		host->mmc->caps2 &= ~(pdata->host_caps2_disable);
}

static const struct sdhci_ops pxav3_sdhci_ops = {
	.platform_reset_exit = pxav3_set_private_registers,
	.set_uhs_signaling = pxav3_set_uhs_signaling,
	.platform_send_init_74_clocks = pxav3_gen_init_74_clocks,
	.get_max_clock = sdhci_pltfm_clk_get_max_clock,
	.clk_prepare = pxav3_clk_prepare,
	.clr_wakeup_event = pxav3_clr_wakeup_event,
	.signal_vol_change = pxav3_signal_vol_change,
	.clk_gate_auto  = pxav3_clk_gate_auto,
	.set_clock = pxav3_set_clock,
	.access_constrain = pxav3_access_constrain,
	.platform_execute_tuning = pxav3_executing_tuning,
	.platform_hw_tuning_prepare = pxav3_hw_tuning_prepare,
	.host_caps_disable = pxav3_host_caps_disable,
};

static struct sdhci_pltfm_data sdhci_pxav3_pdata = {
	.quirks = SDHCI_QUIRK_DATA_TIMEOUT_USES_SDCLK
		| SDHCI_QUIRK_NO_ENDATTR_IN_NOPDESC
		| SDHCI_QUIRK_32BIT_ADMA_SIZE
		| SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN,
	.ops = &pxav3_sdhci_ops,
};

static int pxav3_init_host_with_pdata(struct sdhci_host *host,
		struct sdhci_pxa_platdata *pdata)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	int ret = 0;

	/* If slot design supports 8 bit data, indicate this to MMC. */
	if (pdata->flags & PXA_FLAG_SD_8_BIT_CAPABLE_SLOT)
		host->mmc->caps |= MMC_CAP_8_BIT_DATA;

	if (pdata->flags & PXA_FLAG_DISABLE_PROBE_CDSCAN)
		host->mmc->caps2 |= MMC_CAP2_DISABLE_PROBE_CDSCAN;

	if (pdata->flags & PXA_FLAG_ENABLE_CLOCK_GATING)
		host->mmc->caps2 |= MMC_CAP2_BUS_AUTO_CLK_GATE;

	if (pdata->quirks)
		host->quirks |= pdata->quirks;
	if (pdata->quirks2)
		host->quirks2 |= pdata->quirks2;
	if (pdata->host_caps)
		host->mmc->caps |= pdata->host_caps;
	if (pdata->host_caps2)
		host->mmc->caps2 |= pdata->host_caps2;
	if (pdata->pm_caps)
		host->mmc->pm_caps |= pdata->pm_caps;

	if (pdata->flags & PXA_FLAG_WAKEUP_HOST) {
		device_init_wakeup(&pdev->dev, 1);
		host->mmc->pm_flags |= MMC_PM_WAKE_SDIO_IRQ;
	} else
		device_init_wakeup(&pdev->dev, 0);

	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id sdhci_pxav3_of_match[] = {
	{
		.compatible = "marvell,pxav3-mmc-v1",
		.data = (void *)&pxav3_regdata_v1,
	},
	{
		.compatible = "marvell,pxav3-mmc-v2",
		.data = (void *)&pxav3_regdata_v2,
	},
	{
		.compatible = "marvell,pxav3-mmc-v3",
		.data = (void *)&pxav3_regdata_v3,
	},
	{},
};
MODULE_DEVICE_TABLE(of, sdhci_pxav3_of_match);

static void pxav3_get_of_perperty(struct sdhci_host *host,
		struct device *dev, struct sdhci_pxa_platdata *pdata)
{
	struct device_node *np = dev->of_node;
	u32 tmp;
	struct property *prop;
	const __be32 *p;
	u32 val, timing;
	struct sdhci_pxa_dtr_data *dtr_data;

	if (of_property_read_bool(np, "marvell,sdh-pm-runtime-en"))
		pdata->flags |= PXA_FLAG_EN_PM_RUNTIME;

	if (!of_property_read_u32(np, "marvell,sdh-flags", &tmp))
		pdata->flags |= tmp;

	if (!of_property_read_u32(np, "marvell,sdh-host-caps", &tmp))
		pdata->host_caps |= tmp;
	if (!of_property_read_u32(np, "marvell,sdh-host-caps2", &tmp))
		pdata->host_caps2 |= tmp;
	if (!of_property_read_u32(np, "marvell,sdh-host-caps-disable", &tmp))
		pdata->host_caps_disable |= tmp;
	if (!of_property_read_u32(np, "marvell,sdh-host-caps2-disable", &tmp))
		pdata->host_caps2_disable |= tmp;
	if (!of_property_read_u32(np, "marvell,sdh-quirks", &tmp))
		pdata->quirks |= tmp;
	if (!of_property_read_u32(np, "marvell,sdh-quirks2", &tmp))
		pdata->quirks2 |= tmp;
	if (!of_property_read_u32(np, "marvell,sdh-pm-caps", &tmp))
		pdata->pm_caps |= tmp;
	if (!of_property_read_u32(np, "lpm-qos", &tmp))
		pdata->lpm_qos = tmp;
	else
		pdata->lpm_qos = PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE;

	/* property "marvell,sdh-tuning-cnt": <tuning_wd_cnt tuning_tt_cnt> */
	if (!of_property_read_u32_index(np, "marvell,sdh-tuning-cnt", 1, &tmp))
		host->tuning_wd_cnt = tmp;
	if (!of_property_read_u32_index(np, "marvell,sdh-tuning-cnt", 2, &tmp))
		host->tuning_tt_cnt = tmp;

	/*
	 * property "marvell,sdh-dtr-data": <timing preset_rate src_rate tx_delay rx_delay>, [<..>]
	 * allow to set clock related parameters.
	 */
	if (of_property_read_bool(np, "marvell,sdh-dtr-data")) {
		dtr_data = devm_kzalloc(dev,
				MMC_TIMING_MAX * sizeof(struct sdhci_pxa_dtr_data),
				GFP_KERNEL);
		if (!dtr_data) {
			dev_err(dev, "failed to allocate memory for sdh-dtr-data\n");
			return;
		}
		of_property_for_each_u32(np, "marvell,sdh-dtr-data", prop, p, timing) {
			if (timing > MMC_TIMING_MAX) {
				dev_err(dev, "invalid timing %d on sdh-dtr-data prop\n",
						timing);
				continue;
			} else {
				dtr_data[timing].timing = timing;
			}
			p = of_prop_next_u32(prop, p, &val);
			if (!p) {
				dev_err(dev, "missing preset_rate for timing %d\n",
						timing);
			} else {
				dtr_data[timing].preset_rate = val;
			}
			p = of_prop_next_u32(prop, p, &val);
			if (!p) {
				dev_err(dev, "missing src_rate for timing %d\n",
						timing);
			} else {
				dtr_data[timing].src_rate = val;
			}
			p = of_prop_next_u32(prop, p, &val);
			if (!p) {
				dev_err(dev, "missing tx_delay for timing %d\n",
						timing);
			} else {
				dtr_data[timing].tx_delay = val;
			}
			p = of_prop_next_u32(prop, p, &val);
			if (!p) {
				dev_err(dev, "missing rx_delay for timing %d\n",
						timing);
			} else {
				dtr_data[timing].rx_delay = val;
			}
			p = of_prop_next_u32(prop, p, &val);
			if (!p) {
				dev_err(dev, "missing rx_sdclk_sel0 for timing %d\n",
						timing);
			} else {
				dtr_data[timing].rx_sdclk_sel0 = val;
			}
			p = of_prop_next_u32(prop, p, &val);
			if (!p) {
				dev_err(dev, "missing rx_sdclk_sel1 for timing %d\n",
						timing);
			} else {
				dtr_data[timing].rx_sdclk_sel1 = val;
			}
			p = of_prop_next_u32(prop, p, &val);
			if (!p) {
				dev_err(dev, "missing fakeclk_en for timing %d\n",
						timing);
			} else {
				dtr_data[timing].fakeclk_en = val;
			}
		}
		pdata->dtr_data = dtr_data;
	}
}
#endif

static int sdhci_pxav3_probe(struct platform_device *pdev)
{
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;
	struct device *dev = &pdev->dev;
	struct sdhci_host *host = NULL;
	struct sdhci_pxa *pxa = NULL;
	const struct of_device_id *match;

	int ret = 0;
	struct clk *clk;

	pxa = kzalloc(sizeof(struct sdhci_pxa), GFP_KERNEL);
	if (!pxa)
		return -ENOMEM;

	host = sdhci_pltfm_init(pdev, &sdhci_pxav3_pdata, 0);
	if (IS_ERR(host)) {
		kfree(pxa);
		return PTR_ERR(host);
	}

	host->tuning_pattern = kmalloc(TUNING_PATTERN_SIZE, GFP_KERNEL);
	if (!host->tuning_pattern)
		goto err_pattern;

	pltfm_host = sdhci_priv(host);
	pltfm_host->priv = pxa;

	/* If sdh-axi-clk doesn't exist on some platform, warn and go ahead. */
	clk = devm_clk_get(dev, "sdh-axi-clk");
	if (IS_ERR(clk)) {
		dev_warn(dev, "failed to get axi clock\n");
	} else {
		pxa->axi_clk = clk;
		clk_prepare_enable(clk);
	}

	clk = devm_clk_get(dev, "sdh-base-clk");
	if (IS_ERR(clk)) {
		dev_err(dev, "failed to get io clock\n");
		ret = PTR_ERR(clk);
		goto err_clk_get;
	}
	pltfm_host->clk = clk;
	pxa->clk = clk;
	clk_prepare_enable(clk);

	host->quirks2 = SDHCI_QUIRK2_TIMEOUT_DIVIDE_4
		| SDHCI_QUIRK2_NO_CURRENT_LIMIT
		| SDHCI_QUIRK2_PRESET_VALUE_BROKEN;

	match = of_match_device(of_match_ptr(sdhci_pxav3_of_match), &pdev->dev);
	if (match) {
		ret = mmc_of_parse(host->mmc);
		if (ret)
			goto err_of_parse;
		sdhci_get_of_property(pdev);
		if (!pdata) {
			pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
			if (!pdata) {
				dev_err(mmc_dev(host->mmc),
					"failed to alloc pdata\n");
				goto err_init_host;
			}
			pdev->dev.platform_data = pdata;
		}
		pxav3_get_of_perperty(host, dev, pdata);
		pdata->regdata = match->data;
	}
	if (pdata) {
		pdata->pinctrl = devm_pinctrl_get(dev);
		if (IS_ERR(pdata->pinctrl))
			dev_err(dev, "could not get pinctrl handle\n");
		pdata->pin_slow = pinctrl_lookup_state(pdata->pinctrl, "default");
		if (IS_ERR(pdata->pin_slow))
			dev_err(dev, "could not get default pinstate\n");
		pdata->pin_fast = pinctrl_lookup_state(pdata->pinctrl, "fast");
		if (IS_ERR(pdata->pin_fast))
			dev_info(dev, "could not get fast pinstate\n");

		ret = pxav3_init_host_with_pdata(host, pdata);
		if (ret) {
			dev_err(mmc_dev(host->mmc),
					"failed to init host with pdata\n");
			goto err_init_host;
		}
		pm_qos_add_request(&pdata->qos_idle, PM_QOS_CPUIDLE_BLOCK,
			PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
		if (pdata->flags & PXA_FLAG_EN_PM_RUNTIME) {
			pm_runtime_get_noresume(&pdev->dev);
			pm_runtime_set_active(&pdev->dev);
			pm_runtime_set_autosuspend_delay(&pdev->dev,
				PXAV3_RPM_DELAY_MS);
			pm_runtime_use_autosuspend(&pdev->dev);
			pm_suspend_ignore_children(&pdev->dev, 1);
			pm_runtime_enable(&pdev->dev);
		}
	}

	/* dma only 32 bit now */
	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	ret = sdhci_add_host(host);
	if (ret) {
		dev_err(&pdev->dev, "failed to add host\n");
		goto err_add_host;
	}

	platform_set_drvdata(pdev, host);

	if (host->mmc->pm_caps & MMC_PM_KEEP_POWER) {
		device_init_wakeup(&pdev->dev, 1);
		host->mmc->pm_flags |= MMC_PM_WAKE_SDIO_IRQ;
	} else {
		device_init_wakeup(&pdev->dev, 0);
	}
	if (pdata && pdata->flags & PXA_FLAG_EN_PM_RUNTIME)
		pm_runtime_put_autosuspend(&pdev->dev);

	pxa->tx_delay.store = tx_delay_store;
	pxa->tx_delay.show = tx_delay_show;
	sysfs_attr_init(&pxa->tx_delay.attr);
	pxa->tx_delay.attr.name = "tx_delay";
	pxa->tx_delay.attr.mode = S_IRUGO | S_IWUSR;
	ret = device_create_file(&host->mmc->class_dev, &pxa->tx_delay);

	pxa->rx_delay.store = rx_delay_store;
	pxa->rx_delay.show = rx_delay_show;
	sysfs_attr_init(&pxa->rx_delay.attr);
	pxa->rx_delay.attr.name = "rx_delay";
	pxa->rx_delay.attr.mode = S_IRUGO | S_IWUSR;
	ret = device_create_file(&host->mmc->class_dev, &pxa->rx_delay);


	return 0;

err_add_host:
	if (pdata && pdata->flags & PXA_FLAG_EN_PM_RUNTIME) {
		pm_runtime_put_noidle(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
	}
err_init_host:
err_of_parse:
	clk_disable_unprepare(pxa->clk);
	if (pdata)
		pm_qos_remove_request(&pdata->qos_idle);
err_clk_get:
	clk_disable_unprepare(pxa->axi_clk);
	kfree(host->tuning_pattern);
err_pattern:
	sdhci_pltfm_free(pdev);
	kfree(pxa);
	return ret;
}

static int sdhci_pxav3_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	pm_runtime_get_sync(&pdev->dev);
	sdhci_remove_host(host, 1);
	pm_runtime_disable(&pdev->dev);

	if (pdata)
		pm_qos_remove_request(&pdata->qos_idle);

	clk_disable_unprepare(pxa->clk);
	clk_disable_unprepare(pxa->axi_clk);

	kfree(host->tuning_pattern);

	sdhci_pltfm_free(pdev);
	kfree(pxa);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_pxav3_suspend(struct device *dev)
{
	int ret;
	struct sdhci_host *host = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	ret = sdhci_suspend_host(host);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}

static int sdhci_pxav3_resume(struct device *dev)
{
	int ret;
	struct sdhci_host *host = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	ret = sdhci_resume_host(host);
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return ret;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int sdhci_pxav3_runtime_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	unsigned long flags;

	if (pxa->clk) {
		spin_lock_irqsave(&host->lock, flags);
		host->runtime_suspended = true;
		spin_unlock_irqrestore(&host->lock, flags);

		clk_disable_unprepare(pxa->clk);
		clk_disable_unprepare(pxa->axi_clk);
	}

	return 0;
}

static int sdhci_pxav3_runtime_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;
	unsigned long flags;

	if (pxa->clk) {
		/* If axi_clk == NULL, do nothing */
		clk_prepare_enable(pxa->axi_clk);
		clk_prepare_enable(pxa->clk);

		spin_lock_irqsave(&host->lock, flags);
		host->runtime_suspended = false;
		spin_unlock_irqrestore(&host->lock, flags);
	}

	return 0;
}
#endif

#ifdef CONFIG_PM
static const struct dev_pm_ops sdhci_pxav3_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_pxav3_suspend, sdhci_pxav3_resume)
	SET_RUNTIME_PM_OPS(sdhci_pxav3_runtime_suspend,
		sdhci_pxav3_runtime_resume, NULL)
};

#define SDHCI_PXAV3_PMOPS (&sdhci_pxav3_pmops)

#else
#define SDHCI_PXAV3_PMOPS NULL
#endif

static struct platform_driver sdhci_pxav3_driver = {
	.driver		= {
		.name	= "sdhci-pxav3",
#ifdef CONFIG_OF
		.of_match_table = sdhci_pxav3_of_match,
#endif
		.owner	= THIS_MODULE,
		.pm	= SDHCI_PXAV3_PMOPS,
	},
	.probe		= sdhci_pxav3_probe,
	.remove		= sdhci_pxav3_remove,
};

module_platform_driver(sdhci_pxav3_driver);

MODULE_DESCRIPTION("SDHCI driver for pxav3");
MODULE_AUTHOR("Marvell International Ltd.");
MODULE_LICENSE("GPL v2");

