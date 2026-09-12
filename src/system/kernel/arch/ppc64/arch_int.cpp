/* PPC64 interrupt core. */
#include <KernelExport.h>
#include <interrupts.h>
#include <arch/int.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <timer.h>
#include <thread.h>
#include <vm/vm.h>

extern "C" void ppc64_exception_entry(uint64 vector, iframe* frame);

void arch_int_enable_io_interrupt(int32) {}
void arch_int_disable_io_interrupt(int32) {}

static void print_iframe(iframe* frame)
{
	dprintf("PPC64 iframe=%p vector=%lx srr0=%p srr1=%lx dar=%p dsisr=%lx\n",
		frame, frame->vector, (void*)frame->srr0, frame->srr1,
		(void*)frame->dar, frame->dsisr);
}

extern "C" void ppc64_exception_entry(uint64 vector, iframe* frame)
{
	frame->vector = vector;
	Thread* thread = thread_get_current_thread();
	if (thread != NULL)
		ppc_push_iframe(&thread->arch_info.iframes, frame);

	switch (vector) {
		case 0x500:
			/* External interrupt routing is installed by the Apple MPIC driver. */
			break;
		case 0x900:
			timer_interrupt();
			break;
		case 0x300:
		case 0x400:
			vm_page_fault(frame->dar, frame->srr0,
				(frame->dsisr & (1ULL << 25)) != 0, false,
				(frame->srr1 & MSR_PRIVILEGE_LEVEL) != 0, NULL);
			break;
		default:
			print_iframe(frame);
			panic("ppc64: unhandled exception vector 0x%lx", vector);
	}

	if (thread != NULL)
		ppc_pop_iframe(&thread->arch_info.iframes);
}

status_t arch_int_init(kernel_args*) { return B_OK; }
status_t arch_int_init_post_vm(kernel_args*) { return B_OK; }
status_t arch_int_init_io(kernel_args*) { return B_OK; }
