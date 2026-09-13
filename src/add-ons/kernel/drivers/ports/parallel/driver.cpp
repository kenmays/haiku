/*
 * Legacy ISA/PCI parallel-port driver for Haiku.
 *
 * Provides the standard SPP data/status/control registers and a small
 * ioctl interface.  The driver deliberately does not claim IRQs yet;
 * higher-level IEEE-1284/ECP/EPP support can be layered on this ABI.
 *
 * Distributed under the terms of the MIT License.
 */

#include <Drivers.h>
#include <ISA.h>
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ioccom.h>

int32 api_version = B_CUR_DRIVER_API_VERSION;

#define DRIVER_NAME "parallel"
#define MAX_PORTS 8

#define LPT_DATA    0
#define LPT_STATUS  1
#define LPT_CONTROL 2

#define PARALLEL_GET_STATUS _IOR('P', 0, uint8)
#define PARALLEL_GET_CONTROL _IOR('P', 1, uint8)
#define PARALLEL_SET_CONTROL _IOW('P', 2, uint8)
#define PARALLEL_GET_BASE _IOR('P', 3, uint16)
#define PARALLEL_GET_MODE _IOR('P', 4, uint32)
#define PARALLEL_SET_MODE _IOW('P', 5, uint32)

#define PARALLEL_MODE_SPP 0
#define PARALLEL_MODE_BIDIR 1

struct parallel_device {
	uint16 base;
	uint32 mode;
	char name[64];
};

static isa_module_info* sISA;
static pci_module_info* sPCI;
static parallel_device sPorts[MAX_PORTS];
static int32 sPortCount;
static char* sNames[MAX_PORTS + 1];

static status_t
parallel_open(const char* name, uint32, void** _cookie)
{
	for (int32 i = 0; i < sPortCount; i++) {
		if (strcmp(name, sPorts[i].name) == 0) {
			*_cookie = &sPorts[i];
			return B_OK;
		}
	}
	return ENODEV;
}

static status_t
parallel_close(void*)
{
	return B_OK;
}

static status_t
parallel_free(void*)
{
	return B_OK;
}

static status_t
parallel_read(void* cookie, off_t, void* buffer, size_t* _length)
{
	if (_length == NULL || buffer == NULL || *_length == 0)
		return B_BAD_VALUE;

	parallel_device* device = (parallel_device*)cookie;
	uint8 value = sISA->read_io_8(device->base + LPT_DATA);
	*(uint8*)buffer = value;
	*_length = 1;
	return B_OK;
}

static status_t
parallel_write(void* cookie, off_t, const void* buffer, size_t* _length)
{
	if (_length == NULL || buffer == NULL || *_length == 0)
		return B_BAD_VALUE;

	parallel_device* device = (parallel_device*)cookie;
	sISA->write_io_8(device->base + LPT_DATA, *(const uint8*)buffer);
	*_length = 1;
	return B_OK;
}

static status_t
parallel_control(void* cookie, uint32 op, void* data, size_t length)
{
	parallel_device* device = (parallel_device*)cookie;
	if (device == NULL)
		return B_BAD_VALUE;

	switch (op) {
		case PARALLEL_GET_STATUS:
			if (data == NULL || length < sizeof(uint8)) return B_BAD_VALUE;
			*(uint8*)data = sISA->read_io_8(device->base + LPT_STATUS);
			return B_OK;

		case PARALLEL_GET_CONTROL:
			if (data == NULL || length < sizeof(uint8)) return B_BAD_VALUE;
			*(uint8*)data = sISA->read_io_8(device->base + LPT_CONTROL);
			return B_OK;

		case PARALLEL_SET_CONTROL:
			if (data == NULL || length < sizeof(uint8)) return B_BAD_VALUE;
			sISA->write_io_8(device->base + LPT_CONTROL, *(uint8*)data);
			return B_OK;

		case PARALLEL_GET_BASE:
			if (data == NULL || length < sizeof(uint16)) return B_BAD_VALUE;
			*(uint16*)data = device->base;
			return B_OK;

		case PARALLEL_GET_MODE:
			if (data == NULL || length < sizeof(uint32)) return B_BAD_VALUE;
			*(uint32*)data = device->mode;
			return B_OK;

		case PARALLEL_SET_MODE:
			if (data == NULL || length < sizeof(uint32)) return B_BAD_VALUE;
			if (*(uint32*)data != PARALLEL_MODE_SPP
				&& *(uint32*)data != PARALLEL_MODE_BIDIR)
				return B_BAD_VALUE;
			device->mode = *(uint32*)data;
			return B_OK;
	}

	return B_DEV_INVALID_IOCTL;
}

static device_hooks sHooks = {
	parallel_open,
	parallel_close,
	parallel_free,
	parallel_control,
	parallel_read,
	parallel_write,
	NULL,
	NULL,
	NULL,
	NULL
};

extern "C" status_t
init_hardware(void)
{
	if (get_module(B_ISA_MODULE_NAME, (module_info**)&sISA) != B_OK)
		return ENOSYS;

	return B_OK;
}

extern "C" status_t
init_driver(void)
{
	static const uint16 kLegacyBases[] = {0x378, 0x278, 0x3bc};

	if (sISA == NULL)
		return ENOSYS;

	sPortCount = 0;
	for (uint32 i = 0; i < sizeof(kLegacyBases) / sizeof(kLegacyBases[0]); i++) {
		if (sPortCount >= MAX_PORTS)
			break;

		parallel_device& device = sPorts[sPortCount];
		memset(&device, 0, sizeof(device));
		device.base = kLegacyBases[i];
		device.mode = PARALLEL_MODE_SPP;
		sprintf(device.name, "ports/parallel/%ld", sPortCount);
		sNames[sPortCount] = device.name;
		sPortCount++;
	}
	sNames[sPortCount] = NULL;

	/*
	 * Do not probe by writing the port.  Firmware/ISA bridges may decode an
	 * otherwise-unused address, so discovery remains conservative and the
	 * standard LPT addresses are exposed for systems that route them there.
	 */
	return sPortCount > 0 ? B_OK : ENODEV;
}

extern "C" void
uninit_driver(void)
{
	if (sISA != NULL) {
		put_module(B_ISA_MODULE_NAME);
		sISA = NULL;
	}
	sPortCount = 0;
}

extern "C" const char**
publish_devices(void)
{
	return (const char**)sNames;
}

extern "C" device_hooks*
find_device(const char* name)
{
	for (int32 i = 0; i < sPortCount; i++) {
		if (strcmp(name, sPorts[i].name) == 0)
			return &sHooks;
	}
	return NULL;
}
