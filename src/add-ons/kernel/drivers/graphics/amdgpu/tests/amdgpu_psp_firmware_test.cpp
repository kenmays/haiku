/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_psp_firmware.h"

#include <stdio.h>
#include <string.h>

static void
WriteLE16(uint8* data, uint16 value)
{
	data[0] = (uint8)value;
	data[1] = (uint8)(value >> 8);
}


static void
WriteLE32(uint8* data, uint32 value)
{
	data[0] = (uint8)value;
	data[1] = (uint8)(value >> 8);
	data[2] = (uint8)(value >> 16);
	data[3] = (uint8)(value >> 24);
}


static void
BuildFirmware(uint8* data, size_t size)
{
	memset(data, 0, size);
	WriteLE32(data, (uint32)size); // total file size
	WriteLE32(data + 4, 44); // common header + legacy PSP descriptor
	WriteLE16(data + 8, 1); // PSP header major
	WriteLE16(data + 10, 0); // PSP header minor
	WriteLE16(data + 12, 9); // Vega 10 MP0 IP major
	WriteLE16(data + 14, 0); // Vega 10 MP0 IP minor
	WriteLE32(data + 16, 0x10203040); // firmware version
	WriteLE32(data + 20, 128); // payload size
	WriteLE32(data + 24, 64); // payload offset
	WriteLE32(data + 28, 0); // CRC field (not authenticated here)
	WriteLE32(data + 32, 0x00010002); // SOS feature version
	WriteLE32(data + 36, 64); // SOS offset within payload
	WriteLE32(data + 40, 64); // SOS size
}


static void
BuildASDFirmware(uint8* data, size_t size)
{
	memset(data, 0, size);
	WriteLE32(data, (uint32)size);
	WriteLE32(data + 4, 32);
	WriteLE16(data + 8, 1);
	WriteLE16(data + 10, 0);
	WriteLE16(data + 12, 9);
	WriteLE16(data + 14, 0);
	WriteLE32(data + 16, 0x55667788);
	WriteLE32(data + 20, 96);
	WriteLE32(data + 24, 64);
	WriteLE32(data + 28, 0); // CRC is not authenticated by this parser.
}


static bool
Check(bool condition, const char* name)
{
	if (!condition)
		fprintf(stderr, "FAIL: %s\n", name);
	return condition;
}


