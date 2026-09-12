/* PPC970 Book III-S hashed page-table MMU. */
#include <KernelExport.h>
#include <boot/kernel_args.h>
#include <arch_mmu.h>
#include <arch/cpu.h>
#include <vm/vm.h>

static ppc64_pteg* sHPT;
static uint64 sHPTSize;
static uint64 sHashMask;
static uint64 sSDR1;
static addr_t sCurrentAddressSpace;

static inline uint64 segment_id(addr_t ea)
	{ return ((uint64)ea >> PPC64_SEGMENT_SHIFT) & PPC64_SLB_ESID_MASK; }

static inline uint64
address_space_vsid(addr_t ea, addr_t addressSpace)
{
	uint64 esid = segment_id(ea);
	if (addressSpace == 0 || IS_KERNEL_ADDRESS(ea))
		return esid;
	uint64 context = (uint64)addressSpace;
	uint64 mixed = context ^ (context >> 17) ^ (context >> 37);
	return (esid ^ mixed) & PPC64_SLB_ESID_MASK;
}

static inline uint64 vpn(addr_t ea, addr_t addressSpace)
	{ return (address_space_vsid(ea, addressSpace) << 16)
		| (((uint64)ea >> PPC64_PAGE_SHIFT) & 0xffff); }
static inline uint64 hash(uint64 v)
	{ return ((v >> 16) ^ (v & 0xffff)) & 0x7fffffffffULL; }
static inline uint64 avpn(uint64 v) { return (v >> 11) << 7; }
static inline ppc64_pte* group(uint64 h)
	{ return &sHPT->pte[(h & sHashMask) * PPC64_HPT_PTES_PER_GROUP]; }

static ppc64_pte*
find(uint64 v, bool secondary)
{
	uint64 h = hash(v);
	if (secondary)
		h = ~h;
	ppc64_pte* p = group(h);
	uint64 wanted = avpn(v);
	for (uint32 i = 0; i < PPC64_HPT_PTES_PER_GROUP; i++) {
		uint64 value = p[i].word0;
		if ((value & PPC64_HPTE_V_VALID)
			&& (value & PPC64_HPTE_V_AVPN_MASK)
			== (wanted & PPC64_HPTE_V_AVPN_MASK))
			return &p[i];
	}
	return NULL;
}

static ppc64_pte*
find_free_or_victim(ppc64_pte* p)
{
	for (uint32 i = 0; i < PPC64_HPT_PTES_PER_GROUP; i++) {
		if (!(p[i].word0 & PPC64_HPTE_V_VALID))
			return &p[i];
	}
	for (int i = PPC64_HPT_PTES_PER_GROUP - 1; i >= 0; i--) {
		if (!(p[i].word0 & PPC64_HPTE_V_BOLTED)) {
			p[i].word0 &= ~PPC64_HPTE_V_VALID;
			sync();
			arch_cpu_global_tlb_invalidate();
			return &p[i];
		}
	}
	return NULL;
}

static status_t
insert(addr_t ea, phys_addr_t pa, uint32 protection, uint32 memoryType,
	addr_t addressSpace)
{
	uint64 v = vpn(ea, addressSpace);
	uint64 h = hash(v);
	uint64 r = ((uint64)pa & PPC64_HPTE_R_RPN) | PPC64_HPTE_R_R;
	bool kernel = IS_KERNEL_ADDRESS(ea);

	if (kernel)
		r |= (protection & B_KERNEL_WRITE_AREA) ? PPC64_HPTE_PP_RWXX
			: PPC64_HPTE_PP_RXRX;
	else if (protection & B_WRITE_AREA)
		r |= PPC64_HPTE_PP_RWRW;
	else
		r |= PPC64_HPTE_PP_RWRX;

	if (memoryType != 0)
		r |= PPC64_HPTE_R_I | PPC64_HPTE_R_G;

	ppc64_pte* p = find_free_or_victim(group(h));
	if (p != NULL) {
		p->word1 = r;
		sync();
		p->word0 = avpn(v) | PPC64_HPTE_V_VALID;
		eieio();
		return B_OK;
	}

	p = find_free_or_victim(group(~h));
	if (p != NULL) {
		p->word1 = r;
		sync();
		p->word0 = avpn(v) | PPC64_HPTE_V_H
			| PPC64_HPTE_V_SECONDARY | PPC64_HPTE_V_VALID;
		eieio();
		return B_OK;
	}
	return B_NO_MEMORY;
}

