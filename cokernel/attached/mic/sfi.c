#include <ihk/debug.h>
#include <ihk/types.h>
#include <ihk/mm.h>
#include <ihk/cpu.h>
#include <errno.h>
#include <string.h>

struct sfi_table_header {
	char    sig[4];
	uint32_t len;
	uint8_t  rev;
	uint8_t  csum;
	char    oem_id[6];
	char    oem_table_id[8];
} __attribute__((packed));

struct sfi_table_simple {
	struct sfi_table_header     header;
	uint64_t             pentry[1];
} __attribute__((packed));
  
struct sfi_mem_entry {
	uint32_t type;
	uint64_t phys_start;
	uint64_t virt_start;
	uint64_t pages;
	uint64_t attrib;
} __attribute__((packed));

#define SFI_SYST_SEARCH_BEGIN       0x000E0000
#define SFI_SYST_SEARCH_END         0x000FFFFF

static struct sfi_table_simple *sfi_table;
static struct sfi_table_header *sfi_cpus, *sfi_mems;
static unsigned int sfi_mem_entries = 0;
static unsigned int sfi_reserved_mem_entries = 0;

static int __sfi_table_num_entry(struct sfi_table_header *hdr, size_t s)
{
	return (int)((hdr->len - sizeof(*hdr)) / s);
}

static struct ihk_mc_cpu_info *ihk_cpu_info;

static void parse_sfi_cpus(struct sfi_table_header *hdr)
{
	int i, nentry;
	uint32_t *apicids;

	nentry = __sfi_table_num_entry(hdr, sizeof(uint32_t));
	kprintf("# of CPUs : %d\n", nentry);

	apicids = (uint32_t *)(hdr + 1);

	/* XXX: if nentry > 510, there will be problem! */
	ihk_cpu_info = early_alloc_page();
	ihk_cpu_info->hw_ids = (int *)(ihk_cpu_info + 1);
	ihk_cpu_info->nodes = (int *)(ihk_cpu_info + 1) + nentry;

	for (i = 0; i < nentry; i++) {
		ihk_cpu_info->hw_ids[i] = apicids[i];
		ihk_cpu_info->nodes = 0;
	}
	ihk_cpu_info->ncpus = nentry;
	kprintf("\n");
}

static unsigned long sfi_mem_begin = (unsigned long)-1, sfi_mem_end = 0;

static void parse_sfi_mems(struct sfi_table_header *hdr)
{
	int i;
	struct sfi_mem_entry *me;
	unsigned long end;

	sfi_mem_entries = __sfi_table_num_entry(hdr, 36);
	kprintf("# of MEMs : %d\n", sfi_mem_entries);
	
	me = (struct sfi_mem_entry *)(hdr + 1);
	for (i = 0; i < sfi_mem_entries; i++) {
		kprintf("mem section 0x%lX - 0x%lX, type: %d\n", 
        me[i].phys_start, me[i].phys_start + (me[i].pages << 12), me[i].type);
		if (me[i].type == 7) { /* CONVENTIONAL */
			if (sfi_mem_begin > me[i].phys_start) {
				sfi_mem_begin = me[i].phys_start;
			}
			end = me[i].phys_start + (me[i].pages << 12);
			if (sfi_mem_end < end) {
				sfi_mem_end = end;
			}
		}

		if (me[i].type == 8) { /* reserved */
			++sfi_reserved_mem_entries;
		}
	}
}

static unsigned long sfi_reserved_entry(struct sfi_table_header *hdr, int ind, 
                                        enum ihk_mc_gma_type type)
{

	switch (type) {

	case IHK_MC_NR_RESERVED_AREAS:
		return sfi_reserved_mem_entries;

	case IHK_MC_RESERVED_AREA_START:
	case IHK_MC_RESERVED_AREA_END: {
			int i;
			struct sfi_mem_entry *me;

			me = (struct sfi_mem_entry *)(hdr + 1);
			for (i = 0; i < sfi_mem_entries; i++) {
				if (me[i].type != 8) {
					continue;
				}

				if (--ind == 0) {
					return (type == IHK_MC_RESERVED_AREA_START) ? 
						me[i].phys_start : 
						(me[i].phys_start + (me[i].pages << 12));
				}
			}
		}
	
	default:
		
		return 0;
	}
}


void init_sfi(void)
{
	unsigned long addr;
	int i, nentry;

	for (addr = SFI_SYST_SEARCH_BEGIN; addr < SFI_SYST_SEARCH_END;
	     addr += 16) {
		if (!strncmp((char *)addr, "SYST", 4)) {
			sfi_table = (struct sfi_table_simple *)addr;
			break;
		}
	}
	if (!addr) {
		return;
	}

	nentry = (sfi_table->header.len - sizeof(sfi_table->header))
		/ sizeof(unsigned long long);
	for (i = 0; i < nentry; i++) {
		struct sfi_table_header *hdr;
		hdr = (struct sfi_table_header *)(sfi_table->pentry[i]);
		kprintf("%c%c%c%c @ %lx\n",
		        hdr->sig[0], hdr->sig[1], hdr->sig[2], hdr->sig[3],
		        sfi_table->pentry[i]);
		if (!strncmp(hdr->sig, "CPUS", 4)) {
			sfi_cpus = early_alloc_page();
			memcpy(sfi_cpus, hdr, hdr->len);

			parse_sfi_cpus(sfi_cpus);
		} else if (!strncmp(hdr->sig, "MMAP", 4)) {
			sfi_mems = early_alloc_page();
			memcpy(sfi_mems, hdr, hdr->len);

			parse_sfi_mems(sfi_mems);
		}
	}
}

unsigned long sfi_get_memory_address(enum ihk_mc_gma_type type, int opt)
{
	switch (type) {
	case IHK_MC_GMA_MAP_START:
	case IHK_MC_GMA_AVAIL_START:
		return sfi_mem_begin;
	case IHK_MC_GMA_MAP_END:
	case IHK_MC_GMA_AVAIL_END:
		return sfi_mem_end;
	case IHK_MC_NR_RESERVED_AREAS:
		return sfi_reserved_mem_entries;
	case IHK_MC_RESERVED_AREA_START:
	case IHK_MC_RESERVED_AREA_END:
		return sfi_reserved_entry(sfi_mems, opt, type);
	default:
		break;
	}

	return -ENOENT;
}

struct ihk_mc_cpu_info *sfi_get_cpu_info(void)
{
	return ihk_cpu_info;
}

void __reserve_arch_pages(unsigned long start, unsigned long end,
                          void (*cb)(unsigned long, unsigned long, int))
{
	cb(0, 0x100000, 1);
}
