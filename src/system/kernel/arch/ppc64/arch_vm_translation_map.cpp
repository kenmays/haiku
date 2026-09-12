/* Native PPC64 hash-page-table translation-map backend. */
#include <arch/vm_translation_map.h>
#include <arch_mmu.h>
#include <KernelExport.h>
#include <boot/kernel_args.h>
#include <vm/VMTranslationMap.h>
#include <vm/vm.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <new>

class PPC64VMTranslationMap : public VMTranslationMap {
public:
	explicit PPC64VMTranslationMap(bool kernel) : fKernel(kernel) {}

	bool Lock() override { recursive_lock_lock(&fLock); return true; }
	void Unlock() override { recursive_lock_unlock(&fLock); }
	addr_t MappedSize() const override { return 0; }
	size_t MaxPagesNeededToMap(addr_t start, addr_t end) const override
		{ return (end - start) / B_PAGE_SIZE + 1; }

	status_t Map(addr_t va, phys_addr_t pa, uint32 flags, uint32 memoryType,
		vm_page_reservation*) override
	{
		return ppc64_map_page_asid(va, pa, flags, memoryType, (addr_t)this);
	}

	status_t Unmap(addr_t start, addr_t end) override
	{
		for (addr_t va = start & ~(addr_t)(B_PAGE_SIZE - 1); va < end;
			va += B_PAGE_SIZE) {
			status_t error = ppc64_unmap_page_asid(va, (addr_t)this);
			if (error != B_OK && error != B_ENTRY_NOT_FOUND)
				return error;
		}
		return B_OK;
	}

	status_t UnmapPage(VMArea* area, addr_t address, bool updatePageQueue,
		bool deletingAddressSpace = false, uint32* _flags = NULL) override
	{
		(void)area; (void)updatePageQueue; (void)deletingAddressSpace;
		status_t error = ppc64_unmap_page_asid(address, (addr_t)this);
		if (_flags != NULL) *_flags = 0;
		return error == B_ENTRY_NOT_FOUND ? B_OK : error;
	}

	status_t Query(addr_t va, phys_addr_t* pa, uint32* flags) override
	{
		return ppc64_query_page_asid(va, pa, flags, (addr_t)this);
	}
	status_t QueryInterrupt(addr_t va, phys_addr_t* pa, uint32* flags) override
		{ return Query(va, pa, flags); }

	status_t Protect(addr_t base, addr_t top, uint32 flags,
		uint32 memoryType) override
	{
		for (addr_t va = base & ~(addr_t)(B_PAGE_SIZE - 1); va <= top;
			va += B_PAGE_SIZE) {
			phys_addr_t pa;
			uint32 oldFlags;
			status_t error = ppc64_query_page_asid(va, &pa, &oldFlags,
				(addr_t)this);
			if (error == B_ENTRY_NOT_FOUND)
				continue;
			if (error != B_OK)
				return error;
			error = ppc64_map_page_asid(va, pa, flags, memoryType,
				(addr_t)this);
			if (error != B_OK)
				return error;
			if (va > top - B_PAGE_SIZE)
				break;
		}
		return B_OK;
	}

	status_t ClearFlags(addr_t va, uint32 flags) override
	{
		bool modified;
		return ppc64_clear_page_flags_asid(va, flags, &modified,
			(addr_t)this);
	}

	bool ClearAccessedAndModified(VMArea* area, addr_t va,
		bool unmapIfUnaccessed, bool& modified) override
	{
		(void)area; (void)unmapIfUnaccessed;
		status_t error = ppc64_clear_page_flags_asid(va, 0xffffffff,
			&modified, (addr_t)this);
		return error == B_OK;
	}

	void Flush() override { arch_cpu_global_tlb_invalidate(); }

private:
	bool fKernel;
};

status_t
arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** map)
{
	*map = new(std::nothrow) PPC64VMTranslationMap(kernel);
	return *map != NULL ? B_OK : B_NO_MEMORY;
}

status_t arch_vm_translation_map_init(kernel_args*, VMPhysicalPageMapper**)
	{ return B_OK; }
status_t arch_vm_translation_map_init_post_area(kernel_args*) { return B_OK; }
status_t arch_vm_translation_map_init_post_sem(kernel_args*) { return B_OK; }

status_t
arch_vm_translation_map_early_map(kernel_args*, addr_t va, phys_addr_t pa, uint8)
{
	return ppc64_map_page(va, pa, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0);
}
status_t arch_vm_translation_map_early_query(addr_t, phys_addr_t*)
	{ return B_NOT_SUPPORTED; }

status_t
ppc_map_address_range(addr_t va, phys_addr_t pa, size_t size)
{
	for (size_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
		status_t error = ppc64_map_page(va + offset, pa + offset,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0);
		if (error != B_OK)
			return error;
	}
	return B_OK;
}
void ppc_unmap_address_range(addr_t va, size_t size)
	{ for (size_t o = 0; o < size; o += B_PAGE_SIZE) ppc64_unmap_page(va + o); }
status_t ppc_remap_address_range(addr_t*, size_t, bool) { return B_NOT_SUPPORTED; }
bool arch_vm_translation_map_is_kernel_page_accessible(addr_t, uint32) { return true; }
void ppc_translation_map_change_asid(VMTranslationMap* map)
	{ ppc64_mmu_switch_address_space((addr_t)map); }