status_t
ppc64_mmu_init(kernel_args* args)
{
	if (args == NULL || args->arch_args.page_table.start == 0)
		return B_ERROR;
	sHPT = (ppc64_pteg*)args->arch_args.page_table.start;
	sHPTSize = args->arch_args.page_table.size;
	if (sHPTSize < 256 * 1024 || (sHPTSize & (sHPTSize - 1)) != 0)
		return B_BAD_VALUE;
	sHashMask = sHPTSize / PPC64_HPT_PTEG_SIZE - 1;
	uint32 htabShift = __builtin_ctzll(sHPTSize);
	sSDR1 = (uint64)args->arch_args.page_table.start | (htabShift - 18);
	sCurrentAddressSpace = 0;
	ppc64_slb_invalidate();
	ppc64_slb_insert(PPC64_SLB_KERNEL_SLOT, 0x8, 0x8 | PPC64_SLB_VSID_KP);
	set_sdr1(sSDR1);
	sync();
	isync();
	return B_OK;
}

status_t ppc64_mmu_init_post_vm(kernel_args*) { return B_OK; }

void
ppc64_mmu_switch_address_space(addr_t addressSpace)
{
	sCurrentAddressSpace = addressSpace;
	ppc64_slb_invalidate();
	ppc64_slb_insert(PPC64_SLB_KERNEL_SLOT, 0x8, 0x8 | PPC64_SLB_VSID_KP);
}

status_t
ppc64_mmu_handle_segment_fault(addr_t address)
{
	if (sHPT == NULL)
		return B_NOT_INITIALIZED;
	uint64 esid = segment_id(address);
	uint64 vsid = address_space_vsid(address, sCurrentAddressSpace);
	bool kernel = sCurrentAddressSpace == 0 || IS_KERNEL_ADDRESS(address);
	uint32 slot = kernel ? (uint32)(esid % PPC64_SLB_USER_SLOT_BASE)
		: PPC64_SLB_USER_SLOT_BASE + (esid % PPC64_SLB_USER_SLOT_COUNT);
	uint64 flags = kernel ? PPC64_SLB_VSID_KP : 0;
	ppc64_slb_insert(slot, esid, vsid | flags);
	return B_OK;
}

status_t
ppc64_map_page_asid(addr_t virtualAddress, phys_addr_t physicalAddress,
	uint32 protection, uint32 memoryType, addr_t addressSpace)
{
	if (sHPT == NULL)
		return B_NOT_INITIALIZED;
	virtualAddress &= ~(addr_t)(PPC64_PAGE_SIZE - 1);
	physicalAddress &= ~(phys_addr_t)(PPC64_PAGE_SIZE - 1);
	uint64 v = vpn(virtualAddress, addressSpace);
	ppc64_pte* p = find(v, false);
	if (p == NULL)
		p = find(v, true);
	if (p != NULL) {
		p->word0 &= ~PPC64_HPTE_V_VALID;
		sync();
		arch_cpu_invalidate_tlb_range(0, virtualAddress,
			virtualAddress + PPC64_PAGE_SIZE);
	}
	return insert(virtualAddress, physicalAddress, protection, memoryType,
		addressSpace);
}

status_t
ppc64_map_page(addr_t virtualAddress, phys_addr_t physicalAddress,
	uint32 protection, uint32 memoryType)
{
	return ppc64_map_page_asid(virtualAddress, physicalAddress, protection,
		memoryType, sCurrentAddressSpace);
}

status_t
ppc64_unmap_page_asid(addr_t virtualAddress, addr_t addressSpace)
{
	if (sHPT == NULL)
		return B_NOT_INITIALIZED;
	virtualAddress &= ~(addr_t)(PPC64_PAGE_SIZE - 1);
	ppc64_pte* p = find(vpn(virtualAddress, addressSpace), false);
	if (p == NULL)
		p = find(vpn(virtualAddress, addressSpace), true);
	if (p == NULL)
		return B_ENTRY_NOT_FOUND;
	p->word0 &= ~PPC64_HPTE_V_VALID;
	sync();
	arch_cpu_invalidate_tlb_range(0, virtualAddress,
		virtualAddress + PPC64_PAGE_SIZE);
	return B_OK;
}

status_t ppc64_unmap_page(addr_t virtualAddress)
	{ return ppc64_unmap_page_asid(virtualAddress, sCurrentAddressSpace); }
