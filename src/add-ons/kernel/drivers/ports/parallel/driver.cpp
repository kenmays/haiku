/*
 * Haiku legacy parallel-port driver.
 * MIT License.
 *
 * Provides conservative SPP/LPT access for legacy ISA systems.
 * Hardware probing is intentionally limited to the standard legacy
 * addresses; platforms without ISA I/O should use a platform bus driver.
 */

#include <Drivers.h>
#include <KernelExport.h>
#include <ISA.h>
#include <OS.h>
#include <errno.h>
#include <string.h>

#define DRIVER_NAME "parallel"
#define MAX_PORTS 3
#define DATA 0
#define STATUS 1
#define CONTROL 2
#define ECR 0x402
#define SPP_STATUS 0x80

static isa_module_info* sISA;
static char sNames[MAX_PORTS][64];
static const uint16 kPorts[MAX_PORTS] = { 0x378, 0x278, 0x3bc };
static int32 sCount;

struct cookie {
	int index;
	uint16 base;
	uint8 control;
};

int32 api_version = B_CUR_DRIVER_API_VERSION;

extern "C" status_t
init_hardware(void)
{
	if (get_module(B_ISA_MODULE_NAME, (module_info**)&sISA) != B_OK)
		return ENODEV;

	// Do not touch hardware during probing. Legacy ports may be absent,
	// decoded by another device, or implemented behind a bridge.
	return B_OK;
}

extern "C" status_t
init_driver(void)
{
	sCount = 0;
	for (int i = 0; i < MAX_PORTS; i++) {
		// A conservative driver publishes standard candidates. Opening a
		// port performs the actual ownership/readback check.
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
	for (int i = 0; i < sCount; i++)
		if (strcmp(name, sNames[i]) == 0)
			return i;
	return -1;
}

static status_t
parallel_open(const char* name, uint32, void** _cookie)
{
	int index = find_port(name);
	if (index < 0)
		return B_ENTRY_NOT_FOUND;

	cookie* c = new cookie;
	if (c == NULL)
		return B_NO_MEMORY;

	c->index = index;
	c->base = kPorts[index];
	c->control = 0;
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
	if (buffer == NULL || _numBytes == NULL || position != 0)
		return B_BAD_VALUE;

	size_t n = *_numBytes;
	if (n == 0)
		return B_OK;

	uint8* out = (uint8*)buffer;
	for (size_t i = 0; i < n; i++)
		out[i] = sISA->read_io_8(c->base + DATA);
	*_numBytes = n;
	return B_OK;
}

static status_t
parallel_write(void* _cookie, off_t position, const void* buffer, size_t* _numBytes)
{
	cookie* c = (cookie*)_cookie;
	if (buffer == NULL || _numBytes == NULL || position != 0)
		return B_BAD_VALUE;

	size_t n = *_numBytes;
	const uint8* in = (const uint8*)buffer;
	for (size_t i = 0; i < n; i++)
		sISA->write_io_8(c->base + DATA, in[i]);
	*_numBytes = n;
	return B_OK;
}

static status_t
parallel_control(void* _cookie, uint32 op, void* data, size_t len)
{
	cookie* c = (cookie*)_cookie;
	if (data == NULL)
		return B_BAD_VALUE;

	// Private interface: low byte selects register, high byte supplies value.
	// 0=status read, 1=control read, 2=control write.
	switch (op) {
		case 0:
			if (len < 1) return B_BAD_VALUE;
			*(uint8*)data = sISA->read_io_8(c->base + STATUS);
			return B_OK;
		case 1:
			if (len < 1) return B_BAD_VALUE;
			*(uint8*)data = sISA->read_io_8(c->base + CONTROL);
			return B_OK;
		case 2:
			if (len < 1) return B_BAD_VALUE;
			c->control = *(uint8*)data;
			sISA->write_io_8(c->base + CONTROL, c->control);
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
