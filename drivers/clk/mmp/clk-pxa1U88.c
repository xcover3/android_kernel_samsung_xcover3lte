#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <dt-bindings/clock/marvell-pxa1U88.h>

#include "clk.h"
#include "clk-pll-helanx.h"
#include "clk-core-helanx.h"

#define APBS_PLL1_CTRL		0x100

#define APBC_RTC		0x28
#define APBC_TWSI0		0x2c
#define APBC_TWSI1		0x60
#define APBC_TWSI3		0x70
#define APBC_KPC		0x30
#define APBC_UART0		0x0
#define APBC_UART1		0x4
#define APBC_GPIO		0x8
#define APBC_PWM0		0xc
#define APBC_PWM1		0x10
#define APBC_PWM2		0x14
#define APBC_PWM3		0x18
#define APBC_SSP0		0x1c
#define APBC_SSP1		0x20
#define APBC_SSP2		0x4c
#define APBC_TERMAL		0x6c

#define APBCP_TWSI2		0x28
#define APBCP_UART2		0x1c

#define MPMU_UART_PLL		0x14

#define APMU_CLK_GATE_CTRL	0x40
#define APMU_SDH0		0x54
#define APMU_SDH1		0x58
#define APMU_SDH2		0xe0
#define APMU_USB		0x5c
#define APMU_TRACE		0x108

/* PLL */
#define MPMU_PLL2CR	   (0x0034)
#define MPMU_PLL3CR	   (0x001c)
#define MPMU_PLL4CR	   (0x0050)
#define MPMU_POSR	   (0x0010)
#define POSR_PLL2_LOCK	   (1 << 29)
#define POSR_PLL3_LOCK	   (1 << 30)
#define POSR_PLL4_LOCK	   (1 << 31)

#define APB_SPARE_PLL2CR    (0x104)
#define APB_SPARE_PLL3CR    (0x108)
#define APB_SPARE_PLL4CR    (0x124)
#define APB_PLL2_SSC_CTRL   (0x130)
#define APB_PLL2_SSC_CONF   (0x134)
#define APB_PLL2_FREQOFFSET_CTRL    (0x138)
#define APB_PLL3_SSC_CTRL   (0x13c)
#define APB_PLL3_SSC_CONF   (0x140)
#define APB_PLL3_FREQOFFSET_CTRL    (0x144)
#define APB_PLL4_SSC_CTRL   (0x148)
#define APB_PLL4_SSC_CONF   (0x14c)
#define APB_PLL4_FREQOFFSET_CTRL    (0x150)

struct pxa1U88_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *mpmu_base;
	void __iomem *apmu_base;
	void __iomem *apbc_base;
	void __iomem *apbcp_base;
	void __iomem *apbs_base;
	void __iomem *ciu_base;
};

static struct mmp_param_fixed_rate_clk fixed_rate_clks[] = {
	{PXA1U88_CLK_CLK32, "clk32", NULL, CLK_IS_ROOT, 32768},
	{PXA1U88_CLK_VCTCXO, "vctcxo", NULL, CLK_IS_ROOT, 26000000},
	{PXA1U88_CLK_PLL1_624, "pll1_624", NULL, CLK_IS_ROOT, 624000000},
	{PXA1U88_CLK_PLL1_416, "pll1_416", NULL, CLK_IS_ROOT, 416000000},
	{PXA1U88_CLK_PLL1_499, "pll1_499", NULL, CLK_IS_ROOT, 499000000},
	{PXA1U88_CLK_PLL1_832, "pll1_832", NULL, CLK_IS_ROOT, 832000000},
	{PXA1U88_CLK_PLL1_1248, "pll1_1248", NULL, CLK_IS_ROOT, 1248000000},
};

