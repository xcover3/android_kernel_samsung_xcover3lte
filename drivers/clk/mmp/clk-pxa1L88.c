#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <dt-bindings/clock/marvell-pxa1L88.h>

#include "clk.h"

#define APBS_PLL1_CTRL		0x100

#define APBC_RTC		0x28
#define APBC_TWSI0		0x2c
#define APBC_TWSI1		0x60
#define APBC_KPC		0x30
#define APBC_UART0		0x0
#define APBC_UART1		0x4
#define APBC_GPIO		0x8

#define APBCP_TWSI2		0x28
#define APBCP_UART2		0x1c

#define MPMU_UART_PLL		0x14

#define APMU_CLK_GATE_CTRL	0x40
#define APMU_SDH0		0x54
#define APMU_SDH1		0x58
#define APMU_SDH2		0xe0
#define APMU_USB		0x5c

struct pxa1L88_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;
	void __iomem *apbcp_base;
	void __iomem *apbs_base;
	void __iomem *ciu_base;
};

static struct mmp_param_fixed_rate_clk fixed_rate_clks[] = {
	{PXA1L88_CLK_CLK32, "clk32", NULL, CLK_IS_ROOT, 32768},
	{PXA1L88_CLK_VCTCXO, "vctcxo", NULL, CLK_IS_ROOT, 26000000},
	{PXA1L88_CLK_PLL1_624, "pll1_624", NULL, CLK_IS_ROOT, 624000000},
	{PXA1L88_CLK_PLL1_416, "pll1_416", NULL, CLK_IS_ROOT, 416000000},
	{PXA1L88_CLK_PLL1_832, "pll1_832", NULL, CLK_IS_ROOT, 832000000},
	{PXA1L88_CLK_PLL1_1248, "pll1_1248", NULL, CLK_IS_ROOT, 1248000000},
};

static struct mmp_param_fixed_factor_clk fixed_factor_clks[] = {
	{PXA1L88_CLK_PLL1_2, "pll1_2", "pll1_624", 1, 2, 0},
	{PXA1L88_CLK_PLL1_4, "pll1_4", "pll1_2", 1, 2, 0},
	{PXA1L88_CLK_PLL1_8, "pll1_8", "pll1_4", 1, 2, 0},
	{PXA1L88_CLK_PLL1_16, "pll1_16", "pll1_8", 1, 2, 0},
	{PXA1L88_CLK_PLL1_6, "pll1_6", "pll1_2", 1, 3, 0},
	{PXA1L88_CLK_PLL1_12, "pll1_12", "pll1_6", 1, 2, 0},
	{PXA1L88_CLK_PLL1_24, "pll1_24", "pll1_12", 1, 2, 0},
	{PXA1L88_CLK_PLL1_48, "pll1_48", "pll1_24", 1, 2, 0},
	{PXA1L88_CLK_PLL1_96, "pll1_96", "pll1_48", 1, 2, 0},
	{PXA1L88_CLK_PLL1_13, "pll1_13", "pll1_624", 1, 13, 0},
	{PXA1L88_CLK_PLL1_13_1_5, "pll1_13_1_5", "pll1_13", 2, 3, 0},
	{PXA1L88_CLK_PLL1_2_1_5, "pll1_2_1_5", "pll1_2", 2, 3, 0},
	{PXA1L88_CLK_PLL1_3_16, "pll1_3_16", "pll1_624", 3, 16, 0},
};

static struct mmp_clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = 0x1fff,
	.den_mask = 0x1fff,
	.num_shift = 16,
	.den_shift = 0,
};

static struct mmp_clk_factor_tbl uart_factor_tbl[] = {
	{.num = 8125, .den = 1536},     /*14.745MHZ */
};

static DEFINE_SPINLOCK(pll1_lock);
static struct mmp_param_general_gate_clk pll1_gate_clks[] = {
	{PXA1L88_CLK_PLL1_416_GATE, "pll1_416_gate", "pll1_416", 0, APMU_CLK_GATE_CTRL, 27, 0, &pll1_lock},
	{PXA1L88_CLK_PLL1_624_GATE, "pll1_624_gate", "pll1_624", 0, APMU_CLK_GATE_CTRL, 26, 0, &pll1_lock},
};

