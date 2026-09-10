/*
 * Legacy floppy disk driver for Haiku.
 * PC NEC765 backend: 1.44 MiB, 1.2 MiB and 360 KiB media.
 * Amiga Paula support is separated behind the platform backend boundary.
 *
 * Distributed under the terms of the MIT License.
 */
#include <KernelExport.h>
#include <Drivers.h>
#include <Kernel.h>
#include <module.h>
#include <lock.h>
#include <util/AutoLock.h>
#include <string.h>
#include "floppy_profiles.h"

#if defined(__i386__) || defined(__x86_64__)
#include <ISA.h>
#endif

#define SECTOR_SIZE 512
#define TYPE_1440 0
#define TYPE_1200 1
#define TYPE_360 2
#define FDC_RETRIES 3
#define FDC_TIMEOUT_US 2000000
#define MOTOR_SPINUP_US 500000
#define MOTOR_IDLE_US 2000000
#define FDC_IRQ 6

struct Geometry {
	uint32 spt;
	uint32 heads;
	uint32 cylinders;
	uint32 sectorSize;
	uint32 dataRate;
};

struct Device {
	recursive_lock lock;
	int drive;
	uint32 type;
	uint32 cylinder;
	bool motorOn;
	bigtime_t motorDeadline;
};

#if defined(__i386__) || defined(__x86_64__)
#define DOR 0x3f2
#define MSR 0x3f4
#define FIFO 0x3f5
#define DIR 0x3f7
#define CCR 0x3f7
#define DOR_RESET 0x04
#define DOR_IRQ_ENABLE 0x08
#define M0 0x10
#define M1 0x20
#define RQM 0x80
#define DIO 0x40
#define DIR_WRITE_PROTECT 0x40
#define DIR_DISK_CHANGED 0x80
#define CMD_READ 0x06
#define CMD_WRITE 0x05
#define CMD_RECAL 0x07
#define CMD_SENSE 0x08
#define CMD_SEEK 0x0f
#define CMD_SPECIFY 0x03

static isa_module_info* sISA;
static Device sPC[2];
static sem_id sIRQSem = -1;
static bool sIRQInstalled = false;
static const Geometry k1440 = {18, 2, 80, SECTOR_SIZE, 500};
static const Geometry k1200 = {15, 2, 80, SECTOR_SIZE, 500};
static const Geometry k360 = {9, 2, 40, SECTOR_SIZE, 250};

static int32 fdc_interrupt(void*)
{
	if (sIRQSem >= 0)
		release_sem(sIRQSem);
	return B_HANDLED_INTERRUPT;
}

static void drain_irq_sem()
{
	if (sIRQSem < 0)
		return;
	while (acquire_sem_etc(sIRQSem, 1, B_RELATIVE_TIMEOUT, 0) == B_OK)
		;
}

static status_t wait_irq()
{
	if (sIRQSem < 0)
		return B_NO_INIT;
	return acquire_sem_etc(sIRQSem, 1, B_RELATIVE_TIMEOUT, FDC_TIMEOUT_US);
}

static status_t wait_phase(bool input)
{
	bigtime_t deadline = system_time() + FDC_TIMEOUT_US;
	while (system_time() < deadline) {
		uint8 status = sISA->read_io_8(MSR);
		if ((status & RQM) != 0 && (((status & DIO) != 0) == input))
			return B_OK;
		cpu_pause();
	}
	return B_TIMED_OUT;
}

static status_t out(uint8 value)
{
	status_t status = wait_phase(false);
	if (status == B_OK)
		sISA->write_io_8(FIFO, value);
	return status;
}

static status_t in(uint8* value)
{
	status_t status = wait_phase(true);
	if (status == B_OK)
		*value = sISA->read_io_8(FIFO);
	return status;
}

static void select_drive(int drive, bool motor)
{
	uint8 value = DOR_RESET | DOR_IRQ_ENABLE | (uint8)drive;
	if (motor)
		value |= drive == 0 ? M0 : M1;
	sISA->write_io_8(DOR, value);
}

