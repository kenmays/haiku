/* PPC970 Book III-S hash MMU bootstrap.
 * The initial loader is expected to enter with a valid low-memory mapping.
 * This code establishes the SLB discipline and records the firmware SDR1
 * value; page-table population is performed by the VM translation map layer. */
#include <KernelExport.h>
#include <boot/kernel_args.h>
#include <arch_mmu.h>

static uint64 sSDR1;

static inline uint64 kernel_esid(addr_t address)
{
	return (uint64)address >> PPC64_SEGMENT_SHIFT;
}

static inline uint64 kernel_vsid(addr_t address)
{
	/* Stable VSID for the kernel address space. The HPT implementation will
	 * replace this with per-address-space VSIDs when user mappings are active. */
	return 0x100000000ULL | kernel_esid(address);
}

status_t ppc64_mmu_init(kernel_args*)
{
	sSDR1 = get_sdr1();
	ppc64_slb_invalidate();
	return B_OK;
}

status_t ppc64_mmu_init_post_vm(kernel_args*)
{
	/* Keep the firmware HPT. It is valid until the translation-map layer
	 * allocates and installs Haiku's own table. */
	return B_OK;
}

void ppc64_mmu_switch_address_space(addr_t addressSpace)
{
	(void)addressSpace;
	/* Address-space VSID allocation is serialized by the VM layer. */
}

status_t ppc64_map_page(addr_t virtualAddress, phys_addr_t physicalAddress,
	uint32 protection, uint32 memoryType)
{
	(void)virtualAddress;
	(void)physicalAddress;
	(void)protection;
	(void)memoryType;
	return B_NOT_SUPPORTED;
}

status_t ppc64_unmap_page(addr_t virtualAddress)
{
	(void)virtualAddress;
	return B_NOT_SUPPORTED;
}