static void pxa1L88_pll_init(struct pxa1L88_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_fixed_rate_clks(unit, fixed_rate_clks,
					ARRAY_SIZE(fixed_rate_clks));

	mmp_register_fixed_factor_clks(unit, fixed_factor_clks,
					ARRAY_SIZE(fixed_factor_clks));

	clk = mmp_clk_register_factor("uart_pll", "pll1_4",
				CLK_SET_RATE_PARENT,
				pxa_unit->mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl), NULL);
	mmp_clk_add(unit, PXA1L88_CLK_UART_PLL, clk);

	mmp_register_general_gate_clks(unit, pll1_gate_clks,
				pxa_unit->apmu_base,
				ARRAY_SIZE(pll1_gate_clks));
}

static struct mmp_param_gate_clk apbc_gate_clks[] = {
	{PXA1L88_CLK_GPIO, "gpio_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_GPIO, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1L88_CLK_KPC, "kpc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_KPC, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1L88_CLK_RTC, "rtc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_RTC, 0x87, 0x83, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
};

static DEFINE_SPINLOCK(uart0_lock);
static DEFINE_SPINLOCK(uart1_lock);
static DEFINE_SPINLOCK(uart2_lock);
static const char *uart_parent_names[] = {"pll1_3_16", "uart_pll"};

static DEFINE_SPINLOCK(twsi0_lock);
static DEFINE_SPINLOCK(twsi1_lock);
static DEFINE_SPINLOCK(twsi2_lock);
static const struct clk_div_table clk_twsi_ref_table[] = {
	{ .val = 0, .div = 19 },
	{ .val = 1, .div = 12 },
	{ .val = 2, .div = 10 },
	{ .val = 0, .div = 0 },
};

static void pxa1L88_apb_periph_clk_init(struct pxa1L88_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_gate_clks(unit, apbc_gate_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_gate_clks));

	clk = clk_register_divider_table(NULL, "twsi0_div",
				"pll1_624", CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_TWSI0, 4, 3, 0,
				clk_twsi_ref_table, &twsi0_lock);
	clk = mmp_clk_register_gate(NULL, "twsi0_clk", "twsi0_div",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_TWSI0,
				0x7, 0x3, 0x0, 0, &twsi0_lock);
	mmp_clk_add(unit, PXA1L88_CLK_TWSI0, clk);

	clk = clk_register_divider_table(NULL, "twsi1_div",
				"pll1_624", CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_TWSI1, 4, 3, 0,
				clk_twsi_ref_table, &twsi1_lock);
	clk = mmp_clk_register_gate(NULL, "twsi1_clk", "twsi1_div",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_TWSI1,
				0x7, 0x3, 0x0, 0, &twsi1_lock);
	mmp_clk_add(unit, PXA1L88_CLK_TWSI1, clk);

	clk = clk_register_divider_table(NULL, "twsi2_div",
				"pll1_624", CLK_SET_RATE_PARENT,
				pxa_unit->apbcp_base + APBCP_TWSI2, 4, 3, 0,
				clk_twsi_ref_table, &twsi2_lock);
	clk = mmp_clk_register_gate(NULL, "twsi2_clk", "twsi2_div",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbcp_base + APBCP_TWSI2,
				0x7, 0x3, 0x0, 0, &twsi2_lock);
	mmp_clk_add(unit, PXA1L88_CLK_TWSI2, clk);

	clk = clk_register_mux(NULL, "uart0_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART0,
				4, 3, 0, &uart0_lock);
	clk = mmp_clk_register_gate(NULL, "uart0_clk", "uart0_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART0,
				0x7, 0x3, 0x0, 0, &uart0_lock);
	mmp_clk_add(unit, PXA1L88_CLK_UART0, clk);

	clk = clk_register_mux(NULL, "uart1_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART1,
				4, 3, 0, &uart1_lock);
	clk = mmp_clk_register_gate(NULL, "uart1_clk", "uart1_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART1,
				0x7, 0x3, 0x0, 0, &uart1_lock);
	mmp_clk_add(unit, PXA1L88_CLK_UART1, clk);

	clk = clk_register_mux(NULL, "uart2_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbcp_base + APBCP_UART2,
				4, 3, 0, &uart2_lock);
	clk = mmp_clk_register_gate(NULL, "uart2_clk", "uart2_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbcp_base + APBCP_UART2,
				0x7, 0x3, 0x0, 0, &uart2_lock);
	mmp_clk_add(unit, PXA1L88_CLK_UART2, clk);
}

