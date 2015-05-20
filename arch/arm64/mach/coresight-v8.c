/*
 * linux/arch/arm64/mach/coresight-v8.c
 *
 * Author:	Neil Zhang <zhangwm@marvell.com>
 * Copyright:	(C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/percpu.h>
#include <linux/of_address.h>

#include <asm/cputype.h>
#include <asm/smp_plat.h>

#define USE_OSLOCK		0

#define EDITR		0x84
#define EDSCR		0x88
#define EDRCR		0x90

#define EDPCSR_LO	0xA0
#define EDPCSR_HI	0xAC
#define EDPRSR		0x314
#define EDLAR		0xFB0
#define EDCIDR		0xFF0

/*
 * Each cluster may have it's own base address for coresight components,
 * while cpu's inside a cluster are expected to occupy consequtive
 * locations.
 */
#define NR_CLUS 2	/* max number of clusters supported */
#define CLUSID(cpu)	(MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 1))
#define CPUID(cpu)	(MPIDR_AFFINITY_LEVEL(cpu_logical_map(cpu), 0))

static void __iomem *debug_base[NR_CLUS];
static void __iomem *cti_base[NR_CLUS];

#define DBG_BASE(cpu)	(debug_base[CLUSID(cpu)] + CPUID(cpu) * 0x2000)
#define CTI_BASE(cpu)	(cti_base[CLUSID(cpu)] + CPUID(cpu) * 0x1000)

void arch_enable_access(u32 cpu)
{
	writel(0xC5ACCE55, DBG_BASE(cpu) + EDLAR);
}

void arch_dump_pcsr(u32 cpu)
{
	void __iomem *p_dbg_base = DBG_BASE(cpu);
	u32 pcsrhi, pcsrlo;
	u64 pcsr;
	int i;

	pr_emerg("=========== dump PCSR for cpu%d ===========\n", cpu);
	for (i = 0; i < 8; i++) {
		pcsrlo = readl_relaxed(p_dbg_base + EDPCSR_LO);
		pcsrhi = readl_relaxed(p_dbg_base + EDPCSR_HI);
		pcsr = pcsrhi;
		pcsr = (pcsr << 32) | pcsrlo;
		pr_emerg("PCSR of cpu%d is 0x%llx\n", cpu, pcsr);
		udelay(20);
	}
}

#define CTI_CTRL		0x0
#define CTI_INTACK		0x10
#define CTI_IN0EN		0x20
#define CTI_APP_PULSE		0x1c
#define CTI_OUT0EN		0xA0
#define CTI_OUT1EN		0xA4
#define CTI_GATE		0x140
#define CTI_LOCK		0xfb0
#define CTI_DEVID		0xfc8

static inline void cti_enable_access(u32 cpu)
{
	writel(0xC5ACCE55, CTI_BASE(cpu) + CTI_LOCK);
}

int arch_halt_cpu(u32 cpu)
{
	u32 timeout, val;
	void __iomem *p_dbg_base = DBG_BASE(cpu);
	void __iomem *p_cti_base = CTI_BASE(cpu);
	/* Enable Halt Debug mode */
	val = readl(p_dbg_base + EDSCR);
	val |= (0x1 << 14);
	writel(val, p_dbg_base + EDSCR);

	/* Enable CTI access */
	cti_enable_access(cpu);

	/* Enable CTI */
	writel(0x1, p_cti_base + CTI_CTRL);

	/* Set output channel0 */
	val = readl(p_cti_base + CTI_OUT0EN) | 0x1;
	writel(val, p_cti_base + CTI_OUT0EN);

	/* Trigger pulse event */
	writel(0x1, p_cti_base + CTI_APP_PULSE);

	/* Wait the cpu halted */
	timeout = 10000;
	do {
		val = readl(p_dbg_base + EDPRSR);
		if (val & (0x1 << 4))
			break;
	} while (--timeout);

	if (!timeout)
		return -1;

	return 0;
}

