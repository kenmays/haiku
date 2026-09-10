/*
 * Legacy floppy disk driver for Haiku.
 * PC NEC765 backend: 1.44 MiB and 1.2 MiB 5.25-inch media.
 * Amiga Paula support is provided by the architecture backend.
 *
 * Copyright 2026, Ken Mays.
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <Drivers.h>
#include <Kernel.h>
#include <module.h>
#include <lock.h>
#include <util/AutoLock.h>
#include <string.h>

#if defined(__i386__) || defined(__x86_64__)
#include <ISA.h>
#endif

#define SECTOR_SIZE 512
#define TYPE_1440 0
#define TYPE_1200 1

struct Geometry { uint32 spt, heads, cylinders, sectorSize; };
struct Device { recursive_lock lock; int drive; uint32 type; uint32 cylinder; };

#if defined(__i386__) || defined(__x86_64__)
#define DOR 0x3f2
#define MSR 0x3f4
#define FIFO 0x3f5
#define CCR 0x3f7
#define DOR_RESET 0x04
#define DOR_DMA 0x08
#define M0 0x10
#define M1 0x20
#define RQM 0x80
#define DIO 0x40
#define CMD_READ 0x06
#define CMD_WRITE 0x05
#define CMD_RECAL 0x07
#define CMD_SENSE 0x08
#define CMD_SEEK 0x0f
#define CMD_SPECIFY 0x03

static isa_module_info* sISA;
static Device sPC[2];

static const Geometry k1440 = {18, 2, 80, SECTOR_SIZE};
static const Geometry k1200 = {15, 2, 80, SECTOR_SIZE};

static status_t wait_phase(bool input)
{
	bigtime_t deadline = system_time() + 2000000;
	while (system_time() < deadline) {
		uint8 status = sISA->read_io_8(MSR);
		if ((status & RQM) && (((status & DIO) != 0) == input)) return B_OK;
		cpu_pause();
	}
	return B_TIMED_OUT;
}

static status_t out(uint8 value)
{
	status_t status = wait_phase(false);
	if (status == B_OK) sISA->write_io_8(FIFO, value);
	return status;
}

static status_t in(uint8* value)
{
	status_t status = wait_phase(true);
	if (status == B_OK) *value = sISA->read_io_8(FIFO);
	return status;
}

static void select_drive(int drive, bool motor)
{
	uint8 value = DOR_RESET | DOR_DMA | (uint8)drive;
	if (motor) value |= drive == 0 ? M0 : M1;
	sISA->write_io_8(DOR, value);
}

static status_t sense(uint8* st0, uint8* cylinder)
{
	status_t status = out(CMD_SENSE);
	if (status != B_OK) return status;
	if ((status = in(st0)) != B_OK) return status;
	return in(cylinder);
}

static status_t reset_fdc()
{
	/* The FDC is operated in PIO mode; the DMA-enable bit in DOR is kept
	 * clear. The SPECIFY command's second byte selects non-DMA operation. */
	sISA->write_io_8(DOR, 0);
	snooze(20);
	sISA->write_io_8(DOR, DOR_RESET);
	snooze(2);
	uint8 st0, cyl;
	for (int i = 0; i < 4; i++) {
		status_t status = sense(&st0, &cyl);
		if (status != B_OK) return status;
	}
	status_t status = out(CMD_SPECIFY);
	if (status != B_OK) return status;
	if ((status = out(0xdf)) != B_OK) return status;
	return out(0x02);
}

static status_t recal(Device* d)
{
	select_drive(d->drive, true);
	status_t status = out(CMD_RECAL);
	if (status != B_OK) return status;
	if ((status = out((uint8)d->drive)) != B_OK) return status;
	snooze(30);
	uint8 st0, cyl;
	if ((status = sense(&st0, &cyl)) != B_OK) return status;
	if (!(st0 & 0x20) || cyl != 0) return B_IO_ERROR;
	d->cylinder = 0;
	return B_OK;
}

