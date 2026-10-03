#include "rdna4_mes.h"
#include "driver.h"
#include "rdna4_fw.h"
#include "rdna4_mes_api.h"
#include <KernelExport.h>
#include <OS.h>
#include <string.h>

/* GFX12 CP-MES block. The generated register file identifies the block
 * base as 0x32000 and the CP_MES_* register indices below. */
static const uint32 MES_BASE = 0x32000;
static const uint32 MES_PRGRM_START = 0x2800;
static const uint32 MES_CNTL = 0x2807;
static const uint32 MES_IC_OP_CNTL = 0x2820;
static const uint32 MES_IC_BASE_LO = 0x5850;
static const uint32 MES_IC_BASE_HI = 0x5851;
static const uint32 MES_IC_BASE_CNTL = 0x5852;
static const uint32 MES_DC_BASE_LO = 0x5854;
static const uint32 MES_DC_BASE_HI = 0x5855;
static const uint32 MES_MIBOUND_LO = 0x585b;
static const uint32 MES_MDBOUND_LO = 0x585d;

/* CP_MES_CNTL fields used by the public GFX12 implementation. */
static const uint32 MES_PIPE0_ACTIVE = 1u << 0;
static const uint32 MES_PIPE1_ACTIVE = 1u << 1;
static const uint32 MES_PIPE0_RESET = 1u << 2;
static const uint32 MES_PIPE1_RESET = 1u << 3;
static const uint32 MES_HALT = 1u << 4;
static const uint32 MES_INVALIDATE_ICACHE = 1u << 5;

static inline volatile uint32*
reg_ptr(rdna4_device& d, uint32 off)
{
	uint32 base = off >= 0x5800 ? 0x3e000 : MES_BASE;
	return (volatile uint32*)(d.mmio + ((size_t)(base + off) << 2));
}

static inline uint32 read_reg(rdna4_device& d, uint32 off)
{
	return *reg_ptr(d, off);
}

static inline void write_reg(rdna4_device& d, uint32 off, uint32 v)
{
	*reg_ptr(d, off) = v;
	(void)*reg_ptr(d, off);
}

