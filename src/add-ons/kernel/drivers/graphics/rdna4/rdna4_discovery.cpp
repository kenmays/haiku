#include "rdna4_discovery.h"
#include "driver.h"
#include <string.h>

#pragma pack(1)
struct table_info {
	uint16 offset;
	uint16 checksum;
	uint16 size;
	uint16 padding;
};
struct binary_header {
	uint32 signature;
	uint16 version_major;
	uint16 version_minor;
	uint16 binary_checksum;
	uint16 binary_size;
	table_info tables[6];
};
struct binary_header_v2 {
	uint32 signature;
	uint16 version_major;
	uint16 version_minor;
	uint16 binary_checksum;
	uint16 binary_size;
	uint16 num_tables;
	uint16 padding;
	table_info tables[1];
};
struct die_info {
	uint16 die_id;
	uint16 die_offset;
};
struct ip_discovery_header {
	uint32 signature;
	uint16 version;
	uint16 size;
	uint32 id;
	uint16 num_dies;
	die_info dies[16];
	uint8 base_addr_64_bit;
	uint8 reserved;
};
struct die_header {
	uint16 die_id;
	uint16 num_ips;
};
struct ip_common {
	uint16 hw_id;
	uint8 instance;
	uint8 base_count;
	uint8 major;
	uint8 minor;
	uint8 revision;
	uint8 flags;
};
struct harvest_info {
	uint16 hw_id;
	uint8 instance;
	uint8 reserved;
};
struct harvest_table {
	uint32 signature;
	uint32 version;
	harvest_info list[32];
};
#pragma pack()

static bool
range_ok(size_t offset, size_t length, size_t size)
{
	return offset <= size && length <= size - offset;
}

static uint16
sum16(const uint8* p, size_t n)
{
	uint32 sum = 0;
	for (size_t i = 0; i < n; i++)
		sum += p[i];
	return (uint16)sum;
}

static const table_info*
get_table(const uint8* data, size_t size, uint32 version, uint32 tableId)
{
	if (tableId >= 6)
		return NULL;

	if (version == 2) {
		if (!range_ok(0, sizeof(binary_header_v2), size))
			return NULL;
		const binary_header_v2* h = (const binary_header_v2*)data;
		if (h->num_tables <= tableId)
			return NULL;
		if (!range_ok(0, 16 + (size_t)h->num_tables * sizeof(table_info), size))
			return NULL;
		return &h->tables[tableId];
	}

	if (version == 0 || version == 1) {
		if (!range_ok(0, sizeof(binary_header), size))
			return NULL;
		const binary_header* h = (const binary_header*)data;
		return &h->tables[tableId];
	}

	return NULL;
}

static bool
table_checksum_ok(const uint8* data, size_t size, const table_info& info)
{
	if (info.offset == 0 || info.size == 0
		|| !range_ok(info.offset, info.size, size))
		return false;
	return sum16(data + info.offset, info.size) == info.checksum;
}

static uint8
lookup_harvest(const uint8* data, size_t size, const table_info* info,
	uint16 hwId, uint8 instance)
{
	if (info == NULL || info->offset == 0
		|| !range_ok(info->offset, sizeof(harvest_table), size))
		return 0;

	const harvest_table* table = (const harvest_table*)(data + info->offset);
	if (table->signature != 0x56524148u)
		return 0;

	for (uint32 i = 0; i < 32; i++) {
		if (table->list[i].hw_id == 0)
			break;
		if (table->list[i].hw_id == hwId
			&& table->list[i].instance == instance)
			return 1;
	}
	return 0;
}

