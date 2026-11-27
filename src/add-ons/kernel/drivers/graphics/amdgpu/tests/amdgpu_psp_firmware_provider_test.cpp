/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_psp_firmware_provider.h"

#include <stdio.h>
#include <string.h>

struct fake_provider {
	uint8 sos[192];
	uint8 asd[160];
	int loads;
	int releases;
	int fail_load_number;
	int fail_release_number;
	const void* released[4];
	const char* names[2];
};


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
BuildSOS(uint8* data, size_t size)
{
	memset(data, 0, size);
	WriteLE32(data, size);
	WriteLE32(data + 4, 44);
	WriteLE16(data + 8, 1);
	WriteLE16(data + 12, 9);
	WriteLE32(data + 16, 0x10203040);
	WriteLE32(data + 20, 128);
	WriteLE32(data + 24, 64);
	WriteLE32(data + 32, 0x00010002);
	WriteLE32(data + 36, 64);
	WriteLE32(data + 40, 64);
}


static void
BuildASD(uint8* data, size_t size)
{
	memset(data, 0, size);
	WriteLE32(data, size);
	WriteLE32(data + 4, 44);
	WriteLE16(data + 8, 1);
	WriteLE16(data + 12, 9);
	WriteLE32(data + 16, 0x55667788);
	WriteLE32(data + 20, 96);
	WriteLE32(data + 24, 64);
	WriteLE32(data + 32, 0x00030004);
}


static status_t
FakeLoad(void* cookie, const char* name, const void** data, size_t* size)
{
	fake_provider* fake = (fake_provider*)cookie;
	fake->loads++;
	if (fake->fail_load_number == fake->loads)
		return B_ENTRY_NOT_FOUND;
	if (fake->loads == 1) {
		fake->names[0] = name;
		*data = fake->sos;
		*size = sizeof(fake->sos);
	} else {
		fake->names[1] = name;
		*data = fake->asd;
		*size = sizeof(fake->asd);
	}
	return B_OK;
}


static status_t
FakeRelease(void* cookie, const void* data, size_t size)
{
	fake_provider* fake = (fake_provider*)cookie;
	fake->releases++;
	if (fake->fail_release_number == fake->releases)
		return B_ERROR;
	if (size == 0 || data == NULL)
		return B_BAD_VALUE;
	fake->released[fake->releases - 1] = data;
	return B_OK;
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
	fake_provider fake;
	memset(&fake, 0, sizeof(fake));
	BuildSOS(fake.sos, sizeof(fake.sos));
	BuildASD(fake.asd, sizeof(fake.asd));

	amdgpu_psp_firmware_provider_ops ops = { FakeLoad, FakeRelease };
	amdgpu_psp_firmware_version_policy policy = {
		0x10203040, 0x10203040, 0x00010002, 0x00010002,
		0x55667788, 0x55667788, 0x00030004, 0x00030004
	};
	amdgpu_psp_firmware_provider provider;
	amdgpu_psp_firmware_bundle bundle;
	amdgpu_psp_firmware_provider_init(&provider);
	amdgpu_psp_firmware_bundle_init(&bundle);

	success &= Check(amdgpu_psp_firmware_provider_load(&provider, &bundle,
		&ops, &fake, &policy) == B_OK && provider.ready && bundle.loaded,
		"both named firmware images load and pass version policy");
	success &= Check(strcmp(fake.names[0], "amdgpu/vega10_sos.bin") == 0
		&& strcmp(fake.names[1], "amdgpu/vega10_asd.bin") == 0,
		"provider requests upstream Vega10 PSP firmware names");
	success &= Check(amdgpu_psp_firmware_provider_unload(&provider) == B_OK
		&& fake.releases == 2 && fake.released[0] == fake.asd
		&& fake.released[1] == fake.sos && !bundle.loaded,
		"unload releases ASD then SOS and clears borrowed bundle views");

	memset(&fake, 0, sizeof(fake));
	BuildSOS(fake.sos, sizeof(fake.sos));
	BuildASD(fake.asd, sizeof(fake.asd));
	fake.fail_load_number = 2;
	amdgpu_psp_firmware_provider_init(&provider);
	amdgpu_psp_firmware_bundle_init(&bundle);
	success &= Check(amdgpu_psp_firmware_provider_load(&provider, &bundle,
		&ops, &fake, &policy) == B_ENTRY_NOT_FOUND && fake.releases == 1
		&& fake.released[0] == fake.sos && provider.bundle == NULL,
		"ASD acquisition failure releases the earlier SOS allocation");

	memset(&fake, 0, sizeof(fake));
	BuildSOS(fake.sos, sizeof(fake.sos));
	BuildASD(fake.asd, sizeof(fake.asd));
	fake.fail_release_number = 1;
	amdgpu_psp_firmware_provider_init(&provider);
	amdgpu_psp_firmware_bundle_init(&bundle);
	success &= Check(amdgpu_psp_firmware_provider_load(&provider, &bundle,
		&ops, &fake, &policy) == B_ERROR && provider.asd_acquired
		&& provider.bundle == NULL && !provider.ready,
		"failed rollback retains the ASD ownership record");
	fake.fail_release_number = 0;
	fake.releases = 0;
	success &= Check(amdgpu_psp_firmware_provider_unload(&provider) == B_OK
		&& !provider.asd_acquired && !provider.sos_acquired,
		"retry unload releases buffers retained after cleanup failure");

	memset(&fake, 0, sizeof(fake));
	BuildSOS(fake.sos, sizeof(fake.sos));
	BuildASD(fake.asd, sizeof(fake.asd));
	amdgpu_psp_firmware_provider_init(&provider);
	amdgpu_psp_firmware_bundle_init(&bundle);
	policy.asd_feature_min++;
	success &= Check(amdgpu_psp_firmware_provider_load(&provider, &bundle,
		&ops, &fake, &policy) == B_NOT_SUPPORTED && fake.releases == 2
		&& !bundle.loaded,
		"version-policy failure releases both unaccepted images");

	if (!success)
		return 1;
	printf("AMDGPU PSP firmware provider tests passed.\n");
	return 0;
}