static void set_data_rate(const Geometry& g)
{
	/* CCR: 0 = 500 kbps, 1 = 300 kbps, 2 = 250 kbps, 3 = 1 Mbps. */
	uint8 value = g.dataRate == 250 ? 2 : 0;
	sISA->write_io_8(CCR, value);
}

static status_t spin_motor(Device* d, const Geometry& g)
{
	set_data_rate(g);
	select_drive(d->drive, true);
	if (!d->motorOn) {
		d->motorOn = true;
		snooze(MOTOR_SPINUP_US);
	}
	d->motorDeadline = system_time() + MOTOR_IDLE_US;
	return B_OK;
}

static void stop_motor(Device* d)
{
	if (d->motorOn) {
		select_drive(d->drive, false);
		d->motorOn = false;
		d->motorDeadline = 0;
	}
}

static void refresh_motor(Device* d)
{
	if (d->motorOn && system_time() >= d->motorDeadline)
		stop_motor(d);
}

static uint8 read_dir()
{
	return sISA->read_io_8(DIR);
}

static status_t sense(uint8* st0, uint8* cylinder)
{
	status_t status = out(CMD_SENSE);
	if (status != B_OK)
		return status;
	if ((status = in(st0)) != B_OK)
		return status;
	return in(cylinder);
}

static status_t reset_fdc()
{
	drain_irq_sem();
	sISA->write_io_8(DOR, 0);
	snooze(20000);
	sISA->write_io_8(DOR, DOR_RESET | DOR_IRQ_ENABLE);

	/* A NEC765-compatible controller reports one completion interrupt for
	 * each drive during reset. Consume all four before issuing commands. */
	for (int i = 0; i < 4; i++) {
		status_t status = wait_irq();
		if (status != B_OK)
			return status;
	}

	uint8 st0, cyl;
	for (int i = 0; i < 4; i++) {
		status_t status = sense(&st0, &cyl);
		if (status != B_OK)
			return status;
	}

	status_t status = out(CMD_SPECIFY);
	if (status != B_OK)
		return status;
	if ((status = out(0xdf)) != B_OK)
		return status;
	/* PIO mode: NDMA=1. */
	return out(0x03);
}

static status_t recal(Device* d, const Geometry& g)
{
	status_t status = spin_motor(d, g);
	if (status != B_OK)
		return status;
	drain_irq_sem();
	status = out(CMD_RECAL);
	if (status != B_OK)
		return status;
	if ((status = out((uint8)d->drive)) != B_OK)
		return status;
	if ((status = wait_irq()) != B_OK)
		return status;

	uint8 st0, cyl;
	if ((status = sense(&st0, &cyl)) != B_OK)
		return status;
	if ((st0 & 0x20) == 0 || cyl != 0)
		return B_IO_ERROR;
	d->cylinder = 0;
	return B_OK;
}

static status_t seek(Device* d, const Geometry& g, uint8 cyl, uint8 head)
{
	if (d->cylinder == cyl)
		return B_OK;
	status_t status = spin_motor(d, g);
	if (status != B_OK)
		return status;
	drain_irq_sem();
	status = out(CMD_SEEK);
	if (status != B_OK)
		return status;
	if ((status = out((head << 2) | d->drive)) != B_OK)
		return status;
	if ((status = out(cyl)) != B_OK)
		return status;
	if ((status = wait_irq()) != B_OK)
		return status;

	uint8 st0, result;
	if ((status = sense(&st0, &result)) != B_OK)
		return status;
	if ((st0 & 0x20) == 0 || result != cyl)
		return B_IO_ERROR;
	d->cylinder = cyl;
	return B_OK;
}

