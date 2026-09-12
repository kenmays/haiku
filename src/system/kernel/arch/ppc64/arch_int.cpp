/* PPC64 interrupt core for Apple Power Mac G5. */
#include <KernelExport.h>
#include <interrupts.h>
#include <arch/int.h>
#include <arch/thread.h>
#include <arch/smp.h>
#include <arch_mmu.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <kscheduler.h>
#include <smp.h>
#include <timer.h>
#include <thread.h>
#include <vm/vm.h>

#include <platform/apple_g5/g5_mpic.h>

#include <string.h>

iframe_stack gBootFrameStack;
extern "C" uint8 __irqvec_start;
extern "C" uint8 __irqvec_end;

void arch_int_enable_io_interrupt(int32 irq) { AppleG5::mpic_enable(irq); }
void arch_int_disable_io_interrupt(int32 irq) { AppleG5::mpic_disable(irq); }

void
arch_int_configure_io_interrupt(int32 irq, interrupt_trigger_mode mode,
	interrupt_trigger_polarity polarity)
{
	AppleG5::mpic_configure(irq, mode == B_LEVEL_TRIGGERED,
		polarity == B_HIGH_ACTIVE_POLARITY || polarity == B_RISING_EDGE_POLARITY);
}

int32 arch_int_assign_to_cpu(int32 irq, int32 cpu)
	{ return AppleG5::mpic_assign_to_cpu(irq, cpu); }

static void
print_iframe(iframe* frame)
{
	dprintf("PPC64 iframe=%p vector=%lx srr0=%p srr1=%lx dar=%p dsisr=%lx\n",
		frame, frame->vector, (void*)frame->srr0, frame->srr1,
		(void*)frame->dar, frame->dsisr);
}

extern "C" void
ppc64_exception_entry(uint64 vector, iframe* frame)
{
	frame->vector = vector;
	Thread* thread = thread_get_current_thread();
	iframe_stack* stack = thread != NULL ? &thread->arch_info.iframes : &gBootFrameStack;
	ppc_push_iframe(stack, frame);

	switch (vector) {
		case 0x380:
		case 0x480:
			if (ppc64_mmu_handle_segment_fault(vector == 0x380
				? frame->dar : frame->srr0) != B_OK) {
				print_iframe(frame);
				panic("ppc64: SLB refill failed");
			}
			break;
		case 0x500: {
			int32 irq;
			while ((irq = AppleG5::mpic_acknowledge()) >= 0) {
				if (irq == 0x20)
					smp_intercpu_interrupt_handler(smp_get_current_cpu());
				else
					io_interrupt_handler(irq, true);
				AppleG5::mpic_eoi();
			}
			break;
		}
		case 0x900:
			timer_interrupt();
			break;
		case 0x300:
		case 0x400: {
			addr_t newIP = 0;
			vm_page_fault(frame->dar, frame->srr0,
				(frame->dsisr & (1ULL << 25)) != 0, false,
				(frame->srr1 & MSR_PRIVILEGE_LEVEL) != 0, &newIP);
			if (newIP != 0)
				frame->srr0 = newIP;
			break;
		}
		case 0x200:
			print_iframe(frame);
			panic("ppc64: machine check exception");
			break;
		default:
			print_iframe(frame);
			panic("ppc64: unhandled exception vector 0x%lx", vector);
	}

	if (thread != NULL) {
		cpu_status state = disable_interrupts();
		if (thread->post_interrupt_callback != NULL) {
			void (*callback)(void*) = thread->post_interrupt_callback;
			void* data = thread->post_interrupt_data;
			thread->post_interrupt_callback = NULL;
			thread->post_interrupt_data = NULL;
			restore_interrupts(state);
			callback(data);
		} else {
			if (thread->cpu->invoke_scheduler) {
				SpinLocker schedulerLocker(thread->scheduler_lock);
				scheduler_reschedule(B_THREAD_READY);
				schedulerLocker.Unlock();
			}
			restore_interrupts(state);
		}
	}

	ppc_pop_iframe(stack);
}

status_t arch_int_init(kernel_args*) { return B_OK; }

status_t
arch_int_init_post_vm(kernel_args* args)
{
	if (args == NULL || args->arch_args.exception_handlers.size < B_PAGE_SIZE)
		return B_BAD_VALUE;

	void* handlers = (void*)(addr_t)args->arch_args.exception_handlers.start;
	area_id area = create_area("ppc64_exception_vectors", &handlers,
		B_EXACT_ADDRESS, args->arch_args.exception_handlers.size,
		B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < B_OK)
		return area;

	size_t vectorSize = (size_t)(&__irqvec_end - &__irqvec_start);
	if (vectorSize > args->arch_args.exception_handlers.size)
		return B_BAD_VALUE;
	memcpy(handlers, &__irqvec_start, vectorSize);
	arch_cpu_sync_icache(handlers, vectorSize);
	return B_OK;
}

status_t arch_int_init_io(kernel_args*) { return AppleG5::mpic_init(); }
status_t arch_int_init_post_device_manager(kernel_args*) { return B_OK; }
