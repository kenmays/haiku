/*
 * PowerMac G5 secondary CPU release.
 *
 * Open Firmware implementations differ in the exact CPU-release ABI.
 * Keep the actual firmware call isolated here and require the platform
 * code to supply the firmware CPU node and trampoline arguments.
 */
#include "g5_smp.h"
#include <debug.h>
#include <boot/openfirmware/openfirmware.h>

namespace {

struct cpu_release_record {
	volatile uint64 entry;
	volatile uint64 stack;
	volatile uint64 ready;
	volatile uint64 go;
};

static cpu_release_record sRelease[SMP_MAX_CPUS] __attribute__((aligned(128)));

static void
flush_record(cpu_release_record* record)
{
	asm volatile("dcbst 0,%0\n\tsync\n\tdcbf 0,%0\n\tsync" :: "r"(record) : "memory");
}

}

namespace AppleG5 {

status_t
smp_release_secondary(kernel_args*, int32 cpu, addr_t entry, addr_t stack)
{
	if (cpu <= 0 || cpu >= SMP_MAX_CPUS || entry == 0 || stack == 0)
		return B_BAD_VALUE;

	cpu_release_record* record = &sRelease[cpu];
	record->entry = entry;
	record->stack = stack;
	record->ready = 0;
	record->go = 0;
	flush_record(record);

	/*
	 * Do not guess an Open Firmware method signature here.  OF CPU startup
	 * is model/firmware dependent (start-cpu versus a release-address
	 * protocol).  The trampoline record is therefore prepared atomically;
	 * the platform's OF layer must invoke the appropriate firmware method.
	 */
	return B_NOT_SUPPORTED;
}

}
