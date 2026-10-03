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
	return B_OK;
}

static bool
range_valid(uint32 offset, uint32 size, uint32 total)
{
	return offset <= total && size <= total - offset;
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
	} else {
		status = parse_ip_firmware((const uint8*)address, request.size,
			request.type, parsed);
		if (status != B_OK) { delete_area(area); return status; }
	}

	/* Replace only the selected IP image. */
	rdna4_firmware_slot& slot = d.firmware[request.type];
	if (slot.staged && slot.area >= 0) delete_area(slot.area);
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

	d.psp_fw_area = area;
	d.psp_fw_cpu = address;
	d.psp_fw_phys = entry.address;
	d.psp_fw_size = request.size;
	d.psp_sos_offset = sosOffset;
	d.psp_sos_size = sosSize;
	d.psp_fw_type = request.type;
	d.psp_fw_version_major = pspMajor;
	d.psp_fw_version_minor = pspMinor;

	return B_OK;
}

status_t
rdna4_firmware_boot(rdna4_device& d)
{
	/*
	 * PSP 14.x is the authentication root. We deliberately do not parse,
	 * replace, or bypass AMD signatures here. The staged image is passed to
	 * the PSP boot protocol once the IP-discovery code supplies the MP0
	 * register base and the remaining PSP boot components (KDB/SPL/SYS)
	 * are available.
	 *
	 * Returning B_NOT_SUPPORTED here is intentional until those hardware
	 * discovery inputs exist; pretending that a raw SOS upload is a complete
	 * PSP boot would be unsafe and would not constitute authenticated boot.
	 */
	if (d.psp_fw_area < 0 || d.psp_fw_type != RDNA4_FW_PSP
		|| d.psp_sos_size == 0)
		return B_NO_INIT;
	return B_NOT_SUPPORTED;
}

void
rdna4_firmware_uninit(rdna4_device& d)
{
	for (uint32 i = 0; i < RDNA4_FW_MAX; i++) {
		if (d.firmware[i].staged && d.firmware[i].area >= 0) {
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
	d.psp_fw_area = -1;
	d.psp_fw_cpu = NULL;
	d.psp_fw_phys = 0;
	d.psp_fw_size = 0;
	d.psp_sos_offset = 0;
	d.psp_sos_size = 0;
	d.psp_fw_type = RDNA4_FW_MAX;
	d.psp_fw_version_major = 0;
	d.psp_fw_version_minor = 0;
}
