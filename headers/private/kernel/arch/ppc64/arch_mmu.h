/* PPC970 Book III-S hashed page-table MMU definitions. */
#ifndef _KERNEL_ARCH_PPC64_MMU_H
#define _KERNEL_ARCH_PPC64_MMU_H

#include <SupportDefs.h>
#include <boot/kernel_args.h>
#include <arch/cpu.h>

#define PPC64_PAGE_SHIFT 12
#define PPC64_PAGE_SIZE (1ULL << PPC64_PAGE_SHIFT)
#define PPC64_SEGMENT_SHIFT 28
#define PPC64_SEGMENT_SIZE (1ULL << PPC64_SEGMENT_SHIFT)
#define PPC64_SLB_ESID_MASK 0xfffffffffULL
#define PPC64_HPT_PTES_PER_GROUP 8
#define PPC64_HPT_PTE_SIZE 16
#define PPC64_HPT_PTEG_SIZE 128

#define PPC64_HPTE_V_VALID 0x0000000000000001ULL
#define PPC64_HPTE_V_SECONDARY 0x0000000000000002ULL
#define PPC64_HPTE_V_LARGE 0x0000000000000004ULL
#define PPC64_HPTE_V_BOLTED 0x0000000000000010ULL
#define PPC64_HPTE_V_H 0x4000000000000000ULL
#define PPC64_HPTE_V_AVPN_MASK 0x3fffffffffffff80ULL

#define PPC64_HPTE_R_RPN 0x0ffffffffffff000ULL
#define PPC64_HPTE_R_PP 0x0000000000000003ULL
#define PPC64_HPTE_R_N 0x0000000000000004ULL
#define PPC64_HPTE_R_G 0x0000000000000008ULL
#define PPC64_HPTE_R_M 0x0000000000000010ULL
#define PPC64_HPTE_R_I 0x0000000000000020ULL
#define PPC64_HPTE_R_W 0x0000000000000040ULL
#define PPC64_HPTE_R_C 0x0000000000000080ULL
#define PPC64_HPTE_R_R 0x0000000000000100ULL
#define PPC64_HPTE_R_WIMG 0x0000000000000078ULL

#define PPC64_HPTE_PP_RWXX 0
#define PPC64_HPTE_PP_RWRX 1
#define PPC64_HPTE_PP_RWRW 2
#define PPC64_HPTE_PP_RXRX 3

struct ppc64_pte { uint64 word0; uint64 word1; };
struct ppc64_pteg { ppc64_pte pte[PPC64_HPT_PTES_PER_GROUP]; };

status_t ppc64_mmu_init(kernel_args* args);
status_t ppc64_mmu_init_post_vm(kernel_args* args);
void ppc64_mmu_switch_address_space(addr_t addressSpace);
status_t ppc64_map_page(addr_t virtualAddress, phys_addr_t physicalAddress,
	uint32 protection, uint32 memoryType);
status_t ppc64_unmap_page(addr_t virtualAddress);

#endif
