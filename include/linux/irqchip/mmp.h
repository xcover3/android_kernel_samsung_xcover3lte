#ifndef	__IRQCHIP_MMP_H
#define	__IRQCHIP_MMP_H

#define ICU_INT_CONF_CPU3	(1 << 9)
#define ICU_INT_CONF_CPU2	(1 << 8)
#define ICU_INT_CONF_CPU1	(1 << 7)
#define ICU_INT_CONF_CPU0	(1 << 6)
#define ICU_INT_CONF_AP(n)	(1 << (6 + (n & 0x3)))
#define ICU_INT_CONF_AP_MASK	(0xF << 6)
#define ICU_INT_CONF_SEAGULL	(1 << 5)
#define ICU_INT_CONF_IRQ_FIQ	(1 << 4)
#define ICU_INT_CONF_PRIO(n)	(n & 0xF)

#define ICU_IRQ_CPU0_MASKED	(ICU_INT_CONF_IRQ_FIQ | ICU_INT_CONF_CPU0)

extern struct irq_chip icu_irq_chip;

#endif	/* __IRQCHIP_MMP_H */
