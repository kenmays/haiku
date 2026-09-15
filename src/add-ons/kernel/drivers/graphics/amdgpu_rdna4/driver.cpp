#include "rdna4.h"

#include <KernelExport.h>
#include <PCI.h>
#include <Drivers.h>
#include <SupportDefs.h>
#include <OS.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static pci_module_info* sPCI;
static char* sDeviceNames[RDNA4_MAX_CARDS + 1];
static pci_info sPCIInfo[RDNA4_MAX_CARDS];
static rdna4_shared_info sSharedInfo[RDNA4_MAX_CARDS];
static area_id sSharedAreas[RDNA4_MAX_CARDS];
static int32 sDeviceCount;

static status_t device_open(const char* name, uint32 flags, void** cookie);
static status_t device_close(void* cookie);
static status_t device_free(void* cookie);
static status_t device_ioctl(void* cookie, uint32 op, void* buffer, size_t length);

static device_hooks sHooks = {
	device_open, device_close, device_free, device_ioctl,
	NULL, NULL, NULL, NULL, NULL, NULL
};

static void
reset_devices()
{
	for (int32 i = 0; i < RDNA4_MAX_CARDS; i++) {
		if (sSharedAreas[i] >= 0)
			delete_area(sSharedAreas[i]);
		sSharedAreas[i] = -1;
		rdna4_device_uninit(sSharedInfo[i]);
		free(sDeviceNames[i]);
		sDeviceNames[i] = NULL;
	}
	sDeviceCount = 0;
	sDeviceNames[0] = NULL;
}

static status_t
find_devices()
{
	int32 cookie = 0;
	sDeviceCount = 0;

	for (int32 i = 0; i < RDNA4_MAX_CARDS; i++)
		sSharedAreas[i] = -1;

	pci_info info;
	while (sDeviceCount < RDNA4_MAX_CARDS
		&& sPCI->get_nth_pci_info(cookie++, &info) == B_OK) {
		if (info.vendor_id != RDNA4_VENDOR_ID
			|| info.class_base != PCI_display
			|| info.class_sub != PCI_vga)
			continue;
		if (rdna4_lookup_device(info.device_id) == NULL)
			continue;

		int32 index = sDeviceCount;
		sPCIInfo[index] = info;
		status_t status = rdna4_device_init(info, sSharedInfo[index]);
		if (status != B_OK)
			continue;
		sSharedInfo[index].device_index = index;

		char name[64];
		snprintf(name, sizeof(name), "graphics/amdgpu_rdna4_%02x%02x%02x",
			info.bus, info.device, info.function);
		sDeviceNames[index] = strdup(name);
		if (sDeviceNames[index] == NULL)
			return B_NO_MEMORY;

		void* address = NULL;
		sSharedAreas[index] = create_area("amdgpu_rdna4_shared", &address,
			B_ANY_ADDRESS, B_PAGE_SIZE, B_FULL_LOCK,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
		if (sSharedAreas[index] < 0)
			return sSharedAreas[index];

		memcpy(address, &sSharedInfo[index], sizeof(rdna4_shared_info));
		sDeviceCount++;
	}

	sDeviceNames[sDeviceCount] = NULL;
	return sDeviceCount > 0 ? B_OK : B_ENTRY_NOT_FOUND;
}

extern "C" status_t
init_hardware(void)
{
	pci_module_info* pci = NULL;
	if (get_module(B_PCI_MODULE_NAME, (module_info**)&pci) != B_OK)
		return B_ERROR;

	status_t status = B_ENTRY_NOT_FOUND;
	int32 cookie = 0;
	pci_info info;
	while (pci->get_nth_pci_info(cookie++, &info) == B_OK) {
		if (info.vendor_id == RDNA4_VENDOR_ID
			&& info.class_base == PCI_display && info.class_sub == PCI_vga
			&& rdna4_lookup_device(info.device_id) != NULL) {
			status = B_OK;
			break;
		}
	}
	put_module(B_PCI_MODULE_NAME);
	return status;
}

extern "C" status_t
init_driver(void)
{
	for (int32 i = 0; i < RDNA4_MAX_CARDS; i++)
		sSharedAreas[i] = -1;

	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&sPCI);
	if (status != B_OK)
		return status;

	status = find_devices();
	if (status != B_OK) {
		reset_devices();
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
	}
	return status;
}

extern "C" void
uninit_driver(void)
{
	reset_devices();
	if (sPCI != NULL)
		put_module(B_PCI_MODULE_NAME);
	sPCI = NULL;
}

extern "C" const char**
publish_devices(void)
{
	return (const char**)sDeviceNames;
}

static int32
find_index(const char* name)
{
	if (name == NULL)
		return -1;
	for (int32 i = 0; i < sDeviceCount; i++)
		if (strcmp(name, sDeviceNames[i]) == 0)
			return i;
	return -1;
}

extern "C" device_hooks*
find_device(const char* name)
{
	return find_index(name) >= 0 ? &sHooks : NULL;
}

static status_t
device_open(const char* name, uint32 flags, void** cookie)
{
	(void)flags;
	int32 index = find_index(name);
	if (index < 0 || cookie == NULL)
		return B_BAD_VALUE;
	*cookie = (void*)(addr_t)index;
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
	int32 index = (int32)(addr_t)cookie;
	if (index < 0 || index >= sDeviceCount || buffer == NULL)
		return B_BAD_VALUE;

	if (op == B_GET_ACCELERANT_SIGNATURE) {
		const char signature[] = "amdgpu_rdna4.accelerant";
		if (length < sizeof(signature))
			return B_BUFFER_OVERFLOW;
		return user_memcpy(buffer, signature, sizeof(signature));
	}

	if (op == RDNA4_GET_PRIVATE_DATA) {
		if (length < sizeof(rdna4_get_private_data))
			return B_BUFFER_OVERFLOW;

		rdna4_get_private_data data;
		status_t status = user_memcpy(&data, buffer, sizeof(data));
		if (status != B_OK || data.magic != RDNA4_PRIVATE_DATA_MAGIC)
			return B_BAD_VALUE;

		data.shared_info_area = clone_area("amdgpu_rdna4_shared", NULL,
			B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, sSharedAreas[index]);
		if (data.shared_info_area < 0)
			return data.shared_info_area;

		return user_memcpy(buffer, &data, sizeof(data));
	}

	return B_NOT_SUPPORTED;
}