static struct mmp_param_fixed_factor_clk fixed_factor_clks[] = {
	{PXA1U88_CLK_PLL1_2, "pll1_2", "pll1_624", 1, 2, 0},
	{PXA1U88_CLK_PLL1_4, "pll1_4", "pll1_2", 1, 2, 0},
	{PXA1U88_CLK_PLL1_8, "pll1_8", "pll1_4", 1, 2, 0},
	{PXA1U88_CLK_PLL1_16, "pll1_16", "pll1_8", 1, 2, 0},
	{PXA1U88_CLK_PLL1_6, "pll1_6", "pll1_2", 1, 3, 0},
	{PXA1U88_CLK_PLL1_12, "pll1_12", "pll1_6", 1, 2, 0},
	{PXA1U88_CLK_PLL1_24, "pll1_24", "pll1_12", 1, 2, 0},
	{PXA1U88_CLK_PLL1_48, "pll1_48", "pll1_24", 1, 2, 0},
	{PXA1U88_CLK_PLL1_96, "pll1_96", "pll1_48", 1, 2, 0},
	{PXA1U88_CLK_PLL1_13, "pll1_13", "pll1_624", 1, 13, 0},
	{PXA1U88_CLK_PLL1_13_1_5, "pll1_13_1_5", "pll1_13", 2, 3, 0},
	{PXA1U88_CLK_PLL1_2_1_5, "pll1_2_1_5", "pll1_2", 2, 3, 0},
	{PXA1U88_CLK_PLL1_13_16, "pll1_13_16", "pll1_624", 3, 16, 0},
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
	{PXA1U88_CLK_PLL1_416_GATE, "pll1_416_gate", "pll1_416", 0, APMU_CLK_GATE_CTRL, 27, 0, &pll1_lock},
	{PXA1U88_CLK_PLL1_624_GATE, "pll1_624_gate", "pll1_624", 0, APMU_CLK_GATE_CTRL, 26, 0, &pll1_lock},
	{PXA1U88_CLK_PLL1_832_GATE, "pll1_832_gate", "pll1_832", 0, APMU_CLK_GATE_CTRL, 30, 0, &pll1_lock},
	{PXA1U88_CLK_PLL1_1248_GATE, "pll1_1248_gate", "pll1_1248", 0, APMU_CLK_GATE_CTRL, 28, 0, &pll1_lock},
	{PXA1U88_CLK_PLL1_312_GATE, "pll1_312_gate", "pll1_2", 0, APMU_CLK_GATE_CTRL, 29, 0, &pll1_lock},
};

enum pll {
	PLL2 = 0,
	PLL3,
	PLL4,
	MAX_PLL_NUM
};

static struct mmp_vco_params pllx_vco_params[MAX_PLL_NUM] = {
	{
		.vco_min = 1200000000UL,
		.vco_max = 3000000000UL,
		.lock_enable_bit = POSR_PLL2_LOCK,
		.default_rate = 2115 * MHZ,
	},
	{
		.vco_min = 1200000000UL,
		.vco_max = 3000000000UL,
		.lock_enable_bit = POSR_PLL3_LOCK,
		.default_rate = 1526 * MHZ,
	},
	{
		.vco_min = 1200000000UL,
		.vco_max = 3000000000UL,
		.lock_enable_bit = POSR_PLL4_LOCK,
		.default_rate = 1595 * MHZ,
	}
};

static struct mmp_pll_params pllx_pll_params[MAX_PLL_NUM] = {
	{
		.default_rate = 1057 * MHZ,
	},
	{
		.default_rate = 1526 * MHZ,
	},
	{
		.default_rate = 1595 * MHZ,
	},
};

static struct mmp_pll_params pllx_pllp_params[MAX_PLL_NUM] = {
	{
		.default_rate = 2115 * MHZ,
	},
	{
		.default_rate = 1526 * MHZ,
	},
	{
		.default_rate = 797 * MHZ,
	},
};

struct plat_pll_info {
	spinlock_t lock;
	const char *vco_name;
	const char *out_name;
	const char *outp_name;
	unsigned long vco_flag;
	unsigned long vcoclk_flag;
	unsigned long out_flag;
	unsigned long outclk_flag;
	unsigned long outp_flag;
	unsigned long outpclk_flag;
};

struct plat_pll_info pllx_platinfo[] = {
	{
		.vco_name = "pll2_vco",
		.out_name = "pll2",
		.outp_name = "pll2p",
		.vcoclk_flag = CLK_IS_ROOT,
		.vco_flag = HELANX_PLL2CR_V1,
		.out_flag = HELANX_PLLOUT,
		.outp_flag = HELANX_PLLOUTP,
	},
	{
		.vco_name = "pll3_vco",
		.out_name = "pll3",
		.outp_name = "pll3p",
		.vcoclk_flag = CLK_IS_ROOT,
		.outpclk_flag = CLK_SET_RATE_PARENT,
		.out_flag = HELANX_PLLOUT,
		.outp_flag = HELANX_PLLOUTP,
	},
	{
		.vco_name = "pll4_vco",
		.out_name = "pll4",
		.outp_name = "pll4p",
		.vcoclk_flag = CLK_IS_ROOT,
		.out_flag = HELANX_PLLOUT,
		.outp_flag = HELANX_PLLOUTP,
	}
};