void arch_insert_inst(u32 cpu)
{
	u32 timeout, val;
	void __iomem *p_dbg_base = DBG_BASE(cpu);

	/* msr dlr_el0, xzr */
	writel(0xD51B453F, p_dbg_base + EDITR);

	/* Wait until the ITR become empty. */
	timeout = 10000;
	do {
		val = readl(p_dbg_base + EDSCR);
		if (val & (0x1 << 24))
			break;
	} while (--timeout);
	if (!timeout)
		pr_emerg("Cannot execute instructions on cpu%d\n", cpu);

	if (val & (0x1 << 6))
		pr_emerg("Occurred exception in debug state on cpu%d\n", cpu);
}

void arch_restart_cpu(u32 cpu)
{
	u32 timeout, val;
	void __iomem *p_dbg_base = DBG_BASE(cpu);
	void __iomem *p_cti_base = CTI_BASE(cpu);

	/* Disable Halt Debug Mode */
	val = readl(p_dbg_base + EDSCR);
	val &= ~(0x1 << 14);
	writel(val, p_dbg_base + EDSCR);

	/* Enable CTI access */
	cti_enable_access(cpu);

	/* Enable CTI */
	writel(0x1, p_cti_base + CTI_CTRL);

	/* ACK the outut event */
	writel(0x1, p_cti_base + CTI_INTACK);

	/* Set output channel1 */
	val = readl(p_cti_base + CTI_OUT1EN) | 0x2;
	writel(val, p_cti_base + CTI_OUT1EN);

	/* Trigger pulse event */
	writel(0x2, p_cti_base + CTI_APP_PULSE);

	/* Wait the cpu become running */
	timeout = 10000;
	do {
		val = readl(p_dbg_base + EDPRSR);
		if (!(val & (0x1 << 4)))
			break;
	} while (--timeout);

	if (!timeout)
		pr_emerg("Cannot restart cpu%d\n", cpu);
}

#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
static void __iomem *etm_base[NR_CLUS];
static void __iomem *local_etf_base[NR_CLUS];
#define ETM_BASE(cpu) \
			(etm_base[CLUSID(cpu)] + 0x1000 * CPUID(cpu))

#define LOCAL_ETF_BASE(cpu) \
			(local_etf_base[CLUSID(cpu)] + 0x1000 * CPUID(cpu))

struct etm_info {
	u32	trc_prgctl;	/* offset: 0x4 */
	u32	trc_config;	/* offset: 0x10 */
	u32	trc_eventctl0;	/* offset: 0x20 */
	u32	trc_eventctl1;	/* offset: 0x24 */
	u32	trc_stallctl;	/* offset: 0x2c */
	u32	trc_tsctlr;	/* offset: 0x30 */
	u32	trc_syncpr;	/* offset: 0x34 */
	u32	trc_bbctl;	/* offset: 0x3c */
	u32	trc_traceid;	/* offset: 0x40 */
	u32	trc_victlr;	/* offset: 0x80 */
	u32	trc_viiectl;	/* offset: 0x84 */
	u32	trc_vissctl;	/* offset: 0x88 */
};

struct etf_info {
	u32	etf_ctrl;	/* offset: 0x20 */
	u32	etf_mode;	/* offset: 0x28 */
	u32	etf_ffcr;	/* offset: 0x304 */
};

static DEFINE_PER_CPU(struct etm_info, cpu_etm_info);
#define TRC_PRGCTLR	0x4
#define TRC_PROCSELR			(0x008)
#define TRC_STATR	0xc
#define TRC_CONFIGR	0x10
#define TRC_AUXCTLR			(0x018)
#define TRC_EVENTCTL0R	0x20
#define TRC_EVENTCTL1R  0x24
#define TRC_STALLCTLR	0x2c
#define TRC_TSCTLR	0x30
#define TRC_SYNCPR	0x34
#define TRC_CCCTLR			(0x038)
#define TRC_BBCTLR	0x3c
#define TRC_TRACEIDR	0x40
#define TRC_QCTLR			(0x044)
#define TRC_VICTLR	0x80
#define TRC_VIIECTLR	0x84
#define TRC_VISSCTLR	0x88

