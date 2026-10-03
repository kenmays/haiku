#ifndef RDNA4_VM_H
#define RDNA4_VM_H

#include <SupportDefs.h>

/*
 * GFX12 PTE/PDE encoding. These bit positions match AMD's GFX12 GPUVM
 * format: physical page in bits 47:12 and GFX12 MTYPE in 55:54.
 */
#define RDNA4_PTE_VALID		(1ull << 0)
#define RDNA4_PTE_SYSTEM		(1ull << 1)
#define RDNA4_PTE_SNOOPED		(1ull << 2)
#define RDNA4_PTE_Z		(1ull << 3)
#define RDNA4_PTE_EXECUTABLE	(1ull << 4)
#define RDNA4_PTE_READABLE	(1ull << 5)
#define RDNA4_PTE_WRITEABLE	(1ull << 6)
#define RDNA4_PTE_FRAGMENT_SHIFT	7
#define RDNA4_PTE_FRAGMENT_MASK	(0x1full << 7)
#define RDNA4_PTE_MTYPE_SHIFT	54
#define RDNA4_PTE_MTYPE_MASK	(3ull << RDNA4_PTE_MTYPE_SHIFT)
#define RDNA4_PTE_PRT		(1ull << 56)
#define RDNA4_PTE_DCC		(1ull << 58)
#define RDNA4_PTE_BUS_ATOMICS	(1ull << 59)
#define RDNA4_PTE_IS_PTE		(1ull << 63)

#define RDNA4_PDE_FRAGMENT_SHIFT	58
#define RDNA4_PDE_FRAGMENT_MASK	(0x1full << RDNA4_PDE_FRAGMENT_SHIFT)
#define RDNA4_PDE_IS_PTE		(1ull << 63)

enum rdna4_mtype {
	RDNA4_MTYPE_NC = 0,
	RDNA4_MTYPE_WC = 1,
	RDNA4_MTYPE_CC = 2,
	RDNA4_MTYPE_UC = 3
};

static inline uint64
rdna4_pte(uint64 physical, uint64 flags)
{
	return (physical & 0x0000fffffffff000ull)
		| (flags & 0xf0ffffffffffffc7ull);
}

static inline uint64
rdna4_pte_set_mtype(uint64 entry, uint32 mtype)
{
	entry &= ~RDNA4_PTE_MTYPE_MASK;
	entry |= ((uint64)mtype & 3) << RDNA4_PTE_MTYPE_SHIFT;
	return entry;
}

static inline uint64
rdna4_pde(uint64 physical, uint64 flags)
{
	return (physical & 0x0000ffffffffffc0ull)
		| (flags & (RDNA4_PDE_FRAGMENT_MASK | RDNA4_PDE_IS_PTE
			| RDNA4_PTE_SYSTEM | RDNA4_PTE_VALID));
}

static inline uint64
rdna4_pde_set_fragment(uint64 entry, uint32 fragment)
{
	entry &= ~RDNA4_PDE_FRAGMENT_MASK;
	entry |= ((uint64)fragment & 0x1f) << RDNA4_PDE_FRAGMENT_SHIFT;
	return entry;
}

bool rdna4_vm_validate_pte(uint64 entry);
bool rdna4_vm_validate_pde(uint64 entry);

#endif
