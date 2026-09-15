#include "rdna4_sdma.h"

RDNA4SDMA::RDNA4SDMA()
	: fInitialized(false), fSequence(0)
{
}

status_t
RDNA4SDMA::Initialize()
{
	status_t status = fRing.Initialize(16384);
	if (status != B_OK)
		return status;
	fSequence = 0;
	fInitialized = true;
	return B_OK;
}

status_t
RDNA4SDMA::Copy(uint64 dst, uint64 src, uint64 size, uint64* _fence)
{
	if (!fInitialized)
		return B_NO_INIT;
	if (dst == 0 || src == 0 || size == 0 || _fence == NULL)
		return B_BAD_VALUE;
	if (size > UINT32_MAX)
		return B_BAD_VALUE;

	/* Compact software operation record. Exact SDMA packet encoding is
	 * generation-specific and is intentionally not guessed here. */
	uint32 packet[4] = {1, (uint32)dst, (uint32)src, (uint32)size};
	uint32 offset;
	status_t status = fRing.Reserve(4, &offset);
	if (status != B_OK)
		return status;
	status = fRing.Write(offset, packet, 4);
	if (status != B_OK)
		return status;
	status = fRing.Commit(4);
	if (status != B_OK)
		return status;

	*_fence = fRing.Signal();
	fSequence = *_fence;
	return B_OK;
}

status_t
RDNA4SDMA::Fill(uint64 dst, uint64 size, uint32 value, uint64* _fence)
{
	if (!fInitialized)
		return B_NO_INIT;
	if (dst == 0 || size == 0 || _fence == NULL)
		return B_BAD_VALUE;
	if (size > UINT32_MAX)
		return B_BAD_VALUE;

	uint32 packet[4] = {2, (uint32)dst, (uint32)size, value};
	uint32 offset;
	status_t status = fRing.Reserve(4, &offset);
	if (status != B_OK)
		return status;
	status = fRing.Write(offset, packet, 4);
	if (status != B_OK)
		return status;
	status = fRing.Commit(4);
	if (status != B_OK)
		return status;

	*_fence = fRing.Signal();
	fSequence = *_fence;
	return B_OK;
}
