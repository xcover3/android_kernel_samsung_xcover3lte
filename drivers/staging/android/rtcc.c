/* drivers/staging/android/rtcc.c
 *
 * RunTime CompCache v3 main file
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
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/cpu.h>
#include <linux/swap.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/vmstat.h>

#include <asm/atomic.h>

/*
 * RTCC reclaim entry, defined in vmscan.c
 */
extern unsigned long rtcc_reclaim_pages(unsigned long nr_to_reclaim,
	int swappiness, unsigned long *nr_swapped);
extern atomic_t kswapd_running;
extern long nr_kswapd_swapped;

static long nr_krtccd_swapped;
static atomic_t krtccd_running;
static atomic_t need_to_reclaim;
static atomic_t krtccd_enabled;
static struct task_struct *krtccd;
static unsigned long prev_jiffy;
static unsigned long boost_end_jiffy;

#define BOOSTMODE_TIMEOUT		60
#define DEF_RECLAIM_INTERVAL	10
#define RTCC_MSG_ASYNC			1
#define RTCC_MSG_SYNC			2
#define RTCC_GRADE_NUM 			5
#define RTCC_GRADE_LIMIT		2

#ifdef CONFIG_NR_CPUS
#define RTCC_GRADE_MULTI		CONFIG_NR_CPUS
#else
#define RTCC_GRADE_MULTI		1
#endif

#define RTCC_DBG				0

int is_rtcc_enabled(void)
{
	return atomic_read(&krtccd_enabled);
}

int get_rtcc_status(void)
{
	return atomic_read(&krtccd_running);
}

static long swap_toplimit;

static int rtcc_boost_mode = 1;
static int rtcc_reclaim_interval = DEF_RECLAIM_INTERVAL;
static int rtcc_reclaim_jiffies = DEF_RECLAIM_INTERVAL * HZ;
static int rtcc_boost_duration = BOOSTMODE_TIMEOUT;
static int rtcc_grade_size = RTCC_GRADE_NUM;
static int rtcc_grade[RTCC_GRADE_NUM] = {
	128 * RTCC_GRADE_MULTI,
	192 * RTCC_GRADE_MULTI,
	256 * RTCC_GRADE_MULTI,
	512 * RTCC_GRADE_MULTI,
	1024 * RTCC_GRADE_MULTI,
};
// These values will be changed when system is booting up
static int rtcc_minfree[RTCC_GRADE_NUM] = {
	56 * 1024, // 224MB
	48 * 1024, // 192MB
	40 * 1024, // 160MB
	32 * 1024, // 128MB
	24 * 1024, // 96MB
};

static inline unsigned long get_swapped_pages(void)
{
	return (total_swap_pages - get_nr_swap_pages());
}

static inline unsigned long get_anon_pages(void)
{
	return global_page_state(NR_INACTIVE_ANON) + global_page_state(NR_ACTIVE_ANON);
}

/*
 * Decide the rtcc grade based on free memory and free swap
 */
static int get_rtcc_grade(void)
{
	int free, i;

	// In boost mode, we will do reclaim in max speed
	if (unlikely(rtcc_boost_mode))
		return RTCC_GRADE_NUM - 1;

	// In other case, choose the grade by free memory level.
	free = global_page_state(NR_FREE_PAGES);
	for (i=0; i<=RTCC_GRADE_LIMIT; i++) {
		if (free >= rtcc_minfree[i])
			break;
	}

	return i;
}

/*
 * Decide reclaim pages a time and the time interval based on rtcc grade
 */
static int get_reclaim_count(void)
{
	int grade, times;

	grade = get_rtcc_grade();

#if RTCC_DBG
	printk("rtcc grade = %d, swap_top = %ld\n", grade, swap_toplimit);
#endif

	if (unlikely(rtcc_boost_mode))
		return rtcc_grade[grade];

	// Divide a large reclaim into several smaller one
	times = rtcc_grade[grade] / rtcc_grade[RTCC_GRADE_LIMIT];
	if (likely(grade < RTCC_GRADE_LIMIT))
		times = 1;
	else
		grade = RTCC_GRADE_LIMIT;

	rtcc_reclaim_jiffies = (rtcc_reclaim_interval * HZ) / times;

	return rtcc_grade[grade];
}

