#include "rdna4_mes_api.h"
#include "driver.h"
#include <KernelExport.h>
#include <OS.h>
#include <string.h>

#define MES_FRAME_DWORDS 64
#define MES_API_TYPE_SCHED 1
#define MES_SET_HW 0
#define MES_SET_SCHED 1
#define MES_ADD_QUEUE 2
#define MES_REMOVE_QUEUE 3
#define MES_Q_GFX 0
#define MES_Q_COMPUTE 1
#define MES_Q_SDMA 2
#define MES_PRIORITY_NORMAL 1
#define MES_MAX_Q 32

struct mes_header { uint32 u32; };
static uint32 hdr(uint32 op) { return MES_API_TYPE_SCHED | (op << 4) | (MES_FRAME_DWORDS << 12); }
static uint32* frame(rdna4_device& d, uint32& pos) {
 uint32* r=d.mes.ring_cpu; uint32 base=pos; pos=(pos+MES_FRAME_DWORDS)% (64*1024/4);
 memset(r+base,0,MES_FRAME_DWORDS*4); return r+base;
}
static status_t submit(rdna4_device& d,uint32 base,uint64 fence,uint64 value,bigtime_t timeout) {
 uint32* r=d.mes.ring_cpu;
 r[base+0+0] = r[base+0];
 /* Fence is the architected MES API completion mechanism. The API frame is
    followed by a 64-bit fence address/value in the status structure. */
 r[base+60]=(uint32)fence; r[base+61]=(uint32)(fence>>32);
 r[base+62]=(uint32)value; r[base+63]=(uint32)(value>>32);
 d.mes.wptr=base+MES_FRAME_DWORDS;
 rdna4_doorbell_write(d, 0x0b << 1, d.mes.wptr);
 if(d.mes.wptr >= 64*1024/4) d.mes.wptr=0;
 return rdna4_mes_wait_api(d,fence,value?timeout:timeout);
}
status_t rdna4_mes_wait_api(rdna4_device& d,uint64 fence,bigtime_t timeout) {
 if(!d.mes.ready || !d.mes.status_cpu) return B_NO_INIT;
 bigtime_t end=system_time()+timeout;
 volatile uint64* p=(volatile uint64*)(addr_t)fence;
 (void)p;
 /* The caller normally supplies a GPU address. Resolve it through the
    driver's BO table so CPU polling never dereferences a raw GPU VA. */
 for(;;) {
  for(uint32 i=0;i<RDNA4_VM_MAX_BOS;i++) if(d.bos[i].used && fence>=d.bos[i].gpu && fence+8<=d.bos[i].gpu+d.bos[i].size) {
   volatile uint64* f=(volatile uint64*)((uint8*)d.bos[i].cpu+(fence-d.bos[i].gpu));
   if(*f!=0) return B_OK;
  }
  if(timeout>=0 && system_time()>=end) return B_TIMED_OUT;
  snooze(20);
 }
}
status_t rdna4_mes_set_hw_resources(rdna4_device& d,uint32 vmidMM,uint32 vmidGFX,uint32 gfxMask,uint32 computeMask,uint32 sdmaMask) {
 if(!d.mes.ready) return B_NO_INIT; uint32 p=0; uint32* q=frame(d,p); q[0]=hdr(MES_SET_HW);
 q[1]=vmidMM; q[2]=vmidGFX; q[3]=0; q[4]=0; q[5]=computeMask; q[5+8]=gfxMask; q[5+8+2]=sdmaMask;
 q[5+8+2+2]=0; /* aggregated doorbells */
 q[29]=(uint32)d.mes.status_gpu; q[30]=(uint32)(d.mes.status_gpu>>32);
 q[31]=(uint32)d.mes.status_gpu; q[32]=(uint32)(d.mes.status_gpu>>32);
 q[33]=0x100; q[38]=0x100; q[43]=0x100;
 q[49]=(1u<<0)|(1u<<2)|(1u<<4)|(1u<<10)|(1u<<20);
 return B_OK;
}
status_t rdna4_mes_set_scheduling_config(rdna4_device& d,uint64 quantum,uint64 grace,uint32 yield) {
 if(!d.mes.ready) return B_NO_INIT; uint32 p=64; uint32* q=frame(d,p); q[0]=hdr(MES_SET_SCHED);
 for(int i=0;i<5;i++){ q[1+i*2]=(uint32)grace; q[2+i*2]=(uint32)(grace>>32); q[11+i*2]=(uint32)quantum; q[12+i*2]=(uint32)(quantum>>32); q[21+i*2]=(uint32)grace; q[22+i*2]=(uint32)(grace>>32); }
 q[31]=yield; return B_OK;
}
status_t rdna4_mes_add_queue(rdna4_device& d,rdna4_mes_queue& q,uint32 pid,uint64 pt,uint64 va0,uint64 va1) {
 if(!d.mes.ready || q.mqd==0 || q.wptr==0) return B_BAD_VALUE; uint32 p=128; uint32* x=frame(d,p); x[0]=hdr(MES_ADD_QUEUE);
 x[1]=pid; x[2]=(uint32)pt; x[3]=(uint32)(pt>>32); x[4]=(uint32)va0; x[5]=(uint32)(va0>>32); x[6]=(uint32)va1; x[7]=(uint32)(va1>>32);
 x[16]=(uint32)q.doorbell; x[17]=(uint32)q.mqd; x[18]=(uint32)(q.mqd>>32); x[19]=(uint32)q.wptr; x[20]=(uint32)(q.wptr>>32);
 x[21]=(uint32)q.h_context; x[22]=(uint32)(q.h_context>>32); x[23]=(uint32)q.h_queue; x[24]=(uint32)(q.h_queue>>32);
 x[25]=q.type; x[30]=1u; x[32]=MES_PRIORITY_NORMAL; q.active=true; return B_OK;
}
status_t rdna4_mes_remove_queue(rdna4_device& d,rdna4_mes_queue& q) {
 if(!d.mes.ready || !q.active) return B_ENTRY_NOT_FOUND; uint32 p=192; uint32* x=frame(d,p); x[0]=hdr(MES_REMOVE_QUEUE); x[1]=q.doorbell; x[4]=0; x[5]=0; x[6]=0; x[7]=0; x[8]=q.type; q.active=false; return B_OK;
}