static void pxa1U88_dynpll_init(struct pxa1U88_clk_unit *pxa_unit)
{
	int idx;
	struct clk *clk;

	pllx_vco_params[PLL2].cr_reg = pxa_unit->mpmu_base + MPMU_PLL2CR;
	pllx_vco_params[PLL2].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL2CR;
	pllx_vco_params[PLL3].cr_reg = pxa_unit->mpmu_base + MPMU_PLL3CR;
	pllx_vco_params[PLL3].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL3CR;
	pllx_vco_params[PLL4].cr_reg = pxa_unit->mpmu_base + MPMU_PLL4CR;
	pllx_vco_params[PLL4].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL4CR;

	pllx_pll_params[PLL2].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL2CR;
	pllx_pll_params[PLL3].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL3CR;
	pllx_pll_params[PLL4].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL4CR;

	pllx_pllp_params[PLL2].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL2CR;
	pllx_pllp_params[PLL3].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL3CR;
	pllx_pllp_params[PLL4].pll_swcr = pxa_unit->apbs_base + APB_SPARE_PLL4CR;

	for (idx = 0; idx < ARRAY_SIZE(pllx_platinfo); idx++) {
		spin_lock_init(&pllx_platinfo[idx].lock);
		/* vco */
		pllx_vco_params[idx].lock_reg = pxa_unit->mpmu_base + MPMU_POSR;
		clk = helanx_clk_register_28nmvco(pllx_platinfo[idx].vco_name,
			0, pllx_platinfo[idx].vcoclk_flag, pllx_platinfo[idx].vco_flag,
			&pllx_platinfo[idx].lock, &pllx_vco_params[idx]);
		clk_set_rate(clk, pllx_vco_params[idx].default_rate);
		/* pll */
		clk = helanx_clk_register_28nmpll(pllx_platinfo[idx].out_name,
			pllx_platinfo[idx].vco_name,
			pllx_platinfo[idx].outclk_flag, pllx_platinfo[idx].out_flag,
			&pllx_platinfo[idx].lock, &pllx_pll_params[idx]);
		clk_set_rate(clk, pllx_pll_params[idx].default_rate);

		/* pllp */
		clk = helanx_clk_register_28nmpll(pllx_platinfo[idx].outp_name,
			pllx_platinfo[idx].vco_name,
			pllx_platinfo[idx].outpclk_flag, pllx_platinfo[idx].outp_flag,
			&pllx_platinfo[idx].lock, &pllx_pllp_params[idx]);
		clk_set_rate(clk, pllx_pllp_params[idx].default_rate);
	}
}

static void pxa1U88_pll_init(struct pxa1U88_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_fixed_rate_clks(unit, fixed_rate_clks,
					ARRAY_SIZE(fixed_rate_clks));

	mmp_register_fixed_factor_clks(unit, fixed_factor_clks,
					ARRAY_SIZE(fixed_factor_clks));

	clk = clk_register_gate(NULL, "pll1_499_en", "pll1_499", 0,
				pxa_unit->apbs_base + APBS_PLL1_CTRL,
				31, 0, NULL);
	mmp_clk_add(unit, PXA1U88_CLK_PLL1_499_EN, clk);

	clk = mmp_clk_register_factor("uart_pll", "pll1_4",
				CLK_SET_RATE_PARENT,
				pxa_unit->mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl), NULL);
	mmp_clk_add(unit, PXA1U88_CLK_UART_PLL, clk);

	mmp_register_general_gate_clks(unit, pll1_gate_clks,
				pxa_unit->apmu_base,
				ARRAY_SIZE(pll1_gate_clks));

	pxa1U88_dynpll_init(pxa_unit);
}

