#include "rdna4.h"

#include <KernelExport.h>
#include <PCI.h>
#include <Drivers.h>
#include <SupportDefs.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define MAX_CARDS 4

static pci_module_info* sPCI;
static char* sDeviceNames[MAX_CARDS + 1];
static pci_info sPCIInfo[MAX_CARDS];
static rdna4_shared_info sSharedInfo[MAX_CARDS];
static int32 sDeviceCount;

static status_t device_open(const char* name, uint32 flags, void** cookie);
static status_t device_close(void* cookie);
static status_t device_free(void* cookie);
static status_t device_ioctl(void* cookie, uint32 op, void* buffer, size_t length);

static device_hooks sHooks = {
	device_open, device_close, device_free, device_ioctl,
	NULL, NULL, NULL, NULL, NULL, NULL
};

static status_t
find_devices()
{
	int32 cookie = 0;
	sDeviceCount = 0;
	pci_info info;

	while (sDeviceCount < MAX_CARDS
		&& sPCI->get_nth_pci_info(cookie++, &info) == B_OK) {
		if (info.vendor_id != RDNA4_VENDOR_ID
			|| info.class_base != PCI_display
			|| info.class_sub != PCI_vga)
			continue;
		if (rdna4_lookup_device(info.device_id) == NULL)
			continue;

		sPCIInfo[sDeviceCount] = info;
		if (rdna4_device_init(info, sSharedInfo[sDeviceCount]) != B_OK)
			continue;

		char name[64];
		snprintf(name, sizeof(name), "graphics/amdgpu_rdna4_%02x%02x%02x",
			info.bus, info.device, info.function);
		sDeviceNames[sDeviceCount] = strdup(name);
		if (sDeviceNames[sDeviceCount] == NULL)
			return B_NO_MEMORY;
		sDeviceCount++;
	}

	sDeviceNames[sDeviceCount] = NULL;
	return sDeviceCount > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}

extern "C" status_t
init_hardware(void)
{
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&sPCI) != B_OK)
		return B_ERROR;
	status_t status = find_devices();
	put_module(B_PCI_MODULE_NAME);
	return status;
}

extern "C" status_t
init_driver(void)
{
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&sPCI);
	if (status != B_OK)
		return status;
	status = find_devices();
	if (status != B_OK) {
		put_module(B_PCI_MODULE_NAME);
		return status;
	}
	return B_OK;
}

extern "C" void
uninit_driver(void)
{
	for (int32 i = 0; i < sDeviceCount; i++) {
		rdna4_device_uninit(sSharedInfo[i]);
		free(sDeviceNames[i]);
		sDeviceNames[i] = NULL;
	}
	sDeviceCount = 0;
	if (sPCI != NULL)
		put_module(B_PCI_MODULE_NAME);
	sPCI = NULL;
}

extern "C" const char**
publish_devices(void)
{
	return (const char**)sDeviceNames;
}

extern "C" device_hooks*
find_device(const char* name)
{
	for (int32 i = 0; i < sDeviceCount; i++) {
		if (strcmp(name, sDeviceNames[i]) == 0)
			return &sHooks;
	}
	return NULL;
}

static int32
find_index(const char* name)
{
	for (int32 i = 0; i < sDeviceCount; i++)
		if (strcmp(name, sDeviceNames[i]) == 0)
			return i;
	return -1;
}

static status_t
device_open(const char* name, uint32 flags, void** cookie)
{
	(void)flags;
	int32 index = find_index(name);
	if (index < 0 || cookie == NULL)
		return B_BAD_VALUE;
	*cookie = &sSharedInfo[index];
	return B_OK;
}

static status_t
device_close(void* cookie)
{
	(void)cookie;
	return B_OK;
}

static status_t
device_free(void* cookie)
{
	(void)cookie;
	return B_OK;
}

static status_t
device_ioctl(void* cookie, uint32 op, void* buffer, size_t length)
{
	(void)cookie;
	(void)op;
	(void)buffer;
	(void)length;
	return B_NOT_SUPPORTED;
}
