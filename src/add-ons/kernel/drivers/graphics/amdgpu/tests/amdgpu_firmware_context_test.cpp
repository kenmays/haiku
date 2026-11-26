/*
 * Copyright 2026, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#include "../amdgpu_firmware_context.h"
#include "../amdgpu_vfct_provider.h"

#include <stdio.h>
#include <string.h>

static bool sMismatchIdentity = false;
static const size_t kTestImageSize = 512;
static size_t sLoadedImageSize = kTestImageSize;
static status_t sQueryStatus = B_BUFFER_OVERFLOW;
static uint16 sRomDeviceId = 0x73bf;
static bool sCorruptRom = false;


static void
BuildTestRom(void* buffer, size_t size)
{
	uint8* image = (uint8*)buffer;
	memset(image, 0, size);
	image[0] = 0x55;
	image[1] = 0xaa;
	image[0x18] = 0x20;
	memcpy(image + 0x20, "PCIR", 4);
	image[0x24] = 0x02; // AMD vendor ID
	image[0x25] = 0x10;
	image[0x26] = (uint8)sRomDeviceId;
	image[0x27] = (uint8)(sRomDeviceId >> 8);
	image[0x2a] = 0x16; // PCIR structure length
	image[0x30] = 0x01; // one 512-byte image
	image[0x34] = 0x00; // x86 option ROM image
	image[0x35] = 0x80; // last image in chain
	image[0x48] = 0x40; // ATOM header offset
	image[0x40] = 34; // ATOM common header size
	memcpy(image + 0x44, "ATOM", 4);

	uint8 checksum = 0;
	for (size_t i = 0; i < size; i++)
		checksum = (uint8)(checksum + image[i]);
	image[size - 1] = (uint8)(0 - checksum);
	if (sCorruptRom)
		image[100] ^= 0x01;
}


status_t
amdgpu_vfct_device_from_pci_device(const amdgpu_pci_device* pciDevice,
	amdgpu_vfct_device* vfctDevice)
{
	if (pciDevice == NULL || vfctDevice == NULL
		|| pciDevice->pci.vendor_id != 0x1002
		|| (pciDevice->identification_flags
			& (AMDGPU_PCI_ID_EXACT | AMDGPU_PCI_ID_IP_DISCOVERY)) == 0) {
		return B_BAD_VALUE;
	}
	vfctDevice->bus = pciDevice->pci.bus;
	vfctDevice->device = pciDevice->pci.device;
	vfctDevice->function = pciDevice->pci.function;
	vfctDevice->vendor_id = pciDevice->pci.vendor_id;
	vfctDevice->device_id = pciDevice->pci.device_id;
	return B_OK;
}


static void
FillInfo(const amdgpu_pci_device* device, amdgpu_vfct_info* info)
{
	memset(info, 0, sizeof(*info));
	info->vendor_id = sMismatchIdentity ? 0x1234 : device->pci.vendor_id;
	info->device_id = device->pci.device_id;
	info->image_size = kTestImageSize;
	info->vbios.vendor_id = info->vendor_id;
	info->vbios.device_id = info->device_id;
	info->vbios.is_atom_bios = true;
}


status_t
amdgpu_query_vbios_size_for_pci_device(const amdgpu_pci_device* device,
	size_t* biosSize, amdgpu_vfct_info* info)
{
	if (device == NULL || biosSize == NULL || info == NULL)
		return B_BAD_VALUE;
	*biosSize = kTestImageSize;
	FillInfo(device, info);
	return sQueryStatus;
}


status_t
amdgpu_load_vbios_for_pci_device(const amdgpu_pci_device* device,
	void* biosBuffer, size_t biosCapacity, size_t* biosSize,
	amdgpu_vfct_info* info)
{
	if (device == NULL || biosBuffer == NULL || biosSize == NULL
		|| info == NULL)
		return B_BAD_VALUE;
	if (biosCapacity < sLoadedImageSize)
		return B_BUFFER_OVERFLOW;
	memset(biosBuffer, 0xa5, sLoadedImageSize);
	if (sLoadedImageSize == kTestImageSize)
		BuildTestRom(biosBuffer, sLoadedImageSize);
	*biosSize = sLoadedImageSize;
	FillInfo(device, info);
	info->image_size = sLoadedImageSize;
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
	amdgpu_pci_device device;
	memset(&device, 0, sizeof(device));
	device.pci.vendor_id = 0x1002;
	device.pci.device_id = 0x73bf;
	device.pci.bus = 2;
	device.pci.device = 3;
	device.pci.function = 1;
	device.identification_flags = AMDGPU_PCI_ID_EXACT;

	amdgpu_firmware_context context;
	memset(&context, 0, sizeof(context));
	success &= Check(amdgpu_firmware_context_query(&context) == B_BAD_VALUE,
		"uninitialized context rejected");
	success &= Check(amdgpu_firmware_context_init(&context, &device) == B_OK
		&& context.state == AMDGPU_FIRMWARE_DEVICE_IDENTIFIED,
		"candidate initializes per-device context");
	success &= Check(amdgpu_firmware_context_query(&context) == B_OK
		&& context.state == AMDGPU_FIRMWARE_SIZE_KNOWN
		&& context.required_size == kTestImageSize,
		"size query advances context state");

	sQueryStatus = B_ENTRY_NOT_FOUND;
	success &= Check(amdgpu_firmware_context_query(&context)
		== B_ENTRY_NOT_FOUND
		&& context.state == AMDGPU_FIRMWARE_DEVICE_IDENTIFIED
		&& context.required_size == 0,
		"failed size refresh invalidates stale query state");
	sQueryStatus = B_BUFFER_OVERFLOW;
	success &= Check(amdgpu_firmware_context_query(&context) == B_OK,
		"firmware context can recover after failed size refresh");

	uint8 tooSmall[32];
	success &= Check(amdgpu_firmware_context_load(&context, tooSmall,
		sizeof(tooSmall)) == B_BUFFER_OVERFLOW
		&& context.state == AMDGPU_FIRMWARE_SIZE_KNOWN,
		"undersized destination does not advance context");

	uint8 image[kTestImageSize];
	memset(image, 0, sizeof(image));
	success &= Check(amdgpu_firmware_context_load(&context, image,
		sizeof(image)) == B_OK
		&& context.state == AMDGPU_FIRMWARE_LOADED
		&& image[0] == 0x55 && context.info.vbios.is_atom_bios
		&& context.info.vbios.device_id == device.pci.device_id,
		"validated image copy advances context to loaded");
	success &= Check(amdgpu_firmware_context_query(&context) == B_BUSY,
		"loaded context cannot be queried again");


	success &= Check(amdgpu_firmware_context_reset(&context) == B_OK
		&& amdgpu_firmware_context_init(&context, &device) == B_OK
		&& amdgpu_firmware_context_query(&context) == B_OK,
		"reinitialize context for ROM validation failures");
	sCorruptRom = true;
	memset(image, 0x5a, sizeof(image));
	success &= Check(amdgpu_firmware_context_load(&context, image,
		sizeof(image)) == B_BAD_DATA
		&& context.state == AMDGPU_FIRMWARE_SIZE_KNOWN
		&& image[0] == 0 && image[kTestImageSize - 1] == 0,
		"corrupt option-ROM checksum rejected and image cleared");
	sCorruptRom = false;
	sRomDeviceId = 0x67df;
	success &= Check(amdgpu_firmware_context_load(&context, image,
		sizeof(image)) == B_BAD_DATA
		&& image[0] == 0 && image[kTestImageSize - 1] == 0,
		"ATOMBIOS PCI device mismatch rejected and image cleared");
	sRomDeviceId = 0x73bf;
	success &= Check(amdgpu_firmware_context_load(&context, image,
		sizeof(image)) == B_OK,
		"valid ROM can be loaded after rejected image attempts");

	success &= Check(amdgpu_firmware_context_reset(&context) == B_OK
		&& context.magic == 0
		&& context.state == AMDGPU_FIRMWARE_UNINITIALIZED,
		"context reset clears state");

	sMismatchIdentity = true;
	success &= Check(amdgpu_firmware_context_init(&context, &device) == B_OK
		&& amdgpu_firmware_context_query(&context) == B_BAD_DATA
		&& context.state == AMDGPU_FIRMWARE_DEVICE_IDENTIFIED,
		"VFCT identity mismatch rejected during query");
	sMismatchIdentity = false;

	success &= Check(amdgpu_firmware_context_query(&context) == B_OK,
		"context can retry query after identity mismatch");
	sMismatchIdentity = true;
	memset(image, 0x5a, sizeof(image));
	success &= Check(amdgpu_firmware_context_load(&context, image,
		sizeof(image)) == B_BAD_DATA
		&& context.state == AMDGPU_FIRMWARE_SIZE_KNOWN
		&& image[0] == 0 && image[kTestImageSize - 1] == 0,
		"changed identity between query and load rejected and image cleared");
	sMismatchIdentity = false;

	success &= Check(amdgpu_firmware_context_reset(&context) == B_OK
		&& amdgpu_firmware_context_init(&context, &device) == B_OK
		&& amdgpu_firmware_context_query(&context) == B_OK,
		"reinitialize context for changed image-size test");
	uint8 largerImage[1024];
	memset(largerImage, 0x5a, sizeof(largerImage));
	sLoadedImageSize = 768;
	success &= Check(amdgpu_firmware_context_load(&context, largerImage,
		sizeof(largerImage)) == B_BAD_DATA
		&& largerImage[0] == 0 && largerImage[767] == 0
		&& largerImage[768] == 0x5a,
		"changed image size rejected and all copied bytes cleared");
	sLoadedImageSize = kTestImageSize;

	device.pci.vendor_id = 0x1234;
	success &= Check(amdgpu_firmware_context_init(&context, &device)
		== B_BAD_VALUE, "non-AMD candidate rejected");

	if (!success)
		return 1;
	printf("AMDGPU firmware context tests passed.\n");
	return 0;
}
