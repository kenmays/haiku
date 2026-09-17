#include "rdna4_caps.h"

void
rdna4_default_capabilities(rdna4_capabilities& caps)
{
	/* Only advertise functionality that the current Haiku implementation
	 * actually exposes.  GFX12 VM, SDMA, VCN, overlays and multi-display
	 * support remain separate bring-up stages and must not be advertised
	 * before their register/firmware paths are live. */
	caps.version = 2;
	caps.flags = RDNA4_CAP_DISPLAY | RDNA4_CAP_GFX12;
	caps.maxDisplays = 1;
	caps.maxEngines = 0;
	caps.maxTextureWidth = 0;
	caps.maxTextureHeight = 0;
	caps.framebufferFormats = 0x00000001;
}
