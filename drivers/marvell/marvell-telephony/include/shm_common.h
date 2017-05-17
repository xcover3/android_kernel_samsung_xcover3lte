/*
    shm_common.h

    Marvell PXA9XX ACIPC-MSOCKET driver for Linux
    Copyright (C) 2010 Marvell International Ltd.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2 as
    published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _SHM_COMMON_H_
#define _SHM_COMMON_H_

#include <linux/dma-mapping.h>

#define SHM_PACKET_PTR(b, n, sz) ((unsigned char *)(b) + (n) * (sz))

/* get the next slot in ring buffer */
static inline int __shm_get_next_slot(int total, int slot)
{
	return slot + 1 == total ? 0 : slot + 1;
}

/* check if ring buffer available */
static inline bool __shm_is_full(int total, int wptr, int rptr)
{
	return __shm_get_next_slot(total, wptr) == rptr;
}

/* check if ring buffer empty */
static inline bool __shm_is_empty(int wptr, int rptr)
{
	return wptr == rptr;
}

static inline int __shm_free_slot(int rptr, int wptr, int total)
{
	int free = rptr - __shm_get_next_slot(total, wptr);
	if (free < 0)
		free += total;
	return free;
}

static inline void __shm_flush_dcache(void *addr, size_t size)
{
	dma_addr_t dma = 0;
#ifdef CONFIG_ARM
	dma = virt_to_dma(NULL, addr);
#elif defined CONFIG_ARM64
	dma = phys_to_dma(NULL, virt_to_phys(addr));
#else
#error unsupported arch
#endif
	dma_sync_single_for_device(NULL, dma,
		size, DMA_TO_DEVICE);
}

static inline void __shm_invalidate_dcache(void *addr, size_t size)
{
	dma_addr_t dma = 0;
#ifdef CONFIG_ARM
	dma = virt_to_dma(NULL, addr);
#elif defined CONFIG_ARM64
	dma = phys_to_dma(NULL, virt_to_phys(addr));
#else
#error unsupported arch
#endif
	dma_sync_single_for_device(NULL, dma,
		size, DMA_FROM_DEVICE);
}

#endif /* _SHM_COMMON_H_ */
