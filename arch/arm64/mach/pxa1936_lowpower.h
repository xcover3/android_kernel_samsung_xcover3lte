/*
 * linux/arch/arm/mach-mmp/include/mach/pxa1936_lowpower.h
 *
 * Author:	Anton Eidelman <antone@marvell.com>
 * Copyright:	(C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MMP_MACH_PXA1936_LOWPOWER_H__
#define __MMP_MACH_PXA1936_LOWPOWER_H__

#define APMU_CORE_STATUS	0x90

/* APMU_CORE_IDLE_CFG bits */
#define	PSW_MODE			(3 << 20)
#define MASK_CLK_OFF_CHECK		(1 << 11)
#define MASK_CLK_STBL_CHECK		(1 << 10)
#define MASK_JTAG_IDLE_CHECK		(1 << 9)
#define MASK_CORE_WFI_IDLE_CHECK	(1 << 8)
#define MASK_GIC_nFIQ_TO_CORE		(1 << 3)
#define MASK_GIC_nIRQ_TO_CORE		(1 << 2)
#define CORE_PWRDWN			(1 << 1)
#define CORE_IDLE			(1 << 0)

/* APMU_CA7MP_IDLE_CFG bits */
#define DIS_MP_L2_SLP			(1 << 19)
#define DIS_MP_SLP			(1 << 18)
#define MP_L2_PWR_OFF			(1 << 16)
#define MASK_SRAM_REPAIR_DONE_CHECK	(1 << 12)
#define MASK_CLK_OFF_CHECK		(1 << 11)
#define MASK_CLK_STBL_CHECK		(1 << 10)
#define MASK_JTAG_IDLE_CHECK		(1 << 9)
#define MASK_IDLE_CHECK			(1 << 8)
#define ICU_INT_WAKEUP_CORE_EN		(1 << 7)
#define DIS_MC_SW_REQ			(1 << 5)
#define MP_WAKE_MC_EN			(1 << 4)
#define L2_SRAM_PWRDWN			(1 << 2)
#define MP_PWRDWN			(1 << 1)
#define MP_IDLE				(1 << 0)

/* ICU */
#define ICU_MASK_FIQ			(1 << 0)
#define ICU_MASK_IRQ			(1 << 1)

/* MPMU regs offset */
#define CPCR			0x0
#define CWUCRS			0x0048
#define CWUCRM			0x004c
#define APCR_C0			0x1080
#define APCR_C1			0x1084
#define APCR_PER		0x1088
#define AWUCRS			0x1048
#define AWUCRM			0x104c
#define PWRMODE_STATUS		0x1030

/* MPMU.APCR_xx bits */
#define PMUM_AXISD		(1 << 31)
#define PMUM_DSPSD		(1 << 30)
#define PMUM_SLPEN		(1 << 29)
#define PMUM_DTCMSD		(1 << 28)
#define PMUM_DDRCORSD		(1 << 27)
#define PMUM_APBSD		(1 << 26)
#define PMUM_BBSD		(1 << 25)
#define PMUM_MSASLPEN		(1 << 14)
#define PMUM_STBYEN		(1 << 13)
#define PMUM_SPDTCMSD		(1 << 12)
#define PMUM_LDMA_MASK		(1 << 3)

/* Wakeup sources */
#define PMUM_GT_WAKEUP		(1 << 30)
#define PMUM_GSM_WAKEUPWMX	(1 << 29)
#define PMUM_WCDMA_WAKEUPX	(1 << 28)
#define PMUM_GSM_WAKEUPWM	(1 << 27)
#define PMUM_WCDMA_WAKEUPWM	(1 << 26)
#define PMUM_AP_ASYNC_INT	(1 << 25)
#define PMUM_AP_FULL_IDLE	(1 << 24)
#define PMUM_SQU_SDH1		(1 << 23)
#define PMUM_SDH_23		(1 << 22)
#define PMUM_KEYPRESS		(1 << 21)
#define PMUM_TRACKBALL		(1 << 20)
#define PMUM_NEWROTARY		(1 << 19)
#define PMUM_WDT		(1 << 18)
#define PMUM_RTC_ALARM		(1 << 17)
#define PMUM_CP_TIMER_3		(1 << 16)
#define PMUM_CP_TIMER_2		(1 << 15)
#define PMUM_CP_TIMER_1		(1 << 14)
#define PMUM_AP1_TIMER_3	(1 << 13)
#define PMUM_AP1_TIMER_2	(1 << 12)
#define PMUM_AP1_TIMER_1	(1 << 11)
#define PMUM_AP0_2_TIMER_3	(1 << 10)
#define PMUM_AP0_2_TIMER_2	(1 << 9)
#define PMUM_AP0_2_TIMER_1	(1 << 8)
#define PMUM_WAKEUP7		(1 << 7)
#define PMUM_WAKEUP6		(1 << 6)
#define PMUM_WAKEUP5		(1 << 5)
#define PMUM_WAKEUP4		(1 << 4)
#define PMUM_WAKEUP3		(1 << 3)
#define PMUM_WAKEUP2		(1 << 2)
#define PMUM_WAKEUP1		(1 << 1)
#define PMUM_WAKEUP0		(1 << 0)
#define PMUM_AP_WAKEUP_MASK     (0xFFFFFFFF \
				& ~(PMUM_GSM_WAKEUPWM | PMUM_WCDMA_WAKEUPWM))

#ifndef __ASSEMBLER__
extern void gic_raise_softirq(const struct cpumask *mask, unsigned int irq);
extern void __iomem *icu_get_base_addr(void);
enum pxa1936_lowpower_state {
	POWER_MODE_CORE_INTIDLE,	/* used for C1 */
	POWER_MODE_CORE_POWERDOWN,	/* used for C2 */
	POWER_MODE_MP_POWERDOWN,	/* used for MP2 */
	POWER_MODE_APPS_IDLE,		/* used for D1P */
	POWER_MODE_APPS_SLEEP,		/* used for non-udr chip sleep, D1 */
	POWER_MODE_UDR_VCTCXO,		/* used for udr with vctcxo, D2 */
	POWER_MODE_UDR,			/* used for udr D2, suspend */
	POWER_MODE_MAX = 15,		/* maximum lowpower states */
};

#endif
#endif
