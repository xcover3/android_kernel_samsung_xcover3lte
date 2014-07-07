#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <dt-bindings/clock/marvell-pxa1928.h>

#include "clk.h"

#define APBC_RTC		0x0
#define APBC_TWSI0		0x4
#define APBC_TWSI1		0x8
#define APBC_TWSI2		0xc
#define APBC_TWSI3		0x10
#define APBC_TWSI4		0x7c
#define APBC_TWSI5		0x80
#define APBC_KPC		0x18
#define APBC_UART0		0x2c
#define APBC_UART1		0x30
#define APBC_UART2		0x34
#define APBC_UART3		0x88
#define APBC_GPIO		0x38
#define APBC_PWM0		0x3c
#define APBC_PWM1		0x40
#define APBC_PWM2		0x44
#define APBC_PWM3		0x48

#define MPMU_UART_PLL		0x14

#define APMU_SDH0		0x54
#define APMU_SDH1		0x58
#define APMU_SDH2		0xe8
#define APMU_SDH3		0xec
#define APMU_SDH4		0x15c
#define APMU_USB		0x5c

struct pxa1928_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;
	void __iomem *ciu_base;
};

static struct mmp_param_fixed_rate_clk fixed_rate_clks[] = {
	{PXA1928_CLK_CLK32, "clk32", NULL, CLK_IS_ROOT, 32768},
	{PXA1928_CLK_VCTCXO, "vctcxo", NULL, CLK_IS_ROOT, 26000000},
	{PXA1928_CLK_PLL1_624, "pll1_624", NULL, CLK_IS_ROOT, 624000000},
	{PXA1928_CLK_PLL1_416, "pll1_416", NULL, CLK_IS_ROOT, 416000000},
	{PXA1928_CLK_USB_PLL, "usb_pll", NULL, CLK_IS_ROOT, 480000000},
};

static struct mmp_param_fixed_factor_clk fixed_factor_clks[] = {
	{PXA1928_CLK_VCTCXO_2, "vctcxo_2", "vctcxo", 1, 2, 0},
	{PXA1928_CLK_VCTCXO_4, "vctcxo_4", "vctcxo", 1, 4, 0},
	{PXA1928_CLK_PLL1_2, "pll1_2", "pll1_624", 1, 2, 0},
	{PXA1928_CLK_PLL1_9, "pll1_9", "pll1_624", 1, 9, 0},
	{PXA1928_CLK_PLL1_12, "pll1_12", "pll1_624", 1, 12, 0},
	{PXA1928_CLK_PLL1_16, "pll1_16", "pll1_624", 1, 16, 0},
	{PXA1928_CLK_PLL1_20, "pll1_20", "pll1_624", 1, 20, 0},
};

static struct mmp_clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = 0x1fff,
	.den_mask = 0x1fff,
	.num_shift = 16,
	.den_shift = 0,
};

static struct mmp_clk_factor_tbl uart_factor_tbl[] = {
	{.num = 832, .den = 234},     /*14.745MHZ */
	{.num = 1, .den = 1},
};

static void pxa1928_pll_init(struct pxa1928_clk_unit *pxa_unit)
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
	mmp_clk_add(unit, PXA1928_CLK_UART_PLL, clk);
}

static struct mmp_param_gate_clk apbc_gate_clks[] = {
	{PXA1928_CLK_TWSI0, "twsi0_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI0, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_TWSI1, "twsi1_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI1, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_TWSI2, "twsi2_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI2, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_TWSI3, "twsi3_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI3, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_TWSI4, "twsi4_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI4, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_TWSI5, "twsi5_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_TWSI5, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_GPIO, "gpio_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_GPIO, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_KPC, "kpc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_KPC, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1928_CLK_RTC, "rtc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_RTC, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1928_CLK_PWM0, "pwm0_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_PWM0, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_PWM1, "pwm1_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_PWM1, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_PWM2, "pwm2_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_PWM2, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1928_CLK_PWM3, "pwm3_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_PWM3, 0x7, 0x3, 0x0, 0, NULL},
};

static DEFINE_SPINLOCK(uart0_lock);
static DEFINE_SPINLOCK(uart1_lock);
static DEFINE_SPINLOCK(uart2_lock);
static DEFINE_SPINLOCK(uart3_lock);
static const char *uart_parent_names[] = {"uart_pll", "vctcxo"};

static void pxa1928_apb_periph_clk_init(struct pxa1928_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_gate_clks(unit, apbc_gate_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_gate_clks));