int
main()
{
	bool success = true;
	uint8 firmware[192];
	BuildFirmware(firmware, sizeof(firmware));

	amdgpu_psp_firmware_info info;
	memset(&info, 0x5a, sizeof(info));
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_OK
		&& info.firmware_version == 0x10203040
		&& info.feature_version == 0x00010002
		&& info.payload_offset == 64 && info.payload_size == 128
		&& info.sys_size == 64 && info.sos_offset == 64
		&& info.sos_size == 64
		&& info.ip_version_major == 9 && info.ip_version_minor == 0,
		"valid Vega10 PSP v1.0 metadata parsed");

	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware, 43,
		&info) == B_BAD_VALUE,
		"truncated PSP header rejected");

	WriteLE32(firmware, sizeof(firmware) - 1);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
	"declared file size mismatch rejected");
	BuildFirmware(firmware, sizeof(firmware));

	WriteLE16(firmware + 8, 2);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
		"unsupported PSP header version rejected");
	BuildFirmware(firmware, sizeof(firmware));

	WriteLE16(firmware + 12, 11);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
		"non-Vega10 MP0 IP version rejected");
	BuildFirmware(firmware, sizeof(firmware));

	WriteLE32(firmware + 24, 180);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
		"payload offset beyond file rejected");
	BuildFirmware(firmware, sizeof(firmware));

	WriteLE32(firmware + 20, 1000);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
		"payload size overflow rejected");
	BuildFirmware(firmware, sizeof(firmware));

	WriteLE32(firmware + 36, 120);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
		"SOS segment offset outside payload rejected");
	BuildFirmware(firmware, sizeof(firmware));

	WriteLE32(firmware + 40, 100);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
		"SOS segment extending beyond payload rejected");
	BuildFirmware(firmware, sizeof(firmware));

	WriteLE32(firmware + 4, 32);
	success &= Check(amdgpu_parse_vega10_psp_firmware(firmware,
		sizeof(firmware), &info) == B_BAD_DATA,
		"header size shorter than PSP v1.0 structure rejected");

	/*
	 * ASD is a separate common-header image with one contiguous payload.
	 * It must not be interpreted as the SOS SYS/SOS legacy descriptor.
	 */
	uint8 asdFirmware[160];
	BuildASDFirmware(asdFirmware, sizeof(asdFirmware));
	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware,
		sizeof(asdFirmware), &info) == B_OK
		&& info.firmware_version == 0x55667788
		&& info.payload_offset == 64 && info.payload_size == 96
		&& info.sys_size == 0 && info.sos_size == 0
		&& info.ip_version_major == 9 && info.ip_version_minor == 0,
		"valid Vega10 ASD common-header image parsed");

	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware, 31,
		&info) == B_BAD_VALUE,
		"truncated ASD common header rejected");
	WriteLE32(asdFirmware, sizeof(asdFirmware) - 1);
	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware,
		sizeof(asdFirmware), &info) == B_BAD_DATA,
		"ASD declared file size mismatch rejected");
	BuildASDFirmware(asdFirmware, sizeof(asdFirmware));
	WriteLE32(asdFirmware + 4, 28);
	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware,
		sizeof(asdFirmware), &info) == B_BAD_DATA,
		"ASD undersized header declaration rejected");
	BuildASDFirmware(asdFirmware, sizeof(asdFirmware));
	WriteLE16(asdFirmware + 10, 1);
	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware,
		sizeof(asdFirmware), &info) == B_BAD_DATA,
		"unsupported ASD header minor version rejected");
	BuildASDFirmware(asdFirmware, sizeof(asdFirmware));
	WriteLE16(asdFirmware + 12, 10);
	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware,
		sizeof(asdFirmware), &info) == B_BAD_DATA,
		"non-Vega10 ASD IP version rejected");
	BuildASDFirmware(asdFirmware, sizeof(asdFirmware));
	WriteLE32(asdFirmware + 24, 150);
	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware,
		sizeof(asdFirmware), &info) == B_BAD_DATA,
		"ASD payload offset outside file rejected");
	BuildASDFirmware(asdFirmware, sizeof(asdFirmware));
	WriteLE32(asdFirmware + 20, 200);
	success &= Check(amdgpu_parse_vega10_asd_firmware(asdFirmware,
		sizeof(asdFirmware), &info) == B_BAD_DATA,
		"ASD payload extending beyond file rejected");

	/*
	 * The bundle exposes borrowed SYS/SOS slices from the SOS package and
	 * the independent ASD payload. The source allocations remain caller-
	 * owned and must outlive the bundle.
	 */
	uint8 bundleSOS[192];
	uint8 bundleASD[160];
	BuildFirmware(bundleSOS, sizeof(bundleSOS));
	BuildASDFirmware(bundleASD, sizeof(bundleASD));
	amdgpu_psp_firmware_bundle bundle;
	memset(&bundle, 0, sizeof(bundle));
	success &= Check(amdgpu_psp_firmware_bundle_load(&bundle, bundleSOS,
		sizeof(bundleSOS), bundleASD, sizeof(bundleASD)) == B_BAD_VALUE,
		"uninitialized PSP bundle rejected");
	amdgpu_psp_firmware_bundle_init(&bundle);
	success &= Check(amdgpu_psp_firmware_bundle_load(&bundle, bundleSOS,
		sizeof(bundleSOS), bundleASD, sizeof(bundleASD)) == B_OK
		&& bundle.loaded && bundle.sys.data == bundleSOS + 64
		&& bundle.sys.size == 64
		&& bundle.sos.data == bundleSOS + 128
		&& bundle.sos.size == 64
		&& bundle.asd.data == bundleASD + 64
		&& bundle.asd.size == 96,
		"validated bundle exposes independent SYS SOS and ASD views");
	success &= Check(amdgpu_psp_firmware_bundle_load(&bundle, bundleSOS,
		sizeof(bundleSOS), bundleASD, sizeof(bundleASD)) == B_BUSY,
		"loaded PSP bundle cannot be overwritten");
	BuildASDFirmware(bundleASD, sizeof(bundleASD));
	WriteLE32(bundleASD + 24, 159);
	success &= Check(amdgpu_psp_firmware_bundle_unload(&bundle) == B_OK
		&& !bundle.loaded && bundle.sys.data == NULL
		&& bundle.sos.data == NULL && bundle.asd.data == NULL,
		"PSP bundle unload clears all borrowed references");
	amdgpu_psp_firmware_bundle_init(&bundle);
	success &= Check(amdgpu_psp_firmware_bundle_load(&bundle, bundleSOS,
		sizeof(bundleSOS), bundleASD, sizeof(bundleASD)) == B_BAD_DATA
		&& !bundle.loaded && bundle.sys.data == NULL
		&& bundle.sos.data == NULL && bundle.asd.data == NULL,
		"invalid ASD prevents publication of a partial firmware bundle");
	success &= Check(amdgpu_psp_firmware_bundle_unload(&bundle) == B_OK,
		"empty initialized PSP bundle unload is safe");

	if (!success)
		return 1;
	printf("AMDGPU PSP firmware parser tests passed.\n");
	return 0;
}