static status_t transfer_once(Device* d, const Geometry& g, uint8 cyl,
	uint8 head, uint8 sector, uint8* data, bool write)
{
	status_t status = seek(d, g, cyl, head);
	if (status != B_OK)
		return status;

	if (write && (read_dir() & DIR_WRITE_PROTECT) != 0)
		return B_READ_ONLY_DEVICE;

	drain_irq_sem();
	uint8 command = (write ? CMD_WRITE : CMD_READ) | 0x40;
	uint8 params[] = { command, (uint8)((head << 2) | d->drive), cyl, head,
		sector, 2, sector, 0x1b, 0xff };
	for (uint32 i = 0; i < sizeof(params); i++) {
		if ((status = out(params[i])) != B_OK)
			return status;
	}

	for (uint32 i = 0; i < SECTOR_SIZE; i++) {
		status = write ? out(data[i]) : in(&data[i]);
		if (status != B_OK)
			return status;
	}

	if ((status = wait_irq()) != B_OK)
		return status;

	uint8 result[7];
	for (int i = 0; i < 7; i++) {
		if ((status = in(&result[i])) != B_OK)
			return status;
	}

	/* ST0 abnormal termination; ST1/ST2 contain controller/media errors. */
	if ((result[0] & 0xc0) != 0 || result[1] != 0 || result[2] != 0)
		return B_IO_ERROR;
	if (result[3] != cyl || result[4] != head || result[5] != sector)
		return B_IO_ERROR;
	return B_OK;
}

static status_t transfer(Device* d, const Geometry& g, uint8 cyl, uint8 head,
	uint8 sector, uint8* data, bool write)
{
	status_t status = B_IO_ERROR;
	for (int attempt = 0; attempt < FDC_RETRIES; attempt++) {
		status = transfer_once(d, g, cyl, head, sector, data, write);
		if (status == B_OK)
			return B_OK;
		status_t recovery = reset_fdc();
		if (recovery != B_OK)
			return recovery;
		d->cylinder = 0;
		if ((status = recal(d, g)) != B_OK)
			return status;
	}
	return status;
}
#endif

#if defined(__m68k__)
/* Paula is track-oriented and has no NEC-765 command FIFO. The Amiga
 * architecture backend is intentionally separate: it requires a board-level
 * custom-chip mapping and DMA interrupt interface that is not currently
 * exposed by the Haiku m68k kernel tree. */
struct AmigaDevice {
	uint32 unit;
	uint32 cylinder;
	bool writeProtected;
};
static AmigaDevice sAmiga[4];
static status_t amiga_init() { return B_NOT_SUPPORTED; }
#endif

static const Geometry& geometry(const Device* d)
{
	if (d->type == TYPE_1200)
		return k1200;
	if (d->type == TYPE_360)
		return k360;
	return k1440;
}

static bool device_name(const char* name, int& drive, uint32& type)
{
	if (name == NULL || strncmp(name, "disk/floppy/", 12) != 0)
		return false;
	if (name[12] < '0' || name[12] > '1')
		return false;
	drive = name[12] - '0';
	if (strcmp(name + 13, "/raw") == 0)
		type = TYPE_1440;
	else if (strcmp(name + 13, "/1.2m/raw") == 0)
		type = TYPE_1200;
	else if (strcmp(name + 13, "/360k/raw") == 0)
		type = TYPE_360;
	else
		return false;
	return true;
}

static status_t open_dev(const char* name, uint32, void** cookie)
{
#if defined(__i386__) || defined(__x86_64__)
	if (cookie == NULL)
		return B_BAD_VALUE;
	int drive;
	uint32 type;
	if (!device_name(name, drive, type))
		return B_BAD_VALUE;

	Device* d = &sPC[drive];
	const Geometry& g = type == TYPE_1200 ? k1200
		: type == TYPE_360 ? k360 : k1440;
	RecursiveLocker locker(&d->lock);
	d->type = type;
	refresh_motor(d);
	status_t status = recal(d, g);
	if (status != B_OK)
		return status;
	*cookie = d;
	return B_OK;
#elif defined(__m68k__)
	(void)name;
	(void)cookie;
	return B_DEV_NOT_READY;
#else
	(void)name;
	(void)cookie;
	return B_NOT_SUPPORTED;
#endif
}

static status_t close_dev(void* cookie)
{
#if defined(__i386__) || defined(__x86_64__)
	if (cookie != NULL) {
		Device* d = (Device*)cookie;
		RecursiveLocker locker(&d->lock);
		stop_motor(d);
	}
#else
	(void)cookie;
#endif
	return B_OK;
}

