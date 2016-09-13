/** 
 * \file mem_alloc.c
 * 
 * \brief IHK-Host: Generic page allocator (not so fast)
 *
 * \author Taku Shimosawa  <shimosawa@is.s.u-tokyo.ac.jp> \par
 * Copyright (C) 2011 - 2012  Taku Shimosawa
 * 
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/bitops.h>

#include <ihk/ihk_host_driver.h>

/** \brief Descriptor of an allocator */
struct ihk_page_allocator_desc {
	/** \brief Start address of the area that the allocator manages */
	unsigned long start;
	/** \brief End address of the area that the allocator manages */
	unsigned long end;
	/** \brief Last index in block */
	unsigned int last;
	/** \brief Number of blocks */
	unsigned int count;
	/** \brief Order of the pages that are allocated for this structure */
	unsigned int flag;
	/** \brief Shift count of a block in this allocator */
	unsigned int shift;
	/** \brief Lock for this structure */
	spinlock_t lock;
	/** \brief List chain for multiple allocators */
	struct list_head list;
	/** \brief Allocation map */
	unsigned long map[0];
};

/** Get the index in the map array */
#define MAP_INDEX(n)    ((n) >> 6)
/** Get the bit number in a map element */
#define MAP_BIT(n)      ((n) & 0x3f)
/** Calculate an address from the map index and map bit index */
#define ADDRESS(desc, index, bit)    \
	((desc)->start + (((index) * 64 + (bit)) << ((desc)->shift)))

/**
 * \brief Initialize a page allocator.
 *
 * \param start Start address of the memory area will this allocator will
 *              manage.
 * \param size  Size of the memory area which this allocator will manage.
 * \param unit  Size of a block, which is a minimum memory area that this
 *              allocator will allocate and free.
 *              Due to the implementation limit, unit should be a power of 2.
 * \return Pointer to the allocator descriptor.
 *         The pointer should be passed to other allocator functions.
 */
void *ihk_pagealloc_init(unsigned long start, unsigned long size,
                         unsigned long unit)
{
	/* Unit must be power of 2, and size and start must be unit-aligned */
	struct ihk_page_allocator_desc *desc;
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
		printk("IHK: failed to allocate page-allocator-desc "\
		       "(%lx, %lx, %lx)\n", start, size, unit);
		return NULL;
	}

	desc->start = start;
	desc->last = 0;
	desc->count = mapaligned >> 3;
	desc->shift = page_shift;
	desc->flag = flag;
	spin_lock_init(&desc->lock);

	/* Reserve align padding area */
	for (i = mapsize; i < mapaligned * 8; i++) {
		desc->map[MAP_INDEX(i)] |= (1 << MAP_BIT(i));
	}

	return desc;
}

/** \brief Finalize a page allocator */
void ihk_pagealloc_destroy(void *__desc)
{
	struct ihk_page_allocator_desc *desc = __desc;
	if (desc->flag) {
		free_pages((unsigned long)desc, desc->flag);
	} else {
		kfree(desc);
	}
}

/**
 * \brief Internal function for the allocation of large blocks
 *
 * If the size of block to allocate is more than 32, the allocator allocates
 * by the unit of 64 blocks (because the map uses bitfield, and unsigned long
 * is 64 bit).
 * \param desc    Pointer to the allocator descriptor.
 * \param nblocks Number of large blocks (in 64 blocks)
 * \return Address of the allocated block. 0 if failed. 
 */
static unsigned long __ihk_pagealloc_large(struct ihk_page_allocator_desc *desc,
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

/**
 * \brief Allocates a memory area.
 *
 * \param __desc  Pointer to an allocator descriptor.
 * \param npages  Number of blocks to allocate
 * \return Address of the allocated block. 0 if failed. 
 */
unsigned long ihk_pagealloc_alloc(void *__desc, int npages)
{
	struct ihk_page_allocator_desc *desc = __desc;
	unsigned int i, mi;
	int j;
	unsigned long v, mask, flags;

	/* If requested page is more than the half of the element,
	 * we allocate the whole element (ulong) */
	if (npages >= 32) {
		return __ihk_pagealloc_large(desc, (npages + 63) >> 6);
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

/**
 * \brief Wrapper function of ihk_pagealloc_alloc.
 *
 * This function accepts a size in byte, instead of block.
 * \param __desc  Pointer to an allocator descriptor.
 * \param size    Number of bytes to allocate
 * \return Address of the allocated block. 0 if failed. 
 */
unsigned long ihk_pagealloc_alloc_size(void *__desc, unsigned long size)
{
	struct ihk_page_allocator_desc *desc = __desc;

	return ihk_pagealloc_alloc(desc, size >> desc->shift);
}

/**
 * \brief Free one or more memory blocks.
 *
 * \param __desc  Pointer to an allocator descriptor.
 * \param address Start address of the block to free
 * \param npages  Number of blocks to free
 *
 * \note npages should be the same number as used in the allocate function.
 */
void ihk_pagealloc_free(void *__desc, unsigned long address, int npages)
{
	struct ihk_page_allocator_desc *desc = __desc;
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

/** \brief Wrapper function for ihk_pagealloc_free in the unit of byte */
void ihk_pagealloc_free_size(void *__desc, unsigned long address,
                                      unsigned long size)
{
	struct ihk_page_allocator_desc *desc = __desc;

	ihk_pagealloc_free(desc, address, size >> desc->shift);
}

EXPORT_SYMBOL(ihk_pagealloc_init);
EXPORT_SYMBOL(ihk_pagealloc_destroy);
EXPORT_SYMBOL(ihk_pagealloc_alloc);
EXPORT_SYMBOL(ihk_pagealloc_free);
EXPORT_SYMBOL(ihk_pagealloc_alloc_size);
EXPORT_SYMBOL(ihk_pagealloc_free_size);

