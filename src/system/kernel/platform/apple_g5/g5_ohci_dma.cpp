#include "g5_ohci_dma.h"
#include "g5_dart.h"

namespace AppleG5OHCI {

status_t
PrepareDMA(phys_addr_t physical, size_t size, addr_t* dmaAddress)
{
	if (dmaAddress == NULL || size == 0)
		return B_BAD_VALUE;
	if (physical > kDMAMaxAddress)
		return B_BAD_VALUE;

	status_t status = AppleG5DART::Map((addr_t)physical, size, dmaAddress);
	if (status != B_OK)
		return status;
	if ((uint64)*dmaAddress + size - 1 > kDMAMaxAddress) {
		AppleG5DART::Unmap(*dmaAddress, size);
		return B_BAD_VALUE;
	}
	DMAMemoryBarrier();
	return B_OK;
}

status_t
FinishDMA(addr_t dmaAddress, size_t size)
{
	if (size == 0)
		return B_BAD_VALUE;
	DMAMemoryBarrier();
	return AppleG5DART::Unmap(dmaAddress, size);
}

}
