/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * AMDGPU native Haiku driver foundation. Per-function contexts and the
 * device-node hook contract are prepared, but nodes are published only after
 * a future ASIC-specific initialization path explicitly marks them ready.
 */
#include <Drivers.h>
#include <KernelExport.h>
#include <PCI.h>
#include <SupportDefs.h>
#include <lock.h>
#include <string.h>

#include "amdgpu_device_context.h"

int32 api_version = B_CUR_DRIVER_API_VERSION;

static pci_module_info* sPCI = NULL;
static amdgpu_device_context sDevices[AMDGPU_MAX_DEVICES];
static uint32 sDeviceCount = 0;
static const char* sDeviceNames[AMDGPU_MAX_DEVICES + 1];
static mutex sRegistryLock = MUTEX_INITIALIZER("amdgpu device registry");


static void
ReleaseInitializedDevices()
{
	while (sDeviceCount > 0) {
		uint32 index = sDeviceCount - 1;
		status_t status = amdgpu_device_context_release(sPCI,
			&sDevices[index]);
		if (status != B_OK) {
			dprintf("amdgpu: failed to unwind device context %" B_PRIu32
				": %" B_PRId32 "\n", index, status);
			return;
		}
		sDeviceCount--;
	}
}


static status_t
device_open(const char* name, uint32, void** cookie)
{
	if (name == NULL || cookie == NULL)
		return B_BAD_VALUE;
	*cookie = NULL;

	status_t status = mutex_lock(&sRegistryLock);
	if (status != B_OK)
		return status;
	for (uint32 i = 0; i < sDeviceCount; i++) {
		amdgpu_device_context& device = sDevices[i];
		if (strcmp(name, device.node_name) == 0) {
			status = amdgpu_device_context_open(&device, cookie);
			mutex_unlock(&sRegistryLock);
			return status;
		}
	}
	mutex_unlock(&sRegistryLock);
	return B_ENTRY_NOT_FOUND;
}


static status_t
device_close(void* cookie)
{
	return amdgpu_device_context_close(cookie);
}


static status_t
device_free(void* cookie)
{
	return amdgpu_device_context_free(cookie);
}


static status_t
device_control(void*, uint32, void*, size_t)
{
	return B_NOT_SUPPORTED;
}


static status_t
device_read(void*, off_t, void*, size_t* length)
{
	if (length != NULL)
		*length = 0;
	return B_NOT_SUPPORTED;
}


static status_t
device_write(void*, off_t, const void*, size_t* length)
{
	if (length != NULL)
		*length = 0;
	return B_NOT_SUPPORTED;
}


static device_hooks sDeviceHooks = {
	device_open,
	device_close,
	device_free,
	device_control,
	device_read,
	device_write,
	NULL,
	NULL,
	NULL,
	NULL
};


extern "C" status_t
init_hardware(void)
{
	pci_module_info* pci = NULL;
	status_t status = get_module(B_PCI_MODULE_NAME, (module_info**)&pci);
	if (status != B_OK)
		return status;

	uint32 count = 0;
	status = amdgpu_enumerate(pci, NULL, 0, &count);
	put_module(B_PCI_MODULE_NAME);
	return status;
}


extern "C" status_t
init_driver(void)
{
	status_t status = mutex_lock(&sRegistryLock);
	if (status != B_OK)
		return status;
	if (sPCI != NULL || sDeviceCount != 0) {
		mutex_unlock(&sRegistryLock);
		return B_BUSY;
	}

	status = get_module(B_PCI_MODULE_NAME, (module_info**)&sPCI);
	if (status != B_OK) {
		sPCI = NULL;
		mutex_unlock(&sRegistryLock);
		return status;
	}

	amdgpu_pci_device candidates[AMDGPU_MAX_DEVICES];
	uint32 candidateCount = 0;
	status = amdgpu_enumerate(sPCI, candidates, AMDGPU_MAX_DEVICES,
		&candidateCount);
	if (status != B_OK) {
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
		mutex_unlock(&sRegistryLock);
		return status;
	}

	memset(sDevices, 0, sizeof(sDevices));
	memset(sDeviceNames, 0, sizeof(sDeviceNames));
	sDeviceCount = 0;
	uint32 exactIDs = 0;
	uint32 ipDiscovery = 0;
	for (uint32 i = 0; i < candidateCount; i++) {
		status = amdgpu_device_context_init(&sDevices[sDeviceCount],
			&candidates[i]);
		if (status != B_OK) {
			dprintf("amdgpu: context initialization failed for candidate "
				"%" B_PRIu32 ": %" B_PRId32 "\n", i, status);
			ReleaseInitializedDevices();
			if (sDeviceCount == 0) {
				put_module(B_PCI_MODULE_NAME);
				sPCI = NULL;
			}
			mutex_unlock(&sRegistryLock);
			return status;
		}

		sDeviceCount++;
		if ((candidates[i].identification_flags & AMDGPU_PCI_ID_EXACT) != 0)
			exactIDs++;
		if ((candidates[i].identification_flags
			& AMDGPU_PCI_ID_IP_DISCOVERY) != 0)
			ipDiscovery++;
	}

	dprintf("amdgpu: retained %" B_PRIu32 " AMD GPU function(s), "
		"%" B_PRIu32 " exact PCI IDs, %" B_PRIu32
		" IP-discovery class matches; contexts initialized, "
		"no hardware claimed and no nodes published.\n",
		sDeviceCount, exactIDs, ipDiscovery);
	mutex_unlock(&sRegistryLock);
	return B_OK;
}


extern "C" void
uninit_driver(void)
{
	status_t status = mutex_lock(&sRegistryLock);
	if (status != B_OK)
		return;

	if (sPCI != NULL) {
		ReleaseInitializedDevices();
		if (sDeviceCount != 0) {
			dprintf("amdgpu: refusing to discard live device contexts\n");
			mutex_unlock(&sRegistryLock);
			return;
		}
		put_module(B_PCI_MODULE_NAME);
		sPCI = NULL;
	}
	memset(sDevices, 0, sizeof(sDevices));
	memset(sDeviceNames, 0, sizeof(sDeviceNames));
	mutex_unlock(&sRegistryLock);
}


extern "C" const char**
publish_devices(void)
{
	status_t status = mutex_lock(&sRegistryLock);
	if (status != B_OK)
		return NULL;

	uint32 count = 0;
	for (uint32 i = 0; i < sDeviceCount; i++) {
		if (amdgpu_device_context_is_node_ready(&sDevices[i]))
			sDeviceNames[count++] = sDevices[i].node_name;
	}
	sDeviceNames[count] = NULL;
	mutex_unlock(&sRegistryLock);
	return count != 0 ? sDeviceNames : NULL;
}


extern "C" device_hooks*
find_device(const char* name)
{
	if (name == NULL)
		return NULL;
	status_t status = mutex_lock(&sRegistryLock);
	if (status != B_OK)
		return NULL;

	device_hooks* hooks = NULL;
	for (uint32 i = 0; i < sDeviceCount; i++) {
		if (strcmp(name, sDevices[i].node_name) == 0
			&& amdgpu_device_context_is_node_ready(&sDevices[i])) {
			hooks = &sDeviceHooks;
			break;
		}
	}
	mutex_unlock(&sRegistryLock);
	return hooks;
}
