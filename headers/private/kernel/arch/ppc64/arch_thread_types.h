/* PowerPC 64-bit architecture thread types. */
#ifndef KERNEL_ARCH_PPC64_THREAD_TYPES_H
#define KERNEL_ARCH_PPC64_THREAD_TYPES_H

#define IFRAME_TRACE_DEPTH 4

struct iframe;
struct iframe_stack {
	struct iframe* frames[IFRAME_TRACE_DEPTH];
	int32 index;
};

struct arch_thread {
	void* sp;
	void* interrupt_stack;
	struct iframe_stack iframes;
};

struct arch_team { char dummy; };
struct arch_fork_arg { struct iframe frame; };

#endif
