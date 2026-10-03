#include "rdna4_gfxhub.h"
#include "driver.h"
#include "rdna4_vm.h"

#include <KernelExport.h>
#include <OS.h>

/* Engine 0 is the legacy VMID invalidation path used by AMDGPU on GFX12. */
	status_t status = flush_engine0(d, 0, 0);
	if (status != B_OK)
		return status;

	d.shared->vm_state = RDNA4_ENGINE_VM_READY;
	return B_OK;
}

void
rdna4_gfxhub_uninit(rdna4_device& d)
{
	if (d.mmio == NULL)
		return;

	uint32 context = read_reg(d, GCVM_CONTEXT0_CNTL);
	context &= ~GCVM_CONTEXT_ENABLE;
	write_reg(d, GCVM_CONTEXT0_CNTL, context);

	uint32 tlb = read_reg(d, GCMC_VM_MX_L1_TLB_CNTL);
	tlb &= ~(GCMC_VM_MX_L1_TLB_ENABLE | GCMC_VM_MX_L1_TLB_ADVANCED_MODEL);
	write_reg(d, GCMC_VM_MX_L1_TLB_CNTL, tlb);
}

status_t
rdna4_gfxhub_flush_tlb(rdna4_device& d, uint32 vmid, uint32 flushType)
{
	if (d.mmio == NULL || !d.vm_ready)
		return B_NO_INIT;
	return flush_engine0(d, vmid, flushType);
}
