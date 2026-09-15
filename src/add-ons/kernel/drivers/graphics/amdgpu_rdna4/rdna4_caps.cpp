#include "rdna4_caps.h"

void
rdna4_default_capabilities(rdna4_capabilities& caps)
{
	caps.version = 1;
	caps.flags = RDNA4_CAP_DISPLAY | RDNA4_CAP_CURSOR | RDNA4_CAP_DP
		| RDNA4_CAP_HDMI | RDNA4_CAP_GPUVM | RDNA4_CAP_SDMA
		| RDNA4_CAP_GFX12 | RDNA4_CAP_VCN | RDNA4_CAP_OVERLAY
		| RDNA4_CAP_MST;
	caps.maxDisplays = 4;
	caps.maxEngines = 1;
	caps.maxTextureWidth = 16384;
	caps.maxTextureHeight = 16384;
	caps.framebufferFormats = 0x0000000f;
}
