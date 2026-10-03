#include "driver.h"
#include "rdna4_gfx.h"
#include "rdna4_vm.h"

#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <string.h>

static status_t
map_resources(rdna4_device& d)
{
	uint32 mmio = 2;
	uint32 framebuffer = 0;

	/* RDNA4 uses the standard AMD PCI memory layout: a small MMIO BAR and
	   a prefetchable framebuffer/VRAM aperture. Prefer BAR2 for MMIO and
	   BAR0 for the framebuffer aperture, but reject empty resources. */
	if (d.pci->u.h0.base_register_sizes[mmio] == 0) {
		mmio = 0;
		if (d.pci->u.h0.base_register_sizes[mmio] == 0)
			return B_BAD_VALUE;
	}
	if (d.pci->u.h0.base_register_sizes[framebuffer] == 0)
		return B_BAD_VALUE;

	d.mmio_phys = d.pci->u.h0.base_registers_pci[mmio];
	d.mmio_size = d.pci->u.h0.base_register_sizes[mmio];
	d.fb_phys = d.pci->u.h0.base_registers_pci[framebuffer];
	d.fb_size = d.pci->u.h0.base_register_sizes[framebuffer];

	d.mmio_area = map_physical_memory("rdna4 MMIO", d.mmio_phys, d.mmio_size,
		B_ANY_KERNEL_BLOCK_ADDRESS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
		(void**)&d.mmio);
	if (d.mmio_area < 0)
		return d.mmio_area;

	d.framebuffer_area = map_physical_memory("rdna4 framebuffer aperture",
		d.fb_phys, d.fb_size, B_ANY_KERNEL_BLOCK_ADDRESS,
		B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA | B_WRITE_COMBINING_MEMORY,
		(void**)&d.framebuffer);
	if (d.framebuffer_area < 0) {
		delete_area(d.mmio_area);
		d.mmio_area = -1;
		return d.framebuffer_area;
	}
	return B_OK;
}

static void
unmap_resources(rdna4_device& d)
{
	if (d.framebuffer_area >= 0)
		delete_area(d.framebuffer_area);
	if (d.mmio_area >= 0)
		delete_area(d.mmio_area);
	d.framebuffer_area = d.mmio_area = -1;
	d.framebuffer = d.mmio = NULL;
}

static void
set_pci_master(rdna4_device& d)
{
	uint16 command = (uint16)gPCI->read_pci_config(d.pci->bus, d.pci->device,
		d.pci->function, PCI_command, 2);
	command |= PCI_command_memory | PCI_command_master;
	gPCI->write_pci_config(d.pci->bus, d.pci->device, d.pci->function,
		PCI_command, 2, command);
}

static status_t
wait_for_gfx_idle(rdna4_device& d, bigtime_t timeout)
{
	if (d.mmio == NULL)
		return B_NO_INIT;

	/* GFX12 regGRBM_STATUS is register 0x0da4; Haiku MMIO access is
	   byte-addressed while AMD register tables are dword-indexed. */
	const uint32 statusOffset = 0x0da4u * 4;
	const uint32 guiActive = 0x80000000u;
	bigtime_t deadline = system_time() + timeout;

	do {
		uint32 status = *(volatile uint32*)(d.mmio + statusOffset);
		if ((status & guiActive) == 0)
			return B_OK;
		snooze(1);
	} while (timeout < 0 || system_time() < deadline);

	return B_TIMED_OUT;
}

