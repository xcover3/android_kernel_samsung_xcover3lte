#include <linux/bitops.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/m4u.h>

#define MAX_PLANE_PER_FRAME	5

struct m4u_plane {
	/* used internally */
	size_t	offset;		/* plane offset from buffer start */
	size_t	size;		/* plane size */
	size_t	capacity;	/* size of dma coherent, in bytes */
	long	flag;
	struct m4u_dscr	*dscr_cpu;
	struct m4u_frame	*frame;	/* point at container frame */

	/* expose to bdt user */
	struct m4u_bdt	bdt;
};

struct m4u_frame {
	/* internal use */
	spinlock_t	lock;	/* MUST hold this lock when access any member */
	long		ref_map;
	struct m4u_plane	plane[MAX_PLANE_PER_FRAME];
};

static inline struct m4u_frame *m4u_alloc_frame(void)
{
	struct m4u_frame *frame;
	int i;

	frame = kzalloc(sizeof(struct m4u_frame), GFP_KERNEL);
	if (unlikely(WARN_ON(frame == NULL)))
		return NULL;
	spin_lock_init(&frame->lock);
	frame->ref_map = 0;
	for (i = 0; i < MAX_PLANE_PER_FRAME; i++)
		frame->plane[i].frame = frame;
	return frame;
}

/* only apply to none-reserved frames */
static inline void m4u_free_frame(struct m4u_frame *frm)
{
	int i;
	for (i = 0; i < MAX_PLANE_PER_FRAME; i++) {
		struct m4u_plane *plane = &frm->plane[i];
		plane->offset = 0;
		plane->size = 0;
		plane->frame = NULL;
		if (plane->bdt.dscr_dma)
			dma_free_coherent(NULL,	plane->capacity,
				plane->dscr_cpu, plane->bdt.dscr_dma);
	}
	kfree(frm);
}

static inline int m4u_fill_frame_by_sg(struct m4u_frame *frm,
			struct sg_table *sgt, unsigned long align_mask_size,
			unsigned long align_mask_addr)
{
	struct scatterlist *sg;
	struct m4u_dscr	*dscr_cpu;
	dma_addr_t new_addr, dscr_dma;
	int nr_dscr = 0, size = 0, i, j;

	/* Find out the buffer size and dis-contiguous segments */
	sg = sgt->sgl;
	new_addr = sg_dma_address(sg) - 1;
	nr_dscr = 0;
	for (i = 0; i < sgt->nents; i++, sg = sg_next(sg)) {
#ifdef CONFIG_NEED_SG_DMA_LENGTH
		sg->dma_length = sg->length;
#endif
		if ((sg_dma_len(sg) & align_mask_size) ||
			(sg_dma_address(sg) & align_mask_addr)) {
			pr_err("the length or address of sg is not aligned\n");
			return -EINVAL;
		}
		if (sg_dma_address(sg) != new_addr)
			nr_dscr++;
		new_addr = sg_dma_address(sg) + sg_dma_len(sg);
		size += sg_dma_len(sg);
	}

	/* setup buffer descriptor table */
	dscr_cpu = dma_alloc_coherent(NULL, nr_dscr * sizeof(struct m4u_dscr),
					&dscr_dma, GFP_KERNEL);
	if (unlikely(WARN_ON(dscr_cpu == NULL)))
		return -ENOMEM;
	memset(dscr_cpu, 0, nr_dscr * sizeof(struct m4u_dscr));
	sg = sgt->sgl;
	new_addr = sg_dma_address(sg) - 1;
	for (i = 0, j = -1; i < sgt->nents; i++, sg = sg_next(sg)) {
		if (sg_dma_address(sg) != new_addr) {
			j++;
			dscr_cpu[j].dma_addr = sg_dma_address(sg);
		}
		dscr_cpu[j].dma_size += sg_dma_len(sg);
		new_addr = sg_dma_address(sg) + sg_dma_len(sg);
	}
	dsb(sy);

	{
		/* make plane 0 to point at Buffer Descriptor Table */
		unsigned long flag;
		struct m4u_plane *plane = &frm->plane[0];
		spin_lock_irqsave(&frm->lock, flag);
		plane->offset = 0;
		plane->size = size;
		plane->capacity = nr_dscr * sizeof(struct m4u_dscr);
		plane->dscr_cpu = dscr_cpu;
		plane->bdt.dscr_dma = dscr_dma;
		plane->bdt.dscr_cnt = nr_dscr;
		plane->bdt.bpd = sizeof(struct m4u_dscr);

		spin_unlock_irqrestore(&frm->lock, flag);
	}
	return 0;
}