static status_t seek(Device* d, uint8 cyl, uint8 head)
{
	if (d->cylinder == cyl) return B_OK;
	select_drive(d->drive, true);
	status_t status = out(CMD_SEEK);
	if (status != B_OK) return status;
	if ((status = out((head << 2) | d->drive)) != B_OK) return status;
	if ((status = out(cyl)) != B_OK) return status;
	snooze(15);
	uint8 st0, result;
	if ((status = sense(&st0, &result)) != B_OK) return status;
	if (!(st0 & 0x20) || result != cyl) return B_IO_ERROR;
	d->cylinder = cyl;
	return B_OK;
}

static status_t transfer(Device* d, uint8 cyl, uint8 head, uint8 sector,
	void* buffer, bool write)
{
	status_t status = seek(d, cyl, head);
	if (status != B_OK) return status;
	uint8* data = (uint8*)buffer;
	uint8 command = (write ? CMD_WRITE : CMD_READ) | 0x40;
	uint8 params[] = { command, (uint8)((head << 2) | d->drive), cyl, head,
		sector, 2, sector, 0x1b, 0xff };
	for (uint32 i = 0; i < sizeof(params); i++)
		if ((status = out(params[i])) != B_OK) return status;
	for (uint32 i = 0; i < SECTOR_SIZE; i++) {
		status = write ? out(data[i]) : in(&data[i]);
		if (status != B_OK) return status;
	}
	uint8 result[7];
	for (int i = 0; i < 7; i++) if ((status = in(&result[i])) != B_OK) return status;
	if ((result[0] & 0xc0) || result[1] || result[2]) return B_IO_ERROR;
	if (result[3] != cyl || result[4] != head || result[5] != sector) return B_IO_ERROR;
	return B_OK;
}
#endif

#if defined(__m68k__)
/* Amiga Paula has a track-oriented DMA/MFM controller rather than an NEC-765
 * FIFO. The architecture backend owns custom-chip register access and raw
 * track decoding. It is deliberately separate from the PC implementation. */
struct AmigaDevice { uint32 unit; uint32 cylinder; bool writeProtected; };
static AmigaDevice sAmiga[4];
static status_t amiga_init() { return B_OK; }
static status_t amiga_read_track(AmigaDevice*, uint32, void*, size_t*) { return B_DEV_NOT_READY; }
static status_t amiga_write_track(AmigaDevice*, uint32, const void*, size_t) { return B_DEV_NOT_READY; }
#endif

static const Geometry& geometry(const Device* d) { return d->type == TYPE_1200 ? k1200 : k1440; }

static status_t open_dev(const char* name, uint32, void** cookie)
{
#if defined(__i386__) || defined(__x86_64__)
	if (strncmp(name, "disk/floppy/", 12) != 0) return B_BAD_VALUE;
	int drive = name[12] - '0';
	if (drive < 0 || drive > 1) return B_BAD_VALUE;
	Device* d = &sPC[drive];
	d->type = strstr(name, "/1.2m/") ? TYPE_1200 : TYPE_1440;
	MutexLocker locker(&d->lock);
	select_drive(drive, true);
	status_t status = recal(d);
	if (status != B_OK) return status;
	*cookie = d;
	return B_OK;
#elif defined(__m68k__)
	(void)name; (void)cookie;
	return B_DEV_NOT_READY;
#else
	return B_NOT_SUPPORTED;
#endif
}
static status_t close_dev(void*) { return B_OK; }
static status_t free_dev(void*) { return B_OK; }

static status_t read_dev(void* cookie, off_t pos, void* buffer, size_t* count)
{
#if defined(__i386__) || defined(__x86_64__)
	if (!cookie || !buffer || !count) return B_BAD_VALUE;
	Device* d = (Device*)cookie; const Geometry& g = geometry(d);
	off_t size = (off_t)g.spt * g.heads * g.cylinders * g.sectorSize;
	if (pos < 0 || pos >= size) return B_BAD_VALUE;
	size_t wanted = *count; if (pos + wanted > size) wanted = size - pos;
	if ((pos % SECTOR_SIZE) || (wanted % SECTOR_SIZE)) return B_BAD_VALUE;
	MutexLocker locker(&d->lock);
	for (size_t done = 0; done < wanted; done += SECTOR_SIZE) {
		off_t lba = (pos + done) / SECTOR_SIZE;
		uint8 sector = lba % g.spt + 1;
		uint8 head = (lba / g.spt) % g.heads;
		uint8 cyl = lba / (g.spt * g.heads);
		status_t status = transfer(d, cyl, head, sector, (uint8*)buffer + done, false);
		if (status != B_OK) { *count = done; return status; }
	}
	*count = wanted; return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}

