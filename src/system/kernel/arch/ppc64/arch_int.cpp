/* PPC64 interrupt core for Apple Power Mac G5. */
#include <KernelExport.h>
#include <interrupts.h>
#include <arch/int.h>
#include <arch/thread.h>
#include <arch/smp.h>
#include <arch_mmu.h>
#include <boot/kernel_args.h>
#include <cpu.h>
#include <debug.h>
#include <kscheduler.h>
#include <ksyscalls.h>
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
void arch_int_configure_io_interrupt(int32 irq, interrupt_trigger_mode mode, interrupt_trigger_polarity polarity)
{ AppleG5::mpic_configure(irq, mode == B_LEVEL_TRIGGERED, polarity == B_HIGH_ACTIVE_POLARITY || polarity == B_RISING_EDGE_POLARITY); }
int32 arch_int_assign_to_cpu(int32 irq, int32 cpu) { return AppleG5::mpic_assign_to_cpu(irq, cpu); }
static void print_iframe(iframe* f) { dprintf("PPC64 iframe=%p vector=%lx srr0=%p srr1=%lx dar=%p dsisr=%lx\n", f, f->vector, (void*)f->srr0, f->srr1, (void*)f->dar, f->dsisr); }

static void
handle_syscall(iframe* f)
{
	/* PPC64 ELF ABI supplies eight integer argument registers: r3..r10.
	 * The caller's parameter save area starts at SP+48; overflow arguments
	 * therefore begin at SP+112 after the eight saved argument slots. */
	uint8 buffer[MAX_SYSCALL_PARAMETERS * 8];
	memset(buffer, 0, sizeof(buffer));
	const uint64 regs[8] = {f->r3, f->r4, f->r5, f->r6, f->r7, f->r8, f->r9, f->r10};
	uint32 number = (uint32)f->r0;
	if (number >= (uint32)kSyscallCount) {
		f->r3 = B_BAD_VALUE;
		return;
	}

	const extended_syscall_info& info = kExtendedSyscallInfos[number];
	for (int32 i = 0; i < info.parameter_count; i++) {
		int32 size = info.parameters[i].size;
		if (size > 8) size = 8;
		if (size <= 0 || info.parameters[i].offset < 0
			|| info.parameters[i].offset + size > (int32)sizeof(buffer))
			continue;

		if (i < 8) {
			memcpy(buffer + info.parameters[i].offset, &regs[i], size);
		} else {
			const addr_t userArgument = f->r1 + 48 + 8 * 8 + (i - 8) * 8;
			user_memcpy(buffer + info.parameters[i].offset,
				(void*)userArgument, size);
		}
	}

	uint64 ret = 0;
	enable_interrupts();
	syscall_dispatcher(number, buffer, &ret);
	f->r3 = ret;
	disable_interrupts();
}

extern "C" void
ppc64_exception_entry(uint64 vector, iframe* f)
{
	f->vector = vector;
	Thread* t = thread_get_current_thread();
	iframe_stack* s = t != NULL ? &t->arch_info.iframes : &gBootFrameStack;
	ppc_push_iframe(s, f);
	switch (vector) {
		case 0x380: case 0x480:
			if (ppc64_mmu_handle_segment_fault(vector == 0x380 ? f->dar : f->srr0) != B_OK) { print_iframe(f); panic("ppc64: SLB refill failed"); }
			break;
		case 0x500: {
			int32 irq;
			while ((irq = AppleG5::mpic_acknowledge()) >= 0) {
				if (irq == 0x20) smp_intercpu_interrupt_handler(smp_get_current_cpu());
				else io_interrupt_handler(irq, true);
				AppleG5::mpic_eoi();
			}
			break;
		}
		case 0x800: f->srr1 |= MSR_FP_AVAILABLE; break;
		case 0x900: timer_interrupt(); break;
		case 0xc00: handle_syscall(f); break;
		case 0x300: case 0x400: {
			cpu_ent* cpu = &gCPU[smp_get_current_cpu()];
			if (cpu->fault_handler != 0) { f->srr0 = cpu->fault_handler; break; }
			addr_t faultAddress = vector == 0x300 ? f->dar : f->srr0;
			addr_t ip = 0;
			vm_page_fault(faultAddress, f->srr0,
				vector == 0x300 && (f->dsisr & (1ULL << 25)) != 0,
				false, (f->srr1 & MSR_PRIVILEGE_LEVEL) != 0, &ip);
			if (ip) f->srr0 = ip;
			break;
		}
		case 0x200: print_iframe(f); panic("ppc64: machine check exception"); break;
		case 0x600: panic("ppc64: alignment exception"); break;
		case 0x700: panic("ppc64: program exception"); break;
		case 0xf20: panic("ppc64: AltiVec unavailable"); break;
		default: print_iframe(f); panic("ppc64: unhandled exception vector 0x%lx", vector);
	}
	if (t != NULL) {
		cpu_status state = disable_interrupts();
		if (t->post_interrupt_callback) {
			void (*cb)(void*) = t->post_interrupt_callback;
			void* d = t->post_interrupt_data;
			t->post_interrupt_callback = NULL;
			t->post_interrupt_data = NULL;
			restore_interrupts(state);
			cb(d);
		} else {
			if (t->cpu->invoke_scheduler) {
				SpinLocker lock(t->scheduler_lock);
				scheduler_reschedule(B_THREAD_READY);
				lock.Unlock();
			}
			restore_interrupts(state);
		}
	}
	ppc_pop_iframe(s);
}

status_t arch_int_init(kernel_args*) { return B_OK; }
status_t arch_int_init_post_vm(kernel_args* args)
{
	if (!args || args->arch_args.exception_handlers.size < B_PAGE_SIZE) return B_BAD_VALUE;
	ppc_cpu_exception_context* c = ppc_get_cpu_exception_context(0);
	if (c) { c->exception_context = c; c->kernel_stack = args->cpu_kstack[0].start + args->cpu_kstack[0].size; ppc_set_current_cpu_exception_context(c); }
	void* handlers = (void*)(addr_t)args->arch_args.exception_handlers.start;
	area_id area = create_area("ppc64_exception_vectors", &handlers, B_EXACT_ADDRESS, args->arch_args.exception_handlers.size, B_ALREADY_WIRED, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < B_OK) return area;
	size_t n = (size_t)(&__irqvec_end - &__irqvec_start);
	if (n > args->arch_args.exception_handlers.size) return B_BAD_VALUE;
	memcpy(handlers, &__irqvec_start, n);
	arch_cpu_sync_icache(handlers, n);
	set_msr(get_msr() & ~MSR_EXCEPTION_PREFIX);
	return B_OK;
}
status_t arch_int_init_io(kernel_args*) { return AppleG5::mpic_init(); }
status_t arch_int_init_post_device_manager(kernel_args*) { return B_OK; }
