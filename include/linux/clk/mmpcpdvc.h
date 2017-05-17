#ifndef __MMP_CPDVC_H
#define __MMP_CPDVC_H


#define MAX_CP_PPNUM 5

struct cpdvc_info {
	unsigned int cpfreq; /* Mhz */
	unsigned int cpvl; /* range from 0~7/0~15 */
};

struct cpmsa_dvc_info {
	struct cpdvc_info cpdvcinfo[MAX_CP_PPNUM];  /* we only use four CP PPs now as max */
	struct cpdvc_info cpaxidvcinfo[MAX_CP_PPNUM];
	struct cpdvc_info lteaxidvcinfo[MAX_CP_PPNUM];
	struct cpdvc_info msadvcvl[MAX_CP_PPNUM];
};

struct ddr_dfc_info {
	unsigned int ddr_idle;
	unsigned int ddr_active;
	unsigned int ddr_high;
};

extern int fillddrdfcinfo(struct ddr_dfc_info *dfc_info);
extern int getddrdfcinfo(struct ddr_dfc_info *dfc_info);

extern int fillcpdvcinfo(struct cpmsa_dvc_info *dvc_info);
extern int getcpdvcinfo(struct cpmsa_dvc_info *dvc_info);

#endif /* __MMP_SDH_TUNING_H */
