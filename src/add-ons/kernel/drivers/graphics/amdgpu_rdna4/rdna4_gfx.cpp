#include "rdna4_gfx.h"

RDNA4GFX::RDNA4GFX()
	: fInitialized(false), fSequence(0)
{
}

status_t
RDNA4GFX::Initialize()
{
	/* A 64 KiB software ring gives the hardware backend a bounded staging
	 * area without assuming a particular GFX12 ring-memory placement. */
	status_t status = fRing.Initialize(16384);
	if (status != B_OK)
		return status;

	fInitialized = true;
	fSequence = 0;
	return B_OK;
}

status_t
RDNA4GFX::Submit(const uint32* commands, uint32 count, rdna4_fence* _fence)
{
	if (!fInitialized)
		return B_NO_INIT;
	if (commands == NULL || count == 0 || _fence == NULL)
		return B_BAD_VALUE;
	if (count > fRing.Capacity())
		return B_BAD_VALUE;

	uint32 offset;
	status_t status = fRing.Reserve(count, &offset);
	if (status != B_OK)
		return status;

	status = fRing.Write(offset, commands, count);
	if (status != B_OK)
		return status;

	status = fRing.Commit(count);
	if (status != B_OK)
		return status;

	_fence->sequence = fRing.Signal();
	fSequence = _fence->sequence;

	/* The ring currently stops at software staging. Hardware doorbell and
	 * write-pointer programming must be supplied by the verified GFX12 ASIC
	 * backend before this sequence represents GPU completion. */
	return B_OK;
}

status_t
RDNA4GFX::Wait(const rdna4_fence& fence, bigtime_t timeout)
{
	if (!fInitialized)
		return B_NO_INIT;
	if (fence.sequence == 0 || fence.sequence > fSequence)
		return B_BAD_VALUE;
	(void)timeout;
	return B_OK;
}

status_t
RDNA4GFX::Reset()
{
	if (!fInitialized)
		return B_NO_INIT;
	fRing.Reset();
	fSequence = 0;
	return B_OK;
}
