#ifndef PXA_M3_REG_H
#define PXA_M3_REG_H

#include <linux/io.h>
#include <linux/regs-addr.h>
#include "shm_map.h"
#define REG_READ(addr)	__raw_readl(addr)
#define REG_WRITE(v, addr)	__raw_writel((v), addr)
#define AM_MPER_PA (0xC9000000 + 0x803000)

void __iomem *am_mapb_base_addr;
void __iomem *am_apmu_base_addr;
void __iomem *am_mciu_base_addr;
void __iomem *am_mper_base_addr;

static struct device *m3rm_dev;

static int init_gnss_base_addr(void)
{
	 /*0xD4090000*/
	am_mapb_base_addr = get_apbsp_base_va();
	if (am_mapb_base_addr == NULL) {
		pr_err("error to ioremap APB_PHYS_BASE\n");
		return -ENOENT;
	}

	/*0xD4282800*/
	am_apmu_base_addr = get_apmu_base_va();
	if (am_apmu_base_addr == NULL) {
		pr_err("error to ioremap APMU_PHYS_BASE\n");
		return -ENOENT;
	}

	/* 0xD4282C00 */
	am_mciu_base_addr = get_ciu_base_va();
	if (am_mciu_base_addr == NULL) {
		pr_err("error to ioremap MCIU_PHYS_BASE\n");
		return -ENOENT;
	}

	/* 0xC9000000; */
	am_mper_base_addr = shm_map(AM_MPER_PA, SZ_4K);
	if (am_mper_base_addr == NULL) {
		pr_err("error to ioremap MCIU_PHYS_BASE\n");
		return -ENOENT;
	}
	return 0;
}

static void deinit_gnss_base_addr(void)
{
	if (am_mapb_base_addr)
		am_mapb_base_addr = NULL;

	if (am_apmu_base_addr)
		am_apmu_base_addr = NULL;

	if (am_mciu_base_addr)
		am_mciu_base_addr = NULL;

	if (am_mper_base_addr) {
		shm_unmap(AM_MPER_PA, am_mper_base_addr);
		am_mper_base_addr = NULL;
	}
}

#define GNSS_PERIPHERAL_BASE  am_mper_base_addr


#define CIU_SYS_SEC_CTRL      (am_mciu_base_addr + 0x5C)
#define APB_SPARE9_REG        (am_mapb_base_addr + 0x120)

#define PMUA_GNSS_PWR_STATUS  (am_apmu_base_addr + 0xf0)
#define PMUA_GNSS_PWR_CTRL    (am_apmu_base_addr + 0xfc)

#define APB_ANAGRP_PWR_ON_OFFSET                      0

#define GNSS_HW_MODE_OFFSET                           0
#define GNSS_RESET_OFFSET                             1
#define GNSS_ISOB_OFFSET                              2
#define GNSS_PWR_ON1_OFFSET                           3
#define GNSS_PWR_ON2_OFFSET                           4
#define GNSS_FUSE_LOAD_START_OFFSET                   5
#define GNSS_FUSE_LOAD_DONE_MASK_OFFSET               6

#define GNSS_AUTO_PWR_ON_OFFSET                       8
#define GNSS_PWR_STAT_OFFSET                          8
#define GNSS_SD_PWR_STAT_OFFSET                       9

#define GNSS_WAKEUP_EN                                0
#define GNSS_WAKEUP_CLR                               1
#define GNSS_WAKEUP_STATUS                            2

#define CIU_GPS_HANDSHAKE                (am_mciu_base_addr + 0x168)
#define CIU_GNSS_HANDSHAKE               (am_mciu_base_addr + 0x168)
#define CIU_GNSS_SQU_START_ADDR          (am_mciu_base_addr + 0x16c)
#define CIU_GNSS_SQU_END_ADDR            (am_mciu_base_addr + 0x170)
#define CIU_GNSS_DDR_START_ADDR          (am_mciu_base_addr + 0x174)
#define CIU_GNSS_DDR_END_ADDR            (am_mciu_base_addr + 0x178)
#define CIU_GNSS_DMA_DDR_START_ADDR      (am_mciu_base_addr + 0x17c)
#define CIU_GNSS_DMA_DDR_END_ADDR        (am_mciu_base_addr + 0x180)

#define GNSS_IRQ_CLR_OFFSET           0
#define AP_GNSS_WAKEUP_OFFSET         1
#define GNSS_CODEINIT_DONE_OFFSET     2
#define GNSS_CODEINIT_RDY_OFFSET      3
#define GNSS_IRQ_OUT_OFFSET           4

static inline void gnss_set_init_done(void)
{
	u32 ciu_reg_v;

	ciu_reg_v = REG_READ(CIU_GPS_HANDSHAKE);
	ciu_reg_v |= (0x1 << GNSS_CODEINIT_DONE_OFFSET);
	REG_WRITE(ciu_reg_v, CIU_GPS_HANDSHAKE);
}

