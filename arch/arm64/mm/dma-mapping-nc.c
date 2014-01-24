/*
 * linux/arch/arm64/mm/dma-mapping-nc.c
 *
 * Copyright:   (C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/gfp.h>
#include <linux/export.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <linux/vmalloc.h>

#include <asm/cacheflush.h>

static void *arm64_dma_alloc(
		struct device *dev,
		size_t size,
		dma_addr_t *dma_handle,
		gfp_t gfp,
		struct dma_attrs *attrs)
{
	struct page *page;
	void *ptr, *coherent_ptr;
	phys_addr_t phys_addr;
	int order;

	size = PAGE_ALIGN(size);
	order = get_order(size);

	page = alloc_pages(gfp, order);
	if (!page) {
		*dma_handle = ~0;
		return NULL;
	}

	if (!page && cma_available)
		page = dma_alloc_from_contiguous(dev,
			size >> PAGE_SHIFT, order);

	phys_addr = page_to_phys(page);
	ptr = phys_to_virt(phys_addr);
	__dma_flush_range(ptr, ptr + size);

	coherent_ptr = (void *)__phys_to_coherent_virt(phys_addr);
	*dma_handle = phys_to_dma(dev, phys_addr);

	return coherent_ptr;
}

static void arm64_dma_free(
		struct device *dev,
		size_t size,
		void *vaddr,
		dma_addr_t dma_handle,
		struct dma_attrs *attrs)
{
	phys_addr_t phys_addr = dma_to_phys(dev, dma_handle);
	struct page *page = phys_to_page(phys_addr);
	int order;

	size = PAGE_ALIGN(size);
	order = get_order(size);

	__free_pages(page, order);
}

static dma_addr_t arm64_dma_map_page(
		struct device *dev,
		struct page *page,
		unsigned long offset,
		size_t size,
		enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	phys_addr_t phys_addr = page_to_phys(page) + offset;

	if (!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		__dma_map_area(phys_to_virt(phys_addr), size, dir);

	return phys_to_dma(dev, phys_addr);
}

static void arm64_dma_unmap_page(
		struct device *dev,
		dma_addr_t dma_handle,
		size_t size,
		enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	if (!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs)) {
		phys_addr_t phys_addr = dma_to_phys(dev, dma_handle);
		__dma_unmap_area(phys_to_virt(phys_addr), size, dir);
	}
}

static int arm64_dma_map_sg(
		struct device *dev,
		struct scatterlist *sg,
		int nents,
		enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		phys_addr_t phys_addr = sg_phys(s);
		s->dma_address = phys_to_dma(dev, phys_addr);
#ifdef CONFIG_NEED_SG_DMA_LENGTH
		s->dma_length = s->length;
#endif
		if (!dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
			__dma_map_area(phys_to_virt(phys_addr),
					s->length, dir);
	}
	return nents;
}

static void arm64_dma_unmap_sg(
		struct device *dev,
		struct scatterlist *sg,
		int nents,
		enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	struct scatterlist *s;
	int i;

	if (dma_get_attr(DMA_ATTR_SKIP_CPU_SYNC, attrs))
		return;

	for_each_sg(sg, s, nents, i) {
		phys_addr_t phys_addr = sg_phys(s);
		__dma_unmap_area(phys_to_virt(phys_addr), s->length, dir);
	}
}

static void arm64_dma_sync_single_for_cpu(
		struct device *dev,
		dma_addr_t dma_handle,
		size_t size,
		enum dma_data_direction dir)
{
	phys_addr_t phys_addr = dma_to_phys(dev, dma_handle);
	__dma_unmap_area(phys_to_virt(phys_addr), size, dir);
}

static void arm64_dma_sync_single_for_device(
		struct device *dev,
		dma_addr_t dma_handle,
		size_t size,
		enum dma_data_direction dir)
{
	phys_addr_t phys_addr = dma_to_phys(dev, dma_handle);
	__dma_map_area(phys_to_virt(phys_addr), size, dir);
}

static void arm64_dma_sync_sg_for_cpu(
		struct device *dev,
		struct scatterlist *sg,
		int nents,
		enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		phys_addr_t phys_addr = sg_phys(s);
		__dma_unmap_area(phys_to_virt(phys_addr), s->length, dir);
	}
}

static void arm64_dma_sync_sg_for_device(
		struct device *dev,
		struct scatterlist *sg,
		int nents,
		enum dma_data_direction dir)
{
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i) {
		phys_addr_t phys_addr = sg_phys(s);
		__dma_map_area(phys_to_virt(phys_addr), s->length, dir);
	}
}

static int arm64_dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

static int arm64_dma_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return 0;
}

struct dma_map_ops arm64_dma_ops = {
	.alloc			= arm64_dma_alloc,
	.free			= arm64_dma_free,
	.map_page		= arm64_dma_map_page,
	.unmap_page		= arm64_dma_unmap_page,
	.map_sg			= arm64_dma_map_sg,
	.unmap_sg		= arm64_dma_unmap_sg,
	.sync_single_for_cpu	= arm64_dma_sync_single_for_cpu,
	.sync_single_for_device	= arm64_dma_sync_single_for_device,
	.sync_sg_for_cpu	= arm64_dma_sync_sg_for_cpu,
	.sync_sg_for_device	= arm64_dma_sync_sg_for_device,
	.dma_supported		= arm64_dma_supported,
	.mapping_error		= arm64_dma_mapping_error,
};
EXPORT_SYMBOL(arm64_dma_ops);

struct dma_map_ops *dma_ops = &arm64_dma_ops;
EXPORT_SYMBOL(dma_ops);

#define PREALLOC_DMA_DEBUG_ENTRIES      4096

static int __init dma_debug_do_init(void)
{
	dma_debug_init(PREALLOC_DMA_DEBUG_ENTRIES);
	return 0;
}
fs_initcall(dma_debug_do_init);
