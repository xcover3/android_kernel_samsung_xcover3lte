#ifndef __MMP_FUSE_H
#define __MMP_FUSE_H

extern unsigned int get_chipprofile(void);
extern unsigned int get_iddq_105(void);
extern unsigned int get_iddq_130(void);
extern unsigned int get_skusetting(void);
extern unsigned int get_chipfab(void);
extern int get_fuse_suspd_voltage(void); /* unit: mV */
extern int is_helan3_stepping_TSMC_B0(void);

enum fab {
	TSMC = 0,
	SEC,
	FAB_MAX,
};

enum svc_versions {
	SEC_SVC_1_01 = 0,
	SVC_1_11,
	SVC_TSMC_1p8G,
	SVC_TSMC_B0,
	NO_SUPPORT,
};

#endif /* __MMP_FUSE_H */
