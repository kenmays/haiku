#ifndef RDNA4_VM_H
#define RDNA4_VM_H

#include <SupportDefs.h>

/* GFX12 GPUVM geometry.  These values describe the architectural page-table
 * format; register programming remains in the generation-specific backend. */
static const uint32 RDNA4_VM_PAGE_SHIFT = 12;
static const uint32 RDNA4_VM_LEVEL_BITS = 9;
static const uint32 RDNA4_VM_LEVEL_ENTRIES = 1 << RDNA4_VM_LEVEL_BITS;
static const uint32 RDNA4_VM_LEVELS = 4;
static const uint64 RDNA4_VM_VA_BITS = 48;
static const uint64 RDNA4_VM_VA_MASK = (1ULL << RDNA4_VM_VA_BITS) - 1;

/* GFX12 PTE fields. */
static const uint64 RDNA4_PTE_VALID = 1ULL << 0;
static const uint64 RDNA4_PTE_SYSTEM = 1ULL << 1;
static const uint64 RDNA4_PTE_SNOOPED = 1ULL << 2;
static const uint64 RDNA4_PTE_Z = 1ULL << 3;
static const uint64 RDNA4_PTE_EXECUTE = 1ULL << 4;
static const uint64 RDNA4_PTE_READ = 1ULL << 5;
static const uint64 RDNA4_PTE_WRITE = 1ULL << 6;
static const uint64 RDNA4_PTE_FRAGMENT_MASK = 0x1fULL << 7;
static const uint64 RDNA4_PTE_ADDR_MASK = 0x0000fffffffff000ULL;
static const uint64 RDNA4_PTE_SW_MASK = 0x3ULL << 52;
static const uint64 RDNA4_PTE_MEMTYPE_MASK = 0x3ULL << 54;
static const uint64 RDNA4_PTE_G = 1ULL << 57;
static const uint64 RDNA4_PTE_D = 1ULL << 58;
static const uint64 RDNA4_PTE_P = 1ULL << 63;

/* GFX12 PDE fields. */
static const uint64 RDNA4_PDE_VALID = 1ULL << 0;
static const uint64 RDNA4_PDE_SYSTEM = 1ULL << 1;
static const uint64 RDNA4_PDE_COHERENT = 1ULL << 2;
static const uint64 RDNA4_PDE_ADDR_MASK = 0x0000ffffffffffc0ULL;
static const uint64 RDNA4_PDE_MEMTYPE_MASK = 0x3ULL << 54;
static const uint64 RDNA4_PDE_A = 1ULL << 56;
static const uint64 RDNA4_PDE_P = 1ULL << 63;

static inline uint64
rdna4_vm_make_pte(uint64 physical, uint64 flags)
{
	return (physical & RDNA4_PTE_ADDR_MASK) | flags;
}

static inline uint64
rdna4_vm_make_pde(uint64 physical, uint64 flags)
{
	return (physical & RDNA4_PDE_ADDR_MASK) | flags;
}

static inline uint32
rdna4_vm_level_index(uint64 gpuVA, uint32 level)
{
	return (uint32)((gpuVA >> (RDNA4_VM_PAGE_SHIFT
		+ level * RDNA4_VM_LEVEL_BITS)) & (RDNA4_VM_LEVEL_ENTRIES - 1));
}

/* The class owns the software mapping representation.  Hardware table
 * allocation and MMU register programming are enabled only by the verified
 * GFX12 backend; this prevents guessed MMIO from becoming functional code. */
class RDNA4VM {
public:
	RDNA4VM();
	~RDNA4VM();

	status_t Initialize();
	status_t Map(uint64 gpuVA, uint64 physical, uint64 size, uint64 flags);
	status_t Unmap(uint64 gpuVA, uint64 size);
	status_t Flush();

private:
	struct Mapping {
		uint64 gpuVA;
		uint64 physical;
		uint64 size;
		uint64 flags;
		Mapping* next;
	};

	bool fInitialized;
	Mapping* fMappings;
};

#endif
