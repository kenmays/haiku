#ifndef RDNA4_CAPS_H
#define RDNA4_CAPS_H

#include <SupportDefs.h>

#define RDNA4_CAP_DISPLAY        0x00000001
#define RDNA4_CAP_CURSOR         0x00000002
#define RDNA4_CAP_DP             0x00000004
#define RDNA4_CAP_HDMI           0x00000008
#define RDNA4_CAP_GPUVM          0x00000010
#define RDNA4_CAP_SDMA           0x00000020
#define RDNA4_CAP_GFX12          0x00000040
#define RDNA4_CAP_VCN            0x00000080
#define RDNA4_CAP_OVERLAY        0x00000100
#define RDNA4_CAP_MST            0x00000200

struct rdna4_capabilities {
	uint32 version;
	uint32 flags;
	uint32 maxDisplays;
	uint32 maxEngines;
	uint32 maxTextureWidth;
	uint32 maxTextureHeight;
	uint32 framebufferFormats;
};

void rdna4_default_capabilities(rdna4_capabilities& caps);

#endif
