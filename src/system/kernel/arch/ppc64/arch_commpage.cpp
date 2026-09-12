/* PPC64 commpage support. */
#include <commpage.h>
#include <KernelExport.h>
#include <elf.h>
#include <smp.h>

#include "syscall_numbers.h"

extern "C" void arch_user_thread_exit();

extern "C" void __attribute__((noreturn))
arch_user_signal_handler(signal_frame_data* data)
{
	if (data->siginfo_handler) {
		auto handler = (void (*)(int, siginfo_t*, void*, void*))data->handler;
		handler(data->info.si_signo, &data->info, &data->context, data->user_data);
	} else {
		auto handler = (void (*)(int, void*, vregs*))data->handler;
		handler(data->info.si_signo, data->user_data, &data->context.uc_mcontext);
	}

	register uint64 syscallNumber asm("r0") = SYSCALL_RESTORE_SIGNAL_FRAME;
	register addr_t argument asm("r3") = (addr_t)data;
	asm volatile("sc" : "+r"(syscallNumber), "+r"(argument) :: "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "memory");
	__builtin_unreachable();
}

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
	register_commpage_function("arch_user_signal_handler",
		COMMPAGE_ENTRY_PPC64_SIGNAL_HANDLER, "commpage_signal_handler",
		(addr_t)&arch_user_signal_handler);
	return B_OK;
}
