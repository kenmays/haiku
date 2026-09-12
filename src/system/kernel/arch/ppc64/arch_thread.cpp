/* PPC64 thread state and context switching. */
#include <arch/cpu.h>
#include <arch/thread.h>
#include <arch_mmu.h>
#include <boot/stage2.h>
#include <commpage_defs.h>
#include <kernel.h>
#include <thread.h>
#include <team.h>
#include <vm/vm_types.h>
#include <vm/vm.h>
#include <vm/VMAddressSpace.h>
#include <string.h>

static arch_thread sInitialState;
extern "C" void ppc_kernel_thread_root();
extern "C" void ppc64_enter_userspace(addr_t entry,addr_t stack,addr_t arg1,
	addr_t arg2,addr_t tls,addr_t returnAddress) __attribute__((noreturn));
void ppc_push_iframe(iframe_stack* s,iframe* f){ASSERT(s->index<IFRAME_TRACE_DEPTH);s->frames[s->index++]=f;}
void ppc_pop_iframe(iframe_stack*s){ASSERT(s->index>0);--s->index;}
iframe* ppc_get_user_iframe(void){Thread*t=thread_get_current_thread();if(!t)return NULL;for(int i=t->arch_info.iframes.index-1;i>=0;i--){iframe*f=t->arch_info.iframes.frames[i];if((f->srr1&MSR_PRIVILEGE_LEVEL)!=0)return f;}return NULL;}
status_t arch_thread_init(kernel_args*){memset(&sInitialState,0,sizeof(sInitialState));return B_OK;}
status_t arch_team_init_team_struct(Team*,bool){return B_OK;}
status_t arch_thread_init_thread_struct(Thread*t){memcpy(&t->arch_info,&sInitialState,sizeof(arch_thread));return B_OK;}
void arch_thread_init_kthread_stack(Thread*t,void*,void*top,void(*entry)(void*),const void*data){uintptr_t*sp=(uintptr_t*)top;sp=(uintptr_t*)((uintptr_t)sp&~0xFULL);sp-=24;sp[0]=(uintptr_t)ppc_kernel_thread_root;sp[2]=(uintptr_t)entry;sp[3]=(uintptr_t)data;t->arch_info.sp=sp;}
status_t arch_thread_init_tls(Thread*t){t->user_local_storage=t->user_stack_base+t->user_stack_size;return B_OK;}
void arch_thread_context_switch(Thread*from,Thread*to){if(to->team->address_space&&from->team!=to->team)ppc_translation_map_change_asid(to->team->address_space->TranslationMap());ppc_context_switch(&from->arch_info.sp,to->arch_info.sp,&to->arch_info);}
void arch_thread_dump_info(void*info){dprintf("\tsp: %p\n",((arch_thread*)info)->sp);}
status_t arch_thread_enter_userspace(Thread*t,addr_t entry,void*a1,void*a2){if(!t||!t->team||!t->team->address_space)return B_BAD_VALUE;addr_t cp=(addr_t)t->team->commpage_address,exitAddr;status_t e=user_memcpy(&exitAddr,&((addr_t*)cp)[COMMPAGE_ENTRY_PPC64_THREAD_EXIT],sizeof(exitAddr));if(e!=B_OK)return e;exitAddr+=cp;addr_t sp=(t->user_stack_base+t->user_stack_size)&~addr_t(0xf);ppc64_mmu_switch_address_space((addr_t)t->team->address_space->TranslationMap());ppc64_enter_userspace(entry,sp,(addr_t)a1,(addr_t)a2,t->user_local_storage,exitAddr);return B_ERROR;}
bool arch_on_signal_stack(Thread*){return false;}
status_t arch_setup_signal_frame(Thread*,struct sigaction*,struct signal_frame_data*){return B_NOT_SUPPORTED;}
int64 arch_restore_signal_frame(struct signal_frame_data*){return B_NOT_SUPPORTED;}
void arch_store_fork_frame(struct arch_fork_arg*arg){iframe*frame=ppc_get_user_iframe();if(!frame){memset(&arg->frame,0,sizeof(arg->frame));return;}memcpy(&arg->frame,frame,sizeof(arg->frame));arg->frame.r3=0;}
void arch_restore_fork_frame(struct arch_fork_arg*arg){ppc64_restore_iframe(&arg->frame);}
