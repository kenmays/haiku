/* PPC64 Open Firmware CPU discovery for Power Mac G5. */
#include <boot/platform/openfirmware/platform_arch.h>
#include <stdio.h>
#include <KernelExport.h>
#include <boot/kernel_args.h>
#include <boot/stage2.h>
#include <kernel.h>
#include <platform/openfirmware/devices.h>
#include <platform/openfirmware/openfirmware.h>

extern "C" uint32 ppc64_boot_get_pvr(void);

status_t
boot_arch_cpu_init(void)
{
	int cpus = of_finddevice("/cpus");
	if (cpus == OF_FAILED) {
		printf("ppc64: /cpus not found\n");
		return B_ERROR;
	}

	int32 busFrequency = 0;
	int32 cpuCount = 0;
	char cpuPath[256];
	intptr_t cookie = 0;

	while (of_get_next_device(&cookie, cpus, "cpu", cpuPath,
		sizeof(cpuPath)) == B_OK) {
		int cpu = of_finddevice(cpuPath);
		if (cpu == OF_FAILED)
			continue;

		if (cpuCount == 0) {
			int32 cpuFrequency = 0;
			int32 timeBaseFrequency = 0;
			if (of_getprop(cpu, "clock-frequency", &cpuFrequency, 4)
				== OF_FAILED)
				return B_ERROR;
			if (of_getprop(cpu, "bus-frequency", &busFrequency, 4)
				== OF_FAILED)
				busFrequency = cpuFrequency;
			if (of_getprop(cpu, "timebase-frequency", &timeBaseFrequency, 4)
				== OF_FAILED)
				return B_ERROR;

			gKernelArgs.arch_args.cpu_frequency = cpuFrequency;
			gKernelArgs.arch_args.bus_frequency = busFrequency;
			gKernelArgs.arch_args.time_base_frequency = timeBaseFrequency;
		}
		cpuCount++;
	}

	if (cpuCount == 0)
		return B_ERROR;

	gKernelArgs.num_cpus = cpuCount;
	printf("ppc64: found %d CPU(s), PVR 0x%08lx\n", cpuCount,
		(unsigned long)ppc64_boot_get_pvr());

	/* Open Firmware supplies a valid identity-mapped allocation for the
	 * initial kernel stacks. Do not merely return an unclaimed fixed VA. */
	addr_t stack = (addr_t)arch_mmu_allocate(NULL,
		cpuCount * (KERNEL_STACK_SIZE
			+ KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE),
		B_READ_AREA | B_WRITE_AREA, false);
	if (stack == 0)
		return B_NO_MEMORY;

	for (int i = 0; i < cpuCount; i++) {
		gKernelArgs.cpu_kstack[i].start = stack;
		gKernelArgs.cpu_kstack[i].size = KERNEL_STACK_SIZE
			+ KERNEL_STACK_GUARD_PAGES * B_PAGE_SIZE;
		stack += gKernelArgs.cpu_kstack[i].size;
	}

	return B_OK;
}
