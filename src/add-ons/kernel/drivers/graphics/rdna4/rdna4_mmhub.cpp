#include "rdna4_mmhub.h"
#include "driver.h"
#include <OS.h>
#define MMHUB_VM_BASE 0x69590u
#define MMHUB_SHARED_BASE 0x69550u
#define VM_L2_CNTL 0x04e4
#define VM_L2_CNTL2 0x04e5
#define VM_CONTEXT0_CNTL 0x0564
#define VM_INV_REQ 0x0575
#define VM_INV_ACK 0x05bb
#define VM_INV_RANGE_LO 0x05ab
#define VM_INV_RANGE_HI 0x05ac
#define VM_PT_BASE_LO 0x05cf
#define VM_PT_BASE_HI 0x05d0
#define VM_PT_START_LO 0x05ef
#define VM_PT_START_HI 0x05f0
#define VM_PT_END_LO 0x060f
#define VM_PT_END_HI 0x0610
#define SYS_AP_LOW 0x0559
#define SYS_AP_HIGH 0x055a
#define L1_TLB 0x055b
static inline volatile uint32* reg(rdna4_device&d,uint32 base,uint32 off){return (volatile uint32*)(d.mmio+((size_t)(base+off)<<2));}
static inline void wr(rdna4_device&d,uint32 b,uint32 o,uint32 v){*reg(d,b,o)=v;(void)*reg(d,b,o);}
static inline uint32 rd(rdna4_device&d,uint32 b,uint32 o){return *reg(d,b,o);}
status_t rdna4_mmhub_init(rdna4_device&d){
 if(!d.mmio||!d.vm_ready)return B_NO_INIT;
 /* MMHUB v4.1/v4.2 is the memory client path used by VCN/JPEG/DCN.  The
    generated GFX12 register map places its VM block at 0x69590. */
 uint32 ctl=rd(d,MMHUB_VM_BASE,VM_CONTEXT0_CNTL);
 ctl|=1u; ctl&=~(7u<<1); ctl|=3u<<1; /* four-level GPUVM */
 wr(d,MMHUB_VM_BASE,VM_CONTEXT0_CNTL,ctl);
 uint64 root=d.vm_root_phys;
 wr(d,MMHUB_VM_BASE,VM_PT_BASE_LO,(uint32)root);
 wr(d,MMHUB_VM_BASE,VM_PT_BASE_HI,(uint32)(root>>32));
 wr(d,MMHUB_VM_BASE,VM_PT_START_LO,0); wr(d,MMHUB_VM_BASE,VM_PT_START_HI,0);
 wr(d,MMHUB_VM_BASE,VM_PT_END_LO,0xffffffff); wr(d,MMHUB_VM_BASE,VM_PT_END_HI,0x1f);
 /* Cover the CPU physical-address domain used by system pages. */
 wr(d,MMHUB_SHARED_BASE,SYS_AP_LOW,0);
 wr(d,MMHUB_SHARED_BASE,SYS_AP_HIGH,0xffffffff);
 uint32 tlb=rd(d,MMHUB_SHARED_BASE,L1_TLB); tlb|=1u|(1u<<4); wr(d,MMHUB_SHARED_BASE,L1_TLB,tlb);
 uint32 l2=rd(d,MMHUB_VM_BASE,VM_L2_CNTL); l2|=1u; wr(d,MMHUB_VM_BASE,VM_L2_CNTL,l2);
 wr(d,MMHUB_VM_BASE,VM_INV_RANGE_LO,0xffffffff); wr(d,MMHUB_VM_BASE,VM_INV_RANGE_HI,0x1f);
 status_t s=rdna4_mmhub_flush(d,0); if(s!=B_OK)return s;
 d.mmhub_ready=true; return B_OK;
}
status_t rdna4_mmhub_flush(rdna4_device&d,uint32 vmid){
 if(!d.mmio||vmid>15)return B_BAD_VALUE;
 uint32 req=(1u<<vmid)|(1u<<18)|(1u<<19)|(1u<<20)|(1u<<21)|(1u<<22);
 wr(d,MMHUB_VM_BASE,VM_INV_REQ,req);
 bigtime_t end=system_time()+100000;
 while(system_time()<end){if(rd(d,MMHUB_VM_BASE,VM_INV_ACK)&(1u<<vmid))return B_OK;snooze(1);}
 return B_TIMED_OUT;
}
void rdna4_mmhub_uninit(rdna4_device&d){
 if(!d.mmio)return; uint32 ctl=rd(d,MMHUB_VM_BASE,VM_CONTEXT0_CNTL);ctl&=~1u;wr(d,MMHUB_VM_BASE,VM_CONTEXT0_CNTL,ctl);d.mmhub_ready=false;
}
