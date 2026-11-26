/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include <ByteOrder.h>
#include <stdio.h>

#include "../amdgpu_vega10_probe.h"


int
main()
{
	static uint32 mmio[AMDGPU_VEGA10_MMIO_MIN_SIZE / sizeof(uint32)];
	amdgpu_register_aperture aperture = {};
	aperture.bar = AMDGPU_VEGA10_REGISTER_BAR;
	aperture.offset = 0;
	aperture.size = sizeof(mmio);
	aperture.address = (volatile uint8*)mmio;

	/* GC base 0x2000 + mmGB_ADDR_CONFIG 0x063e, in dword units. */
	static const uint32 kGBAddrConfigByteOffset = 0x98f8;
	mmio[kGBAddrConfigByteOffset / sizeof(uint32)]
		= B_HOST_TO_LENDIAN_INT32(0x12345678);

	uint32 value = 0;
	status_t status = amdgpu_vega10_read_gb_addr_config(&aperture, &value);
	if (status != B_OK || value != 0x12345678) {
		fprintf(stderr, "FAIL: read-only GB_ADDR_CONFIG probe (%ld, 0x%08lx)\n",
			(long)status, (unsigned long)value);
		return 1;
	}

	if (amdgpu_vega10_read_gb_addr_config(&aperture, NULL) != B_BAD_VALUE) {
		fprintf(stderr, "FAIL: null output rejected\n");
		return 1;
	}

	aperture.bar = 2;
	if (amdgpu_vega10_read_gb_addr_config(&aperture, &value)
		!= B_BAD_DATA) {
		fprintf(stderr, "FAIL: non-BAR5 aperture rejected\n");
		return 1;
	}

	printf("AMDGPU Vega 10 read-only probe tests passed.\n");
	return 0;
}
