#ifndef RDNA4_H
#define RDNA4_H

#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>

#define RDNA4_VENDOR_ID 0x1002
#define RDNA4_PRIVATE_DATA_MAGIC 0x52443434
#define RDNA4_GET_PRIVATE_DATA 0x52443440
#define RDNA4_ACCELERANT_NAME "amdgpu_rdna4"
#define RDNA4_MAX_CARDS 8
#define RDNA4_DEVICE_NAME_LENGTH 64

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
	uint32 size;
	uint32 chip;
	uint32 device_index;
	uint16 pci_vendor_id;
	uint16 pci_device_id;
	uint8 bus;
	uint8 device;
	uint8 function;
	uint8 revision;
	char device_name[RDNA4_DEVICE_NAME_LENGTH];

	uint64 mmio_physical;
	uint64 mmio_size;
	uint64 framebuffer_physical;
	uint64 framebuffer_size;

	uint32 framebuffer_width;
	uint32 framebuffer_height;
	uint32 framebuffer_pitch;
	uint32 framebuffer_depth;
	uint32 framebuffer_format;
	uint32 capabilities;

	area_id mmio_area;
	area_id framebuffer_area;
	bool initialized;
	bool framebuffer_fallback;
	bool display_active;
	bool cursor_visible;
};

struct rdna4_get_private_data {
	uint32 magic;
	area_id shared_info_area;
};

status_t rdna4_device_init(const pci_info& pci, rdna4_shared_info& shared);
void rdna4_device_uninit(rdna4_shared_info& shared);
const rdna4_pci_device* rdna4_lookup_device(uint16 deviceID);

#endif