/* PPC64 thread state and context switching. */
#include <arch/cpu.h>
#include <arch/thread.h>
#include <boot/stage2.h>
#include <kernel.h>
#include <thread.h>
#include <vm/vm_types.h>
#include <string.h>

static arch_thread sInitialState;
extern "C" void ppc_kernel_thread_root();

void ppc_push_iframe(struct iframe_stack* stack, struct iframe* frame)
{
	ASSERT(stack->index < IFRAME_TRACE_DEPTH);
	stack->frames[stack->index++] = frame;
}

void ppc_pop_iframe(struct iframe_stack* stack)
{
	ASSERT(stack->index > 0);
	--stack->index;
}

struct iframe* ppc_get_user_iframe(void)
{
	Thread* thread = thread_get_current_thread();
	if (thread == NULL)
		return NULL;
	for (int i = thread->arch_info.iframes.index - 1; i >= 0; --i) {
		iframe* frame = thread->arch_info.iframes.frames[i];
		if ((frame->srr1 & MSR_PRIVILEGE_LEVEL) != 0)
			return frame;
	}
	return NULL;
}

status_t arch_thread_init(kernel_args*) { memset(&sInitialState, 0, sizeof(sInitialState)); return B_OK; }
status_t arch_team_init_team_struct(Team*, bool) { return B_OK; }
status_t arch_thread_init_thread_struct(Thread* thread)
{
	memcpy(&thread->arch_info, &sInitialState, sizeof(arch_thread));
	return B_OK;
}

void arch_thread_init_kthread_stack(Thread* thread, void*, void* stackTop,
	void (*entry)(void*), const void* data)
{
	/* ppc_kernel_thread_root expects the three call targets in r13-r15. */
	uintptr_t* sp = (uintptr_t*)stackTop;
	sp = (uintptr_t*)((uintptr_t)sp & ~0xFULL);
	sp -= 24;
	sp[0] = (uintptr_t)ppc_kernel_thread_root;
	sp[2] = (uintptr_t)entry;
	sp[3] = (uintptr_t)data;
	thread->arch_info.sp = sp;
}

status_t arch_thread_init_tls(Thread*) { return B_OK; }

void arch_thread_context_switch(Thread* from, Thread* to)
{
	if (to->team->address_space != NULL && from->team != to->team)
		ppc_translation_map_change_asid(to->team->address_space->TranslationMap());
	ppc_context_switch(&from->arch_info.sp, to->arch_info.sp);
}

void arch_thread_dump_info(void* info)
{
	dprintf("\tsp: %p\n", ((arch_thread*)info)->sp);
}

status_t arch_thread_enter_userspace(Thread*, addr_t, void*, void*)
{
	panic("ppc64: user entry not implemented");
	return B_ERROR;
}
bool arch_on_signal_stack(Thread*) { return false; }
status_t arch_setup_signal_frame(Thread*, struct sigaction*, struct signal_frame_data*) { return B_NOT_SUPPORTED; }
int64 arch_restore_signal_frame(struct signal_frame_data*) { return B_NOT_SUPPORTED; }
void arch_store_fork_frame(struct arch_fork_arg*) {}
void arch_restore_fork_frame(struct arch_fork_arg*) { panic("ppc64: fork restore not implemented"); }
