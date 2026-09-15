#ifndef RDNA4_HW_H
#define RDNA4_HW_H

#include <SupportDefs.h>

/*
 * Hardware operations are deliberately expressed as a narrow interface.
 * Register offsets and firmware layouts are supplied by the target ASIC
 * implementation instead of being guessed by generic code.
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