static status_t
alloc_resources(rdna4_device& d)
{
	rdna4_mes_state& m = d.mes;
	m.ring_area = m.status_area = -1;
	void* cpu = NULL;
	status_t s = B_OK;

	m.ring_area = create_area("rdna4 mes ring", &cpu, B_ANY_KERNEL_ADDRESS,
		64 * 1024, B_CONTIGUOUS, B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (m.ring_area < 0) return m.ring_area;
	m.ring_cpu = (uint32*)cpu;
	physical_entry e;
	s = get_memory_map(cpu, 64 * 1024, &e, 1);
	if (s != B_OK || e.size < 64 * 1024) return s != B_OK ? s : B_NOT_SUPPORTED;
	m.ring_phys = e.address;
	rdna4_bo rb = {}; rb.area=m.ring_area; rb.cpu=m.ring_cpu; rb.size=64*1024; rb.physical=e.address; rb.used=true;
	s = rdna4_vm_map_bo(d, rb, B_PAGE_SIZE); if (s != B_OK) { delete_area(m.ring_area); m.ring_area=-1; return s; }
	m.ring_gpu = rb.gpu;
	memset(m.ring_cpu, 0, 64 * 1024);

	volatile uint64* status = NULL;
	m.status_area = create_area("rdna4 mes status", (void**)&status,
		B_ANY_KERNEL_ADDRESS, B_PAGE_SIZE, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (m.status_area < 0) return m.status_area;
	m.status_cpu = status;
	s = get_memory_map((void*)status, B_PAGE_SIZE, &e, 1);
	if (s != B_OK) return s;
	m.status_phys = e.address;
	rdna4_bo sb = {}; sb.area=m.status_area; sb.cpu=(void*)m.status_cpu; sb.size=B_PAGE_SIZE; sb.physical=e.address; sb.used=true;
	s = rdna4_vm_map_bo(d, sb, B_PAGE_SIZE); if (s != B_OK) { delete_area(m.status_area); m.status_area=-1; return s; }
	m.status_gpu = sb.gpu;
	*m.status_cpu = 0;
	m.wptr = 0;
	return B_OK;
}

static void
free_resources(rdna4_device& d)
{
	if (d.mes.status_area >= 0) {
		rdna4_bo b={}; b.area=d.mes.status_area; b.cpu=(void*)d.mes.status_cpu;
		b.size=B_PAGE_SIZE; b.physical=d.mes.status_phys; b.gpu=d.mes.status_gpu; b.used=true;
		if (d.vm_ready && b.gpu) rdna4_vm_unmap_bo(d,b);
		delete_area(d.mes.status_area);
	}
	if (d.mes.ring_area >= 0) {
		rdna4_bo b={}; b.area=d.mes.ring_area; b.cpu=d.mes.ring_cpu;
		b.size=64*1024; b.physical=d.mes.ring_phys; b.gpu=d.mes.ring_gpu; b.used=true;
		if (d.vm_ready && b.gpu) rdna4_vm_unmap_bo(d,b);
		delete_area(d.mes.ring_area);
	}
	d.mes = {};
	d.mes.ring_area = d.mes.status_area = -1;
}

static status_t
load_pipe0(rdna4_device& d)
{
	const rdna4_firmware_slot& f = d.firmware[RDNA4_FW_MES];
	if (!f.staged || f.ucode_size == 0 || f.data_size == 0)
		return B_NO_INIT;

	/* The current Linux GFX12 path loads MES code/data into GPU-addressable
	 * memory, programs the instruction/data cache bases, invalidates and
	 * primes the instruction cache, then sets the MES program counter. */
	uint32 ctl = read_reg(d, MES_CNTL);
	ctl &= ~(MES_PIPE0_ACTIVE | MES_PIPE1_ACTIVE);
	ctl |= MES_PIPE0_RESET | MES_PIPE1_RESET | MES_HALT;
	write_reg(d, MES_CNTL, ctl);

	write_reg(d, MES_IC_BASE_CNTL, 0);
	uint64 firmwareGPU = f.psp_loaded_gpu != 0 ? f.psp_loaded_gpu : f.gpu;
	write_reg(d, MES_IC_BASE_LO, (uint32)firmwareGPU);
	write_reg(d, MES_IC_BASE_HI, (uint32)(firmwareGPU >> 32));
	write_reg(d, MES_MIBOUND_LO, 0x1fffff);
	write_reg(d, MES_DC_BASE_LO, (uint32)(firmwareGPU + f.data_offset));
	write_reg(d, MES_DC_BASE_HI,
		(uint32)((firmwareGPU + f.data_offset) >> 32));
	write_reg(d, MES_MDBOUND_LO, 0x7ffff);

	uint32 ic = read_reg(d, MES_IC_OP_CNTL);
	ic &= ~(1u | (1u << 1));
	ic |= 1u << 1; /* invalidate */
	write_reg(d, MES_IC_OP_CNTL, ic);
	ic = read_reg(d, MES_IC_OP_CNTL);
	ic |= 1u; /* prime */
	write_reg(d, MES_IC_OP_CNTL, ic);

	uint64 start = f.ucode_start;
	if (start == 0)
		start = firmwareGPU + f.ucode_offset;
	start >>= 2;
	write_reg(d, MES_PRGRM_START, (uint32)start);
	write_reg(d, 0x289d, (uint32)(start >> 32));

	ctl = MES_PIPE0_ACTIVE;
	write_reg(d, MES_CNTL, ctl);
	snooze(50);
	return B_OK;
}

status_t
rdna4_mes_init(rdna4_device& d)
{
	if (d.mmio == NULL || !d.vm_ready || !d.gfxhub_ready)
		return B_NO_INIT;
	return alloc_resources(d);
}

status_t
rdna4_mes_start(rdna4_device& d)
{
	if (d.mes.ring_area < 0)
		return B_NO_INIT;
	status_t s = load_pipe0(d);
	if (s != B_OK)
		return s;
	d.mes.ready = true;
	d.mes.completion_seq = 0;
	status_t api = rdna4_mes_set_hw_resources(d, 1u << 0, 1u << 0, 1u, 0xffu, 0x3u);
	if (api != B_OK) { rdna4_mes_stop(d); return api; }
	api = rdna4_mes_set_scheduling_config(d, 1000, 100, 10);
	if (api != B_OK) { rdna4_mes_stop(d); return api; }
	if (d.shared) {
		d.shared->mes_state = RDNA4_ENGINE_SCHEDULER_READY;
		d.shared->feature_mask |= RDNA4_FEATURE_MES;
	}
	return B_OK;
}

status_t
rdna4_mes_stop(rdna4_device& d)
{
	if (d.mes.ring_area < 0)
		return B_OK;
	uint32 ctl = read_reg(d, MES_CNTL);
	ctl &= ~(MES_PIPE0_ACTIVE | MES_PIPE1_ACTIVE);
	ctl |= MES_INVALIDATE_ICACHE | MES_PIPE0_RESET | MES_PIPE1_RESET | MES_HALT;
	write_reg(d, MES_CNTL, ctl);
	d.mes.ready = false;
	if (d.shared) d.shared->mes_state = RDNA4_ENGINE_OFF;
	return B_OK;
}

void
rdna4_mes_uninit(rdna4_device& d)
{
	rdna4_mes_stop(d);
	free_resources(d);
}

status_t
rdna4_mes_wait(rdna4_device& d, bigtime_t timeout)
{
	if (!d.mes.ready)
		return B_NO_INIT;
	bigtime_t deadline = system_time() + timeout;
	for (;;) {
		/* mstatus is a RISC-V machine-status register exposed by CP-MES. */
		uint32 status = read_reg(d, 0x2816);
		if (status != 0)
			return B_OK;
		if (timeout >= 0 && system_time() >= deadline)
			return B_TIMED_OUT;
		snooze(10);
	}
}
