#include "rdna4_gfxhub.h"
#include "driver.h"
#include "rdna4_vm.h"
#include <KernelExport.h>
#include <OS.h>

#define GCVM_CONTEXT0_CNTL 0x1624
#define GCVM_INVALIDATE_ENG0_SEM 0x1635
#define GCVM_INVALIDATE_ENG0_REQ 0x1647
#define GCVM_INVALIDATE_ENG0_ACK 0x1659
#define GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32 0x168f
#define GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32 0x1690
#define GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32 0x16af
#define GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32 0x16b0
#define GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32 0x16cf
#define GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32 0x16d0
#define GCMC_VM_SYSTEM_APERTURE_LOW_ADDR 0x1619
#define GCMC_VM_SYSTEM_APERTURE_HIGH_ADDR 0x161a
#define GCMC_VM_MX_L1_TLB_CNTL 0x161b
#define GCVM_L2_CNTL 0x15d0
#define GCVM_L2_CNTL2 0x15d1

#define GCVM_CONTEXT_ENABLE (1u<<0)
#define GCVM_CONTEXT_PAGE_TABLE_DEPTH_SHIFT 1
#define GCVM_CONTEXT_PAGE_TABLE_DEPTH_MASK (7u<<1)
#define GCVM_CONTEXT_PAGE_TABLE_BLOCK_MASK (0x1fu<<8)
#define GCVM_CONTEXT_RETRY_FAULT (1u<<28)
#define GCVM_INV_FLUSH_TYPE_SHIFT 16
#define GCVM_INV_FLUSH_TYPE_MASK (3u<<16)
#define GCVM_INV_L2_PTES (1u<<18)
#define GCVM_INV_L2_PDE0 (1u<<19)
#define GCVM_INV_L2_PDE1 (1u<<20)
#define GCVM_INV_L2_PDE2 (1u<<21)
#define GCVM_INV_L1_PTES (1u<<22)
#define GCMC_VM_MX_L1_TLB_ENABLE (1u<<0)
#define GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_SHIFT 1
#define GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_MASK (3u<<1)
#define GCMC_VM_MX_L1_TLB_ADVANCED_MODEL (1u<<4)
#define GCVM_L2_ENABLE (1u<<0)
#define GCVM_L2_INVALIDATE_L1_TLBS (1u<<0)
#define GCVM_L2_INVALIDATE_CACHE (1u<<1)

static inline volatile uint32* reg_ptr(rdna4_device&d,uint32 r){return (volatile uint32*)(d.mmio+((size_t)r<<2));}
static inline uint32 read_reg(rdna4_device&d,uint32 r){return *reg_ptr(d,r);}
static inline void write_reg(rdna4_device&d,uint32 r,uint32 v){*reg_ptr(d,r)=v;(void)*reg_ptr(d,r);}

static status_t flush_engine0(rdna4_device&d,uint32 vmid,uint32 flushType)
{
 if(vmid>15)return B_BAD_VALUE;
 uint32 req=(1u<<vmid)|((flushType<<GCVM_INV_FLUSH_TYPE_SHIFT)&GCVM_INV_FLUSH_TYPE_MASK)
  |GCVM_INV_L2_PTES|GCVM_INV_L2_PDE0|GCVM_INV_L2_PDE1|GCVM_INV_L2_PDE2|GCVM_INV_L1_PTES;
 write_reg(d,GCVM_INVALIDATE_ENG0_REQ,req);
 bigtime_t deadline=system_time()+100000;
 while(system_time()<deadline){
  if(read_reg(d,GCVM_INVALIDATE_ENG0_ACK)&(1u<<vmid))return B_OK;
  snooze(1);
 }
 return B_TIMED_OUT;
}

status_t rdna4_gfxhub_init(rdna4_device&d)
{
 if(!d.mmio||!d.vm_ready)return B_NO_INIT;
 uint32 c=read_reg(d,GCVM_CONTEXT0_CNTL);
 c&=~(GCVM_CONTEXT_PAGE_TABLE_DEPTH_MASK|GCVM_CONTEXT_PAGE_TABLE_BLOCK_MASK|GCVM_CONTEXT_RETRY_FAULT);
 c|=GCVM_CONTEXT_ENABLE|(3u<<GCVM_CONTEXT_PAGE_TABLE_DEPTH_SHIFT);
 write_reg(d,GCVM_CONTEXT0_CNTL,c);
 uint64 root=d.vm_root_phys;
 write_reg(d,GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_LO32,(uint32)root);
 write_reg(d,GCVM_CONTEXT0_PAGE_TABLE_BASE_ADDR_HI32,(uint32)(root>>32));
 write_reg(d,GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_LO32,0);
 write_reg(d,GCVM_CONTEXT0_PAGE_TABLE_START_ADDR_HI32,0);
 write_reg(d,GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_LO32,0xffffffffu);
 write_reg(d,GCVM_CONTEXT0_PAGE_TABLE_END_ADDR_HI32,0x0000000fu);
 uint32 tlb=read_reg(d,GCMC_VM_MX_L1_TLB_CNTL);
 tlb|=GCMC_VM_MX_L1_TLB_ENABLE|GCMC_VM_MX_L1_TLB_ADVANCED_MODEL;
 tlb=(tlb&~GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_MASK)|(3u<<GCMC_VM_MX_L1_TLB_SYSTEM_ACCESS_SHIFT);
 write_reg(d,GCMC_VM_MX_L1_TLB_CNTL,tlb);
 uint32 l2=read_reg(d,GCVM_L2_CNTL)|GCVM_L2_ENABLE;write_reg(d,GCVM_L2_CNTL,l2);
 uint32 l22=read_reg(d,GCVM_L2_CNTL2)|GCVM_L2_INVALIDATE_L1_TLBS|GCVM_L2_INVALIDATE_CACHE;write_reg(d,GCVM_L2_CNTL2,l22);
 status_t s=flush_engine0(d,0,0);if(s!=B_OK)return s;
 d.shared->vm_state=RDNA4_ENGINE_VM_READY;return B_OK;
}
void rdna4_gfxhub_uninit(rdna4_device&d)
{
 if(!d.mmio)return;
 uint32 c=read_reg(d,GCVM_CONTEXT0_CNTL)&~GCVM_CONTEXT_ENABLE;write_reg(d,GCVM_CONTEXT0_CNTL,c);
 uint32 t=read_reg(d,GCMC_VM_MX_L1_TLB_CNTL)&~(GCMC_VM_MX_L1_TLB_ENABLE|GCMC_VM_MX_L1_TLB_ADVANCED_MODEL);write_reg(d,GCMC_VM_MX_L1_TLB_CNTL,t);
}
status_t rdna4_gfxhub_flush_tlb(rdna4_device&d,uint32 vmid,uint32 flushType)
{
 if(!d.mmio||!d.vm_ready)return B_NO_INIT;
 return flush_engine0(d,vmid,flushType);
}
