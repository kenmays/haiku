#include "rdna4_gfxhub.h"
#include "driver.h"
#include "rdna4_vm.h"

#include <KernelExport.h>
#include <OS.h>

/* GFX12.0/12.0.1 GC register indices. */
#define GCVM_CONTEXT0_CNTL                         0x1624
#define GCVM_INVALIDATE_ENG0_SEM                   0x1635
#define GCVM_INVALIDATE_ENG0_REQ                   0x1647
#define GCVM_INVALIDATE_ENG0_ACK                   0x1659
#define GCVM_INVALIDATE_ENG17_REQ                  0x1658
#define GCVM_INVALIDATE_ENG17_ACK                  0x166a
#define GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO32      0x168d
#define GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI32      0x168e
#define GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32    0x168f
#define GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32    0x1690
#define GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32   0x16af
#define GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32   0x16b0
#define GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32     0x16cf
#define GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32     0x16d0
#define GCMC_VM_SYSTEM_APERTURE_LOW_ADDR           0x1619
#define GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR          0x161a
#define GCMC_VM_MX_L1_TLB_CNTL                     0x161b
#define GCVM_L2_CNTL                               0x15d0
#define GCVM_L2_CNTL2                              0x15d1

/*
 * GCVM_CONTEXT*_CNTL fields.
 *
 * GFX12 retains the standard AMD VM context encoding:
 *   ENABLE_CONTEXT bit 0
 *   PAGE_TABLE_DEPTH bits 1..3
 *   PAGE_TABLE_BLOCK_SIZE bits 8..12
 *   RETRY_PERMISSION_OR_INVALID_PAGE_FAULT bit 28
 */
#define GCVM_CONTEXT_ENABLE                        (1u << 0)
#define GCVM_CONTEXT_PAGE_TABLE_DEPTH_SHIFT        1
#define GCVM_CONTEXT_PAGE_TABLE_DEPTH_MASK         (7u << GCVM_CONTEXT_PAGE_TABLE_DEPTH_SHIFT)
#define GCVM_CONTEXT_PAGE_TABLE_BLOCK_SHIFT        8
#define GCVM_CONTEXT_PAGE_TABLE_BLOCK_MASK         (0x1fu << GCVM_CONTEXT_PAGE_TABLE_BLOCK_SHIFT)
#define GCVM_CONTEXT_RETRY_FAULT                   (1u << 28)

/*
 * GCVM_INVALIDATE_ENG0_REQ fields. The VMID selector occupies the low
 * sixteen bits; the remaining controls select the legacy GFX12 invalidation
 * path used by the public GFXHub implementation.
 */
#define GCVM_INV_VMID_MASK                         0x0000ffffu
#define GCVM_INV_FLUSH_TYPE_SHIFT                  16
#define GCVM_INV_FLUSH_TYPE_MASK                   (3u << GCVM_INV_FLUSH_TYPE_SHIFT)
#define GCVM_INV_L2_PTES                           (1u << 18)
#define GCVM_INV_L2_PDE0                           (1u << 19)
#define GCVM_INV_L2_PDE1                           (1u << 20)
#define GCVM_INV_L2_PDE2                           (1u << 21)
#define GCVM_INV_L1_PTES                           (1u << 22)

/* VM L1 TLB controls. */
#define GCMC_VM_MX_L1_TLB_ENABLE                   (1u << 0)
#define GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_SHIFT      1
#define GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_MASK       (3u << GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_SHIFT)
#define GCMC_VM_MX_L1_TLB_ADVANCED_MODEL           (1u << 4)

/* GFX12 L2 controls used to force an initial clean translation state. */
#define GCVM_L2_ENABLE                             (1u << 0)
#define GCVM_L2_INVALIDATE_L1_TLBS                 (1u << 0)
#define GCVM_L2_INVALIDATE_CACHE                   (1u << 1)

static inline volatile uint32*
reg_ptr(rdna4_device& d, uint32 reg)
{
	return (volatile uint32*)(d.mmio + ((size_t)reg << 2));
}

static inline void
write_reg(rdna4_device& d, uint32 reg, uint32 value)
{
	*reg_ptr(d, reg) = value;
	(void)*reg_ptr(d, reg);
}

static inline uint32
read_reg(rdna4_device& d, uint32 reg)
{
	return *reg_ptr(d, reg);
}

