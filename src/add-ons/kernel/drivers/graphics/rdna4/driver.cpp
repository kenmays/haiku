#include "driver.h"

#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_CARDS RDNA4_MAX_CARDS

struct rdna4_pci_id {
	uint16 id;
	uint32 gfx;
	const char* name;
};

static const rdna4_pci_id kDevices[] = {
	{ RDNA4_DEVICE_NAVI44, RDNA4_GFX12_0, "AMD Radeon RX 9060 / Navi 44" },
	{ RDNA4_DEVICE_NAVI48_ALT, RDNA4_GFX12_1,
		"AMD Radeon AI PRO R9700 / Navi 48" },
	{ RDNA4_DEVICE_NAVI44_MOBILE, RDNA4_GFX12_0,
		"AMD Radeon RDNA4 mobile" }
};

pci_module_info* gPCI = NULL;
mutex gDriverLock;
rdna4_device* gDevices[MAX_CARDS] = {};
char* gDeviceNames[MAX_CARDS + 1] = {};

static status_t
find_gpu(int32* cookie, pci_info& info, const rdna4_pci_id*& id)
{
	for (int32 i = *cookie;
		gPCI->get_nth_pci_info(i, &info) == B_OK; i++) {
		if (info.vendor_id != RDNA4_VENDOR_ID
			|| info.class_base != PCI_display)
			continue;

		for (size_t n = 0; n < sizeof(kDevices) / sizeof(kDevices[0]); n++) {
			if (info.device_id == kDevices[n].id) {
				*cookie = i + 1;
				id = &kDevices[n];
				return B_OK;
			}
		}
	}
	return B_ENTRY_NOT_FOUND;
}

extern "C" status_t
init_hardware(void)
{
	status_t status = get_module(B_PCI_MODULE_NAME,
		(module_info**)&gPCI);
	if (status != B_OK)
		return status;

	int32 cookie = 0;
	pci_info info;
	const rdna4_pci_id* id;
	status = find_gpu(&cookie, info, id);
	put_module(B_PCI_MODULE_NAME);
	return status;
}

extern "C" status_t
init_driver(void)
{
	status_t status = get_module(B_PCI_MODULE_NAME,
		(module_info**)&gPCI);
	if (status != B_OK)
		return status;

	mutex_init(&gDriverLock, "rdna4 driver");

	int32 cookie = 0;
	int32 found = 0;
	while (found < MAX_CARDS) {
		pci_info* pci = (pci_info*)malloc(sizeof(pci_info));
		if (pci == NULL)
			break;

		const rdna4_pci_id* id;
		if (find_gpu(&cookie, *pci, id) != B_OK) {
			free(pci);
			break;
		}

		status = gPCI->reserve_device(pci->bus, pci->device, pci->function,
			"rdna4", NULL);
		if (status != B_OK) {
			free(pci);
			continue;
		}

		rdna4_device* device = (rdna4_device*)calloc(1,
			sizeof(rdna4_device));
		if (device == NULL) {
			gPCI->unreserve_device(pci->bus, pci->device, pci->function,
				"rdna4", NULL);
			free(pci);
			break;
		}

		device->id = found;
		device->pci = pci;
		device->device_id = pci->device_id;
		device->gfx_ip = id->gfx;
		device->revision = pci->revision;
		device->mmio_area = -1;
		device->framebuffer_area = -1;
		device->shared_area = -1;
		device->init_status = B_NO_INIT;
		mutex_init(&device->lock, "rdna4 device");

		char name[64];
		snprintf(name, sizeof(name),
			"graphics/rdna4_%02x%02x%02x",
			pci->bus, pci->device, pci->function);
		gDeviceNames[found] = strdup(name);
		if (gDeviceNames[found] == NULL) {
			mutex_destroy(&device->lock);
			gPCI->unreserve_device(pci->bus, pci->device, pci->function,
				"rdna4", NULL);
			free(device);
			free(pci);
			break;
		}

		gDevices[found] = device;
		found++;
	}

	gDeviceNames[found] = NULL;
	if (found == 0) {
		mutex_destroy(&gDriverLock);
		put_module(B_PCI_MODULE_NAME);
		return B_ENTRY_NOT_FOUND;
	}
	return B_OK;
}

extern "C" void
uninit_driver(void)
{
	for (int32 i = 0; i < MAX_CARDS && gDeviceNames[i] != NULL; i++) {
		rdna4_device* device = gDevices[i];
		if (device == NULL)
			continue;

		mutex_lock(&device->lock);
		if (device->open_count != 0) {
			mutex_unlock(&device->lock);
			continue;
		}
		mutex_unlock(&device->lock);

		gPCI->unreserve_device(device->pci->bus, device->pci->device,
			device->pci->function, "rdna4", NULL);
		mutex_destroy(&device->lock);
		free(device->pci);
		free(device);
		free(gDeviceNames[i]);
		gDevices[i] = NULL;
		gDeviceNames[i] = NULL;
	}

	mutex_destroy(&gDriverLock);
	put_module(B_PCI_MODULE_NAME);
}

extern "C" const char**
publish_devices(void)
{
	return (const char**)gDeviceNames;
}

extern "C" device_hooks*
find_device(const char* name)
{
	for (int32 i = 0; gDeviceNames[i] != NULL; i++) {
		if (strcmp(name, gDeviceNames[i]) == 0)
			return &gDeviceHooks;
	}
	return NULL;
}
