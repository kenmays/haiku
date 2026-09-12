#include <computed_asm_macros.h>
#include <cpu.h>
#define DEFINE_MACRO(macro, value) DEFINE_COMPUTED_ASM_MACRO(macro, value)
#define DEFINE_OFFSET_MACRO(prefix, structure, member) \
	DEFINE_MACRO(prefix##_##member, offsetof(struct structure, member));
void dummy()
{
	DEFINE_OFFSET_MACRO(CPU_ENT, cpu_ent, fault_handler);
	DEFINE_OFFSET_MACRO(CPU_ENT, cpu_ent, fault_handler_stack_pointer);
}
