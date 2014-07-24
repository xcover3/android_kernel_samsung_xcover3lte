#ifndef COMMON_REGS_H
#define COMMON_REGS_H
#include <linux/regs-addr.h>

/* APMU regs */
#define		APMU_VIR_BASE	(get_apmu_base_va())
#define		APMU_REG(x)	(APMU_VIR_BASE + x)
/* Debug register */
#define APMU_DEBUG             APMU_REG(0x0088)
#define APMU_CORE_STATUS       APMU_REG(0x090)

#define APMU_DEBUG_CP_HALT        (1 << 0)
#define APMU_DEBUG_CP_CLK_OFF_ACK (1 << 3)


/* MPMU regs */
#define MPMU_VIR_BASE	(get_mpmu_base_va())
#define MPMU_REG(x)	(MPMU_VIR_BASE + x)

#define MPMU_CPSR		MPMU_REG(0x0004)
#define MPMU_CPRR		MPMU_REG(0x0020)
#define MPMU_APRR		MPMU_REG(0x1020)
#define MPMU_ACGR		MPMU_REG(0x1024)
#define MPMU_CP_REMAP_REG0	MPMU_REG(0x1084)
#define MPMU_CPPMU_REG1		MPMU_REG(0x1200)

#define MPMU_APRR_SGR       (1 << 7)
#define MPMU_APRR_RST_DONE  (1 << 8)

#define MPMU_CPRR_DSPR (1 << 2)
#define MPMU_CPRR_BBR  (1 << 3)
#define MPMU_CPRR_DSRAMINT  (1 << 5)
#define MPMU_APRR_CPR	(1 << 0)
#define MPMU_CPPMU_AXI_208M	(1 << 11 | 1 << 9 | 2 << 6)
#define MPMU_CPPMU_AXI_CLEAR	(~(0x1F << 6))

/* CIU regs */
#define CIU_VIR_BASE	(get_ciu_base_va())
#define CIU_REG(x)		(CIU_VIR_BASE + x)
#define CIU_FABRIC_CKGT_CTRL0   CIU_REG(0x0064)
#define CIU_FABRIC_CKGT_CTRL1   CIU_REG(0x0068)
#define CIU_SW_BRANCH_ADDR			CIU_REG(0x0024)


/* watchdog related */
#define TMR_CCR		(0x0000)
#define TMR_TN_MM(n, m)	(0x0004 + ((n) << 3) + (((n) + (m)) << 2))
#define TMR_CR(n)	(0x0028 + ((n) << 2))
#define TMR_SR(n)	(0x0034 + ((n) << 2))
#define TMR_IER(n)	(0x0040 + ((n) << 2))
#define TMR_PLVR(n)	(0x004c + ((n) << 2))
#define TMR_PLCR(n)	(0x0058 + ((n) << 2))
#define TMR_WMER	(0x0064)
#define TMR_WMR		(0x0068)
#define TMR_WVR		(0x006c)
#define TMR_WSR		(0x0070)
#define TMR_ICR(n)	(0x0074 + ((n) << 2))
#define TMR_WICR	(0x0080)
#define TMR_CER		(0x0084)
#define TMR_CMR		(0x0088)
#define TMR_ILR(n)	(0x008c + ((n) << 2))
#define TMR_WCR		(0x0098)
#define TMR_WFAR	(0x009c)
#define TMR_WSAR	(0x00A0)
#define TMR_CVWR(n)	(0x00A4 + ((n) << 2))

#define TMR_WMER_WE		(1 << 0)
#define TMR_WMER_WRIE	(1 << 1)

#define TMR_WFAR_KEY	(0xbaba)
#define TMR_WSAR_KEY	(0xeb10)

#endif
