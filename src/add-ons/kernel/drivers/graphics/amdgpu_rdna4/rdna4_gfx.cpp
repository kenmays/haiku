#include "rdna4_gfx.h"
#include "rdna4_ring.h"

RDNA4GFX::RDNA4GFX()
	: fInitialized(false), fSequence(0)
{
}

status_t
RDNA4GFX::Initialize()
{
	/* The ring is usable as a validated software staging queue.  Hardware
	 * doorbell/ring programming remains generation-specific and is not
	 * attempted here. */
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

	/* Validate packet storage without issuing unverified GFX12 MMIO.  A real
	 * backend will replace this staging step with the hardware ring writer. */
	if (count > 1U << 20)
		return B_BAD_VALUE;

	++fSequence;
	_fence->sequence = fSequence;
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
	/* Software staging completes at commit time.  Hardware completion must be
	 * wired to the GFX12 interrupt/fence path before this becomes a GPU wait. */
	return B_OK;
}

status_t
RDNA4GFX::Reset()
{
	if (!fInitialized)
		return B_NO_INIT;
	fSequence = 0;
	return B_OK;
}