status_t
rdna4_discovery_parse(rdna4_device& d, const void* data, size_t size)
{
	if (data == NULL || size < sizeof(binary_header))
		return B_BAD_DATA;

	const uint8* b = (const uint8*)data;
	const binary_header* h = (const binary_header*)b;

	if (h->signature != 0x28211407u)
		return B_BAD_DATA;
	if (h->binary_size < sizeof(binary_header) || h->binary_size > size)
		return B_BAD_DATA;
	if (h->version_major > 2)
		return B_NOT_SUPPORTED;

	size_t checksumOffset = offsetof(binary_header, binary_checksum) + sizeof(h->binary_checksum);
	if (h->binary_size < checksumOffset
		|| sum16(b + checksumOffset, h->binary_size - checksumOffset)
		!= h->binary_checksum)
		return B_BAD_DATA;

	const table_info* ipInfo = get_table(b, h->binary_size, h->version_major, 0);
	if (ipInfo == NULL || !table_checksum_ok(b, h->binary_size, *ipInfo))
		return B_BAD_DATA;

	if (!range_ok(ipInfo->offset, sizeof(ip_discovery_header), h->binary_size))
		return B_BAD_DATA;

	const ip_discovery_header* ih =
		(const ip_discovery_header*)(b + ipInfo->offset);
	if (ih->signature != 0x53445049u || ih->id != 0
		|| ih->num_dies == 0 || ih->num_dies > 16
		|| ih->size != ipInfo->size
		|| ih->size < sizeof(ip_discovery_header)
		|| !range_ok(ipInfo->offset, ih->size, h->binary_size))
		return B_BAD_DATA;

	memset(&d.discovery, 0, sizeof(d.discovery));
	d.discovery.version = 1;
	d.discovery.binary_version_major = h->version_major;
	d.discovery.binary_version_minor = h->version_minor;
	d.discovery.table_version = ih->version;
	d.discovery.num_dies = ih->num_dies;

	const table_info* harvestInfo =
		get_table(b, h->binary_size, h->version_major, 2);
	if (harvestInfo != NULL && harvestInfo->offset != 0
		&& harvestInfo->size >= sizeof(harvest_table)
		&& range_ok(harvestInfo->offset, harvestInfo->size, h->binary_size)
		&& table_checksum_ok(b, h->binary_size, *harvestInfo)) {
		const harvest_table* harvest =
			(const harvest_table*)(b + harvestInfo->offset);
		if (harvest->signature != 0x56524148u)
			harvestInfo = NULL;
	}

	for (uint32 di = 0; di < ih->num_dies; di++) {
		uint16 dieOffset = ih->dies[di].die_offset;
		if (!range_ok(dieOffset, sizeof(die_header), h->binary_size))
			return B_BAD_DATA;

		const die_header* dh = (const die_header*)(b + dieOffset);
		if (dh->die_id != ih->dies[di].die_id
			|| dh->num_ips > RDNA4_DISCOVERY_MAX_IPS)
			return B_BAD_DATA;

		size_t off = dieOffset + sizeof(*dh);
		for (uint32 j = 0; j < dh->num_ips; j++) {
			if (d.discovery.ip_count >= RDNA4_DISCOVERY_MAX_IPS
				|| !range_ok(off, sizeof(ip_common), h->binary_size))
				return B_BUFFER_OVERFLOW;

			const ip_common* p = (const ip_common*)(b + off);
			uint32 n = p->base_count;
			if (n == 0 || n > RDNA4_DISCOVERY_MAX_BASES)
				return B_BAD_DATA;

			size_t addressSize = ih->base_addr_64_bit ? 8 : 4;
			size_t bytes = sizeof(ip_common) + n * addressSize;
			if (!range_ok(off, bytes, ipInfo->offset + ipInfo->size)
				|| off + bytes > ipInfo->offset + ih->size)
				return B_BAD_DATA;

			rdna4_discovered_ip& out = d.discovery.ip[d.discovery.ip_count++];
		out.hw_id = p->hw_id;
		out.instance = p->instance;
		out.major = p->major;
		out.minor = p->minor;
		out.revision = p->revision;
		out.base_count = n;

		/* Versions 0-2 carry the harvest nibble in the IP record.
		 * Version 3/4 records use the separate harvest table. */
		if (ih->version <= 2)
			out.harvest = (uint8)(p->flags & 0x0f);
		else
			out.harvest = lookup_harvest(b, h->binary_size,
				harvestInfo, p->hw_id, p->instance);

		if (ih->base_addr_64_bit) {
			const uint64* base = (const uint64*)(b + off + sizeof(ip_common));
			for (uint32 k = 0; k < n; k++)
				out.base[k] = base[k];
		} else {
			const uint32* base = (const uint32*)(b + off + sizeof(ip_common));
			for (uint32 k = 0; k < n; k++)
				out.base[k] = base[k];
		}
		out.reserved = 0;
		off += bytes;
	}
	}

	d.discovery.valid = 1;
	return B_OK;
}

static status_t
scan_region(rdna4_device& d, size_t start, size_t length)
{
	if (start >= d.fb_size)
		return B_ENTRY_NOT_FOUND;
	if (length > d.fb_size - start)
		length = d.fb_size - start;

	for (size_t off = start; off + sizeof(binary_header) <= start + length;
		off += 256) {
		const binary_header* h = (const binary_header*)(d.framebuffer + off);
		if (h->signature != 0x28211407u)
			continue;
		if (h->binary_size == 0 || h->binary_size > (4u << 20)
			|| off + h->binary_size > d.fb_size)
			continue;
		if (rdna4_discovery_parse(d, d.framebuffer + off, h->binary_size) == B_OK)
			return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}

status_t
rdna4_discovery_scan_vram(rdna4_device& d)
{
	if (d.framebuffer == NULL || d.fb_size < 4096)
		return B_NO_INIT;

	/* AMDGPU publishes the discovery TMR VRAM offset/size through the
	 * driver-scratch registers. Prefer that exact location when it lies
	 * in the BAR0-visible aperture, then retain the legacy top-of-VRAM
	 * scan as a fallback. */
	if (d.mmio != NULL) {
		uint32 scratchLo = *(volatile uint32*)(d.mmio + (0x94u << 2));
		uint32 scratchHi = *(volatile uint32*)(d.mmio + (0x95u << 2));
		uint32 scratchSize = *(volatile uint32*)(d.mmio + (0x96u << 2));
		uint64 tmrOffset = ((uint64)scratchHi << 32) | scratchLo;
		if (scratchSize != 0 && tmrOffset < d.fb_size) {
			size_t length = scratchSize;
			if (length > d.fb_size - (size_t)tmrOffset)
				length = d.fb_size - (size_t)tmrOffset;
			if (scan_region(d, (size_t)tmrOffset, length) == B_OK)
				return B_OK;
		}
	}

	size_t scan = d.fb_size < (16u << 20) ? d.fb_size : (16u << 20);
	size_t start = d.fb_size - scan;
	return scan_region(d, start, scan);
}

status_t
rdna4_discovery_init(rdna4_device& d)
{
	memset(&d.discovery, 0, sizeof(d.discovery));
	return rdna4_discovery_scan_vram(d);
}

const rdna4_discovery_state*
rdna4_discovery_get(const rdna4_device& d)
{
	return &d.discovery;
}
