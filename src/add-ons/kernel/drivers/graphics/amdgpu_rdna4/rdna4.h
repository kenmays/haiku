#ifndef RDNA4_H
#define RDNA4_H

#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>

#define RDNA4_VENDOR_ID 0x1002

#define RDNA4_GFX1200 0x1200
#define RDNA4_GFX1201 0x1201

enum rdna4_chip {
	RDNA4_CHIP_UNKNOWN = 0,
	RDNA4_CHIP_GFX1200,
	RDNA4_CHIP_GFX1201
};

struct rdna4_pci_device {
	uint16 device_id;
	rdna4_chip chip;
	const char* name;
};

struct rdna4_shared_info {
	uint32 version;
	uint32 chip;
	uint16 pci_vendor_id;
	uint16 pci_device_id;
	uint8 bus;
	uint8 device;
	uint8 function;
	uint8 reserved;

	uint64 mmio_physical;
	uint64 mmio_size;
	uint64 framebuffer_physical;
	uint64 framebuffer_size;

	uint32 framebuffer_width;
	uint32 framebuffer_height;
	uint32 framebuffer_pitch;
	uint32 framebuffer_depth;

	bool initialized;
	bool framebuffer_fallback;
};

status_t rdna4_device_init(const pci_info& pci, rdna4_shared_info& shared);
void rdna4_device_uninit(rdna4_shared_info& shared);

const rdna4_pci_device* rdna4_lookup_device(uint16 deviceID);

#endif