	clk = clk_register_mux(NULL, "uart0_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART0,
				4, 1, 0, &uart0_lock);
	clk = mmp_clk_register_gate(NULL, "uart0_clk", "uart0_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART0,
				0x7, 0x3, 0x0, 0, &uart0_lock);
	mmp_clk_add(unit, PXA1928_CLK_UART0, clk);

	clk = clk_register_mux(NULL, "uart1_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART1,
				4, 1, 0, &uart1_lock);
	clk = mmp_clk_register_gate(NULL, "uart1_clk", "uart1_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART0,
				0x7, 0x3, 0x0, 0, &uart1_lock);
	mmp_clk_add(unit, PXA1928_CLK_UART1, clk);

	clk = clk_register_mux(NULL, "uart2_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART2,
				4, 1, 0, &uart2_lock);
	clk = mmp_clk_register_gate(NULL, "uart2_clk", "uart2_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART2,
				0x7, 0x3, 0x0, 0, &uart2_lock);
	mmp_clk_add(unit, PXA1928_CLK_UART2, clk);

	clk = clk_register_mux(NULL, "uart3_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART3,
				4, 1, 0, &uart2_lock);
	clk = mmp_clk_register_gate(NULL, "uart3_clk", "uart3_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART3,
				0x7, 0x3, 0x0, 0, &uart3_lock);
	mmp_clk_add(unit, PXA1928_CLK_UART3, clk);
}

static DEFINE_SPINLOCK(sdh0_lock);
static const char *sdh_parent_names[] = {"pll1_624", "pll5p", "pll5", "pll1_416"};
static struct mmp_clk_mix_config sdh_mix_config = {
	.reg_info = DEFINE_MIX_REG_INFO(4, 10, 2, 8, 32),
};

static void pxa1928_axi_periph_clk_init(struct pxa1928_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	clk = mmp_clk_register_gate(NULL, "usb_clk", "usb_pll", 0,
				pxa_unit->apmu_base + APMU_USB,
				0x9, 0x9, 0x1, 0, NULL);
	mmp_clk_add(unit, PXA1928_CLK_USB, clk);

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH0;
	clk = mmp_clk_register_mix(NULL, "sdh_mix_clk", sdh_parent_names,
				ARRAY_SIZE(sdh_parent_names),
				CLK_SET_RATE_PARENT,
				&sdh_mix_config, &sdh0_lock);

	clk = mmp_clk_register_gate(NULL, "sdh0_clk", "sdh_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH0,
				0x1b, 0x1b, 0x0, 0, &sdh0_lock);
	mmp_clk_add(unit, PXA1928_CLK_SDH0, clk);

	clk = mmp_clk_register_gate(NULL, "sdh1_clk", "sdh_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH1,
				0x1b, 0x1b, 0x0, 0, NULL);
	mmp_clk_add(unit, PXA1928_CLK_SDH1, clk);

	clk = mmp_clk_register_gate(NULL, "sdh2_clk", "sdh_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH2,
				0x1b, 0x1b, 0x0, 0, NULL);
	mmp_clk_add(unit, PXA1928_CLK_SDH2, clk);
}

static void __init pxa1928_clk_init(struct device_node *np)
{
	struct pxa1928_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit) {
		pr_err("failed to allocate memory for pxa1928 clock unit\n");
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

	pxa_unit->ciu_base = of_iomap(np, 3);
	if (!pxa_unit->ciu_base) {
		pr_err("failed to map ciu registers\n");
		return;
	}

	mmp_clk_init(np, &pxa_unit->unit, PXA1928_NR_CLKS);

	pxa1928_pll_init(pxa_unit);

	pxa1928_apb_periph_clk_init(pxa_unit);

	pxa1928_axi_periph_clk_init(pxa_unit);
}

CLK_OF_DECLARE(pxa1928_clk, "marvell,pxa1928-clock", pxa1928_clk_init);
