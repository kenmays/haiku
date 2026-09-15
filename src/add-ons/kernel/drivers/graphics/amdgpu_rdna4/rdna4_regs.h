#ifndef RDNA4_REGS_H
#define RDNA4_REGS_H

#include <SupportDefs.h>

/*
 * MMIO access helpers deliberately expose only bounded access.  Register
 * offsets are supplied by the hardware-generation-specific implementation;
 * this header contains no guessed GFX12/DCN4 offsets.
 */
struct rdna4_mmio {
	volatile uint8* base;
	uint64 size;
};

static inline bool
rdna4_mmio_valid(const rdna4_mmio& mmio, uint32 offset, uint32 width)
{
	if (mmio.base == NULL || width == 0)
		return false;
	if ((uint64)offset + width > mmio.size)
		return false;
	return (offset & (width - 1)) == 0;
}

static inline uint32
rdna4_read32(const rdna4_mmio& mmio, uint32 offset)
{
	if (!rdna4_mmio_valid(mmio, offset, sizeof(uint32)))
		return 0xffffffffU;
	return *(volatile uint32*)(mmio.base + offset);
}

static inline void
rdna4_write32(const rdna4_mmio& mmio, uint32 offset, uint32 value)
{
	if (!rdna4_mmio_valid(mmio, offset, sizeof(uint32)))
		return;
	*(volatile uint32*)(mmio.base + offset) = value;
}

static inline uint64
rdna4_read64(const rdna4_mmio& mmio, uint32 offset)
{
	if (!rdna4_mmio_valid(mmio, offset, sizeof(uint64)))
		return UINT64_MAX;
	return *(volatile uint64*)(mmio.base + offset);
}

static inline void
rdna4_write64(const rdna4_mmio& mmio, uint32 offset, uint64 value)
{
	if (!rdna4_mmio_valid(mmio, offset, sizeof(uint64)))
		return;
	*(volatile uint64*)(mmio.base + offset) = value;
}

#endif
