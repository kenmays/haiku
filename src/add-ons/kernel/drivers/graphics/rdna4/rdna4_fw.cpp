#include "rdna4_fw.h"
#include "driver.h"
#include "rdna4_vm.h"

#include <KernelExport.h>
#include <OS.h>
#include <string.h>

static const uint32 kFirmwareMagic = RDNA4_PRIVATE_DATA_MAGIC;
static const uint32 kMaxFirmwareSize = 64 * 1024 * 1024;

struct common_fw_header {
	uint32 size_bytes;
	uint32 header_size_bytes;
	uint16 header_version_major;
	uint16 header_version_minor;
	uint16 ip_version_major;
	uint16 ip_version_minor;
	uint32 ucode_version;
	uint32 ucode_size_bytes;
	uint32 ucode_array_offset_bytes;
	uint32 crc32;
} __attribute__((packed));

struct psp_fw_bin_desc {
	uint32 fw_type;
	uint32 fw_version;
	uint32 offset_bytes;
	uint32 size_bytes;
} __attribute__((packed));

static bool range_valid(uint32 offset, uint32 size, uint32 total);

struct fw_parsed {
	uint32 ucodeOffset;
	uint32 ucodeSize;
	uint32 dataOffset;
	uint32 dataSize;
	uint64 ucodeStart;
	uint64 dataStart;
	uint32 version;
};

static status_t parse_ip_firmware(const uint8* data, uint32 size,
	uint32 type, fw_parsed& out)
{
	memset(&out, 0, sizeof(out));
	if (size < sizeof(common_fw_header))
		return B_BAD_DATA;
	const common_fw_header* h = (const common_fw_header*)data;
	if (h->size_bytes > size || h->header_size_bytes < sizeof(common_fw_header)
		|| h->header_size_bytes > h->size_bytes)
		return B_BAD_DATA;
	out.version = h->ucode_version;
	if (type == RDNA4_FW_SDMA0 || type == RDNA4_FW_SDMA1) {
		if (h->header_version_major != 3 || h->header_size_bytes < sizeof(common_fw_header) + 12)
			return B_BAD_DATA;
		out.ucodeOffset = *(const uint32*)(data + sizeof(common_fw_header) + 4);
		out.ucodeSize = *(const uint32*)(data + sizeof(common_fw_header) + 8);
		if (!range_valid(out.ucodeOffset, out.ucodeSize, h->size_bytes))
			return B_BAD_DATA;
		return B_OK;
	}
	if (type == RDNA4_FW_MES || type == RDNA4_FW_MES1 || type == RDNA4_FW_UNI_MES) {
		if (h->header_version_major != 1 || h->header_size_bytes < sizeof(common_fw_header) + 40)
			return B_BAD_DATA;
		const uint32* p = (const uint32*)(data + sizeof(common_fw_header));
		out.ucodeSize = p[1]; out.ucodeOffset = p[2];
		out.dataSize = p[4]; out.dataOffset = p[5];
		out.ucodeStart = (uint64)p[7] << 32 | p[6];
		out.dataStart = (uint64)p[9] << 32 | p[8];
		if (!range_valid(out.ucodeOffset, out.ucodeSize, h->size_bytes)
			|| !range_valid(out.dataOffset, out.dataSize, h->size_bytes))
			return B_BAD_DATA;
		return B_OK;
	}
	out.ucodeOffset = h->ucode_array_offset_bytes;
	out.ucodeSize = h->ucode_size_bytes;
	if (!range_valid(out.ucodeOffset, out.ucodeSize, h->size_bytes))
		return B_BAD_DATA;
	return B_OK;
}

static bool
range_valid(uint32 offset, uint32 size, uint32 total)
{
	return offset <= total && size <= total - offset;
}