static struct mmp_param_gate_clk apbc_gate_clks[] = {
	{PXA1U88_CLK_TWSI0, "twsi0_clk", "pll1_13_1_5", CLK_SET_RATE_PARENT, APBC_TWSI0, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1U88_CLK_TWSI1, "twsi1_clk", "pll1_13_1_5", CLK_SET_RATE_PARENT, APBC_TWSI1, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1U88_CLK_TWSI3, "twsi3_clk", "pll1_13_1_5", CLK_SET_RATE_PARENT, APBC_TWSI3, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1U88_CLK_GPIO, "gpio_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_GPIO, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1U88_CLK_KPC, "kpc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_KPC, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1U88_CLK_RTC, "rtc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_RTC, 0x7, 0x3, 0x0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1U88_CLK_PWM0, "pwm0_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM0, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1U88_CLK_PWM1, "pwm1_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM1, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1U88_CLK_PWM2, "pwm2_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM2, 0x7, 0x3, 0x0, 0, NULL},
	{PXA1U88_CLK_PWM3, "pwm3_clk", "pll1_48", CLK_SET_RATE_PARENT, APBC_PWM3, 0x7, 0x3, 0x0, 0, NULL},
};

static DEFINE_SPINLOCK(uart0_lock);
static DEFINE_SPINLOCK(uart1_lock);
static DEFINE_SPINLOCK(uart2_lock);
static const char *uart_parent_names[] = {"pll1_3_16", "uart_pll"};

#ifdef CONFIG_CORESIGHT_SUPPORT
static void pxa1U88_coresight_clk_init(struct pxa1U88_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;
	struct clk *clk;

	clk = mmp_clk_register_gate(NULL, "DBGCLK", "pll1_416", 0,
			pxa_unit->apmu_base + APMU_TRACE,
			0x10008, 0x10008, 0x0, 0, NULL);
	mmp_clk_add(unit, PXA1U88_CLK_DBGCLK, clk);

	/* TMC clock */
	clk = mmp_clk_register_gate(NULL, "TRACECLK", "DBGCLK", 0,
			pxa_unit->apmu_base + APMU_TRACE,
			0x10010, 0x10010, 0x0, 0, NULL);
	mmp_clk_add(unit, PXA1U88_CLK_TRACECLK, clk);
}
#endif

static void pxa1U88_apb_periph_clk_init(struct pxa1U88_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	mmp_register_gate_clks(unit, apbc_gate_clks, pxa_unit->apbc_base,
				ARRAY_SIZE(apbc_gate_clks));

	clk = mmp_clk_register_gate(NULL, "twsi2_clk", "pll1_13_1_5",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbcp_base + APBCP_TWSI2,
				0x7, 0x3, 0x0, 0, NULL);
	mmp_clk_add(unit, PXA1U88_CLK_TWSI2, clk);

	clk = clk_register_mux(NULL, "uart0_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART0,
				4, 3, 0, &uart0_lock);
	clk = mmp_clk_register_gate(NULL, "uart0_clk", "uart0_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART0,
				0x7, 0x3, 0x0, 0, &uart0_lock);
	mmp_clk_add(unit, PXA1U88_CLK_UART0, clk);

	clk = clk_register_mux(NULL, "uart1_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART1,
				4, 3, 0, &uart1_lock);
	clk = mmp_clk_register_gate(NULL, "uart1_clk", "uart1_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbc_base + APBC_UART1,
				0x7, 0x3, 0x0, 0, &uart1_lock);
	mmp_clk_add(unit, PXA1U88_CLK_UART1, clk);

	clk = clk_register_mux(NULL, "uart2_mux", uart_parent_names,
				ARRAY_SIZE(uart_parent_names),
				CLK_SET_RATE_PARENT,
				pxa_unit->apbcp_base + APBCP_UART2,
				4, 3, 0, &uart2_lock);
	clk = mmp_clk_register_gate(NULL, "uart2_clk", "uart2_mux",
				CLK_SET_RATE_PARENT,
				pxa_unit->apbcp_base + APBCP_UART2,
				0x7, 0x3, 0x0, 0, &uart2_lock);
	mmp_clk_add(unit, PXA1U88_CLK_UART2, clk);

#ifdef CONFIG_CORESIGHT_SUPPORT
	pxa1U88_coresight_clk_init(pxa_unit);
#endif
}

static DEFINE_SPINLOCK(sdh0_lock);
static DEFINE_SPINLOCK(sdh1_lock);
static DEFINE_SPINLOCK(sdh2_lock);
static const char *sdh_parent_names[] = {"pll1_416", "pll1_624"};
static struct mmp_clk_mix_config sdh_mix_config = {
	.reg_info = DEFINE_MIX_REG_INFO(3, 8, 1, 6, 11),
};

