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

/* PPC970 uses 256 MiB segments and 4 KiB base pages. */
static inline uint64 vsid(addr_t ea)
{
	return ((uint64)ea >> PPC64_SEGMENT_SHIFT) & PPC64_SLB_ESID_MASK;
}

static inline uint64 vpn(addr_t ea)
{
	return (vsid(ea) << 16) | (((uint64)ea >> PPC64_PAGE_SHIFT) & 0xffff);
}

/* Book III-S 256 MiB-segment hash, with the 39-bit architectural mask. */
static inline uint64 hash(uint64 v)
{
	const uint64 pageMask = (1ULL << 16) - 1;
	return ((v >> 16) ^ ((v & pageMask))) & 0x7fffffffffULL;
}

static inline uint64 avpn(uint64 v)
{
	/* AVPN omits the low 11 VPN bits and is stored at V[7:55]. */
	return ((v >> 11) << 7) | (0ULL << PPC64_HPTE_V_SSIZE_SHIFT);
}

static inline ppc64_pte* group(uint64 h)
{
	return &sHPT->pte[(h & sHashMask) * PPC64_HPT_PTES_PER_GROUP];
}

static ppc64_pte* find(uint64 v, bool secondary)
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

static status_t insert(addr_t ea, phys_addr_t pa, uint32 protection,
	uint32 memoryType)
{
	uint64 v = vpn(ea);
	uint64 h = hash(v);
	uint64 r = ((uint64)pa & PPC64_HPTE_R_RPN) | PPC64_HPTE_R_R;

	/* PPC64 Haiku protection values are collapsed to the Book III-S PP bits. */
	if (protection & B_KERNEL_WRITE_AREA)
		r |= PPC64_HPTE_PP_RWXX;
	else
		r |= PPC64_HPTE_PP_RXRX;

	/* memoryType == 0 is normal cacheable memory. */
	if (memoryType != 0)
		r |= PPC64_HPTE_R_I | PPC64_HPTE_R_G;

	ppc64_pte* p = group(h);
	for (uint32 i = 0; i < PPC64_HPT_PTES_PER_GROUP; i++) {
		if (!(p[i].word0 & PPC64_HPTE_V_VALID)) {
			p[i].word1 = r;
			sync();
			p[i].word0 = avpn(v) | PPC64_HPTE_V_VALID;
			eieio();
			return B_OK;
		}
	}

	/* Secondary PTEG uses the one's-complement hash and H=1. */
	p = group(~h);
	for (uint32 i = 0; i < PPC64_HPT_PTES_PER_GROUP; i++) {
		if (!(p[i].word0 & PPC64_HPTE_V_VALID)) {
			p[i].word1 = r;
			sync();
			p[i].word0 = avpn(v) | PPC64_HPTE_V_H
				| PPC64_HPTE_V_SECONDARY | PPC64_HPTE_V_VALID;
			eieio();
			return B_OK;
		}
	}
	return B_NO_MEMORY;
}

status_t ppc64_mmu_init(kernel_args* args)
{
	if (args == NULL || args->arch_args.page_table.start == 0)
		return B_ERROR;

	sHPT = (ppc64_pteg*)args->arch_args.page_table.start;
	sHPTSize = args->arch_args.page_table.size;
	if (sHPTSize < 256 * 1024 || (sHPTSize & (sHPTSize - 1)) != 0)
		return B_BAD_VALUE;

	sHashMask = sHPTSize / PPC64_HPT_PTEG_SIZE - 1;
	/* SDR1[HTABSIZE] = log2(HPT bytes) - 18. */
	uint32 htabShift = __builtin_ctzll(sHPTSize);
	sSDR1 = (uint64)args->arch_args.page_table.start
		+ (htabShift - 18);

	/* Preserve the 256 MiB segment convention while establishing a clean SLB. */
	ppc64_slb_invalidate();
	ppc64_slb_insert(0x8, 0x8 | PPC64_SLB_VSID_KP);

	set_sdr1(sSDR1);
	sync();
	isync();
	return B_OK;
}

status_t ppc64_mmu_init_post_vm(kernel_args*)
{
	return B_OK;
}

void ppc64_mmu_switch_address_space(addr_t addressSpace)
{
	(void)addressSpace;
	/* Address-space VSID/SLB allocation is installed by the VM translation map. */
}

status_t ppc64_map_page(addr_t virtualAddress, phys_addr_t physicalAddress,
	uint32 protection, uint32 memoryType)
{
	if (sHPT == NULL)
		return B_NOT_INITIALIZED;

	virtualAddress &= ~(addr_t)(PPC64_PAGE_SIZE - 1);
	physicalAddress &= ~(phys_addr_t)(PPC64_PAGE_SIZE - 1);

	ppc64_pte* p = find(vpn(virtualAddress), false);
	if (p == NULL)
		p = find(vpn(virtualAddress), true);
	if (p != NULL) {
		p->word0 &= ~PPC64_HPTE_V_VALID;
		sync();
		arch_cpu_invalidate_tlb_range(0, virtualAddress,
			virtualAddress + PPC64_PAGE_SIZE);
	}
	return insert(virtualAddress, physicalAddress, protection, memoryType);
}

status_t ppc64_unmap_page(addr_t virtualAddress)
{
	if (sHPT == NULL)
		return B_NOT_INITIALIZED;

	virtualAddress &= ~(addr_t)(PPC64_PAGE_SIZE - 1);
	ppc64_pte* p = find(vpn(virtualAddress), false);
	if (p == NULL)
		p = find(vpn(virtualAddress), true);
	if (p == NULL)
		return B_ENTRY_NOT_FOUND;

	p->word0 &= ~PPC64_HPTE_V_VALID;
	sync();
	arch_cpu_invalidate_tlb_range(0, virtualAddress,
		virtualAddress + PPC64_PAGE_SIZE);
	return B_OK;
}
