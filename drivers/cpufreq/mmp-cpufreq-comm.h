/*
 * drivers/cpufreq/mmp-cpufreq-comm.h
 *
 * Copyright (C) 2015 Marvell Semiconductors Inc.
 *
 * Author:
 *	Bill Zhou <zhoub@marvell.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

char *__cpufreq_printf(const char *fmt, ...);

#ifdef CONFIG_DEVFREQ_GOV_THROUGHPUT
#define DEFAULT_BUSY_UPTHRD		UL(30)
/* unit of cpu swap rate is KHz */
#define DEFAULT_BUSY_UPTHRD_SWAP	UL(800000)

struct cpufreq_ddr_upthrd {
	struct clk *cpu_clk;
	unsigned long new_cpu_rate;
	struct pm_qos_request ddr_upthrd_max_qos;
	/* to protect busy_ddr_upthrd & busy_ddr_upthrd_swap */
	struct mutex data_lock;
	unsigned long busy_upthrd;
	unsigned long busy_upthrd_swap;
	struct list_head node;
};

void cpufreq_ddr_upthrd_send_req(struct cpufreq_ddr_upthrd *cdu);
struct cpufreq_ddr_upthrd *cpufreq_ddr_upthrd_init(struct clk *clk);
#endif