/*
 * RTCC thread entry
 */
static int rtcc_thread(void * nothing)
{
	unsigned long nr_to_reclaim, nr_reclaimed, nr_swapped;
#if RTCC_DBG
	unsigned long dt;
	struct timeval tv1, tv2;
#endif

	set_freezable();

	for ( ; ; ) {
		try_to_freeze();
		if (kthread_should_stop())
			break;

		if (likely(atomic_read(&krtccd_running) == 1)) {
#if RTCC_DBG
			do_gettimeofday(&tv1);
#endif
			swap_toplimit = get_swapped_pages() + get_anon_pages() / 2;
			swap_toplimit = min(swap_toplimit, total_swap_pages);

			nr_to_reclaim = get_reclaim_count();
			nr_swapped = 0;

			nr_reclaimed = rtcc_reclaim_pages(nr_to_reclaim, 200, &nr_swapped);
			nr_krtccd_swapped += nr_swapped;

			printk("reclaimed %ld (swapped %ld) pages.\n", nr_reclaimed, nr_swapped);

			if (likely(rtcc_boost_mode == 0)) {
				if (get_rtcc_grade() <= 0) {
					// If free memory is enough, cancel reclaim
					atomic_set(&need_to_reclaim, 0);
				} else if ((swap_toplimit - get_swapped_pages()) <= rtcc_grade[RTCC_GRADE_NUM-2]) {
					// If swap space is more than anon, also cancel reclaim
					atomic_set(&need_to_reclaim, 0);
				}
			} else if (get_anon_pages() < swap_toplimit / 4) {
				rtcc_boost_mode = 0;
				printk("swapped %ldMB enough, exit boost mode.\n", get_swapped_pages()/256);
			} else if (time_after(jiffies, boost_end_jiffy)) {
				rtcc_boost_mode = 0;
				printk("time out, swapped %ldMB, exit boost mode.\n", get_swapped_pages()/256);
			}

			atomic_set(&krtccd_running, 0);

#if RTCC_DBG
			do_gettimeofday(&tv2);
			dt = tv2.tv_sec*1000000 + tv2.tv_usec - tv1.tv_sec*1000000 - tv1.tv_usec;
			printk("cost %ldms, %ldus one page, ", dt/1000, dt/nr_reclaimed);
#endif
		}

		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}

	return 0;
}

/*
 * Dump some RTCC status
 */
static void rtcc_dump(void)
{
	printk("\nneed_to_reclaim = %d\n", atomic_read(&need_to_reclaim));
	printk("krtccd_running = %d\n", atomic_read(&krtccd_running));
}

/*
 * RTCC set interval by framework code
 */
static int rtcc_set_interval(const char *val, struct kernel_param *kp)
{
	param_set_int(val, kp);

	return 0;
}

/*
 * RTCC set duration by framework code
 */
static int rtcc_set_duration(const char *val, struct kernel_param *kp)
{
	param_set_int(val, kp);

	return 0;
}

/*
 * RTCC disable/enable by framework code
 */
static int rtcc_enable(const char *val, struct kernel_param *kp)
{
	int enable;

	sscanf(val, "%d", &enable);

	if (enable && atomic_read(&krtccd_enabled)) {
		pr_info("rtcc has been enabled!\n");
		return 0;
	}

	if (!enable && !atomic_read(&krtccd_enabled)) {
		pr_info("rtcc has been disabled!\n");
		return 0;
	}

	if (enable) {
		prev_jiffy = jiffies;
		boost_end_jiffy = jiffies + rtcc_boost_duration * HZ;
		atomic_set(&krtccd_enabled, 1);
	} else
		atomic_set(&krtccd_enabled, 0);

	return 0;
}