static status_t free_dev(void*)
{
	return B_OK;
}

static status_t read_dev(void* cookie, off_t pos, void* buffer, size_t* count)
{
#if defined(__i386__) || defined(__x86_64__)
	if (cookie == NULL || buffer == NULL || count == NULL)
		return B_BAD_VALUE;
	Device* d = (Device*)cookie;
	const Geometry& g = geometry(d);
	off_t size = (off_t)g.spt * g.heads * g.cylinders * g.sectorSize;
	if (pos < 0 || pos > size)
		return B_BAD_VALUE;
	size_t wanted = *count;
	if (pos + (off_t)wanted > size)
		wanted = size - pos;
	if ((pos % SECTOR_SIZE) != 0 || (wanted % SECTOR_SIZE) != 0)
		return B_BAD_VALUE;

	uint8 sectorBuffer[SECTOR_SIZE];
	RecursiveLocker locker(&d->lock);
	for (size_t done = 0; done < wanted; done += SECTOR_SIZE) {
		off_t lba = (pos + done) / SECTOR_SIZE;
		uint8 sector = lba % g.spt + 1;
		uint8 head = (lba / g.spt) % g.heads;
		uint8 cyl = lba / (g.spt * g.heads);
		status_t status = transfer(d, g, cyl, head, sector, sectorBuffer, false);
		if (status != B_OK) {
			*count = done;
			return status;
		}
		if (user_memcpy((uint8*)buffer + done, sectorBuffer, SECTOR_SIZE) != B_OK) {
			*count = done;
			return B_BAD_ADDRESS;
		}
		d->motorDeadline = system_time() + MOTOR_IDLE_US;
	}
	*count = wanted;
	return B_OK;
#else
	(void)cookie;
	(void)pos;
	(void)buffer;
	(void)count;
	return B_NOT_SUPPORTED;
#endif
}

static status_t write_dev(void* cookie, off_t pos, const void* buffer, size_t* count)
{
#if defined(__i386__) || defined(__x86_64__)
	if (cookie == NULL || buffer == NULL || count == NULL)
		return B_BAD_VALUE;
	Device* d = (Device*)cookie;
	const Geometry& g = geometry(d);
	off_t size = (off_t)g.spt * g.heads * g.cylinders * g.sectorSize;
	if (pos < 0 || pos > size)
		return B_BAD_VALUE;
	size_t wanted = *count;
	if (pos + (off_t)wanted > size)
		wanted = size - pos;
	if ((pos % SECTOR_SIZE) != 0 || (wanted % SECTOR_SIZE) != 0)
		return B_BAD_VALUE;

	uint8 sectorBuffer[SECTOR_SIZE];
	RecursiveLocker locker(&d->lock);
	for (size_t done = 0; done < wanted; done += SECTOR_SIZE) {
		if (user_memcpy(sectorBuffer, (const uint8*)buffer + done,
				SECTOR_SIZE) != B_OK) {
			*count = done;
			return B_BAD_ADDRESS;
		}
		off_t lba = (pos + done) / SECTOR_SIZE;
		uint8 sector = lba % g.spt + 1;
		uint8 head = (lba / g.spt) % g.heads;
		uint8 cyl = lba / (g.spt * g.heads);
		status_t status = transfer(d, g, cyl, head, sector, sectorBuffer, true);
		if (status != B_OK) {
			*count = done;
			return status;
		}
		d->motorDeadline = system_time() + MOTOR_IDLE_US;
	}
	*count = wanted;
	return B_OK;
#else
	(void)cookie;
	(void)pos;
	(void)buffer;
	(void)count;
	return B_NOT_SUPPORTED;
#endif
}

