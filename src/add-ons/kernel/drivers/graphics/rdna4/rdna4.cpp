#include "driver.h"
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <string.h>
#include <stdlib.h>

static inline uint32 rd32(rdna4_device& d, uint32 r) { return *(volatile uint32*)(d.mmio + r); }
static inline void wr32(rdna4_device& d, uint32 r, uint32 v) { *(volatile uint32*)(d.mmio + r) = v; }

static status_t map_resources(rdna4_device& d)
{
	d.mmio_phys = d.pci->u.h0.base_registers[2] ? d.pci->u.h0.base_registers[2]
		: d.pci->u.h0.base_registers[0];
	d.mmio_size = d.pci->u.h0.base_register_sizes[2] ? d.pci->u.h0.base_register_sizes[2]
		: d.pci->u.h0.base_register_sizes[0];
	d.fb_phys = d.pci->u.h0.base_registers[0];
	d.fb_size = d.pci->u.h0.base_register_sizes[0];
	if (d.mmio_size == 0 || d.fb_size == 0)
		return B_BAD_VALUE;

	area_id a = map_physical_memory("rdna4 mmio", d.mmio_phys, d.mmio_size,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA, (void**)&d.mmio);
	if (a < 0) return a;
	d.mmio_area = a;

	a = map_physical_memory("rdna4 framebuffer", d.fb_phys, d.fb_size,
		B_ANY_KERNEL_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&d.framebuffer);
	if (a < 0) {
		delete_area(d.mmio_area); d.mmio_area = -1;
		return a;
	}
	d.framebuffer_area = a;
	return B_OK;
}

static void unmap_resources(rdna4_device& d)
{
	if (d.framebuffer_area >= 0) delete_area(d.framebuffer_area);
	if (d.mmio_area >= 0) delete_area(d.mmio_area);
	d.framebuffer_area = d.mmio_area = -1;
	d.framebuffer = d.mmio = NULL;
}

status_t rdna4_init(rdna4_device& d)
{
	status_t s = map_resources(d);
	if (s != B_OK) return s;

	/* Safe discovery only. Firmware/PSP/GFX programming is deliberately
	 * gated until the exact GFX12 IP revision has been identified. */
	d.gfx_ip = d.device_id == 0x7551 ? 0x1201 : 0x1200;

	d.shared = NULL;
	d.shared_area = create_area("rdna4 shared", (void**)&d.shared,
		B_ANY_ADDRESS, B_PAGE_SIZE, B_FULL_LOCK, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (d.shared_area < 0) {
		unmap_resources(d);
		return d.shared_area;
	}
	memset(d.shared, 0, B_PAGE_SIZE);
	d.shared->version = 1;
	d.shared->device_id = d.device_id;
	d.shared->revision = d.revision;
	d.shared->gfx_ip = d.gfx_ip;
	d.shared->feature_mask = RDNA4_FEATURE_DISPLAY | RDNA4_FEATURE_CURSOR |
		RDNA4_FEATURE_VRAM | RDNA4_FEATURE_GTT | RDNA4_FEATURE_3D;
	d.shared->registers_area = d.mmio_area;
	d.shared->framebuffer_area = d.framebuffer_area;
	d.shared->registers_phys = d.mmio_phys;
	d.shared->framebuffer_phys = d.fb_phys;
	d.shared->registers_size = d.mmio_size;
	d.shared->framebuffer_size = d.fb_size;
	return B_OK;
}

void rdna4_uninit(rdna4_device& d)
{
	if (d.shared_area >= 0) delete_area(d.shared_area);
	d.shared = NULL;
	d.shared_area = -1;
	unmap_resources(d);
}

status_t rdna4_ioctl(rdna4_device& d, uint32 op, void* buffer, size_t length)
{
	if (buffer == NULL) return B_BAD_ADDRESS;
	switch (op) {
	case B_GET_ACCELERANT_SIGNATURE:
		if (length < strlen(RDNA4_ACCELERANT_NAME) + 1) return B_BUFFER_OVERFLOW;
		return user_strlcpy((char*)buffer, RDNA4_ACCELERANT_NAME, length) < B_OK
			? B_BAD_ADDRESS : B_OK;

	case RDNA4_GET_PRIVATE_DATA: {
		if (length < sizeof(rdna4_private_data)) return B_BUFFER_OVERFLOW;
		rdna4_private_data p;
		if (user_memcpy(&p, buffer, sizeof(p)) != B_OK) return B_BAD_ADDRESS;
		if (p.magic != RDNA4_PRIVATE_DATA_MAGIC) return B_BAD_VALUE;
		p.shared_area = d.shared_area;
		return user_memcpy(buffer, &p, sizeof(p));
	}

	case RDNA4_GET_GPU_INFO: {
		if (length < sizeof(rdna4_gpu_info)) return B_BUFFER_OVERFLOW;
		rdna4_gpu_info i = {};
		i.version = 1;
		i.gfx_ip = d.gfx_ip;
		i.device_id = d.device_id;
		i.revision = d.revision;
		i.wave_size = 32;
		i.feature_mask = d.shared ? d.shared->feature_mask : 0;
		return user_memcpy(buffer, &i, sizeof(i));
	}

	default:
		return B_DEV_INVALID_IOCTL;
	}
}
