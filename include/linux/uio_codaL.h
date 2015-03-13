/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * (C) Copyright 2012 Marvell International Ltd.
 * All Rights Reserved
 */

#ifndef _UIO_CODAL_H_
#define _UIO_CODAL_H_

#define CODAL_IOC_MAGIC 'C'
#define CODAL_POWER_ON		_IO(CODAL_IOC_MAGIC, 1)
#define CODAL_POWER_OFF		_IO(CODAL_IOC_MAGIC, 2)
#define CODAL_CLK_ON		_IO(CODAL_IOC_MAGIC, 3)
#define CODAL_CLK_OFF		_IO(CODAL_IOC_MAGIC, 4)
#define CODAL_LOCK		_IOW(CODAL_IOC_MAGIC, 5, unsigned int)
#define CODAL_UNLOCK		_IO(CODAL_IOC_MAGIC, 6)
#define CODAL_GETSET_INFO	_IOW(CODAL_IOC_MAGIC, 7, unsigned int)
#define CODAL_GET_JPU_STATUS	_IOW(CODAL_IOC_MAGIC, 8, unsigned int)
#define UIO_CODAL_NAME	"pxa-codaL"

#define VPU_FEATURE_BITMASK_SRAM(n)	((n & 0x7) << 0)
#define SRAM_EXTERNAL	0
#define SRAM_INTERNAL	1

#define VPU_FEATURE_BITMASK_NV21(n)	((n & 0x7) << 3)
#define NV21_NONE		0
#define NV21_SUPPORT	1

#define VPU_FEATURE_BITMASK_NO2NDAXI(n)	((n & 0x7) << 6)
#define SECONDAXI_NONE	1
#define SECONDAXI_SUPPORT	0

/* definition should be same as user space lib*/
#define VPU_CODEC_TYPEID_ENC_H264	0x1
#define VPU_CODEC_TYPEID_ENC_MPEG4	0x2
#define VPU_CODEC_TYPEID_ENC_H263	0x4
#define VPU_CODEC_TYPEID_ENC_JPG	0x8
#define VPU_CODEC_TYPEID_ENC_MASK	0xf
#define VPU_CODEC_TYPEID_DEC_H264	0x10000
#define VPU_CODEC_TYPEID_DEC_MPEG4	0x20000
#define VPU_CODEC_TYPEID_DEC_H263	0x40000
#define VPU_CODEC_TYPEID_DEC_MPEG2	0x80000
#define VPU_CODEC_TYPEID_DEC_VC1	0x100000
#define VPU_CODEC_TYPEID_DEC_JPG	0x200000
#define VPU_CODEC_TYPEID_DEC_RV	0x400000
#define VPU_CODEC_TYPEID_DEC_DIV3	0x800000

#endif /* _UIO_CODAL_H_ */