static status_t control_dev(void* cookie, uint32 op, void* arg, size_t len)
{
#if defined(__i386__) || defined(__x86_64__)
	if (cookie == NULL || arg == NULL)
		return B_BAD_VALUE;
	Device* d = (Device*)cookie;
	const Geometry& g = geometry(d);
	off_t size = (off_t)g.spt * g.heads * g.cylinders * g.sectorSize;
	if (op == B_GET_DEVICE_SIZE) {
		if (len < sizeof(size_t))
			return B_BAD_VALUE;
		size_t value = (size_t)size;
		return user_memcpy(arg, &value, sizeof(value));
	}
	if (op == B_GET_GEOMETRY || op == B_GET_BIOS_GEOMETRY) {
		if (len < sizeof(device_geometry))
			return B_BAD_VALUE;
		device_geometry info;
		memset(&info, 0, sizeof(info));
		info.bytes_per_sector = g.sectorSize;
		info.sectors_per_track = g.spt;
		info.cylinder_count = g.cylinders;
		info.head_count = g.heads;
		return user_memcpy(arg, &info, sizeof(info));
	}
#else
	(void)cookie;
	(void)op;
	(void)arg;
	(void)len;
#endif
	return B_BAD_VALUE;
}

static device_hooks sHooks = {
	open_dev, close_dev, free_dev, control_dev, read_dev, write_dev
};

static const char* const kDevices[] = {
	"disk/floppy/0/raw", "disk/floppy/1/raw",
	"disk/floppy/0/1.2m/raw", "disk/floppy/1/1.2m/raw",
	"disk/floppy/0/360k/raw", "disk/floppy/1/360k/raw",
	NULL
};

static status_t init_driver()
{
#if defined(__i386__) || defined(__x86_64__)
	status_t status = get_module("bus_managers/isa/v1", (module_info**)&sISA);
	if (status != B_OK)
		return status;

	sIRQSem = create_sem(0, "floppy irq");
	if (sIRQSem < 0) {
		put_module("bus_managers/isa/v1");
		sISA = NULL;
		return sIRQSem;
	}

	status = install_io_interrupt_handler(FDC_IRQ, &fdc_interrupt, NULL, 0);
	if (status != B_OK) {
		delete_sem(sIRQSem);
		sIRQSem = -1;
		put_module("bus_managers/isa/v1");
		sISA = NULL;
		return status;
	}
	sIRQInstalled = true;

	status = reset_fdc();
	if (status != B_OK) {
		remove_io_interrupt_handler(FDC_IRQ, &fdc_interrupt, NULL);
		sIRQInstalled = false;
		delete_sem(sIRQSem);
		sIRQSem = -1;
		put_module("bus_managers/isa/v1");
		sISA = NULL;
		return status;
	}
	for (int i = 0; i < 2; i++) {
		sPC[i].drive = i;
		sPC[i].type = TYPE_1440;
		sPC[i].cylinder = 0;
		sPC[i].motorOn = false;
		sPC[i].motorDeadline = 0;
		recursive_lock_init(&sPC[i].lock, "floppy lock");
	}
#elif defined(__m68k__)
	return amiga_init();
#else
	return B_NOT_SUPPORTED;
#endif
	return B_OK;
}

static void uninit_driver()
{
#if defined(__i386__) || defined(__x86_64__)
	for (int i = 0; i < 2; i++) {
		stop_motor(&sPC[i]);
		recursive_lock_destroy(&sPC[i].lock);
	}
	if (sIRQInstalled) {
		remove_io_interrupt_handler(FDC_IRQ, &fdc_interrupt, NULL);
		sIRQInstalled = false;
	}
	if (sIRQSem >= 0) {
		delete_sem(sIRQSem);
		sIRQSem = -1;
	}
	if (sISA != NULL)
		put_module("bus_managers/isa/v1");
	sISA = NULL;
#endif
}

static const char** publish_devices()
{
#if defined(__i386__) || defined(__x86_64__)
	return kDevices;
#else
	return NULL;
#endif
}

static device_hooks* find_device(const char* name)
{
#if defined(__i386__) || defined(__x86_64__)
	int drive;
	uint32 type;
	return device_name(name, drive, type) ? &sHooks : NULL;
#else
	(void)name;
	return NULL;
#endif
}

static module_info sModule = { "drivers/disk/floppy/legacy/v7", 0, NULL };
_EXPORT module_info* modules[] = { &sModule, NULL };