static status_t
stage_payload(rdna4_device& d, rdna4_firmware_slot& slot, uint32 type)
{
	slot.payload_area = -1;
	slot.payload_cpu = NULL;
	slot.payload_phys = 0;
	slot.payload_gpu = 0;
	slot.payload_size = 0;
	slot.payload2_area = -1;
	slot.payload2_cpu = NULL;
	slot.payload2_phys = 0;
	slot.payload2_gpu = 0;
	slot.payload2_size = 0;
	if (slot.ucode_size == 0)
		return B_OK;

	size_t size = (slot.ucode_size + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);
	void* cpu = NULL;
	area_id area = create_area("rdna4 firmware payload", &cpu,
		B_ANY_KERNEL_ADDRESS, size, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < 0)
		return area;

	physical_entry entry;
	status_t status = get_memory_map(cpu, size, &entry, 1);
	if (status != B_OK || entry.size < size) {
		delete_area(area);
		return status != B_OK ? status : B_NOT_SUPPORTED;
	}

	memcpy(cpu, (uint8*)slot.cpu + slot.ucode_offset, slot.ucode_size);
	if (size > slot.ucode_size)
		memset((uint8*)cpu + slot.ucode_size, 0, size - slot.ucode_size);

	rdna4_bo bo = {};
	bo.area = area;
	bo.cpu = cpu;
	bo.size = size;
	bo.alignment = B_PAGE_SIZE;
	bo.physical = entry.address;
	bo.used = true;
	status = rdna4_vm_map_bo(d, bo, B_PAGE_SIZE);
	if (status != B_OK) {
		delete_area(area);
		return status;
	}

	slot.payload_area = area;
	slot.payload_cpu = cpu;
	slot.payload_phys = entry.address;
	slot.payload_gpu = bo.gpu;
	slot.payload_size = slot.ucode_size;

	if (type == RDNA4_FW_GFX_IMU) {
		/* IMU v1.0 contains separate IRAM and DRAM payloads. PSP expects
		 * two authenticated LOAD_IP_FW commands (types 68 and 69). */
		const uint8* image = (const uint8*)slot.cpu;
		if (slot.size < 48)
			return B_BAD_DATA;
		uint32 iramSize = *(const uint32*)(image + 32);
		uint32 iramOffset = *(const uint32*)(image + 36);
		uint32 dramSize = *(const uint32*)(image + 40);
		uint32 dramOffset = *(const uint32*)(image + 44);
		if (!range_valid(iramOffset, iramSize, slot.size)
			|| !range_valid(dramOffset, dramSize, slot.size)
			|| iramSize == 0 || dramSize == 0)
			return B_BAD_DATA;

		size_t dramAlloc = (dramSize + B_PAGE_SIZE - 1) & ~(size_t)(B_PAGE_SIZE - 1);
		void* dramCpu = NULL;
		area_id dramArea = create_area("rdna4 IMU DRAM", &dramCpu,
			B_ANY_KERNEL_ADDRESS, dramAlloc, B_CONTIGUOUS,
			B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
		if (dramArea < 0)
			return dramArea;
		physical_entry de;
		status_t ds = get_memory_map(dramCpu, dramAlloc, &de, 1);
		if (ds != B_OK || de.size < dramAlloc) {
			delete_area(dramArea);
			return ds != B_OK ? ds : B_NOT_SUPPORTED;
		}
		memcpy(dramCpu, image + dramOffset, dramSize);
		if (dramAlloc > dramSize)
			memset((uint8*)dramCpu + dramSize, 0, dramAlloc - dramSize);
		rdna4_bo dbo = {};
		dbo.area = dramArea; dbo.cpu = dramCpu; dbo.size = dramAlloc;
		dbo.alignment = B_PAGE_SIZE; dbo.physical = de.address; dbo.used = true;
		ds = rdna4_vm_map_bo(d, dbo, B_PAGE_SIZE);
		if (ds != B_OK) {
			delete_area(dramArea);
			return ds;
		}
		slot.payload2_area = dramArea;
		slot.payload2_cpu = dramCpu;
		slot.payload2_phys = de.address;
		slot.payload2_gpu = dbo.gpu;
		slot.payload2_size = dramSize;
		slot.payload_size = iramSize;
		/* Replace the first payload with IRAM data. */
		memcpy(slot.payload_cpu, image + iramOffset, iramSize);
		if (((slot.ucode_size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1)) > iramSize)
			memset((uint8*)slot.payload_cpu + iramSize, 0,
				((slot.ucode_size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1)) - iramSize);
	}
	return B_OK;
}

static void
free_slot_payload(rdna4_device& d, rdna4_firmware_slot& slot)
{
	if (slot.payload2_area >= 0) {
		rdna4_bo b2 = {};
		b2.area = slot.payload2_area; b2.cpu = slot.payload2_cpu;
		b2.size = (slot.payload2_size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1);
		b2.physical = slot.payload2_phys; b2.gpu = slot.payload2_gpu; b2.used = true;
		if (d.vm_ready && b2.gpu) rdna4_vm_unmap_bo(d, b2);
		delete_area(slot.payload2_area);
		slot.payload2_area = -1; slot.payload2_cpu = NULL; slot.payload2_phys = 0;
		slot.payload2_gpu = 0; slot.payload2_size = 0;
	}
	if (slot.payload_area < 0)
		return;
	rdna4_bo bo = {};
	bo.area = slot.payload_area;
	bo.cpu = slot.payload_cpu;
	bo.size = (slot.payload_size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1);
	bo.physical = slot.payload_phys;
	bo.gpu = slot.payload_gpu;
	bo.used = true;
	if (d.vm_ready && bo.gpu)
		rdna4_vm_unmap_bo(d, bo);
	delete_area(slot.payload_area);
	slot.payload_area = -1;
	slot.payload_cpu = NULL;
	slot.payload_phys = 0;
	slot.payload_gpu = 0;
	slot.payload_size = 0;
}

static status_t
parse_psp_container(const uint8* data, uint32 size, uint32& sosOffset,
	uint32& sosSize, uint32& major, uint32& minor)
{
	if (size < sizeof(common_fw_header))
		return B_BAD_DATA;

	const common_fw_header* h = (const common_fw_header*)data;
	if (h->size_bytes > size || h->header_size_bytes < sizeof(common_fw_header)
		|| h->header_size_bytes > h->size_bytes)
		return B_BAD_DATA;

	major = h->header_version_major;
	minor = h->header_version_minor;
	sosOffset = sosSize = 0;

	if (major == 1) {
		if (h->header_size_bytes < sizeof(common_fw_header) + 12
			|| size < sizeof(common_fw_header) + 12)
			return B_BAD_DATA;
		const uint8* p = data + sizeof(common_fw_header);
		uint32 fwVersion = *(const uint32*)(p + 0);
		uint32 offset = *(const uint32*)(p + 4);
		uint32 length = *(const uint32*)(p + 8);
		(void)fwVersion;
		if (!range_valid(offset, length, h->size_bytes))
			return B_BAD_DATA;
		sosOffset = offset;
		sosSize = length;
		return B_OK;
	}

	if (major != 2 || size < sizeof(common_fw_header) + 4)
		return B_BAD_DATA;

	uint32 count = *(const uint32*)(data + sizeof(common_fw_header));
	if (count == 0 || count > 64)
		return B_BAD_DATA;

	uint64 tableEnd = (uint64)sizeof(common_fw_header) + 4
		+ (uint64)count * sizeof(psp_fw_bin_desc);
	if (tableEnd > h->header_size_bytes || tableEnd > h->size_bytes)
		return B_BAD_DATA;

	const psp_fw_bin_desc* bins = (const psp_fw_bin_desc*)
		(data + sizeof(common_fw_header) + 4);
	for (uint32 i = 0; i < count; i++) {
		/* PSP_FW_TYPE_PSP_SOS is 1 in AMD's public ucode ABI. */
		if (bins[i].fw_type != 1)
			continue;
		if (!range_valid(bins[i].offset_bytes, bins[i].size_bytes,
			h->size_bytes))
			return B_BAD_DATA;
		sosOffset = bins[i].offset_bytes;
		sosSize = bins[i].size_bytes;
		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}

status_t
rdna4_stage_firmware(rdna4_device& d, const rdna4_firmware_stage& request)
{
	if (request.magic != kFirmwareMagic || request.size == 0
		|| request.size > kMaxFirmwareSize || request.user_address == 0)
		return B_BAD_VALUE;
	if (request.type >= RDNA4_FW_MAX)
		return B_BAD_VALUE;

	void* address = NULL;
	area_id area = create_area("rdna4 firmware", &address,
		B_ANY_KERNEL_ADDRESS, (request.size + B_PAGE_SIZE - 1)
			& ~(uint32)(B_PAGE_SIZE - 1),
		B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < 0)
		return area;

	if (user_memcpy(address, (const void*)(addr_t)request.user_address,
		request.size) != B_OK) {
		delete_area(area);
		return B_BAD_ADDRESS;
	}

	physical_entry entry;
	status_t status = get_memory_map(address,
		(request.size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1),
		&entry, 1);
	if (status != B_OK || entry.size < request.size) {
		delete_area(area);
		return status != B_OK ? status : B_NOT_SUPPORTED;
	}

	uint32 sosOffset = 0, sosSize = 0, pspMajor = 0, pspMinor = 0;
	fw_parsed parsed = {};
	if (request.type == RDNA4_FW_PSP) {
		status = parse_psp_container((const uint8*)address, request.size,
			sosOffset, sosSize, pspMajor, pspMinor);
		if (status != B_OK) { delete_area(area); return status; }
	} else if (request.type == RDNA4_FW_DISCOVERY) {
		/* Discovery is a table, not an IP microcode image. */
	} else {
		status = parse_ip_firmware((const uint8*)address, request.size,
			request.type, parsed);
		if (status != B_OK) { delete_area(area); return status; }
	}

	/* Replace only the selected IP image. */
	rdna4_firmware_slot& slot = d.firmware[request.type];
	if (slot.staged) {
		free_slot_payload(d, slot);
		if (slot.area >= 0)
			delete_area(slot.area);
	}
	slot = {};
	slot.area = area;
	slot.cpu = address;
	slot.phys = entry.address;
	slot.size = request.size;
	slot.ucode_offset = parsed.ucodeOffset;
	slot.ucode_size = parsed.ucodeSize;
	slot.data_offset = parsed.dataOffset;
	slot.data_size = parsed.dataSize;
	slot.ucode_start = parsed.ucodeStart;
	slot.data_start = parsed.dataStart;
	slot.version = parsed.version;
	slot.gpu = 0;
	slot.staged = true;
	/* Firmware is no longer consumed through a raw physical address. Map the
	 * staged system pages into VMID0 so GFX/SDMA/MES/VCN can reach them through
	 * the same GPUVM/GART translation used by normal buffers. */
	rdna4_bo fwbo = {};
	fwbo.area = area; fwbo.cpu = address; fwbo.size = (request.size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1);
	fwbo.alignment = B_PAGE_SIZE; fwbo.physical = entry.address; fwbo.flags = 0; fwbo.used = true;
	status = rdna4_vm_map_bo(d, fwbo, B_PAGE_SIZE);
	if (status != B_OK) { delete_area(area); slot = {}; slot.area = -1; return status; }
	slot.gpu = fwbo.gpu;
	status = stage_payload(d, slot, request.type);
	if (status != B_OK) { rdna4_vm_unmap_bo(d, fwbo); delete_area(area); slot = {}; slot.area = -1; return status; }

	if (request.type == RDNA4_FW_PSP) {
		d.psp_fw_area = area;
		d.psp_fw_cpu = address;
		d.psp_fw_phys = entry.address;
		d.psp_fw_size = request.size;
		d.psp_sos_offset = sosOffset;
		d.psp_sos_size = sosSize;
		d.psp_fw_type = request.type;
		d.psp_fw_version_major = pspMajor;
		d.psp_fw_version_minor = pspMinor;
	} else if (request.type == RDNA4_FW_DISCOVERY) {
		status = rdna4_discovery_parse(d, address, request.size);
		if (status != B_OK) { rdna4_vm_unmap_bo(d, fwbo); delete_area(area); slot = {}; slot.area = -1; return status; }
	}

	return B_OK;
}

static status_t
psp_wait(rdna4_device& d, uint32 reg, uint32 mask, uint32 value, bigtime_t timeout)
{
	bigtime_t deadline=system_time()+timeout;
	while(system_time()<deadline){
		uint32 v=*(volatile uint32*)(d.mmio+((size_t)reg<<2));
		if((v&mask)==value)return B_OK;
		snooze(100);
	}
	return B_TIMED_OUT;
}

status_t
rdna4_firmware_boot(rdna4_device& d)
{
	if(d.mmio==NULL||d.psp_fw_area<0||d.psp_fw_type!=RDNA4_FW_PSP||d.psp_sos_size==0)
		return B_NO_INIT;

	/* PSP 14.x C2P registers. The Linux PSP v14 implementation uses the
	 * same MP0 SMN C2PMSG handshake: wait for the bootloader-ready bit,
	 * provide the SOS physical address in C2PMSG_36, issue LOAD_SOSDRV,
	 * then wait for the tOS sign-of-life response. */
	const uint32 C2P35=0x16063;
	const uint32 C2P36=0x16064;
	const uint32 C2P81=0x16091;
	const uint32 LOAD_SOSDRV=0x20000;

	uint32 alive=*(volatile uint32*)(d.mmio+((size_t)C2P81<<2));
	if(alive!=0){
		if(d.shared)d.shared->psp_state=RDNA4_ENGINE_RUNNING;
		return B_OK;
	}
	status_t s=psp_wait(d,C2P35,0x80000000u,0x80000000u,500000);
	if(s!=B_OK)return s;

	if(d.psp_boot_area>=0)delete_area(d.psp_boot_area);
	d.psp_boot_area=-1;d.psp_boot_cpu=NULL;d.psp_boot_phys=0;d.psp_boot_size=0;
	void*cpu=NULL;
	size_t size=(d.psp_sos_size+B_PAGE_SIZE-1)&~(size_t)(B_PAGE_SIZE-1);
	d.psp_boot_area=create_area("rdna4 PSP SOS",&cpu,B_ANY_KERNEL_ADDRESS,size,
		B_CONTIGUOUS,B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA);
	if(d.psp_boot_area<0)return d.psp_boot_area;
	d.psp_boot_cpu=cpu;d.psp_boot_size=size;
	physical_entry e;
	s=get_memory_map(cpu,size,&e,1);
	if(s!=B_OK||e.size<size){delete_area(d.psp_boot_area);d.psp_boot_area=-1;return s!=B_OK?s:B_NOT_SUPPORTED;}
	d.psp_boot_phys=e.address;
	memset(cpu,0,size);
	memcpy(cpu,(uint8*)d.psp_fw_cpu+d.psp_sos_offset,d.psp_sos_size);
	*(volatile uint32*)(d.mmio+((size_t)C2P36<<2))=(uint32)(d.psp_boot_phys>>20);
	*(volatile uint32*)(d.mmio+((size_t)C2P35<<2))=LOAD_SOSDRV;
	(void)*(volatile uint32*)(d.mmio+((size_t)C2P35<<2));
	snooze(20000);
	uint32 before=*(volatile uint32*)(d.mmio+((size_t)C2P81<<2));
	bigtime_t deadline=system_time()+5000000;
	while(system_time()<deadline){
		uint32 now=*(volatile uint32*)(d.mmio+((size_t)C2P81<<2));
		if(now!=before){if(d.shared){d.shared->psp_state=RDNA4_ENGINE_RUNNING;d.shared->gfx_state=RDNA4_ENGINE_FIRMWARE_READY;d.shared->smu_state=RDNA4_ENGINE_FIRMWARE_READY;}return B_OK;}
		snooze(100);
	}
	return B_TIMED_OUT;
}

status_t
rdna4_firmware_remap(rdna4_device& d)
{
	if (!d.vm_ready)
		return B_NO_INIT;
	for (uint32 i = 0; i < RDNA4_FW_MAX; i++) {
		rdna4_firmware_slot& f = d.firmware[i];
		if (!f.staged)
			continue;
		if (f.area >= 0 && f.size != 0 && f.gpu == 0) {
			rdna4_bo b = {};
			b.area = f.area; b.cpu = f.cpu;
			b.size = (f.size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1);
			b.physical = f.phys; b.used = true;
			status_t status = rdna4_vm_map_bo(d, b, B_PAGE_SIZE);
			if (status != B_OK)
				return status;
			f.gpu = b.gpu;
		}
		if (f.payload2_area >= 0 && f.payload2_size != 0 && f.payload2_gpu == 0) {
			rdna4_bo b = {};
			b.area = f.payload2_area; b.cpu = f.payload2_cpu;
			b.size = (f.payload2_size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1);
			b.physical = f.payload2_phys; b.used = true;
			status_t status = rdna4_vm_map_bo(d, b, B_PAGE_SIZE);
			if (status != B_OK) return status;
			f.payload2_gpu = b.gpu;
		}
		if (f.payload_area >= 0 && f.payload_size != 0 && f.payload_gpu == 0) {
			rdna4_bo b = {};
			b.area = f.payload_area; b.cpu = f.payload_cpu;
			b.size = (f.payload_size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1);
			b.physical = f.payload_phys; b.used = true;
			status_t status = rdna4_vm_map_bo(d, b, B_PAGE_SIZE);
			if (status != B_OK)
				return status;
			f.payload_gpu = b.gpu;
		}
	}
	return B_OK;
}

void
rdna4_firmware_uninit(rdna4_device& d)
{
	for (uint32 i = 0; i < RDNA4_FW_MAX; i++) {
		if (d.firmware[i].staged && d.firmware[i].area >= 0) {
			free_slot_payload(d, d.firmware[i]);
			rdna4_bo fwbo = {};
			fwbo.area = d.firmware[i].area; fwbo.cpu = d.firmware[i].cpu;
			fwbo.size = (d.firmware[i].size + B_PAGE_SIZE - 1) & ~(uint32)(B_PAGE_SIZE - 1);
			fwbo.physical = d.firmware[i].phys; fwbo.gpu = d.firmware[i].gpu; fwbo.used = true;
			if (d.vm_ready && fwbo.gpu) rdna4_vm_unmap_bo(d, fwbo);
			delete_area(d.firmware[i].area);
		}
		d.firmware[i] = {};
		d.firmware[i].area = -1;
	}
	if (d.psp_boot_area >= 0) delete_area(d.psp_boot_area);
	d.psp_fw_area = -1;
	d.psp_boot_area = -1;
	d.psp_boot_cpu = NULL;
	d.psp_boot_phys = 0;
	d.psp_boot_size = 0;
	d.psp_fw_cpu = NULL;
	d.psp_fw_phys = 0;
	d.psp_fw_size = 0;
	d.psp_sos_offset = 0;
	d.psp_sos_size = 0;
	d.psp_fw_type = RDNA4_FW_MAX;
	d.psp_fw_version_major = 0;
	d.psp_fw_version_minor = 0;
}
