/*
 * Copyright (c) 2006-2014 MStar Semiconductor, Inc.
 * All rights reserved.

 * Unless otherwise stipulated in writing, any and all information contained
 * herein regardless in any format shall remain the sole proprietary of
 * MStar Semiconductor Inc. and be kept in strict confidence
 * (??MStar Confidential Information??) by the recipient.
 * Any unauthorized act including without limitation unauthorized disclosure,
 * copying, use, reproduction, sale, distribution, modification, disassembling,
 * reverse engineering and compiling of the contents of MStar Confidential
 * Information is unlawful and strictly prohibited. MStar hereby reserves the
 * rights to any and all damages, losses, costs and expenses resulting
 * therefrom.
 */

/*
 *
 * @file    mstar_drv_utility_adaption.h
 *
 * @brief   This file defines the interface of touch screen
 *
 *
 */

#ifndef __MSTAR_DRV_UTILITY_ADAPTION_H__
#define __MSTAR_DRV_UTILITY_ADAPTION_H__ (1)


#include "mstar_drv_common.h"

#define BK_REG8_WL(addr, val)    (RegSetLByteValue(addr, val))
#define BK_REG8_WH(addr, val)    (RegSetHByteValue(addr, val))
#define BK_REG16_W(addr, val)    (RegSet16BitValue(addr, val))
#define BK_REG8_RL(addr)        (RegGetLByteValue(addr))
#define BK_REG8_RH(addr)        (RegGetHByteValue(addr))
#define BK_REG16_R(addr)        (RegGet16BitValue(addr))

#define PRINTF_EMERG(fmt, ...)  pr_emerg(pr_fmt(fmt), ##__VA_ARGS__)
#define PRINTF_ALERT(fmt, ...)  pr_alert(pr_fmt(fmt), ##__VA_ARGS__)
#define PRINTF_CRIT(fmt, ...)   pr_crit(pr_fmt(fmt), ##__VA_ARGS__)
#define PRINTF_ERR(fmt, ...)    pr_err(pr_fmt(fmt), ##__VA_ARGS__)
#define PRINTF_WARN(fmt, ...)   pr_warn(pr_fmt(fmt), ##__VA_ARGS__)
#define PRINTF_NOTICE(fmt, ...) pr_notice(pr_fmt(fmt), ##__VA_ARGS__)
#define PRINTF_INFO(fmt, ...)   pr_info(pr_fmt(fmt), ##__VA_ARGS__)
#define PRINTF_DEBUG(fmt, ...)  pr_debug(pr_fmt(fmt), ##__VA_ARGS__)

extern u16  RegGet16BitValue(u16 nAddr);
extern u8   RegGetLByteValue(u16 nAddr);
extern u8   RegGetHByteValue(u16 nAddr);
extern void RegSet16BitValue(u16 nAddr, u16 nData);
extern void RegSetLByteValue(u16 nAddr, u8 nData);
extern void RegSetHByteValue(u16 nAddr, u8 nData);
extern void RegSet16BitValueOn(u16 nAddr, u16 nData);
extern void RegSet16BitValueOff(u16 nAddr, u16 nData);
extern u16  RegGet16BitValueByAddressMode(u16 nAddr,
		enum AddressMode_e eAddressMode);
extern void RegSet16BitValueByAddressMode(u16 nAddr, u16 nData,
		enum AddressMode_e eAddressMode);
extern void RegMask16BitValue(u16 nAddr, u16 nMask, u16 nData,
		enum AddressMode_e eAddressMode);
extern void DbBusEnterSerialDebugMode(void);
extern void DbBusExitSerialDebugMode(void);
extern void DbBusIICUseBus(void);
extern void DbBusIICNotUseBus(void);
extern void DbBusIICReshape(void);
extern void DbBusStopMCU(void);
extern void DbBusNotStopMCU(void);
extern s32 IicWriteData(u8 nSlaveId, u8 *pBuf, u16 nSize);
extern s32 IicReadData(u8 nSlaveId, u8 *pBuf, u16 nSize);
extern s32 IicSegmentReadDataByDbBus(u8 nRegBank, u8 nRegAddr, u8 *pBuf,
		u16 nSize, u16 nMaxI2cLengthLimit);
extern s32 IicSegmentReadDataBySmBus(u16 nAddr, u8 *pBuf, u16 nSize,
		u16 nMaxI2cLengthLimit);
extern void mstpMemSet(void *pDst, s8 nVal, u32 nSize);
extern void mstpMemCopy(void *pDst, void *pSource, u32 nSize);
extern void mstpDelay(u32 nTime);

#endif /* __MSTAR_DRV_UTILITY_ADAPTION_H__ */
