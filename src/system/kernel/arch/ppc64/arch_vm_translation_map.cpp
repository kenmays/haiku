/* Minimal native PPC64 translation-map backend. The software map is kept
 * separate from the Book III-S HPT so the HPT implementation can be enabled
 * without changing VM-facing semantics. */
#include <arch/vm_translation_map.h>
#include <arch_mmu.h>
#include <KernelExport.h>
#include <boot/kernel_args.h>
#include <vm/VMTranslationMap.h>
#include <vm/vm.h>
#include <vm/vm_priv.h>
#include <vm/VMAddressSpace.h>
#include <string.h>

struct Mapping {
	addr_t va;
	phys_addr_t pa;
	uint32 flags;
	uint32 memoryType;
	Mapping* next;
};

class PPC64VMTranslationMap : public VMTranslationMap {
public:
	explicit PPC64VMTranslationMap(bool kernel) : fKernel(kernel), fMappings(NULL) {}
	~PPC64VMTranslationMap() { Clear(); }
	bool Lock() { recursive_lock_lock(&fLock); return true; }
	void Unlock() { recursive_lock_unlock(&fLock); }
	addr_t MappedSize() const { return fMapCount * B_PAGE_SIZE; }
	size_t MaxPagesNeededToMap(addr_t start, addr_t end) const { return (end - start) / B_PAGE_SIZE + 1; }
	status_t Map(addr_t va, phys_addr_t pa, uint32 flags, uint32 memoryType, vm_page_reservation*) {
		va &= ~(addr_t)(B_PAGE_SIZE - 1);
		Mapping* m = Find(va);
		if (m == NULL) {
			m = new(std::nothrow) Mapping;
			if (m == NULL) return B_NO_MEMORY;
			m->next = fMappings;
			fMappings = m;
			++fMapCount;
		}
		m->va = va; m->pa = pa & ~(phys_addr_t)(B_PAGE_SIZE - 1);
		m->flags = flags; m->memoryType = memoryType;
		return ppc64_map_page(m->va, m->pa, flags, memoryType);
	}
	status_t Unmap(addr_t start, addr_t end) {
		for (addr_t va = start & ~(addr_t)(B_PAGE_SIZE - 1); va < end; va += B_PAGE_SIZE)
			UnmapOne(va);
		return B_OK;
	}
	status_t UnmapPage(VMArea*, addr_t address, bool, bool = false, uint32* = NULL) { return UnmapOne(address); }
	status_t Query(addr_t va, phys_addr_t* pa, uint32* flags) {
		Mapping* m = Find(va & ~(addr_t)(B_PAGE_SIZE - 1));
		if (!m) return B_ENTRY_NOT_FOUND;
		if (pa) *pa = m->pa + (va & (B_PAGE_SIZE - 1));
		if (flags) *flags = m->flags;
		return B_OK;
	}
	status_t QueryInterrupt(addr_t va, phys_addr_t* pa, uint32* flags) { return Query(va, pa, flags); }
	status_t Protect(addr_t base, addr_t top, uint32 flags, uint32) {
		for (addr_t va = base & ~(addr_t)(B_PAGE_SIZE - 1); va < top; va += B_PAGE_SIZE) {
			Mapping* m = Find(va); if (m) m->flags = flags;
		}
		return B_OK;
	}
	status_t ClearFlags(addr_t va, uint32 flags) { Mapping* m = Find(va & ~(addr_t)(B_PAGE_SIZE - 1)); if (!m) return B_ENTRY_NOT_FOUND; m->flags &= ~flags; return B_OK; }
	bool ClearAccessedAndModified(VMArea*, addr_t, bool, bool& modified) { modified = false; return true; }
	void Flush() { arch_cpu_global_tlb_invalidate(); }

private:
	Mapping* Find(addr_t va) const { for (Mapping* m = fMappings; m; m = m->next) if (m->va == va) return m; return NULL; }
	status_t UnmapOne(addr_t va) {
		va &= ~(addr_t)(B_PAGE_SIZE - 1);
		Mapping** p = &fMappings;
		while (*p) {
			if ((*p)->va == va) { Mapping* m = *p; *p = m->next; delete m; --fMapCount; ppc64_unmap_page(va); return B_OK; }
			p = &(*p)->next;
		}
		return B_ENTRY_NOT_FOUND;
	}
	void Clear() { while (fMappings) { Mapping* m = fMappings; fMappings = m->next; delete m; } fMapCount = 0; }
	bool fKernel;
	Mapping* fMappings;
};

status_t arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** map)
{
	*map = new(std::nothrow) PPC64VMTranslationMap(kernel);
	return *map != NULL ? B_OK : B_NO_MEMORY;
}

status_t arch_vm_translation_map_init(kernel_args*, VMPhysicalPageMapper**)
{
	return B_OK;
}
status_t arch_vm_translation_map_init_post_area(kernel_args*) { return B_OK; }
status_t arch_vm_translation_map_init_post_sem(kernel_args*) { return B_OK; }
status_t arch_vm_translation_map_early_map(kernel_args*, addr_t va, phys_addr_t pa, uint8)
{
	return ppc64_map_page(va, pa, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0);
}
status_t arch_vm_translation_map_early_query(addr_t va, phys_addr_t* pa)
{
	(void)va; (void)pa; return B_NOT_SUPPORTED;
}
status_t ppc_map_address_range(addr_t va, phys_addr_t pa, size_t size)
{
	for (size_t offset = 0; offset < size; offset += B_PAGE_SIZE) {
		status_t error = ppc64_map_page(va + offset, pa + offset,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, 0);
		if (error != B_OK) return error;
	}
	return B_OK;
}
void ppc_unmap_address_range(addr_t va, size_t size) { for (size_t o = 0; o < size; o += B_PAGE_SIZE) ppc64_unmap_page(va + o); }
status_t ppc_remap_address_range(addr_t*, size_t, bool) { return B_NOT_SUPPORTED; }
bool arch_vm_translation_map_is_kernel_page_accessible(addr_t, uint32) { return true; }
void ppc_translation_map_change_asid(VMTranslationMap*) {}
