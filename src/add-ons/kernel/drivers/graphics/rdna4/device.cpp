#include "driver.h"

#include <Drivers.h>
#include <KernelExport.h>
#include <SupportDefs.h>
#include <string.h>

static status_t open_device(const char* name, uint32, void** _cookie);
static status_t close_device(void*);
static status_t free_device(void*);
static status_t ioctl_device(void*, uint32, void*, size_t);
static status_t read_device(void*, off_t, void*, size_t*);
static status_t write_device(void*, off_t, const void*, size_t*);

device_hooks gDeviceHooks = {
	open_device, close_device, free_device, ioctl_device,
	read_device, write_device, NULL, NULL, NULL, NULL
};

static status_t
open_device(const char* name, uint32, void** _cookie)
{
	for (int32 i = 0; gDeviceNames[i] != NULL; i++) {
		if (strcmp(name, gDeviceNames[i]) != 0)
			continue;

		rdna4_device* device = gDevices[i];
		if (device == NULL)
			return B_ERROR;

		mutex_lock(&device->lock);
		if (device->open_count == 0)
			device->init_status = rdna4_init(*device);

		if (device->init_status == B_OK) {
			device->open_count++;
			*_cookie = device;
		}
		mutex_unlock(&device->lock);
		return device->init_status;
	}

	return B_ENTRY_NOT_FOUND;
}

static status_t
close_device(void*)
{
	return B_OK;
}

static status_t
free_device(void* cookie)
{
	rdna4_device* device = (rdna4_device*)cookie;
	mutex_lock(&device->lock);

	if (device->open_count > 0 && --device->open_count == 0) {
		rdna4_uninit(*device);
		device->init_status = B_NO_INIT;
	}

	mutex_unlock(&device->lock);
	return B_OK;
}

static status_t
ioctl_device(void* cookie, uint32 op, void* buffer, size_t length)
{
	rdna4_device* device = (rdna4_device*)cookie;
	mutex_lock(&device->lock);
	status_t status = rdna4_ioctl(*device, op, buffer, length);
	mutex_unlock(&device->lock);
	return status;
}

static status_t
read_device(void*, off_t, void*, size_t* length)
{
	*length = 0;
	return B_NOT_ALLOWED;
}

static status_t
write_device(void*, off_t, const void*, size_t* length)
{
	*length = 0;
	return B_NOT_ALLOWED;
}
