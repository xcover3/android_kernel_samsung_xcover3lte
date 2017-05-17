#ifndef __MMPX_DT_H
#define __MMPX_DT_H

extern void mmp_clk_of_init(void);

#ifdef CONFIG_MRVL_LOG
extern void __init pxa_reserve_logmem(void);
#endif

#endif
