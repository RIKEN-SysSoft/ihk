#include <aal/cpu.h>
#include <aal/debug.h>
#include <aal/mm.h>
#include <types.h>
#include <memory.h>
#include <string.h>
#include <errno.h>
#include <list.h>

static char *last_page;
extern char _head[], _end[];

struct aal_mc_pa_ops *pa_ops;

void *early_alloc_page(void)
{
	void *p;

	if (!last_page) {
		last_page = (char *)(((unsigned long)_end + PAGE_SIZE)
		                     & PAGE_MASK);
	} else if (last_page == (void *)-1) {
		panic("Early allocator is already finalized. Do not use it.\n");
	}

	p = last_page;
	last_page += PAGE_SIZE;

	return p;
}

void *arch_alloc_page(enum aal_mc_ap_flag flag)
{
	if (pa_ops)
		return pa_ops->alloc(1, flag);
	else
		return early_alloc_page();
}
void arch_free_page(void *ptr)
{
	if (pa_ops)
		pa_ops->free(ptr, 1);
}

void *get_last_early_heap(void)
{
	return last_page;
}

struct page_table {
	uint64_t entry[PT_ENTRIES];
};

static struct page_table *init_pt;

static unsigned long setup_l2(struct page_table *pt,
                              unsigned long page_head, unsigned long start,
                              unsigned end)
{
	int i;
	unsigned long phys;

	for (i = 0; i < PT_ENTRIES; i++) {
		phys = page_head + ((unsigned long)i << PTL2_SHIFT);

		if (phys + PTL2_SIZE < start || phys >= end) {
			pt->entry[i] = 0;
			continue;
		}

		pt->entry[i] = phys | PFL2_KERN_ATTR | PFL2_SIZE;
	}

	return virt_to_phys(pt);
}

static unsigned long setup_l3(struct page_table *pt,
                              unsigned long page_head, unsigned long start,
                              unsigned end)
{
	int i;
	unsigned long phys, pt_phys;

	for (i = 0; i < PT_ENTRIES; i++) {
		phys = page_head + ((unsigned long)i << PTL3_SHIFT);

		if (phys + PTL3_SIZE < start || phys >= end) {
			pt->entry[i] = 0;
			continue;
		}

		pt_phys = setup_l2(arch_alloc_page(0), phys, start, end);

		pt->entry[i] = pt_phys | PFL3_KERN_ATTR;
	}

	return virt_to_phys(pt);
}

static void init_normal_area(struct page_table *pt)
{
	unsigned long map_start, map_end, phys, pt_phys;
	int ident_index, virt_index;

	map_start = aal_mc_get_memory_address(AAL_MC_GMA_MAP_START, 0);
	map_end = aal_mc_get_memory_address(AAL_MC_GMA_MAP_END, 0);

	kprintf("map_start = %lx, map_end = %lx\n", map_start, map_end);
	ident_index = map_start >> PTL4_SHIFT;
	virt_index = (MAP_ST_START >> PTL4_SHIFT) & (PT_ENTRIES - 1);

	memset(pt, 0, sizeof(struct page_table));

	for (phys = (map_start & ~(PTL4_SIZE - 1)); phys < map_end;
	     phys += PTL4_SIZE) {
		pt_phys = setup_l3(arch_alloc_page(0), phys,
		                   map_start, map_end);

		pt->entry[ident_index++] = pt_phys | PFL4_KERN_ATTR;
		pt->entry[virt_index++] = pt_phys | PFL4_KERN_ATTR;
	}
}

static struct page_table *__alloc_new_pt(void)
{
	struct page_table *newpt = arch_alloc_page(0);

	memset(newpt, 0, sizeof(struct page_table));

	return newpt;
}

#define ATTR_MASK (PTATTR_WRITABLE | PTATTR_USER)
static unsigned long attr_to_l4attr(enum aal_pt_attribute attr)
{
	return (attr & ATTR_MASK) | PFL4_PRESENT;
}
static unsigned long attr_to_l3attr(enum aal_pt_attribute attr)
{
	return (attr & ATTR_MASK) | PFL3_PRESENT;
}
static unsigned long attr_to_l2attr(enum aal_pt_attribute attr)
{
	unsigned long r = (attr & (ATTR_MASK | PTATTR_LARGEPAGE))
		| PFL2_PRESENT;

	if ((attr & PTATTR_UNCACHABLE) && (attr & PTATTR_LARGEPAGE)) {
		return r | PFL2_PCD | PFL2_PWT; 
	}
	return r;
}
static unsigned long attr_to_l1attr(enum aal_pt_attribute attr)
{
	if (attr & PTATTR_UNCACHABLE) {
		return (attr & ATTR_MASK) | PFL1_PWT | PFL1_PWT | PFL1_PRESENT;
	} else { 
		return (attr & ATTR_MASK) | PFL1_PRESENT;
	}
}