static inline void gnss_clr_init_done(void)
{
	u32 ciu_reg_v;

	ciu_reg_v = REG_READ(CIU_GPS_HANDSHAKE);
	ciu_reg_v &= ~(0x1 << GNSS_CODEINIT_DONE_OFFSET);
	REG_WRITE(ciu_reg_v, CIU_GPS_HANDSHAKE);
}

/* CIU_SYS_SEC_CTRL bit 29:30 is reserved */
#define CIU_SYS_SEC_CTRL_DEBUG_OFFSET     29
#define CIU_SYS_SEC_CTRL_SECURE_OFFSET    30

static inline void set_gnss_debug_mode(void)
{
	u32 ciu_sys_reg_v;

	ciu_sys_reg_v = REG_READ(CIU_SYS_SEC_CTRL);
	ciu_sys_reg_v |= (0x1 << CIU_SYS_SEC_CTRL_DEBUG_OFFSET);
	REG_WRITE(ciu_sys_reg_v, CIU_SYS_SEC_CTRL);
}

static inline void set_gnss_secure_mode(void)
{
	u32 ciu_sys_reg_v;

	ciu_sys_reg_v = REG_READ(CIU_SYS_SEC_CTRL);
	ciu_sys_reg_v |= (0x1 << CIU_SYS_SEC_CTRL_SECURE_OFFSET);
	REG_WRITE(ciu_sys_reg_v, CIU_SYS_SEC_CTRL);
}

static inline void set_gnss_work_mode(void)
{
	u32 ciu_sys_reg_v;

	ciu_sys_reg_v = REG_READ(CIU_SYS_SEC_CTRL);
	ciu_sys_reg_v &= ~(0x1 << CIU_SYS_SEC_CTRL_DEBUG_OFFSET);
	REG_WRITE(ciu_sys_reg_v, CIU_SYS_SEC_CTRL);
}

static inline void set_gnss_nonsecure_mode(void)
{
	u32 ciu_sys_reg_v;

	ciu_sys_reg_v = REG_READ(CIU_SYS_SEC_CTRL);
	ciu_sys_reg_v &= ~(0x1 << CIU_SYS_SEC_CTRL_SECURE_OFFSET);
	REG_WRITE(ciu_sys_reg_v, CIU_SYS_SEC_CTRL);
}

static inline int is_gnss_in_pm2(void)
{
	int ret;
	u32 pmua_pwr_reg_v;

	pmua_pwr_reg_v = REG_READ(PMUA_GNSS_PWR_STATUS);
	pmua_pwr_reg_v &= (0x1 << GNSS_SD_PWR_STAT_OFFSET);
	if (!pmua_pwr_reg_v)
		ret = 1;
	else
		ret = 0;
	return ret;
}

#define GNSS_PER_SRAM_BASE_ADDR        (am_mper_base_addr + 0x5c)
#define GNSS_PER_DDR_BASE_ADDR         (am_mper_base_addr + 0x58)
#define GNSS_PER_REG_BASE_ADDR         (am_mper_base_addr + 0x54)
#define GNSS_PER_REG_SRAM_MODE_ADDR    (am_mper_base_addr + 0x30)

#define SET_GNSS_SRAM_BASE(addr) REG_WRITE(addr, GNSS_PER_SRAM_BASE_ADDR)
#define SET_GNSS_DDR_BASE(addr) REG_WRITE(addr, GNSS_PER_DDR_BASE_ADDR)
#define SET_GNSS_REG_BASE(addr) REG_WRITE(addr, GNSS_PER_REG_BASE_ADDR)

#define SET_GNSS_SRAM_START(addr) REG_WRITE(addr, CIU_GNSS_SQU_START_ADDR)
#define SET_GNSS_SRAM_END(addr) REG_WRITE(addr, CIU_GNSS_SQU_END_ADDR)
#define SET_GNSS_DDR_START(addr) REG_WRITE(addr, CIU_GNSS_DDR_START_ADDR)
#define SET_GNSS_DDR_END(addr) REG_WRITE(addr, CIU_GNSS_DDR_END_ADDR)
#define SET_GNSS_DMA_START(addr) REG_WRITE(addr, CIU_GNSS_DMA_DDR_START_ADDR)
#define SET_GNSS_DMA_END(addr) REG_WRITE(addr, CIU_GNSS_DMA_DDR_END_ADDR)


/*switch memory to all sram mode*/
/* all sram used by ap*/
#define GNSS_ALL_SRAM_MODE() REG_WRITE(0x0, GNSS_PER_REG_SRAM_MODE_ADDR)
/* half sram used by ap */
#define GNSS_HALF_SRAM_MODE() REG_WRITE(0x2, GNSS_PER_REG_SRAM_MODE_ADDR)
#define GNSS_HALF_SRAM_MODE_V2() REG_WRITE(0x1, GNSS_PER_REG_SRAM_MODE_ADDR)
/*  no sram can be used by ap */
#define GNSS_NO_SRAM_MODE() REG_WRITE(0x3, GNSS_PER_REG_SRAM_MODE_ADDR)


#endif
