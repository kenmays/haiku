/* PPC970 Book III-S hash MMU definitions. */
#ifndef _KERNEL_ARCH_PPC64_MMU_H
#define _KERNEL_ARCH_PPC64_MMU_H

#include <SupportDefs.h>
#include <arch/cpu.h>

#define PPC64_PAGE_SHIFT 12
#define PPC64_PAGE_SIZE (1ULL << PPC64_PAGE_SHIFT)
#define PPC64_SEGMENT_SHIFT 28
#define PPC64_SEGMENT_SIZE (1ULL << PPC64_SEGMENT_SHIFT)
#define PPC64_SLB_ESID_MASK 0xfffffffffULL
#define PPC64_SLB_INDEX_BITS 6
#define PPC64_HPT_PTES_PER_GROUP 8

struct ppc64_pte {
	uint64 word0;
	uint64 word1;
};

struct ppc64_pteg {
	ppc64_pte pte[PPC64_HPT_PTES_PER_GROUP];
};

status_t ppc64_mmu_init(kernel_args* args);
status_t ppc64_mmu_init_post_vm(kernel_args* args);
void ppc64_mmu_switch_address_space(addr_t addressSpace);
status_t ppc64_map_page(addr_t virtualAddress, phys_addr_t physicalAddress,
	uint32 protection, uint32 memoryType);
status_t ppc64_unmap_page(addr_t virtualAddress);

#endif
