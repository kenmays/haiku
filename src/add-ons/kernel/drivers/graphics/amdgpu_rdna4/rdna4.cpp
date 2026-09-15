#include "rdna4.h"

#include <KernelExport.h>
#include <PCI.h>
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
	shared.version = 1;
	shared.chip = device->chip;
	shared.pci_vendor_id = pci.vendor_id;
	shared.pci_device_id = pci.device_id;
	shared.bus = pci.bus;
	shared.device = pci.device;
	shared.function = pci.function;

	/*
	 * This first-stage driver deliberately does not write GFX12/DCN4
	 * registers.  Firmware ownership and the boot display remain intact.
	 * Exact BAR selection and resource mapping are performed by the
	 * platform driver once the Haiku PCI API exposes the required BAR data.
	 */
	shared.initialized = true;
	shared.framebuffer_fallback = true;
	return B_OK;
}

void
rdna4_device_uninit(rdna4_shared_info& shared)
{
	shared.initialized = false;
}
