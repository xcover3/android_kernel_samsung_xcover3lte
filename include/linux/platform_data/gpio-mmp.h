#ifndef __GPIO_MMP_H
#define __GPIO_MMP_H

struct mmp_gpio_platform_data {
	unsigned int *bank_offset;
	unsigned int nbank;
};

#endif /* __GPIO_MMP_H */