#define TRC_VIPCSSCTLR			(0x08C)
#define TRC_VDCTLR			(0x0A0)
#define TRC_VDSACCTLR			(0x0A4)
#define TRC_VDARCCTLR			(0x0A8)
/* Derived resources registers */
#define TRC_SEQEVRn(n)			(0x100 + (n * 4))
#define TRC_SEQRSTEVR			(0x118)
#define TRC_SEQSTR			(0x11C)
#define TRC_EXTINSELR			(0x120)
#define TRC_CNTRLDVRn(n)			(0x140 + (n * 4))
#define TRC_CNTCTLRn(n)			(0x150 + (n * 4))
#define TRC_CNTVRn(n)			(0x160 + (n * 4))
/* ID registers */
#define TRC_IDR8				(0x180)
#define TRC_IDR9				(0x184)
#define TRC_IDR10			(0x188)
#define TRC_IDR11			(0x18C)
#define TRC_IDR12			(0x190)
#define TRC_IDR13			(0x194)
#define TRC_IMSPEC0			(0x1C0)
#define TRC_IMSPECn(n)			(0x1C0 + (n * 4))
#define TRC_IDR0				(0x1E0)
#define TRC_IDR1				(0x1E4)
#define TRC_IDR2				(0x1E8)
#define TRC_IDR3				(0x1EC)
#define TRC_IDR4				(0x1F0)
#define TRC_IDR5				(0x1F4)
#define TRC_IDR6				(0x1F8)
#define TRC_IDR7				(0x1FC)
/* Resource selection registers */
#define TRC_RSCTLRn(n)			(0x200 + (n * 4))
/* Single-shot comparator registers */
#define TRC_SSCCRn(n)			(0x280 + (n * 4))
#define TRC_SSCSRn(n)			(0x2A0 + (n * 4))
#define TRC_SSPCICRn(n)			(0x2C0 + (n * 4))
/* Management registers (0x300-0x314) */
#define TRC_OSLAR			(0x300)
#define TRC_OSLSR			(0x304)
#define TRC_PDCR				(0x310)
#define TRC_PDSR				(0x314)
/* Trace registers (0x318-0xEFC) */
/* Comparator registers */
#define TRC_ACVRn(n)			(0x400 + (n * 8))
#define TRC_ACATRn(n)			(0x480 + (n * 8))
#define TRC_DVCVRn(n)			(0x500 + (n * 16))
#define TRC_DVCMRn(n)			(0x580 + (n * 16))
#define TRC_CIDCVRn(n)			(0x600 + (n * 8))
#define TRC_VMIDCVRn(n)			(0x640 + (n * 8))
#define TRC_CIDCCTLR0			(0x680)
#define TRC_CIDCCTLR1			(0x684)
#define TRC_VMIDCCTLR0			(0x688)
#define TRC_VMIDCCTLR1			(0x68C)
/* Management register (0xF00) */
/* Integration control registers */
#define TRC_ITCTRL			(0xF00)
/* Trace registers (0xFA0-0xFA4) */
/* Claim tag registers */
#define TRC_CLAIMSET			(0xFA0)
#define TRC_CLAIMCLR			(0xFA4)
/* Management registers (0xFA8-0xFFC) */
#define TRC_DEVAFF0			(0xFA8)
#define TRC_DEVAFF1			(0xFAC)
#define TRC_LAR				(0xFB0)
#define TRC_LSR				(0xFB4)
#define TRC_AUTHSTATUS			(0xFB8)
#define TRC_DEVARCH			(0xFBC)
#define TRC_DEVID			(0xFC8)
#define TRC_DEVTYPE			(0xFCC)
#define TRC_PIDR4			(0xFD0)
#define TRC_PIDR5			(0xFD4)
#define TRC_PIDR6			(0xFD8)
#define TRC_PIDR7			(0xFDC)
#define TRC_PIDR0			(0xFE0)
#define TRC_PIDR1			(0xFE4)
#define TRC_PIDR2			(0xFE8)
#define TRC_PIDR3			(0xFEC)
#define TRC_CIDR0			(0xFF0)
#define TRC_CIDR1			(0xFF4)
#define TRC_CIDR2			(0xFF8)
#define TRC_CIDR3			(0xFFC)