static void pxa1U88_axi_periph_clk_init(struct pxa1U88_clk_unit *pxa_unit)
{
	struct clk *clk;
	struct mmp_clk_unit *unit = &pxa_unit->unit;

	clk = mmp_clk_register_gate(NULL, "usb_clk", NULL, 0,
				pxa_unit->apmu_base + APMU_USB,
				0x9, 0x9, 0x1, 0, NULL);
	mmp_clk_add(unit, PXA1U88_CLK_USB, clk);

	clk = mmp_clk_register_gate(NULL, "sdh_axi_clk", NULL, 0,
				pxa_unit->apmu_base + APMU_SDH0,
				0x8, 0x8, 0x0, 0, &sdh0_lock);
	mmp_clk_add(unit, PXA1U88_CLK_SDH_AXI, clk);

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH0;
	clk = mmp_clk_register_mix(NULL, "sdh0_mix_clk", sdh_parent_names,
				ARRAY_SIZE(sdh_parent_names),
				CLK_SET_RATE_PARENT,
				&sdh_mix_config, &sdh0_lock);
	clk = mmp_clk_register_gate(NULL, "sdh0_clk", "sdh0_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH0,
				0x12, 0x12, 0x0, 0, &sdh0_lock);
	mmp_clk_add(unit, PXA1U88_CLK_SDH0, clk);

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH1;
	clk = mmp_clk_register_mix(NULL, "sdh1_mix_clk", sdh_parent_names,
				ARRAY_SIZE(sdh_parent_names),
				CLK_SET_RATE_PARENT,
				&sdh_mix_config, &sdh1_lock);
	clk = mmp_clk_register_gate(NULL, "sdh1_clk", "sdh1_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH1,
				0x12, 0x12, 0x0, 0, &sdh1_lock);
	mmp_clk_add(unit, PXA1U88_CLK_SDH1, clk);

	sdh_mix_config.reg_info.reg_clk_ctrl = pxa_unit->apmu_base + APMU_SDH2;
	clk = mmp_clk_register_mix(NULL, "sdh2_mix_clk", sdh_parent_names,
				ARRAY_SIZE(sdh_parent_names),
				CLK_SET_RATE_PARENT,
				&sdh_mix_config, &sdh2_lock);
	clk = mmp_clk_register_gate(NULL, "sdh2_clk", "sdh2_mix_clk",
				CLK_SET_RATE_PARENT,
				pxa_unit->apmu_base + APMU_SDH2,
				0x12, 0x12, 0x0, 0, &sdh2_lock);
	mmp_clk_add(unit, PXA1U88_CLK_SDH2, clk);
}

static DEFINE_SPINLOCK(fc_seq_lock);

/* CORE */
static const char *core_parent[] = {
	"pll1_624", "pll1_1248", "pll2", "pll1_832", "pll3p",
};

static struct parents_table core_parent_table[] = {
	{
		.parent_name = "pll1_624",
		.hw_sel_val = 0x0,
	},
	{
		.parent_name = "pll1_1248",
		.hw_sel_val = 0x1,
	},
	{
		.parent_name = "pll2",
		.hw_sel_val = 0x2,
	},
	{
		.parent_name = "pll1_832",
		.hw_sel_val = 0x3,
	},
	{
		.parent_name = "pll3p",
		.hw_sel_val = 0x5,
	},
};

/*
 * For HELAN2:
 * PCLK = AP_CLK_SRC / (CORE_CLK_DIV + 1)
 * BIU_CLK = PCLK / (BIU_CLK_DIV + 1)
 * MC_CLK = PCLK / (MC_CLK_DIV + 1)
 *
 * AP clock source:
 * 0x0 = PLL1 624 MHz
 * 0x1 = PLL1 1248 MHz  or PLL3_CLKOUT
 * (depending on FCAP[2])
 * 0x2 = PLL2_CLKOUT
 * 0x3 = PLL1 832 MHZ
 * 0x5 = PLL3_CLKOUTP
 */
