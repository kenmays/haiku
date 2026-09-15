#include "rdna4_gfx.h"

RDNA4GFX::RDNA4GFX()
	: fInitialized(false), fSequence(0)
{
}

status_t
RDNA4GFX::Initialize()
{
	/* Do not emit packets until the exact GFX12 packet/register definitions
	 * are selected for the target firmware revision. */
	fInitialized = true;
	fSequence = 0;
	return B_OK;
}

status_t
RDNA4GFX::Submit(const uint32* commands, uint32 count, rdna4_fence* _fence)
{
	if (!fInitialized || commands == NULL || count == 0 || _fence == NULL)
		return B_BAD_VALUE;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4GFX::Wait(const rdna4_fence& fence, bigtime_t timeout)
{
	(void)fence;
	(void)timeout;
	if (!fInitialized)
		return B_NO_INIT;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4GFX::Reset()
{
	if (!fInitialized)
		return B_NO_INIT;
	fSequence = 0;
	return B_NOT_SUPPORTED;
}
