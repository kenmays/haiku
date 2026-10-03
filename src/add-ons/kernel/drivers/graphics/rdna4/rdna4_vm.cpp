#include "rdna4_vm.h"

bool
rdna4_vm_validate_pte(uint64 entry)
{
	if ((entry & RDNA4_PTE_VALID) == 0)
		return true;

	/* PTE physical address must be 4 KiB aligned and remain in bits 47:12. */
	if ((entry & 0x0000000000000fffull) != 0)
		return false;

	/* A valid GFX12 PTE is explicitly marked as a PTE. */
	if ((entry & RDNA4_PTE_IS_PTE) == 0)
		return false;

	return true;
}

bool
rdna4_vm_validate_pde(uint64 entry)
{
	if ((entry & RDNA4_PTE_VALID) == 0)
		return true;
	if ((entry & 0x000000000000003full) != RDNA4_PTE_VALID
		&& (entry & 0x000000000000003full) !=
			(RDNA4_PTE_VALID | RDNA4_PTE_SYSTEM))
		return false;
	return true;
}
