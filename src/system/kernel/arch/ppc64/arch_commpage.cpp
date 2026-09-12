/* PPC64 commpage support. */
#include <commpage.h>
#include <KernelExport.h>
#include <elf.h>
#include <smp.h>

#include "syscall_numbers.h"

extern "C" void arch_user_thread_exit();

static void
register_commpage_function(const char* functionName, int32 commpageIndex,
	const char* symbolName, addr_t expectedAddress)
{
	elf_symbol_info symbolInfo;
	if (elf_lookup_kernel_symbol(functionName, &symbolInfo) != B_OK)
		panic("ppc64: failed to find commpage function %s", functionName);
	ASSERT(symbolInfo.address == expectedAddress);
	addr_t position = fill_commpage_entry(commpageIndex,
		(void*)symbolInfo.address, symbolInfo.size);
	image_id image = get_commpage_image();
	elf_add_memory_image_symbol(image, symbolName, position,
		symbolInfo.size, B_SYMBOL_TYPE_TEXT);
}

status_t arch_commpage_init(void) { return B_OK; }

status_t
arch_commpage_init_post_cpus(void)
{
	register_commpage_function("arch_user_thread_exit",
		COMMPAGE_ENTRY_PPC64_THREAD_EXIT, "commpage_thread_exit",
		(addr_t)&arch_user_thread_exit);
	return B_OK;
}
