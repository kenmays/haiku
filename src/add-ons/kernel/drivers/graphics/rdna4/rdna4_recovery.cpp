#include "rdna4_recovery.h"
#include "driver.h"
#include "rdna4_gfx.h"
#include "rdna4_gfxhub.h"
#include "rdna4_vm.h"
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
	if (d.shared == NULL)
		return B_NO_INIT;
	d.shared->gfx_state = RDNA4_ENGINE_RESETTING;
	d.shared->vm_state = RDNA4_ENGINE_RESETTING;
	rdna4_gfx_ring_free(d);
	if (d.gfxhub_ready)
		rdna4_gfxhub_uninit(d);
	d.gfxhub_ready = false;
	rdna4_vm_uninit(d);
	d.vm_ready = false;

	status_t status = do_flr(d);
	if (status != B_OK) {
		d.shared->gfx_state = RDNA4_ENGINE_FAILED;
		d.shared->vm_state = RDNA4_ENGINE_FAILED;
		return status;
	}

	status = rdna4_vm_init(d);
	if (status == B_OK)
		status = rdna4_gfxhub_init(d);
	if (status == B_OK) {
		d.gfxhub_ready = true;
		status = rdna4_gfx_ring_alloc(d);
	}

	d.shared->gpu_reset_generation++;
	if (status == B_OK) {
		d.shared->vm_state = RDNA4_ENGINE_VM_READY;
		d.shared->gfx_state = RDNA4_ENGINE_DISCOVERED;
		d.shared->mes_state = RDNA4_ENGINE_OFF;
		d.shared->sdma_state = RDNA4_ENGINE_OFF;
	} else {
		d.shared->vm_state = RDNA4_ENGINE_FAILED;
		d.shared->gfx_state = RDNA4_ENGINE_FAILED;
	}
	return status;
}