/* The following operations are needed by XDB */
static inline void etm_lock(void)
{
	void __iomem *p_etm_base = ETM_BASE(raw_smp_processor_id());
	/*lock the software lock*/
	mb();
	writel_relaxed(0x0, p_etm_base + TRC_LAR);
}

/* The following operations are needed by XDB */
static inline void etm_unlock(void)
{
	void __iomem *p_etm_base = ETM_BASE(raw_smp_processor_id());
	/*Unlock the software lock*/
	writel_relaxed(0xC5ACCE55, p_etm_base + TRC_LAR);
	mb();
}

/* The following operations are needed by XDB */
static inline void etm_os_unlock(void)
{
	void __iomem *p_etm_base = ETM_BASE(raw_smp_processor_id());

	/*unlock the os lock*/
	mb();
	writel_relaxed(0x0, p_etm_base + TRC_OSLAR);
}

/* The following operations are needed by XDB */
static inline void etm_os_lock(void)
{
	void __iomem *p_etm_base = ETM_BASE(raw_smp_processor_id());

	/*Lock the os lock*/
	writel_relaxed(0x1, p_etm_base + TRC_OSLAR);
	mb();
}

static void coresight_etm_save(void)
{
	struct etm_info *p_etm_info;
	u32 timeout, val;
	void __iomem *p_etm_base = ETM_BASE(raw_smp_processor_id());

	mb();
	isb();

	/* unlock software lock */
	etm_unlock();

	/* Fix me, normally, lock OS lock can disable trace unit and external access,
	 * but found OS lock can't lock/unlock issue when doing MBTF test, so replace
	 * with TRC_PRGCTLR to disabel trace unit
	 */
#if USE_OSLOCK
	etm_os_lock();
#else
	writel_relaxed(0x0, p_etm_base + TRC_PRGCTLR);
#endif

	/* Check the programmers' model is stable */
	timeout = 100;
	do {
		val = readl(p_etm_base + TRC_STATR);
		if (val & (0x1 << 1))
			break;
		udelay(1);
	} while (--timeout);
	if (!timeout)
		pr_info("cpu%d's programmer model is unstable.\n",
			raw_smp_processor_id());

	/* save register */
	p_etm_info = this_cpu_ptr(&cpu_etm_info);
	p_etm_info->trc_config = readl_relaxed(p_etm_base + TRC_CONFIGR);
	p_etm_info->trc_eventctl0 = readl_relaxed(p_etm_base + TRC_EVENTCTL0R);
	p_etm_info->trc_eventctl1 = readl_relaxed(p_etm_base + TRC_EVENTCTL1R);
	p_etm_info->trc_stallctl = readl_relaxed(p_etm_base + TRC_STALLCTLR);
	p_etm_info->trc_tsctlr = readl_relaxed(p_etm_base + TRC_TSCTLR);
	p_etm_info->trc_syncpr = readl_relaxed(p_etm_base + TRC_SYNCPR);
	p_etm_info->trc_bbctl = readl_relaxed(p_etm_base + TRC_BBCTLR);
	p_etm_info->trc_traceid = readl_relaxed(p_etm_base + TRC_TRACEIDR);
	p_etm_info->trc_victlr = readl_relaxed(p_etm_base + TRC_VICTLR);
	p_etm_info->trc_viiectl = readl_relaxed(p_etm_base + TRC_VIIECTLR);
	p_etm_info->trc_vissctl = readl_relaxed(p_etm_base + TRC_VISSCTLR);
#if USE_OSLOCK
	p_etm_info->trc_prgctl = readl_relaxed(p_etm_base + TRC_PRGCTLR);
#endif

	/* ensure trace unit is idle to be powered down */
	timeout = 100;
	do {
		val = readl(p_etm_base + TRC_STATR);
		if (val & (0x1 << 0))
			break;
		udelay(1);
	} while (--timeout);
	if (!timeout)
		pr_info("cpu%d's programmer model is not idle.\n",
			raw_smp_processor_id());

	/* lock software lock */
	etm_lock();
}

