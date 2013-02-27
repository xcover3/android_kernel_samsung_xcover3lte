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

static void pxav3_set_private_registers(struct sdhci_host *host, u8 mask)
{
	struct platform_device *pdev = to_platform_device(mmc_dev(host->mmc));
	struct sdhci_pxa_platdata *pdata = pdev->dev.platform_data;

	if (mask == SDHCI_RESET_ALL) {
		/*
		 * tune timing of read data/command when crc error happen
		 * no performance impact
		 */
		if (pdata && 0 != pdata->clk_delay_cycles) {
			u16 tmp;

			tmp = readw(host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);
			tmp |= (pdata->clk_delay_cycles & SDCLK_DELAY_MASK)
				<< SDCLK_DELAY_SHIFT;
			tmp |= SDCLK_SEL;
			writew(tmp, host->ioaddr + SD_CLOCK_BURST_SIZE_SETUP);
		}
	}
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

static int pxav3_set_uhs_signaling(struct sdhci_host *host, unsigned int uhs)
{
	u16 ctrl_2;

	/*
	 * Set V18_EN -- UHS modes do not work without this.
	 * does not change signaling voltage
	 */
	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);

	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
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
		break;
	case MMC_TIMING_UHS_DDR50:
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50 | SDHCI_CTRL_VDD_180;
		break;
	}

	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
	dev_dbg(mmc_dev(host->mmc),
		"%s uhs = %d, ctrl_2 = %04X\n",
		__func__, uhs, ctrl_2);

	return 0;
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
	int ret = 0;

	/* If slot design supports 8 bit data, indicate this to MMC. */
	if (pdata->flags & PXA_FLAG_SD_8_BIT_CAPABLE_SLOT)
		host->mmc->caps |= MMC_CAP_8_BIT_DATA;

	if (pdata->flags & PXA_FLAG_DISABLE_PROBE_CDSCAN)
		host->mmc->caps2 |= MMC_CAP2_DISABLE_PROBE_CDSCAN;

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
		| SDHCI_QUIRK2_NO_CURRENT_LIMIT;

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
		ret = pxav3_init_host_with_pdata(host, pdata);
		if (ret) {
			dev_err(mmc_dev(host->mmc),
					"failed to init host with pdata\n");
			goto err_init_host;
		}
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

	return 0;

err_add_host:
	if (pdata && pdata->flags & PXA_FLAG_EN_PM_RUNTIME) {
		pm_runtime_put_noidle(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
	}
err_init_host:
err_of_parse:
	clk_disable_unprepare(pxa->clk);
err_clk_get:
	clk_disable_unprepare(pxa->axi_clk);
	sdhci_pltfm_free(pdev);
	kfree(pxa);
	return ret;
}

static int sdhci_pxav3_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_pxa *pxa = pltfm_host->priv;

	pm_runtime_get_sync(&pdev->dev);
	sdhci_remove_host(host, 1);
	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(pxa->clk);
	clk_disable_unprepare(pxa->axi_clk);

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

