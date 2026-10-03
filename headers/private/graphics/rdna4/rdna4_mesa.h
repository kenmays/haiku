#ifndef RDNA4_MESA_H
#define RDNA4_MESA_H

#include "rdna4.h"

/*
 * Minimal native Mesa contract. Mesa must not include Linux DRM headers on
 * Haiku. The winsys opens graphics/rdna4_* and uses these private ioctls.
 *
 * GPU virtual addresses are opaque. In particular, physical_address returned
 * by RDNA4_ALLOCATE_BUFFER is for diagnostics/staging only and must never be
 * submitted directly to GFX12.
 */
struct rdna4_mesa_device {
	int fd;
	rdna4_gpu_info info;
	rdna4_vm_info vm;
};

struct rdna4_mesa_bo {
	area_id area;
	uint64 size;
	uint64 gpu_address;
	uint32 flags;
};

static inline status_t
rdna4_mesa_query(int fd, rdna4_gpu_info* info)
{
	return ioctl(fd, RDNA4_GET_GPU_INFO, info, sizeof(*info));
}

static inline status_t
rdna4_mesa_query_vm(int fd, rdna4_vm_info* vm)
{
	return ioctl(fd, RDNA4_GET_VM_INFO, vm, sizeof(*vm));
}

static inline status_t
rdna4_mesa_query_firmware(int fd, rdna4_firmware_info* firmware)
{
	return ioctl(fd, RDNA4_GET_FIRMWARE_INFO, firmware, sizeof(*firmware));
}

#endif
