/*
 * Haiku legacy parallel-port driver.
 * Distributed under the terms of the MIT License.
 */

#include <Drivers.h>
#include <ISA.h>
#include <KernelExport.h>
#include <OS.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#define DRIVER_NAME "parallel"
#define MAX_PORTS 3

#define LPT_DATA 0
#define LPT_STATUS 1
#define LPT_CONTROL 2

static isa_module_info* sISA;
static char sNames[MAX_PORTS][64];
static const uint16 kPorts[MAX_PORTS] = { 0x378, 0x278, 0x3bc };
static int32 sCount;

struct cookie {
	uint16 base;
};

int32 api_version = B_CUR_DRIVER_API_VERSION;

extern "C" status_t
init_hardware(void)
{
	if (get_module(B_ISA_MODULE_NAME, (module_info**)&sISA) != B_OK)
		return ENODEV;

	// Do not perform destructive legacy-port probing here. On systems with
	// no ISA bus the ISA module should reject initialization.
	return B_OK;
}

extern "C" status_t
init_driver(void)
{
	sCount = 0;
	for (int i = 0; i < MAX_PORTS; i++) {
		sprintf(sNames[sCount], "ports/parallel/lpt%d", i + 1);
		sCount++;
	}
	return B_OK;
}

extern "C" void
uninit_driver(void)
{
	if (sISA != NULL) {
		put_module(B_ISA_MODULE_NAME);
		sISA = NULL;
	}
	sCount = 0;
}

extern "C" const char**
publish_devices(void)
{
	static const char* names[MAX_PORTS + 1];
	for (int i = 0; i < sCount; i++)
		names[i] = sNames[i];
	names[sCount] = NULL;
	return names;
}

static int
find_port(const char* name)
{
	for (int i = 0; i < sCount; i++) {
		if (strcmp(name, sNames[i]) == 0)
			return i;
	}
	return -1;
}

static status_t
parallel_open(const char* name, uint32, void** _cookie)
{
	if (_cookie == NULL)
		return B_BAD_VALUE;

	int index = find_port(name);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	cookie* c = new cookie;
	if (c == NULL)
		return B_NO_MEMORY;

	c->base = kPorts[index];
	*_cookie = c;
	return B_OK;
}

static status_t
parallel_close(void*)
{
	return B_OK;
}

static status_t
parallel_free(void* _cookie)
{
	delete (cookie*)_cookie;
	return B_OK;
}

static status_t
parallel_read(void* _cookie, off_t position, void* buffer, size_t* _numBytes)
{
	cookie* c = (cookie*)_cookie;
	if (c == NULL || buffer == NULL || _numBytes == NULL || position != 0)
		return B_BAD_VALUE;

	size_t count = *_numBytes;
	uint8* out = (uint8*)buffer;
	for (size_t i = 0; i < count; i++)
		out[i] = sISA->read_io_8(c->base + LPT_DATA);
	*_numBytes = count;
	return B_OK;
}

static status_t
parallel_write(void* _cookie, off_t position, const void* buffer, size_t* _numBytes)
{
	cookie* c = (cookie*)_cookie;
	if (c == NULL || buffer == NULL || _numBytes == NULL || position != 0)
		return B_BAD_VALUE;

	size_t count = *_numBytes;
	const uint8* in = (const uint8*)buffer;
	for (size_t i = 0; i < count; i++)
		sISA->write_io_8(c->base + LPT_DATA, in[i]);
	*_numBytes = count;
	return B_OK;
}

static status_t
parallel_control(void* _cookie, uint32 op, void* data, size_t len)
{
	cookie* c = (cookie*)_cookie;
	if (c == NULL || data == NULL || len < 1)
		return B_BAD_VALUE;

	// Driver-private register access interface:
	// 0 = read status, 1 = read control, 2 = write control.
	switch (op) {
		case 0:
			*(uint8*)data = sISA->read_io_8(c->base + LPT_STATUS);
			return B_OK;
		case 1:
			*(uint8*)data = sISA->read_io_8(c->base + LPT_CONTROL);
			return B_OK;
		case 2:
			sISA->write_io_8(c->base + LPT_CONTROL, *(uint8*)data);
			return B_OK;
		default:
			return B_BAD_VALUE;
	}
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

extern "C" device_hooks*
find_device(const char* name)
{
	return find_port(name) >= 0 ? &sHooks : NULL;
}