static void
fill_firmware_info(rdna4_device& d, rdna4_firmware_info& info)
{
	memset(&info, 0, sizeof(info));
	info.gfx_ip = d.gfx_ip;
	info.psp_ip = 0x1403;
	info.smu_ip = 0x1403;
	info.sdma_ip = d.gfx_ip == RDNA4_GFX12_1 ? 0x0701 : 0x0700;
	info.mes_ip = 0x1201;
	info.flags = 1; /* names describe externally supplied firmware */

	if (d.gfx_ip == RDNA4_GFX12_1) {
		strlcpy(info.gfx_pfp, "amdgpu/gc_12_0_1_pfp.bin", sizeof(info.gfx_pfp));
		strlcpy(info.gfx_me, "amdgpu/gc_12_0_1_me.bin", sizeof(info.gfx_me));
		strlcpy(info.gfx_mec, "amdgpu/gc_12_0_1_mec.bin", sizeof(info.gfx_mec));
		strlcpy(info.gfx_rlc, "amdgpu/gc_12_0_1_rlc.bin", sizeof(info.gfx_rlc));
		strlcpy(info.gfx_toc, "amdgpu/gc_12_0_1_toc.bin", sizeof(info.gfx_toc));
		strlcpy(info.sdma0, "amdgpu/sdma_7_0_1.bin", sizeof(info.sdma0));
		strlcpy(info.sdma1, "amdgpu/sdma_7_0_1.bin", sizeof(info.sdma1));
	} else {
		strlcpy(info.gfx_pfp, "amdgpu/gc_12_0_0_pfp.bin", sizeof(info.gfx_pfp));
		strlcpy(info.gfx_me, "amdgpu/gc_12_0_0_me.bin", sizeof(info.gfx_me));
		strlcpy(info.gfx_mec, "amdgpu/gc_12_0_0_mec.bin", sizeof(info.gfx_mec));
		strlcpy(info.gfx_rlc, "amdgpu/gc_12_0_0_rlc.bin", sizeof(info.gfx_rlc));
		strlcpy(info.gfx_toc, "amdgpu/gc_12_0_0_toc.bin", sizeof(info.gfx_toc));
		strlcpy(info.sdma0, "amdgpu/sdma_7_0_0.bin", sizeof(info.sdma0));
		strlcpy(info.sdma1, "amdgpu/sdma_7_0_0.bin", sizeof(info.sdma1));
	}
	strlcpy(info.mes, "amdgpu/gc_12_1_0_mes.bin", sizeof(info.mes));
	strlcpy(info.mes1, "amdgpu/gc_12_1_0_mes1.bin", sizeof(info.mes1));
	strlcpy(info.uni_mes, "amdgpu/gc_12_1_0_uni_mes.bin",
		sizeof(info.uni_mes));
}

status_t
rdna4_init(rdna4_device& d)
{
	set_pci_master(d);

	status_t status = map_resources(d);
	if (status != B_OK)
		return status;

	d.gfx_ip = d.device_id == RDNA4_DEVICE_NAVI48_ALT
		? RDNA4_GFX12_1 : RDNA4_GFX12_0;

	d.shared = NULL;
	d.shared_area = create_area("rdna4 shared", (void**)&d.shared,
		B_ANY_ADDRESS, B_PAGE_SIZE, B_FULL_LOCK,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA | B_CLONEABLE_AREA);
	if (d.shared_area < 0) {
		unmap_resources(d);
		return d.shared_area;
	}

	memset(d.shared, 0, B_PAGE_SIZE);
	d.shared->version = 2;
	d.shared->device_id = d.device_id;
	d.shared->revision = d.revision;
	d.shared->gfx_ip = d.gfx_ip;
	d.shared->wave_size = 32;
	d.shared->registers_area = d.mmio_area;
	d.shared->framebuffer_area = d.framebuffer_area;
	d.shared->registers_phys = d.mmio_phys;
	d.shared->framebuffer_phys = d.fb_phys;
	d.shared->registers_size = d.mmio_size;
	d.shared->framebuffer_size = d.fb_size;
	d.shared->feature_mask = RDNA4_FEATURE_DISPLAY
		| RDNA4_FEATURE_CURSOR | RDNA4_FEATURE_VRAM | RDNA4_FEATURE_GTT;
	d.shared->gfx_state = RDNA4_ENGINE_DISCOVERED;
	d.shared->display_state = RDNA4_ENGINE_DISCOVERED;
	d.shared->psp_state = RDNA4_ENGINE_DISCOVERED;
	d.shared->smu_state = RDNA4_ENGINE_DISCOVERED;
	d.shared->mes_state = RDNA4_ENGINE_OFF;
	d.shared->sdma_state = RDNA4_ENGINE_OFF;
	d.shared->gpu_reset_generation = 0;

	/* Do not claim GFX/MES/SDMA/3D until firmware, GPUVM and queues are
	   actually running. B_PARALLEL_ACCESS remains valid for the mapped
	   display framebuffer only. */
	return B_OK;
}

void
rdna4_uninit(rdna4_device& d)
{
	if (d.shared_area >= 0)
		delete_area(d.shared_area);
	d.shared = NULL;
	d.shared_area = -1;
	unmap_resources(d);
}

