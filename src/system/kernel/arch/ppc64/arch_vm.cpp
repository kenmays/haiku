/* PPC64 VM integration. */
#include <KernelExport.h>
#include <boot/kernel_args.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>
#include <arch_mmu.h>

status_t arch_vm_init(kernel_args* args)
{
	return ppc64_mmu_init(args);
}

status_t arch_vm_init2(kernel_args* args)
{
	return ppc64_mmu_init_post_vm(args);
}

status_t arch_vm_init_post_area(kernel_args*) { return B_OK; }

status_t arch_vm_init_end(kernel_args* args)
{
	for (uint32 i = 0; i < args->arch_args.num_virtual_ranges_to_keep; i++) {
		addr_range& range = args->arch_args.virtual_ranges_to_keep[i];
		if (!IS_KERNEL_ADDRESS(range.start))
			continue;
		phys_addr_t physicalAddress;
		if (vm_get_page_mapping(VMAddressSpace::KernelID(), range.start,
			&physicalAddress) != B_OK)
			panic("ppc64: missing boot mapping for %p", (void*)range.start);
	}
	return B_OK;
}

status_t arch_vm_init_post_modules(kernel_args*) { return B_OK; }
void arch_vm_aspace_swap(VMAddressSpace*, VMAddressSpace*) {}
bool arch_vm_supports_protection(team_id, uint32) { return true; }
void arch_vm_unset_memory_type(VMArea*) {}
status_t arch_vm_set_memory_type(VMArea*, phys_addr_t, uint32, uint32*) { return B_OK; }