static void coresight_etm_restore(void)
{
	struct etm_info *p_etm_info;
	void __iomem *p_etm_base = ETM_BASE(raw_smp_processor_id());

	/* unlock software lock */
	etm_unlock();

	/* Fix me, normally, lock OS lock can disable trace unit and external access,
	 * but found OS lock can't lock/unlock issue when doing MBTF test, so replace
	 * with TRC_PRGCTLR to disabel trace unit
	 */
#if USE_OSLOCK
	etm_os_lock();
#else
	writel_relaxed(0x0, p_etm_base + TRC_PRGCTLR);
#endif

	/* restore registers */
	p_etm_info = this_cpu_ptr(&cpu_etm_info);
	writel_relaxed(p_etm_info->trc_config, p_etm_base + TRC_CONFIGR);
	writel_relaxed(p_etm_info->trc_eventctl0, p_etm_base + TRC_EVENTCTL0R);
	writel_relaxed(p_etm_info->trc_eventctl1, p_etm_base + TRC_EVENTCTL1R);
	writel_relaxed(p_etm_info->trc_stallctl, p_etm_base + TRC_STALLCTLR);
	writel_relaxed(p_etm_info->trc_tsctlr, p_etm_base + TRC_TSCTLR);
	writel_relaxed(p_etm_info->trc_syncpr, p_etm_base + TRC_SYNCPR);
	writel_relaxed(p_etm_info->trc_bbctl, p_etm_base + TRC_BBCTLR);
	writel_relaxed(p_etm_info->trc_traceid, p_etm_base + TRC_TRACEIDR);
	writel_relaxed(p_etm_info->trc_victlr, p_etm_base + TRC_VICTLR);
	writel_relaxed(p_etm_info->trc_viiectl, p_etm_base + TRC_VIIECTLR);
	writel_relaxed(p_etm_info->trc_vissctl, p_etm_base + TRC_VISSCTLR);
#if USE_OSLOCK
	writel_relaxed(p_etm_info->trc_prgctl, p_etm_base + TRC_PRGCTLR);
#else
	writel_relaxed(0x1, p_etm_base + TRC_PRGCTLR);
#endif

	/* unlock os lock */
	etm_os_unlock();

	/* lock software lock */
	etm_lock();
}

static DEFINE_PER_CPU(struct etf_info, local_etf_info);

#define TMC_STS		0xc
#define TMC_CTL		0x20
#define TMC_MODE	0x28
#define TMC_FFCR	0x304
#define TMC_LAR		0xfb0

static inline void local_etf_enable_access(void)
{
	writel_relaxed(0xC5ACCE55,
		LOCAL_ETF_BASE(raw_smp_processor_id()) + TMC_LAR);
}

static inline void local_etf_disable_access(void)
{
	writel_relaxed(0x0,
		LOCAL_ETF_BASE(raw_smp_processor_id()) + TMC_LAR);
}