static DEFINE_SPINLOCK(sdh0_lock);
static DEFINE_SPINLOCK(sdh1_lock);
static DEFINE_SPINLOCK(sdh2_lock);
static const char *sdh_parent_names[] = {"pll1_416", "pll1_624"};
static struct mmp_clk_mix_config sdh_mix_config = {
	.reg_info = DEFINE_MIX_REG_INFO(3, 8, 1, 6, 11),
};

static void pxa1L88_axi_periph_clk_init(struct pxa1L88_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	clk = mmp_clk_register_gate(NULL, "usb_clk", NULL, 0,
				pxa_unit->apmu_base + APMU_USB,
				0x9, 0x9, 0x1, 0, NULL);
	mmp_clk_add(unit, PXA1L88_CLK_USB, clk);

	clk = mmp_clk_register_gate(NULL, "sdh_axi_clk", NULL, 0,
				pxa_unit->apmu_base + APMU_SDH0,
				0x8, 0x8, 0x0, 0, &sdh0_lock);
	mmp_clk_add(unit, PXA1L88_CLK_SDH_AXI, clk);

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH0;
	clk = mmp_clk_register_mix(NULL, "sdh0_mix_clk", sdh_parent_names,
				ARRAY_SIZE(sdh_parent_names),
				CLK_SET_RATE_PARENT,
				&sdh_mix_config, &sdh0_lock);
	clk = mmp_clk_register_gate(NULL, "sdh0_clk", "sdh0_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH0,
				0x12, 0x12, 0x0, 0, &sdh0_lock);
	mmp_clk_add(unit, PXA1L88_CLK_SDH0, clk);

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH1;
	clk = mmp_clk_register_mix(NULL, "sdh1_mix_clk", sdh_parent_names,
				ARRAY_SIZE(sdh_parent_names),
				CLK_SET_RATE_PARENT,
				&sdh_mix_config, &sdh1_lock);
	clk = mmp_clk_register_gate(NULL, "sdh1_clk", "sdh1_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH1,
				0x12, 0x12, 0x0, 0, &sdh1_lock);
	mmp_clk_add(unit, PXA1L88_CLK_SDH1, clk);

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH2;
	clk = mmp_clk_register_mix(NULL, "sdh2_mix_clk", sdh_parent_names,
				ARRAY_SIZE(sdh_parent_names),
				CLK_SET_RATE_PARENT,
				&sdh_mix_config, &sdh2_lock);
	clk = mmp_clk_register_gate(NULL, "sdh2_clk", "sdh2_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH2,
				0x12, 0x12, 0x0, 0, &sdh2_lock);
	mmp_clk_add(unit, PXA1L88_CLK_SDH2, clk);
}

static void __init pxa1L88_clk_init(struct device_node *np)
{
	struct pxa1L88_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit) {
		pr_err("failed to allocate memory for pxa1L88 clock unit\n");
		return;
	}

	pxa_unit->mpmu_base = of_iomap(np, 0);
	if (!pxa_unit->mpmu_base) {
		pr_err("failed to map mpmu registers\n");
		return;
	}

	pxa_unit->apmu_base = of_iomap(np, 1);
	if (!pxa_unit->mpmu_base) {
		pr_err("failed to map apmu registers\n");
		return;
	}

	pxa_unit->apbc_base = of_iomap(np, 2);
	if (!pxa_unit->apbc_base) {
		pr_err("failed to map apbc registers\n");
		return;
	}

	pxa_unit->apbcp_base = of_iomap(np, 3);
	if (!pxa_unit->mpmu_base) {
		pr_err("failed to map apbcp registers\n");
		return;
	}

	pxa_unit->apbs_base = of_iomap(np, 4);
	if (!pxa_unit->apbs_base) {
		pr_err("failed to map apbs registers\n");
		return;
	}

	pxa_unit->ciu_base = of_iomap(np, 5);
	if (!pxa_unit->ciu_base) {
		pr_err("failed to map ciu registers\n");
		return;
	}

	mmp_clk_init(np, &pxa_unit->unit, PXA1L88_NR_CLKS);

	pxa1L88_pll_init(pxa_unit);

	pxa1L88_apb_periph_clk_init(pxa_unit);

	pxa1L88_axi_periph_clk_init(pxa_unit);
}

CLK_OF_DECLARE(pxa1L88_clk, "marvell,pxa1L88-clock", pxa1L88_clk_init);
