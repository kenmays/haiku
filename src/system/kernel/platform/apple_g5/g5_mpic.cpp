/*
 * Apple Power Mac G5 U3/U3H OpenPIC interrupt controller.
 *
 * The G5 device tree exposes the MPIC as an OpenPIC-compatible interrupt
 * controller. The implementation deliberately keeps the controller access
 * in the platform layer so the PPC64 exception path does not depend on a
 * loadable driver during early kernel startup.
 */
#include <KernelExport.h>
#include <ByteOrder.h>
#include <boot/kernel_args.h>
#include <platform/openfirmware/openfirmware.h>
#include <platform/openfirmware/devices.h>
#include <vm/vm.h>

#include <string.h>

#include "g5_mpic.h"

namespace AppleG5 {

static volatile uint8* sRegs;
static area_id sArea = -1;
static uint32 sCpuCount = 1;
static uint32 sIRQCount = 0;
static int32 sCurrentCpu;

static const uint32 FEATURE = 0x1000;
static const uint32 CONFIG = 0x1020;
static const uint32 SPURIOUS = 0x10e0;
static const uint32 IPI_VECTOR = 0x10a0;
static const uint32 SRC_BASE = 0x10000;
static const uint32 CPU_BASE = 0x20000;
static const uint32 IPI_DISPATCH = 0x40;
static const uint32 CPU_PRIORITY = 0x80;
static const uint32 IACK = 0xa0;
static const uint32 EOI = 0xb0;

static inline uint32
read32(uint32 offset)
{
	return B_SWAP_INT32(*(volatile uint32*)(sRegs + offset));
}

static inline void
write32(uint32 offset, uint32 value)
{
	*(volatile uint32*)(sRegs + offset) = B_SWAP_INT32(value);
	asm volatile("eieio" ::: "memory");
}

static bool
get_reg_address(intptr_t node, phys_addr_t& address, size_t& size)
{
	uint32 cellsAddress = B_HOST_TO_BENDIAN_INT32(2);
	uint32 cellsSize = B_HOST_TO_BENDIAN_INT32(1);
	intptr_t parent = of_parent(node);
	if (parent > 0) {
		of_getprop(parent, "#address-cells", &cellsAddress, sizeof(cellsAddress));
		of_getprop(parent, "#size-cells", &cellsSize, sizeof(cellsSize));
		cellsAddress = B_BENDIAN_TO_HOST_INT32(cellsAddress);
		cellsSize = B_BENDIAN_TO_HOST_INT32(cellsSize);
	}

	uint32 reg[8];
	int length = of_getprop(node, "reg", reg, sizeof(reg));
	if (length <= 0)
		return false;

	const uint32* p = reg;
	address = 0;
	for (uint32 i = 0; i < cellsAddress && i < 4; i++)
		address = (address << 32) | B_BENDIAN_TO_HOST_INT32(*p++);

	size = 0;
	for (uint32 i = 0; i < cellsSize && i < 2 &&
		i + cellsAddress < B_COUNT_OF(reg); i++)
		size = (size << 32) | B_BENDIAN_TO_HOST_INT32(*p++);

	if (size < 0x21000)
		size = 0x40000;
	return address != 0;
}

status_t
mpic_init()
{
	if (sRegs != NULL)
		return B_OK;

	intptr_t cookie = 0;
	intptr_t node = 0;
	while (true) {
		char path[B_PATH_NAME_LENGTH];
		status_t status = of_get_next_device(&cookie, 0, "interrupt-controller",
			path, sizeof(path));
		if (status != B_OK)
			break;
		node = cookie;
		char compatible[256];
		int length = of_getprop(node, "compatible", compatible,
			sizeof(compatible) - 1);
		if (length > 0) {
			compatible[length] = '\0';
			if (strstr(compatible, "open-pic") == NULL
				&& strstr(compatible, "openpic") == NULL)
				continue;
		}
		break;
	}

	if (node == 0)
		node = of_finddevice("/u3@0/interrupt-controller");
	if (node <= 0)
		node = of_finddevice("/interrupt-controller");
	if (node <= 0)
		return B_ENTRY_NOT_FOUND;

	phys_addr_t physical;
	size_t size;
	if (!get_reg_address(node, physical, size))
		return B_BAD_VALUE;

	void* virtualAddress = NULL;
	sArea = map_physical_memory("g5-mpic", physical, size,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		&virtualAddress);
	if (sArea < 0)
		return sArea;

	sRegs = (volatile uint8*)virtualAddress;
	uint32 feature = read32(FEATURE);
	sCpuCount = ((feature >> 8) & 0x1f) + 1;
	sIRQCount = ((feature >> 16) & 0x7ff) + 1;
	if (sCpuCount > 4)
		sCpuCount = 4;
	sCurrentCpu = 0;

	for (uint32 irq = 0; irq < sIRQCount; irq++) {
		uint32 source = SRC_BASE + irq * 0x20;
		write32(source, 0x80000000U | (8U << 16) | irq
			| 0x00400000U | 0x00800000U);
		write32(source + 0x10, 1);
	}

	uint32 config = read32(CONFIG);
	write32(CONFIG, config | 0x20000000U);
	write32(IPI_VECTOR, (8U << 16) | 0x20);
	write32(CPU_BASE + CPU_PRIORITY, 0);
	write32(SPURIOUS, 0xff);

	dprintf("apple_g5: MPIC at %p, %u CPUs, %u IRQs\n", sRegs,
		sCpuCount, sIRQCount);
	return B_OK;
}

status_t
mpic_init_per_cpu(int32 cpu)
{
	if (!sRegs)
		return B_NOT_INITIALIZED;
	if (cpu < 0 || (uint32)cpu >= sCpuCount)
		return B_BAD_VALUE;
	sCurrentCpu = cpu;
	write32(CPU_BASE + cpu * 0x1000 + CPU_PRIORITY, 0);
	return B_OK;
}

int32
mpic_acknowledge()
{
	if (!sRegs)
		return -1;
	uint32 value = read32(CPU_BASE + sCurrentCpu * 0x1000 + IACK);
	uint32 irq = value & 0xff;
	return irq == 0xff ? -1 : (int32)irq;
}

void
mpic_eoi()
{
	if (sRegs)
		write32(CPU_BASE + sCurrentCpu * 0x1000 + EOI, 0);
}

void
mpic_enable(int32 irq)
{
	if (!sRegs || irq < 0 || (uint32)irq >= sIRQCount)
		return;
	uint32 offset = SRC_BASE + irq * 0x20;
	write32(offset, read32(offset) & ~0x80000000U);
}

void
mpic_disable(int32 irq)
{
	if (!sRegs || irq < 0 || (uint32)irq >= sIRQCount)
		return;
	uint32 offset = SRC_BASE + irq * 0x20;
	write32(offset, read32(offset) | 0x80000000U);
}

void
mpic_configure(int32 irq, bool level, bool activeHigh)
{
	if (!sRegs || irq < 0 || (uint32)irq >= sIRQCount)
		return;
	uint32 offset = SRC_BASE + irq * 0x20;
	uint32 value = read32(offset);
	value &= ~(0x00400000U | 0x00800000U);
	if (level)
		value |= 0x00400000U;
	if (activeHigh)
		value |= 0x00800000U;
	write32(offset, value);
}

int32
mpic_assign_to_cpu(int32 irq, int32 cpu)
{
	if (!sRegs || irq < 0 || (uint32)irq >= sIRQCount
		|| cpu < 0 || (uint32)cpu >= sCpuCount)
		return B_BAD_VALUE;
	write32(SRC_BASE + irq * 0x20 + 0x10, 1U << cpu);
	return B_OK;
}

void
mpic_send_ipi(int32 cpu, uint8 vector)
{
	if (!sRegs || cpu < 0 || (uint32)cpu >= sCpuCount)
		return;
	/* The IPI vector register is global; the per-CPU dispatch register
	 * supplies the destination mask. */
	write32(IPI_VECTOR, (8U << 16) | vector);
	write32(CPU_BASE + sCurrentCpu * 0x1000 + IPI_DISPATCH, 1U << cpu);
}

uint32 mpic_cpu_count() { return sCpuCount; }
bool mpic_is_initialized() { return sRegs != NULL; }

}
