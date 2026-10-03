#include "rdna4_irq.h"
#include "driver.h"
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>

static int32 rdna4_gpu_interrupt(void* cookie)
{
	rdna4_device* d = (rdna4_device*)cookie;
	if (d == NULL || d->mmio == NULL)
		return B_UNHANDLED_INTERRUPT;
	if (d->fence_sem >= 0) {
		release_sem(d->fence_sem);
		return B_INVOKE_SCHEDULER;
	}
	return B_HANDLED_INTERRUPT;
}

status_t rdna4_irq_init(rdna4_device& d)
{
	if (d.pci == NULL)
		return B_NO_INIT;
	if (d.fence_sem < 0)
		d.fence_sem = create_sem(0, "rdna4 fence irq");
	if (d.fence_sem < 0)
		return d.fence_sem;
	status_t status = install_io_interrupt_handler(
		d.pci->u.h0.interrupt_line, rdna4_gpu_interrupt, &d, 0);
	if (status != B_OK) {
		delete_sem(d.fence_sem);
		d.fence_sem = -1;
		return status;
	}
	d.irq_installed = true;
	return B_OK;
}

void rdna4_irq_uninit(rdna4_device& d)
{
	if (d.irq_installed && d.pci != NULL) {
		remove_io_interrupt_handler(d.pci->u.h0.interrupt_line,
			rdna4_gpu_interrupt, &d);
		d.irq_installed = false;
	}
	if (d.fence_sem >= 0) {
		delete_sem(d.fence_sem);
		d.fence_sem = -1;
	}
}

status_t rdna4_wait_fence_event(rdna4_device& d, bigtime_t timeout)
{
	if (d.fence_sem < 0)
		return B_NO_INIT;
	return acquire_sem_etc(d.fence_sem, 1, B_RELATIVE_TIMEOUT, timeout);
}
