/* Apple U3/U3H/U4 DART (DMA address remapping table) support. */
#ifndef _KERNEL_APPLE_G5_DART_H
#define _KERNEL_APPLE_G5_DART_H

#include <SupportDefs.h>

namespace AppleG5DART {
status_t Init(addr_t base, size_t size, bool u4 = false);
void Shutdown();

/* Map physical memory into the 32-bit PCI DMA aperture. */
status_t Map(addr_t physical, size_t size, addr_t* _dmaAddress);
status_t Unmap(addr_t dmaAddress, size_t size);

inline status_t
MapPhysical(addr_t physical, size_t size, addr_t* _dmaAddress)
{
	return Map(physical, size, _dmaAddress);
}

bool Enabled();
addr_t Base();
size_t Size();
}

#endif
