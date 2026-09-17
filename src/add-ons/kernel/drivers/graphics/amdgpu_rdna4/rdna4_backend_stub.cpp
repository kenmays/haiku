#include "rdna4_backend.h"

/*
 * Generic fallback backend. It establishes the lifecycle and explicitly
 * refuses hardware activation until a verified GFX12/DCN4 implementation is
 * selected. This object is useful during bring-up because callers can test
 * capability negotiation without accidentally programming unknown registers.
 */
static status_t
unsupported_discover(void* context, rdna4_backend_info& info)
{
	(void)context;
	info.gfx_version = 0;
	info.dcn_version = 0;
	info.vmid_count = 0;
	return B_NOT_SUPPORTED;
}

const rdna4_backend_ops kRDNA4UnsupportedBackend = {
	unsupported_discover,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL
};
