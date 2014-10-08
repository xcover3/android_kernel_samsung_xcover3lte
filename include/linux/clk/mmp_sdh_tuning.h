#ifndef __MMP_SDH_TUNING_H
#define __MMP_SDH_TUNING_H

extern int sdh_tunning_scaling2minfreq(void);
extern int sdh_tunning_restorefreq(void);
extern void plat_set_vl_min(unsigned int vl_num);
extern void plat_set_vl_max(unsigned int vl_num);
extern unsigned int plat_get_vl_min(void);
extern unsigned int plat_get_vl_max(void);

#endif /* __MMP_SDH_TUNING_H */
