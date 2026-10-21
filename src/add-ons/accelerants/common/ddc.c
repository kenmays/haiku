/*
 * Copyright 2003, Thomas Kurschel. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/*!	DDC communication */


#include "ddc_int.h"
#include "ddc.h"
#include "i2c.h"

#include <KernelExport.h>
#include <OS.h>

#include <stdlib.h>
#include <string.h>


#define READ_RETRIES 4		// number of retries to read ddc data

#define TRACE_DDC
#ifdef TRACE_DDC
extern void _sPrintf(const char* format, ...);
#	define TRACE(x...) _sPrintf("DDC: " x)
#else
#	define TRACE(x...) ;
#endif


//! Verify checksum of DDC data.
static status_t
verify_checksum(const uint8 *data, size_t len)
{
	uint32 index;
	uint8 sum = 0;
	uint8 allOr = 0;

	for (index = 0; index < len; ++index, ++data) {
		sum += *data;
		allOr |= *data;
	}

	if (allOr == 0) {
		TRACE("%s: DDC information contains zeros only\n", __func__);
		return B_ERROR;
	}

	if (sum != 0) {
		TRACE("%s: Checksum error in DDC information\n", __func__);
		return B_IO_ERROR;
	}

	return B_OK;
}


static status_t
call_send_receive(const i2c_bus *bus, int slave_address,
	const uint8 *writeBuffer, size_t writeLength, uint8 *readBuffer,
	size_t readLength)
{
	i2c_send_receive send_receive = bus->send_receive;
	if (send_receive == NULL)
		send_receive = i2c_send_receive_callback;
	return send_receive(bus, slave_address, writeBuffer, writeLength, readBuffer,
		readLength);
}


//!	Read ddc2 data from monitor
static status_t
ddc2_read(const i2c_bus *bus, int start, uint8 *buffer, size_t length)
{
	status_t status = B_OK;
	uint8 segment;
	uint8 offset;
	int i;

	if (start < 0)
		return B_BAD_VALUE;

	// E-DDC addresses blocks beyond the first two using the segment
	// pointer at 0x30 and an 8-bit offset at the EDID slave 0x50.
	segment = (uint8)(start >> 8);
	offset = (uint8)(start & 0xff);

	for (i = 0; i < READ_RETRIES; ++i) {
		if (start >= 0x100) {
			status = call_send_receive(bus, 0x30, &segment, 1, NULL, 0);
			if (status != B_OK) {
				TRACE("%s: E-DDC segment selection failed\n", __func__);
				continue;
			}
		}

		status = call_send_receive(bus, 0x50, &offset, 1, buffer, length);
		if (status != B_OK) {
			TRACE("%s: DDC information read failure\n", __func__);
			continue;
		}

		status = verify_checksum(buffer, length);
		if (status == B_OK)
			break;

		dprintf("%s: DDC checksum incorrect!\n", __func__);
	}

	return status;
}

/*!
	Reading VDIF has not been tested.
	it seems that almost noone supports VDIF which makes testing hard,
	but what's the point anyway?
*/
#if 0
static status_t
ddc2_read_vdif(const i2c_bus *bus, int start,
	void **vdif, size_t *vdif_len)
{
	status_t res;
	uint8 *data, *cur_data;
	int i;
	uint8 buffer[64];

	*vdif = NULL;
	*vdif_len = 0;

	res = ddc2_read(bus, start, buffer, 64);
	SHOW_INFO(2, "%x", buffer[0]);
	if (res != B_OK || buffer[0] == 0)
		return B_OK;

	// each block is 63 bytes plus 1 checksum long
	// we strip the checksum but store data directly into
	// buffer, so we need an extra byte for checksum of the last block
	data = malloc(buffer[0] * 63 + 1);
	if (data == NULL)
		return B_NO_MEMORY;

	cur_data = data;
	for (i = 0; i < buffer[0]; ++i) {
		ddc2_read(bus, start + i * 64, cur_data, 64);
		// strip checksum byte
		cur_data += 63;
	}

	*vdif_len = buffer[0] * 63;
	*vdif = data;
	return B_OK;
}
#endif


