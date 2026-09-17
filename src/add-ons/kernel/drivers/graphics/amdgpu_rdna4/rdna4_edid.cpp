#include "rdna4_edid.h"

static uint8
edid_checksum(const uint8* data)
{
	uint8 sum = 0;
	for (uint32 i = 0; i < 128; i++)
		sum = (uint8)(sum + data[i]);
	return sum;
}

status_t
rdna4_edid_validate(const uint8* data, size_t size)
{
	if (data == NULL || size < 128)
		return B_BAD_VALUE;

	static const uint8 kHeader[8] = {0x00, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0x00};
	for (uint32 i = 0; i < 8; i++) {
		if (data[i] != kHeader[i])
			return B_BAD_VALUE;
	}
	if (edid_checksum(data) != 0)
		return B_BAD_VALUE;
	return B_OK;
}

status_t
rdna4_edid_dimensions(const uint8* data, size_t size,
	uint32* _width, uint32* _height)
{
	if (_width == NULL || _height == NULL)
		return B_BAD_VALUE;
	status_t status = rdna4_edid_validate(data, size);
	if (status != B_OK)
		return status;

	uint32 width = (uint32)data[56] | ((uint32)(data[58] & 0xf0) << 4);
	uint32 height = (uint32)data[59] | ((uint32)(data[61] & 0xf0) << 4);
	if (width == 0 || height == 0)
		return B_BAD_VALUE;
	*_width = width;
	*_height = height;
	return B_OK;
}
