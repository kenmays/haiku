#include "rdna4_recovery.h"
#include "driver.h"
#include "rdna4_gfx.h"
#include "rdna4_gfxhub.h"
#include "rdna4_vm.h"
#include "rdna4_sdma.h"
#include "rdna4_mes.h"
#include "rdna4_vcn.h"
#include "rdna4_irq.h"
#include "rdna4_fw.h"
#include "rdna4_psp.h"
#include "rdna4_mmhub.h"
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>

static status_t do_flr(rdna4_device& d)
{
	uint8 cap = (uint8)gPCI->read_pci_config(d.pci->bus, d.pci->device,
		d.pci->function, PCI_capabilities_ptr, 1);
	for (uint32 i = 0; i < 48 && cap >= 0x40; i++) {
		uint8 id = (uint8)gPCI->read_pci_config(d.pci->bus, d.pci->device,
			d.pci->function, cap, 1);
		uint8 next = (uint8)gPCI->read_pci_config(d.pci->bus, d.pci->device,
			d.pci->function, cap + 1, 1);
		if (id == PCI_cap_id_pcie) {
			uint16 control = (uint16)gPCI->read_pci_config(d.pci->bus,
				d.pci->device, d.pci->function, cap + 8, 2);
			control |= 1u << 15;
			gPCI->write_pci_config(d.pci->bus, d.pci->device,
				d.pci->function, cap + 8, 2, control);
			snooze(100000);
			return B_OK;
		}
		if (next == 0 || next == cap)
			break;
		cap = next;
	}
	return B_NOT_SUPPORTED;
}

status_t rdna4_gpu_recover(rdna4_device& d)
{
	if (d.shared == NULL || d.mmio == NULL)
		return B_NO_INIT;

	d.shared->gfx_state = RDNA4_ENGINE_RESETTING;
	d.shared->vm_state = RDNA4_ENGINE_RESETTING;
	d.shared->sdma_state = RDNA4_ENGINE_RESETTING;
	d.shared->mes_state = RDNA4_ENGINE_RESETTING;
	d.shared->psp_state = RDNA4_ENGINE_RESETTING;

	/* Quiesce every engine and release all GPUVM-backed allocations before
	 * tearing down the address space. This is the critical ordering for a
	 * reset: no stale GPU virtual address may survive the FLR. */
	rdna4_vcn_uninit(d);
	rdna4_mes_uninit(d);
	rdna4_sdma_uninit(d);
	rdna4_irq_uninit(d);
	rdna4_psp_uninit(d);
	rdna4_gfx_ring_free(d);
	rdna4_mmhub_uninit(d);
	if (d.gfxhub_ready)
		rdna4_gfxhub_uninit(d);
	d.gfxhub_ready = false;
	rdna4_vm_uninit(d);
	d.vm_ready = false;
	for (uint32 i = 0; i < RDNA4_FW_MAX; i++) {
		d.firmware[i].gpu = 0;
		d.firmware[i].payload_gpu = 0;
		d.firmware[i].psp_loaded_gpu = 0;
	}

	status_t status = do_flr(d);
	if (status != B_OK) {
		d.shared->gfx_state = RDNA4_ENGINE_FAILED;
		d.shared->vm_state = RDNA4_ENGINE_FAILED;
		d.shared->psp_state = RDNA4_ENGINE_FAILED;
		return status;
	}

	/* FLR clears PCI command state on some firmware revisions. */
	uint16 command = (uint16)gPCI->read_pci_config(d.pci->bus, d.pci->device,
		d.pci->function, PCI_command, 2);
	command |= PCI_command_memory | PCI_command_master;
	gPCI->write_pci_config(d.pci->bus, d.pci->device, d.pci->function,
		PCI_command, 2, command);

	status = rdna4_vm_init(d);
	if (status != B_OK)
		goto failed;
	status = rdna4_firmware_remap(d);
	if (status != B_OK)
		goto failed;
	status = rdna4_gfxhub_init(d);
	if (status != B_OK)
		goto failed;
	d.gfxhub_ready = true;
	status = rdna4_mmhub_init(d);
	if (status != B_OK)
		goto failed;

	/* PSP SOS must be alive before any authenticated IP image is submitted. */
	status = rdna4_firmware_boot(d);
	if (status != B_OK)
		goto failed;
	rdna4_psp_init(d);
	status = rdna4_psp_load_firmware(d);
	if (status != B_OK)
		goto failed;

	status = rdna4_irq_init(d);
	if (status != B_OK)
		goto failed;

	status = rdna4_gfx_ring_alloc(d);
	if (status != B_OK)
		goto failed;
	status = rdna4_gfx_program_ring(d);
	if (status != B_OK)
		goto failed;

	status = rdna4_sdma_init(d);
	if (status != B_OK)
		goto failed;
	status = rdna4_sdma_start(d);
	if (status != B_OK)
		goto failed;

	status = rdna4_mes_init(d);
	if (status != B_OK)
		goto failed;
	status = rdna4_mes_start(d);
	if (status != B_OK)
		goto failed;

	status = rdna4_vcn_init(d);
	if (status != B_OK)
		goto failed;
	status = rdna4_vcn_start(d);
	if (status != B_OK)
		goto failed;

	d.shared->gpu_reset_generation++;
	d.shared->vm_state = RDNA4_ENGINE_VM_READY;
	d.shared->gfx_state = RDNA4_ENGINE_RUNNING;
	d.shared->sdma_state = RDNA4_ENGINE_RUNNING;
	d.shared->mes_state = RDNA4_ENGINE_RUNNING;
	d.shared->psp_state = RDNA4_ENGINE_RUNNING;
	return B_OK;

failed:
	rdna4_vcn_uninit(d);
	rdna4_mes_uninit(d);
	rdna4_sdma_uninit(d);
	rdna4_irq_uninit(d);
	rdna4_psp_uninit(d);
	rdna4_gfx_ring_free(d);
	rdna4_mmhub_uninit(d);
	if (d.gfxhub_ready) {
		rdna4_gfxhub_uninit(d);
		d.gfxhub_ready = false;
	}
	rdna4_vm_uninit(d);
	d.vm_ready = false;
	d.shared->gfx_state = RDNA4_ENGINE_FAILED;
	d.shared->vm_state = RDNA4_ENGINE_FAILED;
	d.shared->sdma_state = RDNA4_ENGINE_FAILED;
	d.shared->mes_state = RDNA4_ENGINE_FAILED;
	d.shared->psp_state = RDNA4_ENGINE_FAILED;
	return status;
}