//	#pragma mark -


void
ddc2_init_timing(i2c_bus *bus)
{
	bus->send_receive = NULL;

	i2c_get100k_timing(&bus->timing);

	// VESA standard
	bus->timing.start_timeout = 550;
	bus->timing.byte_timeout = 2200;
	bus->timing.bit_timeout = 40;
	bus->timing.ack_start_timeout = 40;
	bus->timing.ack_timeout = 40;
}


//! Read EDID and VDIF from monitor via ddc2
static status_t
validate_edid_extension(const edid1_raw *raw)
{
	const uint8 *data = (const uint8 *)raw;

	// CTA-861 extension: validate the detailed-timing offset and ensure
	// every data-block header and payload fits inside the collection.
	if (data[0] == 0x02) {
		uint8 offset = data[2];
		if (offset != 0 && (offset < 4 || offset > 127))
			return B_BAD_DATA;

		{
			size_t dataEnd = offset == 0 ? 127 : offset;
			if (dataEnd >= 4) {
				for (size_t i = 4; i < dataEnd;) {
					size_t blockLength = (data[i] & 0x1f) + 1;
					if (i + blockLength > dataEnd)
						return B_BAD_DATA;
					i += blockLength;
				}
			}
		}
	} else if (data[0] == 0x70) {
		// DisplayID's payload length follows its 3-byte tag/version/length
		// header. Keep it within the extension before the checksum byte.
		if (data[2] > 122)
			return B_BAD_DATA;
	}

	return B_OK;
}


status_t
ddc2_read_edid1(const i2c_bus *bus, edid1_info *edid,
	void **vdif, size_t *vdifLength)
{
	edid1_raw *raw;
	size_t rawLength;
	status_t status;

	if (bus == NULL || edid == NULL)
		return B_BAD_VALUE;

	// Make optional outputs deterministic on every failure path as well as
	// on success. Callers must not observe stale VDIF data after a bad EDID.
	if (vdif != NULL)
		*vdif = NULL;
	if (vdifLength != NULL)
		*vdifLength = 0;

	raw = (edid1_raw *)malloc(sizeof(edid1_raw));
	if (raw == NULL)
		return B_NO_MEMORY;

	status = ddc2_read(bus, 0, (uint8 *)raw, sizeof(edid1_raw));
	if (status != B_OK) {
		free(raw);
		return status;
	}

	static const uint8 kEdidHeader[8] = {0x00, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0x00};
	if (memcmp(raw->header.pad, kEdidHeader, sizeof(kEdidHeader)) != 0) {
		TRACE("%s: invalid EDID header\n", __func__);
		free(raw);
		return B_BAD_DATA;
	}

	if (raw->version.version != 1 || raw->version.revision > 4) {
		TRACE("%s: EDID version or revision out of range\n", __func__);
		free(raw);
		return B_BAD_DATA;
	}

	// edid_decode() walks extension blocks following the base block.
	// Allocate and validate every declared block before calling it.
	rawLength = sizeof(edid1_raw) * ((size_t)raw->num_sections + 1);
	if (rawLength > sizeof(edid1_raw)) {
		edid1_raw *extendedRaw = (edid1_raw *)realloc(raw, rawLength);
		if (extendedRaw == NULL) {
			free(raw);
			return B_NO_MEMORY;
		}
		raw = extendedRaw;

		for (uint32 i = 1; i <= raw[0].num_sections; i++) {
			status = ddc2_read(bus, (int)(i * sizeof(edid1_raw)),
				(uint8 *)&raw[i], sizeof(edid1_raw));
			if (status == B_OK)
				status = validate_edid_extension(&raw[i]);
			if (status != B_OK) {
				TRACE("%s: invalid EDID extension %u\n", __func__, i);
				free(raw);
				return status;
			}
		}
	}

	edid_decode(edid, raw);
	free(raw);

	if (vdif != NULL)
		*vdif = NULL;
	if (vdifLength != NULL)
		*vdifLength = 0;

	// Skip VDIF until its reader has been tested.
	return B_OK;
}