static struct cpu_opt helan2_op_array[] = {
	{
		.pclk = 312,
		.pdclk = 156,
		.baclk = 156,
		.ap_clk_sel = 0x0,
	},
	{
		.pclk = 624,
		.pdclk = 312,
		.baclk = 156,
		.ap_clk_sel = 0x0,
	},
	{
		.pclk = 832,
		.pdclk = 416,
		.baclk = 208,
		.ap_clk_sel = 0x3,
	},
	{
		.pclk = 1057,
		.pdclk = 528,
		.baclk = 264,
		.ap_clk_sel = 0x2,
	},
	{
		.pclk = 1248,
		.pdclk = 624,
		.baclk = 312,
		.ap_clk_sel = 0x1,
	},
	{
		.pclk = 1526,
		.pdclk = 763,
		.baclk = 381,
		.ap_clk_sel = 0x5,
		.ap_clk_src = 1526,
	},
	{
		.pclk = 1803,
		.pdclk = 901,
		.baclk = 450,
		.ap_clk_sel = 0x5,
		.ap_clk_src = 1803,
	}
};

static struct cpu_rtcwtc cpu_rtcwtc_1u88[] = {
	{.max_pclk = 624, .l1_xtc = 0x02222222, .l2_xtc = 0x00002221,},
	{.max_pclk = 1526, .l1_xtc = 0x02666666, .l2_xtc = 0x00006265,},
	{.max_pclk = 1803, .l1_xtc = 0x02AAAAAA, .l2_xtc = 0x0000A2A9,},
};

static struct core_params core_params = {
	.parent_table = core_parent_table,
	.parent_table_size = ARRAY_SIZE(core_parent_table),
	.cpu_opt = helan2_op_array,
	.cpu_opt_size = ARRAY_SIZE(helan2_op_array),
	.cpu_rtcwtc_table = cpu_rtcwtc_1u88,
	.cpu_rtcwtc_table_size = ARRAY_SIZE(cpu_rtcwtc_1u88),
	.bridge_cpurate = 1248,
	.max_cpurate = 1526,
#if 0
	.dcstat_support = true,
#endif
};

/* DDR */
static const char *ddr_parent[] = {
	"pll1_624", "pll1_832", "pll2", "pll4", "pll3p",
};

/*
 * DDR clock source:
 * 0x0 = PLL1 624 MHz
 * 0x1 = PLL1 832 MHz
 * 0x4 = PLL2_CLKOUT
 * 0x5 = PLL4_CLKOUT
 * 0x6 = PLL3_CLKOUTP
 */
static struct parents_table ddr_parent_table[] = {
	{
		.parent_name = "pll1_624",
		.hw_sel_val = 0x0,
	},
	{
		.parent_name = "pll1_832",
		.hw_sel_val = 0x1,
	},
	{
		.parent_name = "pll2",
		.hw_sel_val = 0x4,
	},
	{
		.parent_name = "pll4",
		.hw_sel_val = 0x5,
	},
	{
		.parent_name = "pll3p",
		.hw_sel_val = 0x6,
	},
};


static struct ddr_opt lpddr800_oparray[] = {
	{
		.dclk = 156,
		.ddr_tbl_index = 2,
		.ddr_freq_level = 0,
		.ddr_clk_sel = 0x0,
	},
	{
		.dclk = 312,
		.ddr_tbl_index = 4,
		.ddr_freq_level = 1,
		.ddr_clk_sel = 0x0,
	},
	{
		.dclk = 416,
		.ddr_tbl_index = 6,
		.ddr_freq_level = 2,
		.ddr_clk_sel = 0x1,
	},
	{
		.dclk = 528,
		.ddr_tbl_index = 8,
		.ddr_freq_level = 3,
		.ddr_clk_sel = 0x4,
	},
	{
		.dclk = 797,
		.ddr_tbl_index = 10,
		.ddr_freq_level = 4,
		.ddr_clk_sel = 0x5,
	},
};

static unsigned long hwdfc_freq_table[] = {
	533000, 533000, 800000, 800000
};

static struct ddr_params ddr_params = {
	.parent_table = ddr_parent_table,
	.parent_table_size = ARRAY_SIZE(ddr_parent_table),
	.hwdfc_freq_table = hwdfc_freq_table,
	.hwdfc_table_size = ARRAY_SIZE(hwdfc_freq_table),
	.dcstat_support = true,
};


static const char *axi_parent[] = {
	"pll1_416", "pll1_624", "pll2", "pll2p",
};

/*
 * AXI clock source:
 * 0x0 = PLL1 416 MHz
 * 0x1 = PLL1 624 MHz
 * 0x2 = PLL2_CLKOUT
 * 0x3 = PLL2_CLKOUTP
 */
