/*
 * AAL - Generic page allocator
 * (C) Copyright 2011 Taku Shimosawa.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/bitops.h>

#include <aal/aal_host_driver.h>

struct aal_page_allocator_desc {
	unsigned long start;
	unsigned int last;
	unsigned int count;
	unsigned int flag;
	unsigned int shift;
	spinlock_t lock;
	unsigned int pad;
	
	unsigned long map[0];
};

#define MAP_INDEX(n)    ((n) >> 6)
#define MAP_BIT(n)      ((n) & 0x3f)
#define ADDRESS(desc, index, bit)    \
	((desc)->start + (((index) * 64 + (bit)) << ((desc)->shift)))

void *aal_pagealloc_init(unsigned long start, unsigned long size,
                         unsigned long unit)
{
	/* Unit must be power of 2, and size and start must be unit-aligned */
	struct aal_page_allocator_desc *desc;
	int i, page_shift, descsize, descorder, mapsize, mapaligned;
	int flag = 0;

	if (!unit) {
		return NULL;
	}
	page_shift = fls(unit) - 1;

	/* round up to 64-bit */
	mapsize = (size >> page_shift);
	mapaligned = ((mapsize + 63) >> 6) << 3;
	descsize = sizeof(*desc) + mapaligned;

	printk("mapsize = %d, aligned = %d, descsize = %d, shift = %d\n",
	       mapsize, mapaligned, descsize, page_shift);
	
	if (descsize >= PAGE_SIZE) {
		descsize = (descsize + PAGE_SIZE - 1) >> PAGE_SHIFT;
		descorder = fls(descsize) - 1;

		if ((1 << descorder) < descsize) {
			descorder++;
		}
		desc = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
		                                descorder);

		flag = descorder;
	} else {
		desc = kzalloc(descsize, GFP_KERNEL);
	}
	if (!desc) {
		printk("AAL: failed to allocate page-allocator-desc "\
		       "(%lx, %lx, %lx)\n", start, size, unit);
		return NULL;
	}

	desc->start = start;
	desc->last = 0;
	desc->count = mapaligned;
	desc->shift = page_shift;
	desc->flag = flag;
	spin_lock_init(&desc->lock);

	/* Reserve align padding area */
	for (i = mapsize; i < mapaligned * 8; i++) {
		desc->map[MAP_INDEX(i)] |= (1 << MAP_BIT(i));
	}

	return desc;
}

void aal_pagealloc_destroy(void *__desc)
{
	struct aal_page_allocator_desc *desc = __desc;
	if (desc->flag) {
		free_pages((unsigned long)desc, desc->flag);
	} else {
		kfree(desc);
	}
}

static unsigned long __aal_pagealloc_large(struct aal_page_allocator_desc *desc,
                                           int nblocks)
{
	unsigned long flags;
	unsigned int i, j, mi;

	printk("pagealloc_large_request : %d\n", nblocks);

	spin_lock_irqsave(&desc->lock, flags);
	for (i = 0, mi = desc->last; i < desc->count; i++, mi++) {
		if (mi > desc->count) {
			mi = 0;
		}
		if (mi + nblocks >= desc->count) {
			continue;
		}
		for (j = mi; j < mi + nblocks; j++) {
			if (desc->map[j]) {
				break;
			}
		}
		if (j == mi + nblocks) {
			for (j = mi; j < mi + nblocks; j++) {
				desc->map[j] = (unsigned long)-1;
			}
			spin_unlock_irqrestore(&desc->lock, flags);

			return ADDRESS(desc, mi, 0);
		}
	}
	
	spin_unlock_irqrestore(&desc->lock, flags);

	return 0;
}

unsigned long aal_pagealloc_alloc(void *__desc, int npages)
{
	struct aal_page_allocator_desc *desc = __desc;
	unsigned int i, mi;
	int j;
	unsigned long v, mask, flags;

	/* If requested page is more than the half of the element,
	 * we allocate the whole element (ulong) */
	printk("pagealloc_request : %d\n", npages);
	if (npages >= 32) {
		return __aal_pagealloc_large(desc, (npages + 63) >> 6);
	}

	mask = (1 << npages) - 1;

	spin_lock_irqsave(&desc->lock, flags);
	for (i = 0, mi = desc->last; i < desc->count; i++, mi++) {
		if (mi > desc->count) {
			mi = 0;
		}
		
		v = desc->map[mi];
		if (v == (unsigned long)-1)
			continue;
		
		for (j = 0; j <= 64 - npages; j++) {
			if (!(v & (mask << j))) { /* free */
				desc->map[mi] |= (mask << j);

				spin_unlock_irqrestore(&desc->lock, flags);
				return ADDRESS(desc, mi, j);
			}
		}
	}
	spin_unlock_irqrestore(&desc->lock, flags);

	/* We use null pointer for failure */
	return 0;
}

void aal_pagealloc_free(void *__desc, unsigned long address, int npages)
{
	struct aal_page_allocator_desc *desc = __desc;
	int i;
	unsigned mi;
	unsigned long flags;

	/* XXX: Parameter check */
	
	spin_lock_irqsave(&desc->lock, flags);
	mi = (address - desc->start) >> desc->shift;
	for (i = 0; i < npages; i++, mi++) {
		desc->map[MAP_INDEX(mi)] &= ~(1UL << MAP_BIT(mi));
	}
	spin_unlock_irqrestore(&desc->lock, flags);
}


EXPORT_SYMBOL(aal_pagealloc_init);
EXPORT_SYMBOL(aal_pagealloc_destroy);
EXPORT_SYMBOL(aal_pagealloc_alloc);
EXPORT_SYMBOL(aal_pagealloc_free);

