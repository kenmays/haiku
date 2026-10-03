#include "rdna4_sdma.h"
#include "driver.h"
#include "rdna4_fw.h"
#include <KernelExport.h>
#include <OS.h>
#include <string.h>

/* GFX12.0 SDMA register block: gc_gfx_cpwd_sdma0_sdmadec. */
static const uint32 SDMA_BASE = 0x4980;
static const uint32 SDMA_INSTANCE_STRIDE = 0x600;
static const uint32 SDMA_HYP_BASE = 0x3e200;
static const uint32 SDMA_HYP_STRIDE = 0x20;
static const uint32 STATUS = 0x24;
static const uint32 MCU_CNTL = 0x588e;
static const uint32 IC_CNTL = 0x5894;
static const uint32 IC_BASE_LO = 0x588f;
static const uint32 IC_BASE_HI = 0x5890;
static const uint32 IC_OP_CNTL = 0x5892;
static const uint32 RB_CNTL = 0x80;
static const uint32 RB_BASE = 0x81;
static const uint32 RB_BASE_HI = 0x82;
static const uint32 RB_RPTR = 0x83;
static const uint32 RB_RPTR_HI = 0x84;
static const uint32 RB_WPTR = 0x85;
static const uint32 RB_WPTR_HI = 0x86;
static const uint32 RB_RPTR_ADDR_LO = 0x87;
static const uint32 RB_RPTR_ADDR_HI = 0x88;
static const uint32 IB_CNTL = 0x89;
static const uint32 DOORBELL = 0x8f;
static const uint32 DOORBELL_OFFSET = 0x91;
static const uint32 RB_WPTR_POLL_LO = 0x98;
static const uint32 RB_WPTR_POLL_HI = 0x99;

static inline uint32
reg(rdna4_device& d, uint32 instance, uint32 off)
{
	if (off >= 0x5880)
		return SDMA_HYP_BASE + instance * SDMA_HYP_STRIDE + off;
	return SDMA_BASE + instance * SDMA_INSTANCE_STRIDE + off;
}

static inline volatile uint32*
mmio(rdna4_device& d, uint32 instance, uint32 off)
{
	return (volatile uint32*)(d.mmio + ((size_t)reg(d, instance, off) << 2));
}

static inline uint32 read32(rdna4_device& d, uint32 i, uint32 o)
{
	return *mmio(d, i, o);
}

static inline void write32(rdna4_device& d, uint32 i, uint32 o, uint32 v)
{
	*mmio(d, i, o) = v;
	(void)*mmio(d, i, o);
}

static uint32 field(uint32 v, uint32 shift, uint32 mask, uint32 x)
{
	v &= ~mask;
	v |= (x << shift) & mask;
	return v;
}

