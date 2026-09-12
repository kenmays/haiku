/* PPC64 interrupt core for Apple Power Mac G5. */
#include <KernelExport.h>
#include <interrupts.h>
#include <arch/int.h>
#include <arch/thread.h>
#include <arch/smp.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <kscheduler.h>
#include <smp.h>
#include <timer.h>
#include <thread.h>
#include <vm/vm.h>

#include <platform/apple_g5/g5_mpic.h>

iframe_stack gBootFrameStack;

void arch_int_enable_io_interrupt(int32 irq) { AppleG5::mpic_enable(irq); }
void arch_int_disable_io_interrupt(int32 irq) { AppleG5::mpic_disable(irq); }

void
arch_int_configure_io_interrupt(int32 irq, interrupt_trigger_mode mode,
	interrupt_trigger_polarity polarity)
{
	AppleG5::mpic_configure(irq, mode == B_LEVEL_TRIGGERED,
		polarity == B_HIGH_ACTIVE_POLARITY || polarity == B_RISING_EDGE_POLARITY);
}

int32
arch_int_assign_to_cpu(int32 irq, int32 cpu)
{
	return AppleG5::mpic_assign_to_cpu(irq, cpu);
}

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
status_t arch_int_init_post_vm(kernel_args*) { return B_OK; }
status_t arch_int_init_io(kernel_args*) { return AppleG5::mpic_init(); }
status_t arch_int_init_post_device_manager(kernel_args*) { return B_OK; }