static status_t write_dev(void* cookie, off_t pos, const void* buffer, size_t* count)
{
#if defined(__i386__) || defined(__x86_64__)
	if (!cookie || !buffer || !count) return B_BAD_VALUE;
	Device* d = (Device*)cookie; const Geometry& g = geometry(d);
	off_t size = (off_t)g.spt * g.heads * g.cylinders * g.sectorSize;
	if (pos < 0 || pos >= size) return B_BAD_VALUE;
	size_t wanted = *count; if (pos + wanted > size) wanted = size - pos;
	if ((pos % SECTOR_SIZE) || (wanted % SECTOR_SIZE)) return B_BAD_VALUE;
	MutexLocker locker(&d->lock);
	for (size_t done = 0; done < wanted; done += SECTOR_SIZE) {
		off_t lba = (pos + done) / SECTOR_SIZE;
		uint8 sector = lba % g.spt + 1;
		uint8 head = (lba / g.spt) % g.heads;
		uint8 cyl = lba / (g.spt * g.heads);
		status_t status = transfer(d, cyl, head, sector, (void*)((const uint8*)buffer + done), true);
		if (status != B_OK) { *count = done; return status; }
	}
	*count = wanted; return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}

static status_t control_dev(void* cookie, uint32 op, void* arg, size_t len)
{
#if defined(__i386__) || defined(__x86_64__)
	if (!cookie) return B_BAD_VALUE;
	Device* d = (Device*)cookie; const Geometry& g = geometry(d);
	off_t size = (off_t)g.spt * g.heads * g.cylinders * g.sectorSize;
	if (op == B_GET_DEVICE_SIZE) {
		if (!arg || len < sizeof(off_t)) return B_BAD_VALUE;
		*(off_t*)arg = size; return B_OK;
	}
	if (op == B_GET_GEOMETRY || op == B_GET_BIOS_GEOMETRY) {
		if (!arg || len < sizeof(device_geometry)) return B_BAD_VALUE;
		device_geometry* out = (device_geometry*)arg; memset(out, 0, sizeof(*out));
		out->bytes_per_sector = g.sectorSize; out->sectors_per_track = g.spt;
		out->cylinder_count = g.cylinders; out->head_count = g.heads; return B_OK;
	}
#endif
	return B_BAD_VALUE;
}

static device_hooks sHooks = { open_dev, close_dev, free_dev, control_dev, read_dev, write_dev };
static const char* const kDevices[] = {
	"disk/floppy/0/raw", "disk/floppy/1/raw",
	"disk/floppy/0/1.2m/raw", "disk/floppy/1/1.2m/raw", NULL
};

static status_t init_driver()
{
#if defined(__i386__) || defined(__x86_64__)
	status_t status = get_module("bus_managers/isa/v1", (module_info**)&sISA);
	if (status != B_OK) return status;
	status = reset_fdc();
	if (status != B_OK) { put_module("bus_managers/isa/v1"); sISA = NULL; return status; }
	for (int i = 0; i < 2; i++) { sPC[i].drive = i; sPC[i].type = TYPE_1440; sPC[i].cylinder = 0; recursive_lock_init(&sPC[i].lock, "floppy lock"); }
#elif defined(__m68k__)
	return amiga_init();
#endif
	return B_OK;
}
static void uninit_driver()
{
#if defined(__i386__) || defined(__x86_64__)
	for (int i = 0; i < 2; i++) recursive_lock_destroy(&sPC[i].lock);
	if (sISA) put_module("bus_managers/isa/v1");
	sISA = NULL;
#endif
}
static const char** publish_devices() { return kDevices; }
static device_hooks* find_device(const char* name) { return strstr(name, "disk/floppy/") == name ? &sHooks : NULL; }

static module_info sModule = { "drivers/disk/floppy/legacy/v3", 0, NULL };
_EXPORT module_info* modules[] = { &sModule, NULL };