static status_t
alloc_ring(rdna4_device& d, uint32 i)
{
	rdna4_sdma_ring& r = d.sdma[i];
	const size_t ringSize = 64 * 1024;
	void* cpu = NULL;
	r.area = -1;
	r.rptr_area = -1;
	r.wptr_area = -1;

	r.area = create_area("rdna4 sdma ring", &cpu, B_ANY_KERNEL_ADDRESS,
		ringSize, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (r.area < 0)
		return r.area;
	r.ring_cpu = (uint32*)cpu;
	physical_entry e;
	status_t s = get_memory_map(cpu, ringSize, &e, 1);
	if (s != B_OK || e.size < ringSize) {
		delete_area(r.area); r.area = -1; return s != B_OK ? s : B_NOT_SUPPORTED;
	}
	r.ring_phys = e.address;
	r.ring_gpu = e.address;

	volatile uint64* ptr = NULL;
	r.rptr_area = create_area("rdna4 sdma rptr", (void**)&ptr, B_ANY_KERNEL_ADDRESS,
		B_PAGE_SIZE, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (r.rptr_area < 0) return r.rptr_area;
	r.rptr_cpu = ptr;
	s = get_memory_map((void*)ptr, B_PAGE_SIZE, &e, 1);
	if (s != B_OK) return s;
	r.rptr_phys = e.address;
	r.rptr_gpu = e.address;

	ptr = NULL;
	r.wptr_area = create_area("rdna4 sdma wptr", (void**)&ptr, B_ANY_KERNEL_ADDRESS,
		B_PAGE_SIZE, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (r.wptr_area < 0) return r.wptr_area;
	r.wptr_cpu = ptr;
	s = get_memory_map((void*)ptr, B_PAGE_SIZE, &e, 1);
	if (s != B_OK) return s;
	r.wptr_phys = e.address;
	r.wptr_gpu = e.address;
	memset(r.ring_cpu, 0, ringSize);
	*r.rptr_cpu = 0;
	*r.wptr_cpu = 0;
	r.wptr = 0;
	return B_OK;
}

static void
free_ring(rdna4_sdma_ring& r)
{
	if (r.wptr_area >= 0) delete_area(r.wptr_area);
	if (r.rptr_area >= 0) delete_area(r.rptr_area);
	if (r.area >= 0) delete_area(r.area);
	r = {};
	r.area = r.rptr_area = r.wptr_area = -1;
}

static status_t
load_ucode(rdna4_device& d, uint32 i)
{
	uint32 type = i == 0 ? RDNA4_FW_SDMA0 : RDNA4_FW_SDMA1;
	if (!d.firmware[type].staged)
		return B_NO_INIT;
	const rdna4_firmware_slot& f = d.firmware[type];
	if (f.ucode_size == 0 || f.ucode_offset + f.ucode_size > f.size)
		return B_BAD_DATA;

	/*
	 * SDMA7 direct firmware mode uses the MC physical address of the
	 * firmware image (GPA=0), primes the instruction cache, then waits
	 * for both ICACHE_PRIMED and UCODE_INIT_DONE. This mirrors the
	 * upstream GFX12 SDMA sequence.
	 */
	uint32 v = read32(d, i, IC_CNTL);
	v &= ~(1u << 0);
	write32(d, i, IC_CNTL, v);
	write32(d, i, IC_BASE_LO, (uint32)f.gpu);
	write32(d, i, IC_BASE_HI, (uint32)(f.gpu >> 32));
	v = read32(d, i, IC_OP_CNTL);
	v |= 1u;
	write32(d, i, IC_OP_CNTL, v);

	bigtime_t deadline = system_time() + 500000;
	while (system_time() < deadline) {
		uint32 op = read32(d, i, IC_OP_CNTL);
		uint32 status = read32(d, i, STATUS);
		if ((op & (1u << 1)) != 0 && (status & (1u << 12)) != 0)
			return B_OK;
		snooze(10);
	}
	return B_TIMED_OUT;
}

static status_t
program_ring(rdna4_device& d, uint32 i)
{
	rdna4_sdma_ring& r = d.sdma[i];
	uint32 rb = read32(d, i, RB_CNTL);
	/* RB_SIZE is log2(number of dwords), starting at bit 1. */
	rb = field(rb, 1, 0x7e, 14);
	rb |= 1u << 23;       /* RB_PRIV */
	rb |= 1u << 12;       /* RPTR_WRITEBACK_ENABLE */
	rb &= ~(1u << 8);
	rb |= 1u << 16;       /* RPTR writeback timer */
	write32(d, i, RB_CNTL, rb);

	write32(d, i, RB_RPTR, 0);
	write32(d, i, RB_RPTR_HI, 0);
	write32(d, i, RB_WPTR, 0);
	write32(d, i, RB_WPTR_HI, 0);
	write32(d, i, RB_WPTR_POLL_LO, (uint32)r.wptr_gpu);
	write32(d, i, RB_WPTR_POLL_HI, (uint32)(r.wptr_gpu >> 32));
	write32(d, i, RB_RPTR_ADDR_LO, (uint32)r.rptr_gpu & 0xfffffffc);
	write32(d, i, RB_RPTR_ADDR_HI, (uint32)(r.rptr_gpu >> 32));
	write32(d, i, RB_BASE, (uint32)(r.ring_gpu >> 8));
	write32(d, i, RB_BASE_HI, (uint32)(r.ring_gpu >> 40));
	write32(d, i, DOORBELL_OFFSET, i * 2);
	write32(d, i, DOORBELL, 1);
	write32(d, i, IB_CNTL, read32(d, i, IB_CNTL) | 1u);
	write32(d, i, RB_CNTL, rb | 1u);
	r.ready = true;
	return B_OK;
}

status_t
rdna4_sdma_init(rdna4_device& d)
{
	if (d.mmio == NULL || !d.vm_ready || !d.gfxhub_ready)
		return B_NO_INIT;
	for (uint32 i = 0; i < 2; i++) {
		status_t s = alloc_ring(d, i);
		if (s != B_OK) { rdna4_sdma_uninit(d); return s; }
	}
	return B_OK;
}

status_t
rdna4_sdma_start(rdna4_device& d)
{
	if (d.sdma[0].area < 0 || d.sdma[1].area < 0)
		return B_NO_INIT;
	for (uint32 i = 0; i < 2; i++) {
		status_t s = load_ucode(d, i);
		if (s != B_OK) return s;
		s = program_ring(d, i);
		if (s != B_OK) return s;
	}
	if (d.shared) {
		d.shared->sdma_state = RDNA4_ENGINE_RUNNING;
		d.shared->feature_mask |= RDNA4_FEATURE_SDMA;
	}
	return B_OK;
}

status_t
rdna4_sdma_stop(rdna4_device& d)
{
	for (uint32 i = 0; i < 2; i++) {
		if (d.sdma[i].area < 0) continue;
		uint32 rb = read32(d, i, RB_CNTL);
		write32(d, i, RB_CNTL, rb & ~1u);
		write32(d, i, IB_CNTL, read32(d, i, IB_CNTL) & ~1u);
		write32(d, i, MCU_CNTL, read32(d, i, MCU_CNTL) | 1u | (1u << 1));
		d.sdma[i].ready = false;
	}
	if (d.shared) d.shared->sdma_state = RDNA4_ENGINE_OFF;
	return B_OK;
}

void
rdna4_sdma_uninit(rdna4_device& d)
{
	rdna4_sdma_stop(d);
	free_ring(d.sdma[0]);
	free_ring(d.sdma[1]);
}

status_t
rdna4_sdma_wait_idle(rdna4_device& d, bigtime_t timeout)
{
	bigtime_t deadline = system_time() + timeout;
	for (;;) {
		bool idle = true;
		for (uint32 i = 0; i < 2; i++)
			if ((read32(d, i, STATUS) & (1u << 0)) == 0) idle = false;
		if (idle) return B_OK;
		if (timeout >= 0 && system_time() >= deadline) return B_TIMED_OUT;
		snooze(10);
	}
}

status_t
rdna4_sdma_submit_copy(rdna4_device& d, uint32 i, uint64 src, uint64 dst,
	uint32 bytes, uint64 fence, uint64 fenceValue)
{
	if (i >= 2 || !d.sdma[i].ready || bytes == 0)
		return B_BAD_VALUE;
	rdna4_sdma_ring& r = d.sdma[i];
	if (bytes > 0x3fffff || (src & 3) || (dst & 3))
		return B_BAD_VALUE;

	/* SDMA linear COPY packet followed by a 64-bit fence and TRAP. */
	uint32* p = r.ring_cpu;
	uint32 w = r.wptr & 0x3fff;
	p[w++] = (3u << 28) | (0u << 24) | (0u << 16) | 0x2; /* COPY */
	p[w++] = bytes - 1;
	p[w++] = 0;
	p[w++] = (uint32)src;
	p[w++] = (uint32)(src >> 32);
	p[w++] = (uint32)dst;
	p[w++] = (uint32)(dst >> 32);
	p[w++] = 0;
	p[w++] = (3u << 28) | 0x0a; /* FENCE */
	p[w++] = (uint32)fence;
	p[w++] = (uint32)(fence >> 32);
	p[w++] = (uint32)fenceValue;
	p[w++] = (uint32)(fenceValue >> 32);
	p[w++] = (3u << 28) | 0x0b; /* TRAP */
	r.wptr = w;
	*r.wptr_cpu = (uint64)r.wptr << 2;
	write32(d, i, RB_WPTR, (uint32)r.wptr << 2);
	write32(d, i, RB_WPTR_HI, (uint32)((uint64)r.wptr << 2 >> 32));
	rdna4_doorbell_write(d, 0x100 + i * 2, (uint64)r.wptr << 2);
	return B_OK;
}
