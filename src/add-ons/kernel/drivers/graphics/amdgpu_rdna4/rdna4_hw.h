#ifndef RDNA4_HW_H
#define RDNA4_HW_H

#include <SupportDefs.h>

/*
 * Generation-specific hardware operations. Generic driver code never embeds
 * guessed register offsets. A verified GFX12/DCN4 backend supplies these
 * operations after ASIC/IP discovery and firmware validation succeeds.
 */
struct rdna4_hw_ops {
	status_t (*init)(void* context);
	status_t (*reset)(void* context);

	status_t (*submit_gfx)(void* context, const uint32* commands,
		uint32 count, uint64 sequence);
	status_t (*submit_sdma)(void* context, const uint32* commands,
		uint32 count, uint64 sequence);
	status_t (*vm_invalidate)(void* context, uint32 vmid);
	status_t (*wait_fence)(void* context, uint64 sequence, bigtime_t timeout);

	/* Interrupt/fence plumbing. The backend owns the actual IH registers. */
	status_t (*enable_interrupts)(void* context);
	status_t (*disable_interrupts)(void* context);
	status_t (*ack_interrupt)(void* context, uint32 source);

	/* Display transport and modeset plumbing. */
	status_t (*aux_transfer)(void* context, uint32 connector,
		bool write, uint32 address, void* buffer, size_t size);
	status_t (*get_edid)(void* context, uint32 connector,
		void* buffer, size_t size, size_t* _actual);
	status_t (*set_display_mode)(void* context, uint32 connector,
		const display_mode* mode, uint64 framebuffer);

	/* Multimedia and power-management firmware interfaces. */
	status_t (*vcn_submit)(void* context, const uint32* commands,
		uint32 count, uint64 sequence);
	status_t (*smu_set_power_state)(void* context, uint32 state);
};

static inline status_t
rdna4_hw_init(const rdna4_hw_ops* ops, void* context)
{
	if (ops == NULL || ops->init == NULL)
		return B_NOT_SUPPORTED;
	return ops->init(context);
}

static inline status_t
rdna4_hw_reset(const rdna4_hw_ops* ops, void* context)
{
	if (ops == NULL || ops->reset == NULL)
		return B_NOT_SUPPORTED;
	return ops->reset(context);
}

#endif
