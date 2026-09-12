/* PPC970 Book III-S hashed page-table MMU. */
#include <KernelExport.h>
#include <boot/kernel_args.h>
#include <arch_mmu.h>
#include <arch/cpu.h>
#include <vm/vm.h>
static ppc64_pteg* sHPT; static uint64 sHPTSize; static uint64 sHashMask; static uint64 sSDR1; static addr_t sCurrentAddressSpace;
static inline uint64 segment_id(addr_t ea) { return ((uint64)ea >> PPC64_SEGMENT_SHIFT) & PPC64_SLB_ESID_MASK; }
static inline uint64 address_space_vsid(addr_t ea, addr_t addressSpace) { uint64 esid=segment_id(ea); if(addressSpace==0||IS_KERNEL_ADDRESS(ea)) return esid; uint64 c=(uint64)addressSpace; return (esid ^ c ^ (c>>17) ^ (c>>37)) & PPC64_SLB_ESID_MASK; }
static inline uint64 vpn(addr_t ea, addr_t as) { return (address_space_vsid(ea,as)<<16)|(((uint64)ea>>PPC64_PAGE_SHIFT)&0xffff); }
static inline uint64 hash(uint64 v) { return ((v>>16)^(v&0xffff))&0x7fffffffffULL; }
static inline uint64 avpn(uint64 v) { return (v>>11)<<7; }
static inline ppc64_pte* group(uint64 h) { return &sHPT->pte[(h&sHashMask)*PPC64_HPT_PTES_PER_GROUP]; }
static ppc64_pte* find(uint64 v,bool secondary) { uint64 h=hash(v); if(secondary) h=~h; ppc64_pte* p=group(h); uint64 wanted=avpn(v); for(uint32 i=0;i<PPC64_HPT_PTES_PER_GROUP;i++) if((p[i].word0&PPC64_HPTE_V_VALID)&&(p[i].word0&PPC64_HPTE_V_AVPN_MASK)==(wanted&PPC64_HPTE_V_AVPN_MASK)) return &p[i]; return NULL; }
static ppc64_pte* find_free_or_victim(ppc64_pte* p) { for(uint32 i=0;i<8;i++) if(!(p[i].word0&PPC64_HPTE_V_VALID)) return &p[i]; for(int i=7;i>=0;i--) if(!(p[i].word0&PPC64_HPTE_V_BOLTED)){p[i].word0&=~PPC64_HPTE_V_VALID;sync();arch_cpu_global_tlb_invalidate();return &p[i];} return NULL; }
static status_t insert(addr_t ea,phys_addr_t pa,uint32 prot,uint32 type,addr_t as) { uint64 v=vpn(ea,as),h=hash(v),r=((uint64)pa&PPC64_HPTE_R_RPN)|PPC64_HPTE_R_R; bool kernel=IS_KERNEL_ADDRESS(ea); if(kernel) r|=(prot&B_KERNEL_WRITE_AREA)?PPC64_HPTE_PP_RWXX:PPC64_HPTE_PP_RXRX; else if(prot&B_WRITE_AREA) r|=PPC64_HPTE_PP_RWRW; else r|=PPC64_HPTE_PP_RWRX; if(type) r|=PPC64_HPTE_R_I|PPC64_HPTE_R_G; ppc64_pte* p=find_free_or_victim(group(h)); if(p){p->word1=r;sync();p->word0=avpn(v)|PPC64_HPTE_V_VALID;eieio();return B_OK;} p=find_free_or_victim(group(~h)); if(p){p->word1=r;sync();p->word0=avpn(v)|PPC64_HPTE_V_H|PPC64_HPTE_V_SECONDARY|PPC64_HPTE_V_VALID;eieio();return B_OK;} return B_NO_MEMORY; }
status_t ppc64_mmu_init(kernel_args* a){if(!a||!a->arch_args.page_table.start)return B_ERROR;sHPT=(ppc64_pteg*)a->arch_args.page_table.start;sHPTSize=a->arch_args.page_table.size;if(sHPTSize<262144||(sHPTSize&(sHPTSize-1)))return B_BAD_VALUE;sHashMask=sHPTSize/128-1;uint32 sh=__builtin_ctzll(sHPTSize);sSDR1=(uint64)a->arch_args.page_table.start|(sh-18);sCurrentAddressSpace=0;ppc64_slb_invalidate();ppc64_slb_insert(0,0x8,0x8|PPC64_SLB_VSID_KERNEL);set_sdr1(sSDR1);sync();isync();return B_OK;}
status_t ppc64_mmu_init_post_vm(kernel_args*){return B_OK;}
void ppc64_mmu_switch_address_space(addr_t as){sCurrentAddressSpace=as;ppc64_slb_invalidate();ppc64_slb_insert(0,0x8,0x8|PPC64_SLB_VSID_KERNEL);}
status_t ppc64_mmu_handle_segment_fault(addr_t address){if(!sHPT)return B_NOT_INITIALIZED;uint64 esid=segment_id(address),vsid=address_space_vsid(address,sCurrentAddressSpace);bool kernel=sCurrentAddressSpace==0||IS_KERNEL_ADDRESS(address);uint32 slot=kernel?(uint32)(esid%8):8+(esid%56);ppc64_slb_insert(slot,esid,vsid|(kernel?PPC64_SLB_VSID_KERNEL:PPC64_SLB_VSID_USER));return B_OK;}
status_t ppc64_map_page_asid(addr_t va,phys_addr_t pa,uint32 prot,uint32 type,addr_t as){if(!sHPT)return B_NOT_INITIALIZED;va&=~(addr_t)0xfff;pa&=~(phys_addr_t)0xfff;uint64 v=vpn(va,as);ppc64_pte* p=find(v,false);if(!p)p=find(v,true);if(p){p->word0&=~PPC64_HPTE_V_VALID;sync();arch_cpu_invalidate_tlb_range(0,va,va+4096);}return insert(va,pa,prot,type,as);}
status_t ppc64_map_page(addr_t va,phys_addr_t pa,uint32 prot,uint32 type){return ppc64_map_page_asid(va,pa,prot,type,sCurrentAddressSpace);}
status_t ppc64_unmap_page_asid(addr_t va,addr_t as){if(!sHPT)return B_NOT_INITIALIZED;va&=~(addr_t)0xfff;ppc64_pte*p=find(vpn(va,as),false);if(!p)p=find(vpn(va,as),true);if(!p)return B_ENTRY_NOT_FOUND;p->word0&=~PPC64_HPTE_V_VALID;sync();arch_cpu_invalidate_tlb_range(0,va,va+4096);return B_OK;}
status_t ppc64_unmap_page(addr_t va){return ppc64_unmap_page_asid(va,sCurrentAddressSpace);}
status_t ppc64_query_page_asid(addr_t va,phys_addr_t*pa,uint32*prot,addr_t as){if(!sHPT)return B_NOT_INITIALIZED;va&=~(addr_t)0xfff;ppc64_pte*p=find(vpn(va,as),false);if(!p)p=find(vpn(va,as),true);if(!p)return B_ENTRY_NOT_FOUND;if(pa)*pa=p->word1&PPC64_HPTE_R_RPN;if(prot){uint32 pp=p->word1&PPC64_HPTE_R_PP;*prot=pp==0?B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA:pp==2?B_READ_AREA|B_WRITE_AREA:B_READ_AREA;}return B_OK;}
status_t ppc64_clear_page_flags_asid(addr_t va,uint32 flags,bool*modified,addr_t as){if(modified)*modified=false;va&=~(addr_t)0xfff;ppc64_pte*p=find(vpn(va,as),false);if(!p)p=find(vpn(va,as),true);if(!p)return B_ENTRY_NOT_FOUND;if(modified)*modified=(p->word1&PPC64_HPTE_R_C)!=0;if(flags)p->word1&=~(uint64)(PPC64_HPTE_R_R|PPC64_HPTE_R_C);eieio();return B_OK;}