/*
 * RTCC triggered by framework code
 */
static int rtcc_trigger(const char *val, struct kernel_param *kp)
{
	int option;

	sscanf(val, "%d", &option);

	if (likely(!atomic_read(&krtccd_enabled))) {
		pr_info("rtcc is disabled, please enable it firstly!");
		return 0;
	}

	if (likely(option == RTCC_MSG_ASYNC)) {
		atomic_set(&need_to_reclaim, 1);
	} else if (option == RTCC_MSG_SYNC) {
		if (atomic_read(&krtccd_running) == 0) {
			atomic_set(&krtccd_running, 1);
			wake_up_process(krtccd);
			prev_jiffy = jiffies;
		}
	} else {
		rtcc_dump();
	}

	return 0;
}

/*
 * RTCC idle handler, called when CPU is idle
 */
static int rtcc_idle_handler(struct notifier_block *nb, unsigned long val, void *data)
{
	if (likely(!atomic_read(&krtccd_enabled)))
		return 0;

	if (likely(atomic_read(&need_to_reclaim) == 0))
		return 0;

	// To prevent RTCC from running too frequently
	if (likely(time_before(jiffies, prev_jiffy + rtcc_reclaim_jiffies)))
		return 0;

	if (unlikely(atomic_read(&kswapd_running) == 1))
		return 0;

	if (unlikely(idle_cpu(task_cpu(krtccd)) && this_cpu_loadx(3) == 0) || rtcc_boost_mode) {
		if (likely(atomic_read(&krtccd_running) == 0)) {
			atomic_set(&krtccd_running, 1);

			wake_up_process(krtccd);
			prev_jiffy = jiffies;
		}
	}

	return 0;
}

static struct notifier_block rtcc_idle_nb = {
	.notifier_call = rtcc_idle_handler,
};

#ifdef CONFIG_KSM_ANDROID
void enable_rtcc(void)
{
	idle_notifier_register(&rtcc_idle_nb);
}
#endif

static int __init rtcc_init(void)
{
	krtccd = kthread_run(rtcc_thread, NULL, "krtccd");
	if (IS_ERR(krtccd)) {
		/* Failure at boot is fatal */
		BUG_ON(system_state == SYSTEM_BOOTING);
	}

	set_user_nice(krtccd, 5);

	atomic_set(&krtccd_enabled, 0);
	atomic_set(&need_to_reclaim, 1);
	atomic_set(&krtccd_running, 0);

#ifndef CONFIG_KSM_ANDROID
	idle_notifier_register(&rtcc_idle_nb);
#endif

	return 0;
}

static void __exit rtcc_exit(void)
{
	idle_notifier_unregister(&rtcc_idle_nb);
	if (krtccd) {
		atomic_set(&need_to_reclaim, 0);
		kthread_stop(krtccd);
		krtccd = NULL;
	}
}

module_param_call(interval, rtcc_set_interval, param_get_int,
	&rtcc_reclaim_interval, S_IWUSR | S_IRUGO);
module_param_call(boost_duration, rtcc_set_duration, param_get_int,
	&rtcc_boost_duration, S_IWUSR | S_IRUGO);
module_param_call(trigger, rtcc_trigger, NULL, NULL, S_IWUSR | S_IRUGO);
module_param_call(enable, rtcc_enable, NULL, NULL, S_IWUSR | S_IRUGO);
module_param_array_named(grade, rtcc_grade, uint, &rtcc_grade_size, S_IRUGO | S_IWUSR);
module_param_array_named(minfree, rtcc_minfree, uint, &rtcc_grade_size, S_IRUGO | S_IWUSR);

module_init(rtcc_init);
module_exit(rtcc_exit);

MODULE_LICENSE("GPL");
