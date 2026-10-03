#include "rdna4_vm.h"
#include "rdna4_gfxhub.h"
#include "driver.h"

#include <KernelExport.h>
#include <OS.h>
#include <string.h>

#define RDNA4_VM_PAGE_SIZE 4096ull
#define RDNA4_VM_LEVELS 4
#define RDNA4_VM_ENTRIES 512
#define RDNA4_VM_START 0x0000000100000000ull
#define RDNA4_VM_END   0x0000fffffffff000ull


static uint32
vm_index(uint64 va, uint8 level)
{
	return (uint32)((va >> (12 + level * 9)) & 0x1ff);
}


static uint64
table_base(uint64 va, uint8 level)
{
	const uint32 bits = 12 + (level + 1) * 9;
	return va & (~0ull << bits);
}


static status_t
allocate_table(rdna4_device& device, uint8 level, uint64 base,
	rdna4_vm_table*& _table)
{
	for (uint32 i = 0; i < RDNA4_VM_MAX_TABLES; i++) {
		rdna4_vm_table& table = device.vm_tables[i];
		if (table.used && table.level == level && table.base == base) {
			_table = &table;
			return B_OK;
		}
	}

	for (uint32 i = 0; i < RDNA4_VM_MAX_TABLES; i++) {
		rdna4_vm_table& table = device.vm_tables[i];
		if (table.used)
			continue;

		void* address = NULL;
		area_id area = create_area("rdna4 gpuvm page table", &address,
			B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_CONTIGUOUS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (area < 0)
			return area;

		physical_entry entry;
		status_t status = get_memory_map(address, B_PAGE_SIZE, &entry, 1);
		if (status != B_OK || entry.size < B_PAGE_SIZE) {
			delete_area(area);
			return status != B_OK ? status : B_NOT_SUPPORTED;
		}

		memset(address, 0, B_PAGE_SIZE);
		table.area = area;
		table.cpu = (uint64*)address;
		table.phys = entry.address;
		table.level = level;
		table.used = 1;
		table.base = base;
		_table = &table;
		return B_OK;
	}

	return B_NO_MEMORY;
}


static rdna4_vm_table*
find_table_by_phys(rdna4_device& device, phys_addr_t phys)
{
	for (uint32 i = 0; i < RDNA4_VM_MAX_TABLES; i++) {
		rdna4_vm_table& table = device.vm_tables[i];
		if (table.used && table.phys == phys)
			return &table;
	}
	return NULL;
}


static status_t
allocate_path(rdna4_device& device, uint64 va, rdna4_vm_table*& _pte)
{
	rdna4_vm_table* current = &device.vm_tables[device.vm_root_index];

	for (int level = 2; level >= 0; level--) {
		const uint32 index = vm_index(va, (uint8)level + 1);
		uint64 entry = current->cpu[index];
		rdna4_vm_table* next = NULL;

		if ((entry & RDNA4_PTE_VALID) != 0) {
			phys_addr_t phys = entry & 0x0000fffffffff000ull;
			next = find_table_by_phys(device, phys);
			if (next == NULL)
				return B_BAD_DATA;
		} else {
			status_t status = allocate_table(device, (uint8)level,
				table_base(va, (uint8)level), next);
			if (status != B_OK)
				return status;

			current->cpu[index] = rdna4_pde(next->phys,
				RDNA4_PTE_VALID | RDNA4_PTE_SYSTEM);
		}
		current = next;
	}

	_pte = current;
	return B_OK;
}


static status_t
lookup_path(rdna4_device& device, uint64 va, rdna4_vm_table*& _pte)
{
 rdna4_vm_table* current = &device.vm_tables[device.vm_root_index];
 for (int level = 2; level >= 0; level--) {
  uint32 index = vm_index(va, (uint8)level + 1);
  uint64 entry = current->cpu[index];
  if ((entry & RDNA4_PTE_VALID) == 0) return B_ENTRY_NOT_FOUND;
  rdna4_vm_table* next = find_table_by_phys(device, entry & 0x0000fffffffff000ull);
  if (!next) return B_BAD_DATA;
  current = next;
 }
 _pte = current; return B_OK;
}


bool
rdna4_vm_validate_pte(uint64 entry)
{
	if ((entry & RDNA4_PTE_VALID) == 0)
		return true;
	if ((entry & 0x0000000000000fffull) != 0)
		return false;
	return (entry & RDNA4_PTE_IS_PTE) != 0;
}


bool
rdna4_vm_validate_pde(uint64 entry)
{
	if ((entry & RDNA4_PTE_VALID) == 0)
		return true;
	return (entry & (RDNA4_PTE_VALID | RDNA4_PTE_SYSTEM))
		== (RDNA4_PTE_VALID | RDNA4_PTE_SYSTEM);
}


status_t
rdna4_vm_init(rdna4_device& device)
{
	memset(device.vm_tables, 0, sizeof(device.vm_tables));
	device.vm_root_index = 0;
	device.vm_next_va = RDNA4_VM_START;

	rdna4_vm_table* root = NULL;
	status_t status = allocate_table(device, 3, 0, root);
	if (status != B_OK)
		return status;

	device.vm_root_index = (uint32)(root - device.vm_tables);
	device.vm_root_phys = root->phys;
	device.vm_ready = true;
	return B_OK;
}


void
rdna4_vm_uninit(rdna4_device& device)
{
	for (uint32 i = 0; i < RDNA4_VM_MAX_TABLES; i++) {
		if (!device.vm_tables[i].used)
			continue;
		delete_area(device.vm_tables[i].area);
		device.vm_tables[i].used = 0;
		device.vm_tables[i].cpu = NULL;
	}
	device.vm_root_phys = 0;
	device.vm_ready = false;
	device.vm_root_index = 0;
	device.vm_next_va = 0;
}


static uint64
align_up(uint64 value, uint64 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}


status_t
rdna4_vm_map_bo(rdna4_device& device, rdna4_bo& bo, uint64 alignment)
{
	if (!device.vm_ready || bo.physical == 0 || bo.size == 0)
		return B_NO_INIT;

	if (alignment < RDNA4_VM_PAGE_SIZE)
		alignment = RDNA4_VM_PAGE_SIZE;
	if ((alignment & (alignment - 1)) != 0)
		return B_BAD_VALUE;

	uint64 va = align_up(device.vm_next_va, alignment);
	const uint64 size = align_up(bo.size, RDNA4_VM_PAGE_SIZE);
	if (va < RDNA4_VM_START || va + size < va || va + size > RDNA4_VM_END)
		return B_NO_MEMORY;

	for (uint64 offset = 0; offset < size; offset += RDNA4_VM_PAGE_SIZE) {
		const uint64 pageVA = va + offset;
		rdna4_vm_table* pteTable = NULL;
		status_t status = allocate_path(device, pageVA, pteTable);
		if (status != B_OK)
			return status;

		uint32 pteIndex = vm_index(pageVA, 0);
		uint64 physical = bo.physical + offset;
		uint64 flags = RDNA4_PTE_VALID | RDNA4_PTE_SYSTEM
			| RDNA4_PTE_SNOOPED | RDNA4_PTE_READABLE
			| RDNA4_PTE_WRITEABLE | RDNA4_PTE_IS_PTE;
		pteTable->cpu[pteIndex] = rdna4_pte(physical, flags);
	}

	bo.gpu = va;
	device.vm_next_va = va + size;

	/*
	 * Page-table writes are CPU stores and therefore require an explicit
	 * GFXHub invalidation before the GPU can consume the new mappings.
	 */
	if (device.gfxhub_ready) {
		status_t status = rdna4_gfxhub_flush_tlb(device, 0, 0);
		if (status != B_OK) {
			/* Leave the mapping installed; the caller can retry the flush. */
			return status;
		}
	}
	return B_OK;
}


status_t
rdna4_vm_unmap_bo(rdna4_device& device, rdna4_bo& bo)
{
	if (!device.vm_ready || bo.gpu == 0 || bo.size == 0)
		return B_BAD_VALUE;

	const uint64 size = align_up(bo.size, RDNA4_VM_PAGE_SIZE);
	for (uint64 offset = 0; offset < size; offset += RDNA4_VM_PAGE_SIZE) {
		const uint64 pageVA = bo.gpu + offset;
		rdna4_vm_table* pteTable = NULL;
		status_t status = lookup_path(device, pageVA, pteTable);
		if (status != B_OK)
			return status;
		pteTable->cpu[vm_index(pageVA, 0)] = 0;
	}

	if (device.gfxhub_ready) {
		status_t status = rdna4_gfxhub_flush_tlb(device, 0, 0);
		if (status != B_OK)
			return status;
	}

	bo.gpu = 0;
	return B_OK;
}
