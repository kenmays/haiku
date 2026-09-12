/* PowerPC 970-class CPU support. */
#include <KernelExport.h>
#include <arch_platform.h>
#include <arch/cpu.h>
#include <arch/thread.h>
#include <boot/kernel_args.h>
#include <vm/VMAddressSpace.h>

status_t arch_cpu_preboot_init_percpu(kernel_args*, int)
{
	set_msr(get_msr() | MSR_FP_AVAILABLE | MSR_64BIT);
	arch_thread_set_current_thread(NULL);
	return B_OK;
}

status_t arch_cpu_init(kernel_args*) { return B_OK; }
status_t arch_cpu_init_post_vm(kernel_args*) { return B_OK; }
status_t arch_cpu_init_percpu(kernel_args*, int) { return B_OK; }
status_t arch_cpu_init_post_modules(kernel_args*) { return B_OK; }

void arch_cpu_sync_icache(void* address, size_t length)
{
	uintptr_t start = (uintptr_t)address & ~(uintptr_t)(CACHE_LINE_SIZE - 1);
	uintptr_t end = ((uintptr_t)address + length + CACHE_LINE_SIZE - 1)
		& ~(uintptr_t)(CACHE_LINE_SIZE - 1);
	for (uintptr_t p = start; p < end; p += CACHE_LINE_SIZE)
		asm volatile("dcbst 0,%0" :: "r"(p) : "memory");
	asm volatile("sync" ::: "memory");
	for (uintptr_t p = start; p < end; p += CACHE_LINE_SIZE)
		asm volatile("icbi 0,%0" :: "r"(p) : "memory");
	asm volatile("sync" ::: "memory");
	isync();
}

void arch_cpu_memory_read_barrier(void) { asm volatile("lwsync" ::: "memory"); }
void arch_cpu_memory_write_barrier(void) { asm volatile("eieio" ::: "memory"); }

void arch_cpu_invalidate_tlb_range(intptr_t, addr_t start, addr_t end)
{
	ppc_sync();
	for (; start < end; start += B_PAGE_SIZE)
		tlbie(start);
	tlbsync();
	ppc_sync();
}

void arch_cpu_invalidate_tlb_list(intptr_t, addr_t pages[], int count)
{
	ppc_sync();
	for (int i = 0; i < count; i++)
		tlbie(pages[i]);
	tlbsync();
	ppc_sync();
}

void arch_cpu_global_tlb_invalidate()
{
	ppc64_slb_invalidate();
	arch_cpu_invalidate_tlb_range(0, 0, 0x100000000ULL);
}

void arch_cpu_user_tlb_invalidate(intptr_t)
{
	arch_cpu_global_tlb_invalidate();
}

status_t arch_cpu_user_memcpy(void* to, const void* from, size_t size,
	addr_t* faultHandler)
{
	addr_t old = *faultHandler;
	if (ppc_set_fault_handler(faultHandler, (addr_t)&&error)) goto error;
	memcpy(to, from, size);
	*faultHandler = old;
	return B_OK;
error:
	*faultHandler = old;
	return B_BAD_ADDRESS;
}

ssize_t arch_cpu_user_strlcpy(char* to, const char* from, size_t size,
	addr_t* faultHandler)
{
	addr_t old = *faultHandler;
	if (ppc_set_fault_handler(faultHandler, (addr_t)&&error)) goto error;
	if (size != 0) {
		size_t n = strnlen(from, size - 1);
		memcpy(to, from, n);
		to[n] = 0;
	}
	*faultHandler = old;
	return strlen(from);
error:
	*faultHandler = old;
	return B_BAD_ADDRESS;
}

status_t arch_cpu_user_memset(void* address, char value, size_t count,
	addr_t* faultHandler)
{
	addr_t old = *faultHandler;
	if (ppc_set_fault_handler(faultHandler, (addr_t)&&error)) goto error;
	memset(address, value, count);
	*faultHandler = old;
	return B_OK;
error:
	*faultHandler = old;
	return B_BAD_ADDRESS;
}

status_t arch_cpu_shutdown(bool reboot)
{
	PPCPlatform::Default()->ShutDown(reboot);
	return B_ERROR;
}

bool ppc_set_fault_handler(addr_t* location, addr_t handler)
{
	*location = handler;
	return false;
}