static struct parents_table axi_parent_table[] = {
	{
		.parent_name = "pll1_416",
		.hw_sel_val = 0x0,
	},
	{
		.parent_name = "pll1_624",
		.hw_sel_val = 0x1,
	},
	{
		.parent_name = "pll2",
		.hw_sel_val = 0x2,
	},
	{
		.parent_name = "pll2p",
		.hw_sel_val = 0x3,
	},
};

static struct axi_opt axi_oparray[] = {
	{
		.aclk = 156,
		.axi_clk_sel = 0x1,
	},
	{
		.aclk = 208,
		.axi_clk_sel = 0x0,
	},
	{
		.aclk = 312,
		.axi_clk_sel = 0x1,
	},
};

static struct axi_params axi_params = {
	.parent_table = axi_parent_table,
	.parent_table_size = ARRAY_SIZE(axi_parent_table),
#if 0
	.dcstat_support = true,
#endif
};

static struct ddr_combclk_relation aclk_dclk_relationtbl_1U88[] = {
	{.dclk_rate = 156000000, .combclk_rate = 156000000},
	{.dclk_rate = 312000000, .combclk_rate = 156000000},
	{.dclk_rate = 398000000, .combclk_rate = 208000000},
	{.dclk_rate = 416000000, .combclk_rate = 208000000},
	{.dclk_rate = 528000000, .combclk_rate = 208000000},
	{.dclk_rate = 667000000, .combclk_rate = 312000000},
	{.dclk_rate = 797000000, .combclk_rate = 312000000},
};

static void __init pxa1U88_acpu_init(struct pxa1U88_clk_unit *pxa_unit)
{
	struct clk *clk;

	core_params.apmu_base = pxa_unit->apmu_base;
	core_params.mpmu_base = pxa_unit->mpmu_base;
	core_params.ciu_base = pxa_unit->ciu_base;

	ddr_params.apmu_base = pxa_unit->apmu_base;
	ddr_params.mpmu_base = pxa_unit->mpmu_base;
	ddr_params.ddr_opt = lpddr800_oparray;
	ddr_params.ddr_opt_size = ARRAY_SIZE(lpddr800_oparray);

	axi_params.apmu_base = pxa_unit->apmu_base;
	axi_params.mpmu_base = pxa_unit->mpmu_base;
	axi_params.axi_opt = axi_oparray;
	axi_params.axi_opt_size = ARRAY_SIZE(axi_oparray);

	clk = mmp_clk_register_core("cpu", core_parent,
		ARRAY_SIZE(core_parent), CLK_GET_RATE_NOCACHE,
		HELANX_FC_V2, &fc_seq_lock, &core_params);
	clk_prepare_enable(clk);

	clk = mmp_clk_register_ddr("ddr", ddr_parent,
		ARRAY_SIZE(ddr_parent), CLK_GET_RATE_NOCACHE,
		HELANX_FC_V2, &fc_seq_lock, &ddr_params);
	clk_prepare_enable(clk);

	clk = mmp_clk_register_axi("axi", axi_parent,
		ARRAY_SIZE(axi_parent), CLK_GET_RATE_NOCACHE,
		HELANX_FC_V2, &fc_seq_lock, &axi_params);
	clk_prepare_enable(clk);
	register_clk_bind2ddr(clk,
		axi_params.axi_opt[axi_params.axi_opt_size - 1].aclk * MHZ,
		aclk_dclk_relationtbl_1U88,
		ARRAY_SIZE(aclk_dclk_relationtbl_1U88));
}

static void __init pxa1U88_clk_init(struct device_node *np)
{
	struct pxa1U88_clk_unit *pxa_unit;

	pxa_unit = kzalloc(sizeof(*pxa_unit), GFP_KERNEL);
	if (!pxa_unit) {
		pr_err("failed to allocate memory for pxa1U88 clock unit\n");
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

	mmp_clk_init(np, &pxa_unit->unit, PXA1U88_NR_CLKS);

	pxa1U88_pll_init(pxa_unit);
	pxa1U88_acpu_init(pxa_unit);
	pxa1U88_apb_periph_clk_init(pxa_unit);

	pxa1U88_axi_periph_clk_init(pxa_unit);
}
CLK_OF_DECLARE(pxa1U88_clk, "marvell,pxa1U88-clock", pxa1U88_clk_init);
