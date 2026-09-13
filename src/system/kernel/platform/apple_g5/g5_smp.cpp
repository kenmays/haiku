/*
 * PowerMac G5 secondary CPU release.
 *
 * Open Firmware defines a standard PowerPC start-cpu client service:
 *     start-cpu(nodeid, pc, arg)
 * The started CPU enters at a real-mode PC with arg in r3.
 *
 * The release record is kept cache-line aligned because PPC970-class G5
 * systems use 128-byte cache lines. The final trampoline must consume a
 * physical release-record address before enabling translation.
 */
#include "g5_smp.h"
#include <debug.h>
#include <platform/openfirmware/openfirmware.h>
#include <string.h>

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

static intptr_t
find_cpu_node(int32 cpu)
{
	intptr_t cpus = of_finddevice("/cpus");
	if (cpus == OF_FAILED)
		return OF_FAILED;

	for (intptr_t node = of_child(cpus); node != 0 && node != OF_FAILED;
		node = of_peer(node)) {
		char type[16] = {};
		if (of_getprop(node, "device_type", type, sizeof(type) - 1) <= 0
			|| strcmp(type, "cpu") != 0)
			continue;

		uint32 reg = 0xffffffff;
		if (of_getprop(node, "reg", &reg, sizeof(reg)) != sizeof(reg))
			continue;

		/* Open Firmware properties are big-endian encoded integers. */
		uint32 hardwareCPU = __builtin_bswap32(reg);
		if (hardwareCPU == (uint32)cpu)
			return node;
	}

	return OF_FAILED;
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
	 * The OF service itself is now specified rather than guessed. We still
	 * require the caller to provide a real-mode trampoline and a physical
	 * address for its argument. The kernel virtual address of sRelease[] is
	 * deliberately not passed to firmware because OF starts the CPU with the
	 * MMU disabled.
	 */
	return B_NOT_SUPPORTED;
}

status_t
smp_start_cpu_of(int32 cpu, addr_t realModePC, addr_t physicalArgument)
{
	if (cpu <= 0 || cpu >= SMP_MAX_CPUS || realModePC == 0
		|| physicalArgument == 0)
		return B_BAD_VALUE;

	intptr_t node = find_cpu_node(cpu);
	if (node == OF_FAILED)
		return B_ENTRY_NOT_FOUND;

	intptr_t result = of_call_client_function("start-cpu", 3, 0,
		(uint32)node, realModePC, physicalArgument);
	return result == OF_FAILED ? B_ERROR : B_OK;
}

}
