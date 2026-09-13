/*
 * Apple G5 OHCI DMA helpers.
 *
 * K2 OHCI is a conventional OHCI host controller, but its DMA engine
 * must see addresses in the Apple DART aperture.  Keep the policy here
 * so the generic OHCI code does not need PowerMac-specific knowledge.
 */
#ifndef _APPLE_G5_OHCI_DMA_H
#define _APPLE_G5_OHCI_DMA_H

#include <SupportDefs.h>
#include <vm/vm_types.h>

namespace AppleG5OHCI {

// K2 OHCI is a 32-bit PCI bus master.
static const uint64 kDMAMaxAddress = 0xffffffffULL;

status_t PrepareDMA(phys_addr_t physical, size_t size, addr_t* dmaAddress);
status_t FinishDMA(addr_t dmaAddress, size_t size);

// K2 OHCI needs posted MMIO writes drained before ownership is handed to
// the controller.  This is intentionally stronger than a compiler barrier.
inline void DMAMemoryBarrier()
{
	asm volatile("sync" ::: "memory");
}

inline void MMIOWriteBarrier()
{
	asm volatile("eieio\n\tsync" ::: "memory");
}

}

#endif
