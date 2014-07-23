/*
 * display clock framework source file
 *
 * Copyright (C) 2014 Marvell
 * huang yonghai <huangyh@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/devfreq.h>

#include "clk.h"

#define LCD_PN_SCLK	(0xd420b1a8)
static DEFINE_SPINLOCK(disp_lock);

static const char *dsisclk_parent[] = {"disp1_sel_clk", "dsipll"};
static const char *dsipll_parent[] = {"pll3"};
static const char *dsi_parent[] = {"dsi_sclk"};
static u32 dsi_mux_tbl[] = {1, 3};

struct mmp_clk_disp {
	struct clk_hw	hw;
	struct clk_mux mux;
	struct clk_divider divider;
	struct mmp_clk_gate2 gate2;
	struct mmp_clk_gate gate;
	spinlock_t *lock;
	const struct clk_ops *mux_ops;
	const struct clk_ops *div_ops;
	const struct clk_ops *gate_ops;
};

static struct mmp_clk_disp pnpath = {
	.div_ops = &clk_divider_ops,
	.divider.width = 8,
	.divider.shift = 0,
	.divider.reg = 0,
	.divider.flags = 0,
	.divider.lock = &disp_lock,
	.gate.mask = 0x10000000,
	.gate.lock = &disp_lock,
	.gate.mask = 0x10000000,
	.gate.val_enable = 0x0,
	.gate.val_disable = 0x10000000,
	.gate.flags = 0x0,
	.gate.lock = &disp_lock,
	.gate_ops = &mmp_clk_gate_ops,
};

static struct mmp_clk_disp dsipath = {
	.div_ops = &clk_divider_ops,
	.divider.width = 4,
	.divider.shift = 8,
	.divider.reg = 0,
	.divider.flags = 0,
	.divider.lock = &disp_lock,
	.gate2.mask = 0xf00,
	.gate2.lock = &disp_lock,
	.gate2.val_disable = 0x0,
	.gate2.val_enable = 0x100,
	.gate2.val_shadow = 0x100,
	.gate2.flags = 0x0,
	.gate2.lock = &disp_lock,
	.gate_ops = &mmp_clk_gate2_ops,
};

void mmp_display_clk_init(void)
{
	struct clk *clk;
	void __iomem *pn_sclk_reg = ioremap(LCD_PN_SCLK, 4);

	if (!pn_sclk_reg)
		pr_err("%s, mmp pn_sclk_reg map error\n", __func__);

	clk = clk_register_mux_table(NULL, "dsipll", dsipll_parent,
		ARRAY_SIZE(dsipll_parent),
		0,
		pn_sclk_reg, 29, 7, 0, dsi_mux_tbl, &disp_lock);
	if (IS_ERR(clk))
		pr_err("%s, mmp register dsipll clk error\n", __func__);
	clk_register_clkdev(clk, "dsipll", NULL);

	clk = clk_register_mux_table(NULL, "dsi_sclk", dsisclk_parent,
		ARRAY_SIZE(dsisclk_parent),
		CLK_SET_RATE_PARENT,
		pn_sclk_reg, 29, 7, 0, dsi_mux_tbl, &disp_lock);
	if (IS_ERR(clk))
		pr_err("%s, mmp register dsisclk clk error\n", __func__);
	clk_register_clkdev(clk, "dsi_sclk", NULL);

	pnpath.divider.reg = pn_sclk_reg;
	pnpath.gate.reg = pn_sclk_reg;
	clk = clk_register_composite(NULL, "mmp_pnpath", dsi_parent,
				ARRAY_SIZE(dsi_parent),
				NULL, NULL,
				&pnpath.divider.hw, pnpath.div_ops,
				&pnpath.gate.hw, pnpath.gate_ops,
				0);
	if (IS_ERR(clk))
		pr_err("%s, mmp register mmp_pnpath clk error\n", __func__);
	clk_register_clkdev(clk, "mmp_pnpath", NULL);

	dsipath.divider.reg = pn_sclk_reg;
	dsipath.gate2.reg = pn_sclk_reg;
	clk = clk_register_composite(NULL, "mmp_dsi1", dsi_parent,
				ARRAY_SIZE(dsi_parent),
				NULL, NULL,
				&dsipath.divider.hw, dsipath.div_ops,
				&dsipath.gate2.hw, dsipath.gate_ops,
				CLK_SET_RATE_PARENT);
	if (IS_ERR(clk))
		pr_err("%s, mmp register mmp_dsi1 clk error\n", __func__);
	clk_register_clkdev(clk, "mmp_dsi1", NULL);

}
