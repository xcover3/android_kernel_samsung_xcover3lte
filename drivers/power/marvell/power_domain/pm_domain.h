#ifndef __MMP_POWER_DOMAIN_H
#define __MMP_POWER_DOMAIN_H

int mmp_pd_init(struct generic_pm_domain *genpd,
	struct dev_power_governor *gov, bool is_off);


#endif