static inline struct m4u_bdt *m4u_frame_to_bdt(struct m4u_frame *frame,
						size_t offset, size_t size)
{
	unsigned long flag;
	struct m4u_plane *plane;
	struct m4u_dscr	*entry;
	size_t cnt;
	int i, p;
	size_t __offset = offset;

	/*
	 * By this point, the Buffer Descriptor Table had already been setup in
	 * plane[0], the left part is to get a clip of it.
	 */
	for (i = 0; i < MAX_PLANE_PER_FRAME; i++) {
		plane = &frame->plane[i];
		if ((plane->offset == offset) && (plane->size >= size)) {
			p = i;
			goto plane_ready;
		}
	}

	/* The clip of BDT is not ready for this plane, now create one */
	for (i = 0; i < MAX_PLANE_PER_FRAME; i++) {
		plane = &frame->plane[i];
		if (plane->dscr_cpu == NULL) {
			p = i;
			goto empty_plane;
		}
	}
	/* No space for the new plane, consider enlarge MAX_PLANE_PER_FRAME? */
	WARN_ON(1);
	return NULL;

empty_plane:
	entry = frame->plane[0].dscr_cpu; /* plane 0 holds the entire BDT */
	cnt = frame->plane[0].bdt.dscr_cnt;
	for (i = 0; i < frame->plane[0].size; i++) {
		if (offset < entry->dma_size)
			goto head_find;
		offset -= entry->dma_size;
		entry++;
		cnt--;
	}
	/* offset is out of the buffer */
	WARN_ON(1);
	return NULL;

head_find:
	/*
	 * TODO: this plane actually point to the end of the buffer, better
	 * reduce the size to the required size
	 */
	plane->dscr_cpu = dma_alloc_coherent(NULL, cnt * sizeof(struct m4u_dscr),
					&plane->bdt.dscr_dma, GFP_KERNEL);
	if (unlikely(WARN_ON(plane->dscr_cpu == NULL)))
		return NULL;
	memset(plane->dscr_cpu, 0, cnt * sizeof(struct m4u_dscr));
	plane->offset = __offset;
	plane->size = frame->plane[0].size - __offset;
	plane->capacity = cnt * sizeof(struct m4u_dscr);
	memcpy(plane->dscr_cpu, entry, cnt * sizeof(struct m4u_dscr));
	plane->dscr_cpu[0].dma_addr += offset;
	plane->dscr_cpu[0].dma_size -= offset;
	plane->bdt.dscr_cnt = cnt;
	plane->bdt.bpd = sizeof(struct m4u_dscr);

plane_ready:
	spin_lock_irqsave(&frame->lock, flag);
	set_bit(p, &frame->ref_map);
	spin_unlock_irqrestore(&frame->lock, flag);
	return &plane->bdt;
}

static int m4u_release_frame(void *meta)
{
	struct m4u_frame *frame = meta;
	WARN_ON(frame->ref_map);
	m4u_free_frame(frame);
	return 0;
}

void m4u_put_bdt(struct m4u_bdt *bdt)
{
	struct m4u_frame *frame;
	struct m4u_plane *plane;
	unsigned long flag;

	if (WARN_ON(bdt == NULL))
		return;

	plane = container_of(bdt, struct m4u_plane, bdt);
	frame = plane->frame;
	spin_lock_irqsave(&frame->lock, flag);
	clear_bit(plane - frame->plane, &frame->ref_map);
	spin_unlock_irqrestore(&frame->lock, flag);
}

struct m4u_bdt *m4u_get_bdt(struct dma_buf *dbuf, struct sg_table *sgt,
			size_t offset, size_t size, unsigned long align_mask_size,
			unsigned long align_mask_addr)
{
	struct m4u_frame *frame;
	struct m4u_bdt *bdt;
	int ret;

	if (unlikely(WARN_ON(dbuf == NULL)))
		return NULL;

	frame = dma_buf_meta_fetch(dbuf, M4U_DMABUF_META_ID);
	if (frame)
		goto get_bdt;

	if (WARN_ON(sgt == NULL))
		return NULL;

	frame = m4u_alloc_frame();
	if (unlikely(WARN_ON(frame == NULL)))
		return NULL;

	ret = m4u_fill_frame_by_sg(frame, sgt,
			align_mask_size, align_mask_addr);
	if (WARN_ON(ret < 0)) {
		m4u_free_frame(frame);
		return NULL;
	}

	ret = dma_buf_meta_attach(dbuf, M4U_DMABUF_META_ID, frame,
					&m4u_release_frame);
	if (WARN_ON(ret < 0)) {
		m4u_free_frame(frame);
		return NULL;
	}

get_bdt:
	if (offset + size > frame->plane[0].size) {
		pr_err("size error: offset %x, sz %x, plane sz %x\n",
			(u32)offset, (u32)size, (u32)frame->plane[0].size);
		return NULL;
	}

	bdt = m4u_frame_to_bdt(frame, offset, size);
	if (bdt == NULL) {
		if (frame->ref_map == 0)
			m4u_release_frame(frame);
		return NULL;
	}

	return bdt;
}
