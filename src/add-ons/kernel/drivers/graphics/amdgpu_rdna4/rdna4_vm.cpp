#include "rdna4_vm.h"

RDNA4VM::RDNA4VM()
	: fInitialized(false)
{
}

status_t
RDNA4VM::Initialize()
{
	/* Hardware page-table allocation and GFX12 TLB programming are
	 * intentionally deferred until the matching upstream register set
	 * and firmware ABI are imported. */
	fInitialized = true;
	return B_OK;
}

status_t
RDNA4VM::Map(uint64 gpuVA, uint64 physical, uint64 size, uint64 flags)
{
	if (!fInitialized || gpuVA == 0 || physical == 0 || size == 0)
		return B_BAD_VALUE;
	(void)flags;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4VM::Unmap(uint64 gpuVA, uint64 size)
{
	if (!fInitialized || gpuVA == 0 || size == 0)
		return B_BAD_VALUE;
	return B_NOT_SUPPORTED;
}

status_t
RDNA4VM::Flush()
{
	if (!fInitialized)
		return B_NO_INIT;
	return B_NOT_SUPPORTED;
}