static void coresight_local_etf_save(void)
{
	struct etf_info *p_local_etf_info;
	void __iomem *local_etb = LOCAL_ETF_BASE(raw_smp_processor_id());
	p_local_etf_info = this_cpu_ptr(&local_etf_info);

	local_etf_enable_access();
	p_local_etf_info->etf_mode = readl_relaxed(local_etb + TMC_MODE);
	p_local_etf_info->etf_ffcr = readl_relaxed(local_etb + TMC_FFCR);
	p_local_etf_info->etf_ctrl = readl_relaxed(local_etb + TMC_CTL);
	local_etf_disable_access();
}

static void coresight_local_etf_restore(void)
{
	struct etf_info *p_local_etf_info;
	void __iomem *local_etb = LOCAL_ETF_BASE(raw_smp_processor_id());
	p_local_etf_info = this_cpu_ptr(&local_etf_info);

	local_etf_enable_access();
	if (!readl_relaxed(local_etb + TMC_CTL)) {
		writel_relaxed(p_local_etf_info->etf_mode,
				local_etb + TMC_MODE);
		writel_relaxed(p_local_etf_info->etf_ffcr,
				local_etb + TMC_FFCR);
		writel_relaxed(p_local_etf_info->etf_ctrl,
				local_etb + TMC_CTL);
	}
	local_etf_disable_access();
}

static void __init coresight_mp_init(u32 enable_mask)
{
	return;
}

static void __init coresight_local_etb_init(u32 cpu)
{
	void __iomem *local_etb = LOCAL_ETF_BASE(cpu);

	writel_relaxed(0xC5ACCE55, local_etb + TMC_LAR);
	writel_relaxed(0x0, local_etb + TMC_MODE);
	writel_relaxed(0x1, local_etb + TMC_FFCR);
	writel_relaxed(0x1, local_etb + TMC_CTL);
	writel_relaxed(0x0, local_etb + TMC_LAR);
}

static void coresight_local_etb_stop(u32 cpu)
{
	void __iomem *local_etb = LOCAL_ETF_BASE(cpu);

	writel_relaxed(0xC5ACCE55, local_etb + TMC_LAR);
	writel_relaxed(0x0, local_etb + TMC_CTL);
	writel_relaxed(0x0, local_etb + TMC_LAR);
	dsb(); /* prevent further progress until stopped */
}

static void __init coresight_etm_enable(u32 cpu)
{
	void __iomem *p_etm_base = ETM_BASE(cpu);
	u32 timeout, val;

	/*Unlock the software lock*/
	writel_relaxed(0xC5ACCE55, p_etm_base + TRC_LAR);

	/*Disable the trace unit*/
	writel_relaxed(0x0, p_etm_base + TRC_PRGCTLR);

	/* Check the programmers' model is stable */
	timeout = 10000;
	do {
		val = readl_relaxed(p_etm_base + TRC_STATR);
		if (val & (0x1 << 1))
			break;
	} while (--timeout);
	if (!timeout)
		pr_info("cpu%d's programmer model is unstable.\n", cpu);

	/* Enable the VMID and context ID */
	writel_relaxed(0xc9, p_etm_base + TRC_CONFIGR);

	/* enable branch broadcasting for the entire memory map */
	writel_relaxed(0x0, p_etm_base + TRC_BBCTLR);

	/*Disable all event tracing*/
	writel_relaxed(0x0, p_etm_base + TRC_EVENTCTL0R);
	writel_relaxed(0x0, p_etm_base + TRC_EVENTCTL1R);

	/*Disable stalling*/
	writel_relaxed(0x0, p_etm_base + TRC_STALLCTLR);

	/*Enable trace sync every 256 bytes*/
	writel_relaxed(0x8, p_etm_base + TRC_SYNCPR);

	/*Set a value for the trace ID*/
	writel_relaxed((cpu + 0x3), p_etm_base + TRC_TRACEIDR);

	/*Disable the timestamp event*/
	writel_relaxed(0x0, p_etm_base + TRC_TSCTLR);

	/*Enable ViewInst to trace everything */
	writel_relaxed(0x201, p_etm_base + TRC_VICTLR);

	/*No address range filtering for ViewInst*/
	writel_relaxed(0x0, p_etm_base + TRC_VIIECTLR);

	/*No start or stop points for ViewInst*/
	writel_relaxed(0x0, p_etm_base + TRC_VISSCTLR);

	/*Enable the trace unit*/
	writel_relaxed(0x1, p_etm_base + TRC_PRGCTLR);

	/*Lock the software lock*/
	writel_relaxed(0x0, p_etm_base + TRC_LAR);
}

