#include "rdna4_frontend_edid.h"

#include <string.h>

static bool
checksum_ok(const uint8* block)
{
	uint8 sum = 0;
	for (uint32 i = 0; i < 128; i++)
		sum = (uint8)(sum + block[i]);
	return sum == 0;
}

static uint16
le16(const uint8* p)
{
	return (uint16)p[0] | ((uint16)p[1] << 8);
}

static void
add_mode(const display_mode& mode, bool preferred,
	rdna4_edid_mode_info* modes, uint32 capacity, uint32& count)
{
	if (count >= capacity)
		return;
	for (uint32 i = 0; i < count; i++) {
		if (modes[i].mode.virtual_width == mode.virtual_width
			&& modes[i].mode.virtual_height == mode.virtual_height
			&& modes[i].mode.timing.pixel_clock == mode.timing.pixel_clock)
			return;
	}
	modes[count].mode = mode;
	modes[count].preferred = preferred;
	count++;
}

status_t
rdna4_frontend_parse_edid(const uint8* data, size_t size,
	rdna4_edid_mode_info* modes, uint32 capacity, uint32* _count)
{
	if (data == NULL || modes == NULL || _count == NULL || capacity == 0)
		return B_BAD_VALUE;
	if (size < 128 || memcmp(data, "\x00\xff\xff\xff\xff\xff\xff\x00", 8) != 0)
		return B_BAD_VALUE;
	if (!checksum_ok(data))
		return B_BAD_VALUE;

	uint32 count = 0;
	for (uint32 i = 0; i < 4; i++) {
		const uint8* d = data + 54 + i * 18;
		uint16 pixel10kHz = le16(d);
		if (pixel10kHz == 0)
			continue;

		uint16 hActive = (uint16)d[2] | ((uint16)(d[4] & 0xf0) << 4);
		uint16 hBlank = (uint16)d[3] | ((uint16)(d[4] & 0x0f) << 8);
		uint16 vActive = (uint16)d[5] | ((uint16)(d[7] & 0xf0) << 4);
		uint16 vBlank = (uint16)d[6] | ((uint16)(d[7] & 0x0f) << 8);
		uint16 hSyncOffset = (uint16)d[8] | ((uint16)(d[11] & 0xc0) << 2);
		uint16 hSyncWidth = (uint16)d[9] | ((uint16)(d[11] & 0x30) << 4);
		uint16 vSyncOffset = (uint16)((d[10] >> 4) & 0x0f)
			| ((uint16)(d[11] & 0x0c) << 2);
		uint16 vSyncWidth = (uint16)(d[10] & 0x0f)
			| ((uint16)(d[11] & 0x03) << 4);

		if (hActive == 0 || vActive == 0 || hBlank == 0 || vBlank == 0)
			continue;

		display_mode mode;
		memset(&mode, 0, sizeof(mode));
		mode.virtual_width = hActive;
		mode.virtual_height = vActive;
		mode.space = B_RGB32_LITTLE;
		mode.timing.pixel_clock = (uint32)pixel10kHz * 10;
		mode.timing.h_display = hActive;
		mode.timing.h_sync_start = hActive + hSyncOffset;
		mode.timing.h_sync_end = mode.timing.h_sync_start + hSyncWidth;
		mode.timing.h_total = hActive + hBlank;
		mode.timing.v_display = vActive;
		mode.timing.v_sync_start = vActive + vSyncOffset;
		mode.timing.v_sync_end = mode.timing.v_sync_start + vSyncWidth;
		mode.timing.v_total = vActive + vBlank;
		add_mode(mode, i == 0, modes, capacity, count);
	}

	*_count = count;
	return count != 0 ? B_OK : B_ENTRY_NOT_FOUND;
}
