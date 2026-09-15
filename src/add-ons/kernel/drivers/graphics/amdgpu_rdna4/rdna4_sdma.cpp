#include "rdna4_sdma.h"

RDNA4SDMA::RDNA4SDMA()
	: fInitialized(false)
{
}

status_t
RDNA4SDMA::Initialize()
{
	/* SDMA ring programming is deferred until the exact RDNA4 SDMA IP
	 * generation and firmware interface are selected. */
	fInitialized = true;
	return B_OK;
}

status_t
RDNA4SDMA::Copy(uint64 dst, uint64 src, uint64 size, uint64* _fence)
{
	if (!fInitialized || dst == 0 || src == 0 || size == 0 || _fence == NULL)
		return B_BAD_VALUE;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4SDMA::Fill(uint64 dst, uint64 size, uint32 value, uint64* _fence)
{
	if (!fInitialized || dst == 0 || size == 0 || _fence == NULL)
		return B_BAD_VALUE;
	(void)value;
	return B_NOT_SUPPORTED;
}
