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

#define EDITR		0x84
#define EDSCR		0x88
#define EDRCR		0x90

#define EDPCSRlo	0xA0
#define EDPCSRhi	0xAC
#define EDPRSR		0x314
#define EDLAR		0xFB0
#define EDCIDR		0xFF0

static void __iomem *debug_base;
static void __iomem *cti_base;

#define DBG_REG(cpu, offset)	(debug_base + cpu * 0x2000 + offset)
#define CTI_REG(cpu, offset)	(cti_base + cpu * 0x1000 + offset)

void arch_enable_access(u32 cpu)
{
	writel(0xC5ACCE55, DBG_REG(cpu, EDLAR));
}

void arch_dump_pcsr(u32 cpu)
{
	u32 pcsrhi, pcsrlo;
	u64 pcsr;
	int i;

	pr_emerg("=========== dump PCSR for cpu%d ===========\n", cpu);
	for (i = 0; i < 8; i++) {
		pcsrlo = readl_relaxed(DBG_REG(cpu, EDPCSRlo));
		pcsrhi = readl_relaxed(DBG_REG(cpu, EDPCSRhi));
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
	writel(0xC5ACCE55, CTI_REG(cpu, CTI_LOCK));
}

int arch_halt_cpu(u32 cpu)
{
	u32 timeout, val;

	/* Enable Halt Debug mode */
	val = readl(DBG_REG(cpu, EDSCR));
	val |= (0x1 << 14);
	writel(val, DBG_REG(cpu, EDSCR));

	/* Enable CTI access */
	cti_enable_access(cpu);

	/* Enable CTI */
	writel(0x1, CTI_REG(cpu, CTI_CTRL));

	/* Set output channel0 */
	val = readl(CTI_REG(cpu, CTI_OUT0EN)) | 0x1;
	writel(val, CTI_REG(cpu, CTI_OUT0EN));

	/* Trigger pulse event */
	writel(0x1, CTI_REG(cpu, CTI_APP_PULSE));

	/* Wait the cpu halted */
	timeout = 10000;
	do {
		val = readl(DBG_REG(cpu, EDPRSR));
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

	/* msr dlr_el0, xzr */
	writel(0xD51B453F, DBG_REG(cpu, EDITR));

	/* Wait until the ITR become empty. */
	timeout = 10000;
	do {
		val = readl(DBG_REG(cpu, EDSCR));
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

	/* Disable Halt Debug Mode */
	val = readl(DBG_REG(cpu, EDSCR));
	val &= ~(0x1 << 14);
	writel(val, DBG_REG(cpu, EDSCR));

	/* Enable CTI access */
	cti_enable_access(cpu);

	/* Enable CTI */
	writel(0x1, CTI_REG(cpu, CTI_CTRL));

	/* ACK the outut event */
	writel(0x1, CTI_REG(cpu, CTI_INTACK));

	/* Set output channel1 */
	val = readl(CTI_REG(cpu, CTI_OUT1EN)) | 0x2;
	writel(val, CTI_REG(cpu, CTI_OUT1EN));

	/* Trigger pulse event */
	writel(0x2, CTI_REG(cpu, CTI_APP_PULSE));

	/* Wait the cpu become running */
	timeout = 10000;
	do {
		val = readl(DBG_REG(cpu, EDPRSR));
		if (!(val & (0x1 << 4)))
			break;
	} while (--timeout);

	if (!timeout)
		pr_emerg("Cannot restart cpu%d\n", cpu);
}

#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
static void __iomem *etm_base;
static void __iomem *local_etf_base;

#define ETM_REG(offset)		(etm_base \
				+ 0x1000 * raw_smp_processor_id() \
				+ offset)

#define LOCAL_ETF_REG(offset)	(local_etf_base \
				+ 0x1000 * raw_smp_processor_id() \
				+ offset)

struct etm_info {
	u32	trc_prgctl;	/* offset: 0x4 */
	u32	trc_config;	/* offset: 0x10 */
	u32	trc_eventctl0;	/* offset: 0x20 */
	u32	trc_eventctl1;	/* offset: 0x24 */
	u32	trc_stallctl;	/* offset: 0x2c */
	u32	trc_syncpr;	/* offset: 0x34 */
	u32	trc_bbctl;	/* offset: 0x3c */
	u32	trc_traceid;	/* offset: 0x40 */
	u32	trc_tsctlr;	/* offset: 0x30 */
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
#define TRC_STATR	0xc
#define TRC_CONFIGR	0x10
#define TRC_EVENTCTL0R	0x20
#define TRC_EVENTCTL1R  0x24
#define TRC_STALLCTLR	0x2c
#define TRC_TSCTLR	0x30
#define TRC_SYNCPR	0x34
#define TRC_BBCTLR	0x3c
#define TRC_TRACEIDR	0x40
#define TRC_VICTLR	0x80
#define TRC_VIIECTLR	0x84
#define TRC_VISSCTLR	0x88

#define TRC_OSLAR	0x300
#define TRC_OSLSR	0x304

#define TRC_PDCR	0x310
#define TRC_PDSR	0x314

#define TRC_LAR		0xFB0
#define TRC_LSR		0xFB4

/* The following operations are needed by XDB */
static inline void etm_enable_access(void)
{
	u32 timeout, val;

	/*Unlock the software lock*/
	writel_relaxed(0xC5ACCE55, ETM_REG(TRC_LAR));

	timeout = 10000;
	do {
		val = readl_relaxed(ETM_REG(TRC_LSR));
		if (!(val & (0x1 << 1)))
			break;
	} while (--timeout);
	if (!timeout)
		pr_err("Software Lock on cpu%d still locked!\n",
			raw_smp_processor_id());

	/*Unlock the OS lock*/
	writel_relaxed(0x0, ETM_REG(TRC_OSLAR));
	timeout = 10000;
	do {
		val = readl_relaxed(ETM_REG(TRC_OSLSR));
		if (!(val & (0x1 << 0)))
			break;
	} while (--timeout);
	if (!timeout)
		pr_err("OSLock on cpu%d still locked!\n",
			raw_smp_processor_id());
}

static void coresight_etm_save(void)
{
	struct etm_info *p_etm_info;

	etm_enable_access();
	p_etm_info = this_cpu_ptr(&cpu_etm_info);
	p_etm_info->trc_config = readl_relaxed(ETM_REG(TRC_CONFIGR));
	p_etm_info->trc_bbctl = readl_relaxed(ETM_REG(TRC_BBCTLR));
	p_etm_info->trc_eventctl0 = readl_relaxed(ETM_REG(TRC_EVENTCTL0R));
	p_etm_info->trc_eventctl1 = readl_relaxed(ETM_REG(TRC_EVENTCTL1R));
	p_etm_info->trc_stallctl = readl_relaxed(ETM_REG(TRC_STALLCTLR));
	p_etm_info->trc_syncpr = readl_relaxed(ETM_REG(TRC_SYNCPR));
	p_etm_info->trc_traceid = readl_relaxed(ETM_REG(TRC_TRACEIDR));
	p_etm_info->trc_tsctlr = readl_relaxed(ETM_REG(TRC_TSCTLR));
	p_etm_info->trc_victlr = readl_relaxed(ETM_REG(TRC_VICTLR));
	p_etm_info->trc_viiectl = readl_relaxed(ETM_REG(TRC_VIIECTLR));
	p_etm_info->trc_vissctl = readl_relaxed(ETM_REG(TRC_VISSCTLR));
	p_etm_info->trc_prgctl = readl_relaxed(ETM_REG(TRC_PRGCTLR));
}

static void coresight_etm_restore(void)
{
	struct etm_info *p_etm_info;
	u32 timeout, val;

	etm_enable_access();

	if (readl_relaxed(ETM_REG(TRC_PRGCTLR)))
		return;

	/* Check the programmers' model is stable */
	timeout = 10000;
	do {
		val = readl_relaxed(ETM_REG(TRC_STATR));
		if (val & (0x1 << 1))
			break;
	} while (--timeout);
	if (!timeout)
		pr_info("cpu%d's programmer model is unstable.\n",
			raw_smp_processor_id());

	p_etm_info = this_cpu_ptr(&cpu_etm_info);
	writel_relaxed(p_etm_info->trc_config, ETM_REG(TRC_CONFIGR));
	writel_relaxed(p_etm_info->trc_bbctl, ETM_REG(TRC_BBCTLR));
	writel_relaxed(p_etm_info->trc_eventctl0, ETM_REG(TRC_EVENTCTL0R));
	writel_relaxed(p_etm_info->trc_eventctl1, ETM_REG(TRC_EVENTCTL1R));
	writel_relaxed(p_etm_info->trc_stallctl, ETM_REG(TRC_STALLCTLR));
	writel_relaxed(p_etm_info->trc_syncpr, ETM_REG(TRC_SYNCPR));
	writel_relaxed(p_etm_info->trc_traceid, ETM_REG(TRC_TRACEIDR));
	writel_relaxed(p_etm_info->trc_tsctlr, ETM_REG(TRC_TSCTLR));
	writel_relaxed(p_etm_info->trc_victlr, ETM_REG(TRC_VICTLR));
	writel_relaxed(p_etm_info->trc_viiectl, ETM_REG(TRC_VIIECTLR));
	writel_relaxed(p_etm_info->trc_vissctl, ETM_REG(TRC_VISSCTLR));
	writel_relaxed(p_etm_info->trc_prgctl, ETM_REG(TRC_PRGCTLR));
}

static DEFINE_PER_CPU(struct etf_info, local_etf_info);

#define TMC_STS		0xc
#define TMC_CTL		0x20
#define TMC_MODE	0x28
#define TMC_FFCR	0x304
#define TMC_LAR		0xfb0

static inline void local_etf_enable_access(void)
{
	writel_relaxed(0xC5ACCE55, LOCAL_ETF_REG(TMC_LAR));
}

static inline void local_etf_disable_access(void)
{
	writel_relaxed(0x0, LOCAL_ETF_REG(TMC_LAR));
}

static void coresight_local_etf_save(void)
{
	struct etf_info *p_local_etf_info;
	p_local_etf_info = this_cpu_ptr(&local_etf_info);

	local_etf_enable_access();
	p_local_etf_info->etf_mode = readl_relaxed(LOCAL_ETF_REG(TMC_MODE));
	p_local_etf_info->etf_ffcr = readl_relaxed(LOCAL_ETF_REG(TMC_FFCR));
	p_local_etf_info->etf_ctrl = readl_relaxed(LOCAL_ETF_REG(TMC_CTL));
	local_etf_disable_access();
}

static void coresight_local_etf_restore(void)
{
	struct etf_info *p_local_etf_info;
	p_local_etf_info = this_cpu_ptr(&local_etf_info);

	local_etf_enable_access();
	if (!readl_relaxed(LOCAL_ETF_REG(TMC_CTL))) {
		writel_relaxed(p_local_etf_info->etf_mode,
				LOCAL_ETF_REG(TMC_MODE));
		writel_relaxed(p_local_etf_info->etf_ffcr,
				LOCAL_ETF_REG(TMC_FFCR));
		writel_relaxed(p_local_etf_info->etf_ctrl,
				LOCAL_ETF_REG(TMC_CTL));
	}
	local_etf_disable_access();
}

static void __init coresight_mp_init(u32 enable_mask)
{
	return;
}

static void __init coresight_local_etb_init(u32 cpu)
{
	void __iomem *local_etb = local_etf_base + 0x1000 * cpu;

	writel_relaxed(0xC5ACCE55, local_etb + TMC_LAR);
	writel_relaxed(0x0, local_etb + TMC_MODE);
	writel_relaxed(0x1, local_etb + TMC_FFCR);
	writel_relaxed(0x1, local_etb + TMC_CTL);
	writel_relaxed(0x0, local_etb + TMC_LAR);
}

static void __init coresight_etm_enable(u32 cpu)
{
	void __iomem *p_etm_base = etm_base + cpu * 0x1000;
	u32 timeout, val;

	/*Unlock the software lock*/
	writel_relaxed(0xC5ACCE55, p_etm_base + TRC_LAR);
	val = readl_relaxed(p_etm_base + TRC_LSR);
	if (val & (0x1 << 1))
		pr_err("Software Lock on cpu%d still locked!\n", cpu);

	/*Unlock the OS lock*/
	writel_relaxed(0x0, p_etm_base + TRC_OSLAR);
	val = readl_relaxed(p_etm_base + TRC_OSLSR);
	if (val & (0x1 << 0))
		pr_err("OS Lock on cpu%d still locked!\n", cpu);

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
		pr_err("Failed to find DBG node!\n");
		return -ENODEV;
	}

	etm_base = of_iomap(node, 0);
	if (!etm_base) {
		pr_err("Failed to map coresight etm register\n");
		return -ENOMEM;
	}

	node = of_find_compatible_node(NULL, NULL, "marvell,coresight-etf");
	if (!node) {
		pr_err("Failed to find CTI node!\n");
		return -ENODEV;
	}

	local_etf_base = of_iomap(node, 0);
	if (!local_etf_base) {
		pr_err("Failed to map coresight local etf register\n");
		return -ENOMEM;
	}

	return 0;
}

void arch_enable_trace(u32 enable_mask)
{
	int cpu;

	coresight_parse_trace_dt();

	coresight_mp_init(enable_mask);
	for_each_possible_cpu(cpu)
		if (test_bit(cpu, (void *)&enable_mask))
			coresight_percore_init(cpu);
}
#endif

void arch_save_coreinfo(void)
{
#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	if (etm_need_enabled()) {
		coresight_etm_save();
		coresight_local_etf_save();
	}
#endif
}

void arch_restore_coreinfo(void)
{
#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	if (etm_need_enabled()) {
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
	struct device_node *node;

	node = of_find_compatible_node(NULL, NULL, "marvell,coresight-dbg");
	if (!node) {
		pr_err("Failed to find DBG node!\n");
		return -ENODEV;
	}

	debug_base = of_iomap(node, 0);
	if (!debug_base) {
		pr_err("Failed to map coresight debug register\n");
		return -ENOMEM;
	}

	node = of_find_compatible_node(NULL, NULL, "marvell,coresight-cti");
	if (!node) {
		pr_err("Failed to find CTI node!\n");
		return -ENODEV;
	}

	cti_base = of_iomap(node, 0);
	if (!cti_base) {
		pr_err("Failed to map coresight cti register\n");
		return -ENOMEM;
	}

	return 0;
}

void arch_coresight_init(void)
{
	coresight_parse_dbg_dt();

	return;
}