status_t
rdna4_ioctl(rdna4_device& d, uint32 op, void* buffer, size_t length)
{
	if (buffer == NULL)
		return B_BAD_ADDRESS;

	switch (op) {
		case RDNA4_GET_PRIVATE_DATA: {
			if (length < sizeof(rdna4_private_data))
				return B_BUFFER_OVERFLOW;
			rdna4_private_data p;
			if (user_memcpy(&p, buffer, sizeof(p)) != B_OK)
				return B_BAD_ADDRESS;
			if (p.magic != RDNA4_PRIVATE_DATA_MAGIC)
				return B_BAD_VALUE;
			p.shared_area = d.shared_area;
			return user_memcpy(buffer, &p, sizeof(p));
		}

		case RDNA4_GET_GPU_INFO: {
			if (length < sizeof(rdna4_gpu_info))
				return B_BUFFER_OVERFLOW;
			rdna4_gpu_info i = {};
			i.version = 2;
			i.gfx_ip = d.gfx_ip;
			i.device_id = d.device_id;
			i.revision = d.revision;
			i.wave_size = 32;
			if (d.shared != NULL) {
				i.vram_size = d.shared->vram_size;
				i.gtt_size = d.shared->gtt_size;
				i.feature_mask = d.shared->feature_mask;
				i.gfx_state = d.shared->gfx_state;
				i.display_state = d.shared->display_state;
				i.psp_state = d.shared->psp_state;
				i.smu_state = d.shared->smu_state;
				i.mes_state = d.shared->mes_state;
				i.sdma_state = d.shared->sdma_state;
				i.reset_generation = d.shared->gpu_reset_generation;
			}
			return user_memcpy(buffer, &i, sizeof(i));
		}

		case RDNA4_GET_FIRMWARE_INFO: {
			if (length < sizeof(rdna4_firmware_info))
				return B_BUFFER_OVERFLOW;
			rdna4_firmware_info info;
			fill_firmware_info(d, info);
			return user_memcpy(buffer, &info, sizeof(info));
		}

		case RDNA4_GET_VM_INFO: {
			if (length < sizeof(rdna4_vm_info))
				return B_BUFFER_OVERFLOW;
			rdna4_vm_info info = {};
			info.version = 1;
			info.page_shift = 12;
			info.pde_levels = 4;
			info.vmid_count = 16;
			info.va_bits = 48;
			info.page_table_flags = RDNA4_PTE_VALID
				| RDNA4_PTE_SYSTEM | RDNA4_PTE_SNOOPED
				| RDNA4_PTE_EXECUTABLE | RDNA4_PTE_READABLE
				| RDNA4_PTE_WRITEABLE | RDNA4_PTE_PRT
				| RDNA4_PTE_DCC | RDNA4_PTE_BUS_ATOMICS
				| RDNA4_PTE_IS_PTE;
			return user_memcpy(buffer, &info, sizeof(info));
		}

		case RDNA4_WAIT_IDLE: {
			if (length < sizeof(rdna4_wait_idle))
				return B_BUFFER_OVERFLOW;
			rdna4_wait_idle request;
			if (user_memcpy(&request, buffer, sizeof(request)) != B_OK)
				return B_BAD_ADDRESS;
			if (request.timeout < 0 || request.timeout > 10000000)
				return B_BAD_VALUE;
			return wait_for_gfx_idle(d, request.timeout);
		}

		case RDNA4_ALLOCATE_BUFFER: {
			if (length < sizeof(rdna4_buffer_request))
				return B_BUFFER_OVERFLOW;
			rdna4_buffer_request request;
			if (user_memcpy(&request, buffer, sizeof(request)) != B_OK)
				return B_BAD_ADDRESS;
			if (request.size == 0 || request.size > (64ull << 20))
				return B_BAD_VALUE;
			if (request.magic != RDNA4_PRIVATE_DATA_MAGIC)
				return B_BAD_VALUE;

			size_t size = (size_t)((request.size + B_PAGE_SIZE - 1)
				& ~(uint64)(B_PAGE_SIZE - 1));
			void* address = NULL;
			area_id area = create_area("rdna4 buffer", &address,
				B_ANY_KERNEL_ADDRESS, size, B_CONTIGUOUS,
				B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA
					| B_CLONEABLE_AREA);
			if (area < 0)
				return area;

			physical_entry entry;
			status_t status = get_memory_map(address, size, &entry, 1);
			if (status != B_OK || entry.size < size) {
				delete_area(area);
				return status != B_OK ? status : B_NOT_SUPPORTED;
			}

			request.area = area;
			request.physical_address = entry.address;
			request.gpu_address = 0; /* assigned by GPUVM, never physical identity */
			if (user_memcpy(buffer, &request, sizeof(request)) != B_OK) {
				delete_area(area);
				return B_BAD_ADDRESS;
			}
			return B_OK;
		}

		case RDNA4_FREE_BUFFER: {
			if (length < sizeof(rdna4_buffer_request))
				return B_BUFFER_OVERFLOW;
			rdna4_buffer_request request;
			if (user_memcpy(&request, buffer, sizeof(request)) != B_OK)
				return B_BAD_ADDRESS;
			if (request.area < 0)
				return B_BAD_VALUE;
			return delete_area(request.area);
		}

		default:
			return B_DEV_INVALID_IOCTL;
	}
}
