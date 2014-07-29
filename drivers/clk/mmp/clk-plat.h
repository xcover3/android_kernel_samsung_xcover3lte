#ifndef __MACH_MMP_CLK_PLAT_H
#define __MACH_MMP_CLK_PLAT_H

#ifdef CONFIG_PXA_DVFS
#ifdef CONFIG_CPU_PXA988
extern int setup_pxa1u88_dvfs_platinfo(void);
#endif
extern int ddr_get_dvc_level(int rate);
#endif

#endif
