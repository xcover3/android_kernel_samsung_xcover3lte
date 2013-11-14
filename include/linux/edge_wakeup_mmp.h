#ifndef __EDGE_WAKEUP_MMP_H
#define __EDGE_WAKEUP_MMP_H
#include <linux/device.h>
typedef void (*edge_handler)(int, void *);

#ifndef __ASSEMBLER__
extern int request_mfp_edge_wakeup(int gpio, edge_handler handler, \
			void *data, struct device *dev);
extern int remove_mfp_edge_wakeup(int gpio);
extern void edge_wakeup_mfp_enable(void);
extern void edge_wakeup_mfp_disable(void);
int edge_wakeup_mfp_status(unsigned long *edge_reg);
#endif
#endif /* __EDGE_WAKEUP_MMP_H */