static int __set_pt_page(struct page_table *pt, void *virt, unsigned long phys,
                         int attr)
{
	int l4idx, l3idx, l2idx, l1idx;
	unsigned long v = (unsigned long)virt;
	struct page_table *newpt;

	if (!pt) {
		pt = init_pt;
	}
	if (attr & PTATTR_LARGEPAGE) {
		phys &= LARGE_PAGE_MASK;
	} else {
		phys &= PAGE_MASK;
	}

	l4idx = (v >> PTL4_SHIFT) & (PT_ENTRIES - 1);
	l3idx = (v >> PTL3_SHIFT) & (PT_ENTRIES - 1);
	l2idx = (v >> PTL2_SHIFT) & (PT_ENTRIES - 1);
	l1idx = (v >> PTL1_SHIFT) & (PT_ENTRIES - 1);

/* TODO: more detailed attribute check */
	if (pt->entry[l4idx] & PFL4_PRESENT) {
		pt = phys_to_virt(pt->entry[l4idx] & PAGE_MASK);
	} else {
		newpt = __alloc_new_pt();
		pt->entry[l4idx] = virt_to_phys(newpt) | attr_to_l4attr(attr);
		pt = newpt;
	}

	if (pt->entry[l3idx] & PFL3_PRESENT) {
		pt = phys_to_virt(pt->entry[l3idx] & PAGE_MASK);
	} else {
		newpt = __alloc_new_pt();
		pt->entry[l3idx] = virt_to_phys(newpt) | attr_to_l3attr(attr);
		pt = newpt;
	}

	if (attr & PTATTR_LARGEPAGE) {
		if (pt->entry[l2idx] & PFL2_PRESENT) {
			if ((pt->entry[l2idx] & PAGE_MASK) != phys) {
				return -EBUSY;
			} else {
				return 0;
			}
		} else {
			pt->entry[l2idx] = phys | attr_to_l2attr(attr)
				| PFL2_SIZE;
			return 0;
		}
	}

	if (pt->entry[l2idx] & PFL2_PRESENT) {
		pt = phys_to_virt(pt->entry[l2idx] & PAGE_MASK);
	} else {
		newpt = __alloc_new_pt();
		pt->entry[l2idx] = virt_to_phys(newpt) | attr_to_l2attr(attr);
		pt = newpt;
	}

	if (pt->entry[l1idx] & PFL1_PRESENT) {
		if ((pt->entry[l1idx] & PAGE_MASK) != phys) {
			return -EBUSY;
		} else {
			return 0;
		}
	}
	pt->entry[l1idx] = phys | attr_to_l1attr(attr);
	return 0;
}

int set_pt_page(struct page_table *pt, void *virt, unsigned long phys,
                int attr)
{
	return __set_pt_page(pt, virt, phys, attr);
}

int set_pt_large_page(struct page_table *pt, void *virt, unsigned long phys,
                      int attr)
{
	return __set_pt_page(pt, virt, phys, attr | PTATTR_LARGEPAGE);
}

void load_page_table(struct page_table *pt)
{
	if (!pt) {
		pt = init_pt;
	}

	asm volatile ("movq %0, %%cr3" : : "r"(pt) : "memory");
}

static unsigned long fixed_virt;
static void init_fixed_area(struct page_table *pt)
{
	fixed_virt = MAP_FIXED_START;

	return;
}

void *map_fixed_area(unsigned long phys, unsigned long size, int uncachable)
{
	unsigned long poffset, paligned;
	int i, npages;
	int flag = PTATTR_WRITABLE;
	void *v = (void *)fixed_virt;

	poffset = phys & (PAGE_SIZE - 1);
	paligned = phys & PAGE_MASK;
	npages = (poffset + size + PAGE_SIZE - 1) >> PAGE_SHIFT;

	if (uncachable) {
		flag |= PTATTR_UNCACHABLE;
	}

	kprintf("map_fixed: %lx => %p (%d pages)\n", paligned, v, npages);

	for (i = 0; i < npages; i++) {
		set_pt_page(init_pt, (void *)fixed_virt, paligned, flag);

		fixed_virt += PAGE_SIZE;
		paligned += PAGE_SIZE;
	}

	load_page_table(init_pt);

	return (char *)v + poffset;
}

void init_page_table(void)
{
	init_pt = arch_alloc_page(0);
	
	memset(init_pt, 0, sizeof(PAGE_SIZE));

	/* Normal memory area */
	init_normal_area(init_pt);
	init_fixed_area(init_pt);

	load_page_table(init_pt);
	kprintf("Page table is now at %p\n", init_pt);
}

extern void __reserve_arch_pages(unsigned long, unsigned long,
                                 void (*)(unsigned long, unsigned long, int));

void aal_mc_reserve_arch_pages(unsigned long start, unsigned long end,
                               void (*cb)(unsigned long, unsigned long, int))
{
	/* Reserve Text */
	cb(virt_to_phys(_head), virt_to_phys(_end), 0);
	/* Reserve temporal heap that we used during initialization */
	cb(virt_to_phys(init_pt), virt_to_phys(get_last_early_heap()), 0);
	/* Reserve trampoline area to boot the second ap */
	cb(AP_TRAMPOLINE, AP_TRAMPOLINE + AP_TRAMPOLINE_SIZE, 0);
	/* Reserve the null page */
	cb(0, PAGE_SIZE, 0);
	/* Micro-arch specific */
	__reserve_arch_pages(start, end, cb);
}

void aal_mc_set_page_allocator(struct aal_mc_pa_ops *ops)
{
	last_page = NULL;
	pa_ops = ops;
}
