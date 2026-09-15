#include "rdna4.h"

#include <KernelExport.h>
#include <PCI.h>
#include <OS.h>
#include <string.h>

static const rdna4_pci_device kDevices[] = {
	{0x7550, RDNA4_CHIP_GFX1201, "AMD Radeon RDNA4 GFX1201"},
	{0x7551, RDNA4_CHIP_GFX1201, "AMD Radeon RDNA4 GFX1201"},
	{0x7580, RDNA4_CHIP_GFX1201, "AMD Radeon RDNA4 GFX1201"},
	{0x7581, RDNA4_CHIP_GFX1201, "AMD Radeon RDNA4 GFX1201"},
	{0x7590, RDNA4_CHIP_GFX1200, "AMD Radeon RDNA4 GFX1200"},
	{0x7591, RDNA4_CHIP_GFX1200, "AMD Radeon RDNA4 GFX1200"},
	{0x75a1, RDNA4_CHIP_GFX1201, "AMD Radeon RDNA4 GFX1201"},
	{0x75b0, RDNA4_CHIP_GFX1201, "AMD Radeon RDNA4 GFX1201"},
	{0, RDNA4_CHIP_UNKNOWN, NULL}
};

const rdna4_pci_device*
rdna4_lookup_device(uint16 deviceID)
{
	for (const rdna4_pci_device* device = kDevices; device->device_id != 0;
		device++) {
		if (device->device_id == deviceID)
			return device;
	}
	return NULL;
}

status_t
rdna4_device_init(const pci_info& pci, rdna4_shared_info& shared)
{
	const rdna4_pci_device* device = rdna4_lookup_device(pci.device_id);
	if (device == NULL || pci.vendor_id != RDNA4_VENDOR_ID)
		return B_NOT_SUPPORTED;

	memset(&shared, 0, sizeof(shared));
	shared.version = 2;
	shared.size = sizeof(shared);
	shared.chip = device->chip;
	shared.pci_vendor_id = pci.vendor_id;
	shared.pci_device_id = pci.device_id;
	shared.bus = pci.bus;
	shared.device = pci.device;
	shared.function = pci.function;
	shared.revision = pci.revision;

	shared.mmio_physical = pci.u.h0.base_registers[0] & ~phys_addr_t(0xFULL);
	shared.mmio_size = pci.u.h0.base_register_sizes[0];
	shared.mmio_area = -1;
	shared.framebuffer_area = -1;
	shared.framebuffer_format = 0;
	shared.capabilities = 0;

	if (shared.mmio_physical == 0 || shared.mmio_size == 0)
		return B_BAD_VALUE;

	void* mmio = NULL;
	shared.mmio_area = map_physical_memory("amdgpu_rdna4 MMIO",
		shared.mmio_physical, shared.mmio_size, B_ANY_KERNEL_ADDRESS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA, &mmio);
	if (shared.mmio_area < 0)
		return shared.mmio_area;

	shared.initialized = true;
	shared.framebuffer_fallback = true;
	shared.display_active = false;
	shared.cursor_visible = true;
	return B_OK;
}

void
rdna4_device_uninit(rdna4_shared_info& shared)
{
	if (shared.mmio_area >= 0)
		delete_area(shared.mmio_area);
	if (shared.framebuffer_area >= 0)
		delete_area(shared.framebuffer_area);
	shared.initialized = false;
	shared.mmio_area = -1;
	shared.framebuffer_area = -1;
}
