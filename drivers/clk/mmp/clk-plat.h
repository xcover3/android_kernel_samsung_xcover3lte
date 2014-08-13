#ifndef __MACH_MMP_CLK_PLAT_H
#define __MACH_MMP_CLK_PLAT_H

#ifdef CONFIG_PXA_DVFS
#ifdef CONFIG_CPU_PXA988
extern int setup_pxa1u88_dvfs_platinfo(void);
extern int setup_pxa1L88_dvfs_platinfo(void);
#endif
extern int ddr_get_dvc_level(int rate);
#endif

/* supported DDR chip type */
enum ddr_type {
	DDR_400M = 0,
	DDR_533M,
	DDR_667M,
	DDR_800M,
	DDR_TYPE_MAX,
};

enum {
	CORE_1p2G = 1183,
	CORE_1p25G = 1248,
	CORE_1p5G = 1482,
};

extern unsigned long max_freq;
extern enum ddr_type ddr_mode;

extern unsigned int mmp_clk_mix_get_opnum(struct clk *clk);
extern unsigned long mmp_clk_mix_get_oprate(struct clk *clk,
		unsigned int index);

#endif
