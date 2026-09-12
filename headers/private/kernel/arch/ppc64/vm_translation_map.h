/* PPC64 VM translation-map architecture hooks. */
#ifndef _KERNEL_ARCH_PPC64_VM_TRANSLATION_MAP_H
#define _KERNEL_ARCH_PPC64_VM_TRANSLATION_MAP_H

struct VMTranslationMap;
struct VMPhysicalPageMapper;
struct kernel_args;

status_t arch_vm_translation_map_create_map(bool kernel, VMTranslationMap** _map);
status_t arch_vm_translation_map_init(kernel_args* args,
	VMPhysicalPageMapper** _physicalPageMapper);
status_t arch_vm_translation_map_init_post_area(kernel_args* args);
status_t arch_vm_translation_map_init_post_sem(kernel_args* args);
status_t arch_vm_translation_map_early_map(kernel_args* args, addr_t va,
	phys_addr_t pa, uint8 attributes);
status_t arch_vm_translation_map_early_query(addr_t va,
	phys_addr_t* _physicalAddress);
status_t ppc_map_address_range(addr_t virtualAddress, phys_addr_t physicalAddress,
	size_t size);
void ppc_unmap_address_range(addr_t virtualAddress, size_t size);
status_t ppc_remap_address_range(addr_t* _virtualAddress, size_t size, bool unmap);
bool arch_vm_translation_map_is_kernel_page_accessible(addr_t virtualAddress,
	uint32 protection);
void ppc_translation_map_change_asid(VMTranslationMap* map);

#endif