static void __init coresight_percore_init(u32 cpu)
{
	coresight_etm_enable(cpu);
	coresight_local_etb_init(cpu);

	dsb();
	isb();
}

static int __init coresight_parse_trace_dt(void)
{
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "marvell,coresight-etm");
	if (!node) {
		pr_err("Failed to find coresight etm node!\n");
		return -ENODEV;
	}

	etm_base[0] = of_iomap(node, 0);
	if (!etm_base[0]) {
		pr_err("Failed to map coresight etm register\n");
		return -ENOMEM;
	}
	etm_base[1] = of_iomap(node, 1); /* NULL in 1-cluster config */

	node = of_find_compatible_node(NULL, NULL, "marvell,coresight-letb");
	if (!node) {
		pr_err("Failed to find local etf node!\n");
		return -ENODEV;
	}

	local_etf_base[0] = of_iomap(node, 0);
	if (!local_etf_base[0]) {
		pr_err("Failed to map coresight local etf register\n");
		return -ENOMEM;
	}
	local_etf_base[1] = of_iomap(node, 1); /* NULL in 1-cluster config */
	return 0;
}

static u32 etm_enable_mask;
static inline int etm_need_save_restore(void)
{
	return !!etm_enable_mask;
}

void arch_enable_trace(u32 enable_mask)
{
	int cpu;

	if (coresight_parse_trace_dt())
		return;

	coresight_mp_init(enable_mask);
	for_each_possible_cpu(cpu)
		if (test_bit(cpu, (void *)&enable_mask))
			coresight_percore_init(cpu);

	etm_enable_mask = enable_mask;
}

void arch_stop_trace(void)
{
	coresight_local_etb_stop(raw_smp_processor_id());
}
#endif

void arch_save_coreinfo(void)
{
#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	if (etm_need_save_restore()) {
		coresight_etm_save();
		coresight_local_etf_save();
	}
#endif
}

void arch_restore_coreinfo(void)
{
#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	if (etm_need_save_restore()) {
		coresight_etm_restore();
		coresight_local_etf_restore();
		dsb();
		isb();
	}
#endif
}

void arch_save_mpinfo(void)
{
	return;
}

void arch_restore_mpinfo(void)
{
	return;
}

static int __init coresight_parse_dbg_dt(void)
{
	/*
	 * Should we check if dtb defines two coresight properties
	 * in two cluster configuration? For now just try of_iomap(, 1)
	 * and silently assume 1-cluster configuration if this fails.
	 */
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "marvell,coresight-dbg");
	if (!node) {
		pr_err("Failed to find DBG node!\n");
		return -ENODEV;
	}

	debug_base[0] = of_iomap(node, 0);
	if (!debug_base[0]) {
		pr_err("Failed to map coresight debug register\n");
		return -ENOMEM;
	}
	debug_base[1] = of_iomap(node, 1); /* NULL in 1-cluster config */

	node = of_find_compatible_node(NULL, NULL, "marvell,coresight-cti");
	if (!node) {
		pr_err("Failed to find CTI node!\n");
		return -ENODEV;
	}

	cti_base[0] = of_iomap(node, 0);
	if (!cti_base[0]) {
		pr_err("Failed to map coresight cti register\n");
		return -ENOMEM;
	}
	cti_base[1] = of_iomap(node, 1); /* NULL in 1-cluster config */
	return 0;
}

void arch_coresight_init(void)
{
	coresight_parse_dbg_dt();

	return;
}