static status_t
flush_engine17(rdna4_device& d, uint32 vmid, uint32 flushType)
{
	if (vmid > 15)
		return B_BAD_VALUE;

	/*
	 * The public GFX12 implementation uses engine 0 for VMID-scoped
	 * invalidation and enables PTE/PDE/L1 invalidation together.
	 */
	uint32 request = (1u << vmid)
		| ((flushType << GCVM_INV_FLUSH_TYPE_SHIFT)
			& GCVM_INV_FLUSH_TYPE_MASK)
		| GCVM_INV_L2_PTES
		| GCVM_INV_L2_PDE0
		| GCVM_INV_L2_PDE1
		| GCVM_INV_L2_PDE2
		| GCVM_INV_L1_PTES;

	/*
	 * The range is programmed to the full 48-bit GPU VA space. This makes
	 * the helper suitable for both mapping and unmapping without requiring
	 * per-object invalidation state.
	 */
	write_reg(d, GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO32, 0xffffffffu);
	write_reg(d, GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI32, 0x0000001fu);

	write_reg(d, GCVM_INVALIDATE_ENG17_REQ, request);

	bigtime_t deadline = system_time() + 100000;
	for (;;) {
		uint32 ack = read_reg(d, GCVM_INVALIDATE_ENG17_ACK);
		if ((ack & (1u << vmid)) != 0)
			return B_OK;
		if (system_time() >= deadline)
			break;
		snooze(1);
	}

	return B_TIMED_OUT;
}

status_t
rdna4_gfxhub_init(rdna4_device& d)
{
	if (d.mmio == NULL || !d.vm_ready)
		return B_NO_INIT;

	/*
	 * VMID0 is the kernel/system address space. The root is a four-level
	 * page table, so PAGE_TABLE_DEPTH is 3 (four levels, including the
	 * root). The context covers the driver's 48-bit VA aperture.
	 */
	uint32 context = read_reg(d, GCVM_CONTEXT0_CNTL);
	context &= ~(GCVM_CONTEXT_PAGE_TABLE_DEPTH_MASK
		| GCVM_CONTEXT_PAGE_TABLE_BLOCK_MASK
		| GCVM_CONTEXT_RETRY_FAULT);
	context |= GCVM_CONTEXT_ENABLE
		| (3u << GCVM_CONTEXT_PAGE_TABLE_DEPTH_SHIFT);
	write_reg(d, GCVM_CONTEXT0_CNTL, context);

	uint64 root = d.vm_root_phys;
	write_reg(d, GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,
		(uint32)root);
	write_reg(d, GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,
		(uint32)(root >> 32));

	/* Context 0 page-table aperture is expressed in 4 KiB pages. */
	write_reg(d, GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32, 0);
	write_reg(d, GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32, 0);
	write_reg(d, GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,
		0xffffffffu);
	write_reg(d, GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,
		0x0000000fu);

	/* Enable the GFXHub L1 translation cache. */
	uint32 tlb = read_reg(d, GCMC_VM_MX_L1_TLB_CNTL);
	tlb |= GCMC_VM_MX_L1_TLB_ENABLE
		| GCMC_VM_MX_L1_TLB_ADVANCED_MODEL;
	tlb &= ~GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_MASK;
	tlb |= 3u << GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_SHIFT;
	write_reg(d, GCMC_VM_MX_L1_TLB_CNTL, tlb);

	/* Enable L2 and force a clean initial translation state. */
	uint32 l2 = read_reg(d, GCVM_L2_CNTL);
	l2 |= GCVM_L2_ENABLE;
	write_reg(d, GCVM_L2_CNTL, l2);

	uint32 l2ctl2 = read_reg(d, GCVM_L2_CNTL2);
	l2ctl2 |= GCVM_L2_INVALIDATE_L1_TLBS | GCVM_L2_INVALIDATE_CACHE;
	write_reg(d, GCVM_L2_CNTL2, l2ctl2);

	/*
	 * Program the invalidation range registers for engine 0. The hardware
	 * exposes eighteen engines on GFX12; engine 0 is sufficient for the
	 * native kernel VMID0 path.
	 */
	write_reg(d, GCVM_INVALIDATE_ENG17_ADDR_RANGE_LO32, 0xffffffffu);
	write_reg(d, GCVM_INVALIDATE_ENG17_ADDR_RANGE_HI32, 0x0000001fu);

	status_t status = flush_engine17(d, 0, 0);
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
	return flush_engine17(d, vmid, flushType);
}
