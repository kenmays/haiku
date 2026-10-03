#include "rdna4_mes_api.h"
#include "driver.h"
#include <OS.h>
#include <string.h>

#define FRAME_DW 64
#define RING_DW (64 * 1024 / 4)
enum { API_SCHED=1, OP_SET_HW=0, OP_SET_SCHED=1, OP_ADD_QUEUE=2, OP_REMOVE_QUEUE=3 };
enum { Q_GFX=0, Q_COMPUTE=1, Q_SDMA=2 };
static inline uint32 header(uint32 op){return (API_SCHED & 0xf)|((op&0xff)<<4)|(FRAME_DW<<12);}
static inline void put64(uint32*p,uint32 off,uint64 v){p[off]=(uint32)v;p[off+1]=(uint32)(v>>32);}
static uint32 alloc_frame(rdna4_device&d){
 uint32 base=d.mes.wptr; if(base+FRAME_DW>RING_DW)base=0;
 memset(d.mes.ring_cpu+base,0,FRAME_DW*4); d.mes.wptr=base+FRAME_DW; if(d.mes.wptr==RING_DW)d.mes.wptr=0; return base;
}
static status_t submit(rdna4_device&d,uint32 base,uint32 statusDw,bigtime_t timeout){
 if(!d.mes.ready||!d.mes.status_cpu)return B_NO_INIT;
 uint64 seq=++d.mes.completion_seq; *d.mes.status_cpu=0;
 uint32*p=d.mes.ring_cpu+base; put64(p,statusDw,d.mes.status_gpu);put64(p,statusDw+2,seq);
 /* GFX12 MES scheduler rings are 64-bit-doorbell rings; Linux uses MES ring0
    doorbell index 0x0b and writes the DW write pointer. */
 rdna4_doorbell_write(d,0x0b<<1,d.mes.wptr);
 bigtime_t end=system_time()+timeout;
 for(;;){
  uint64 v=*d.mes.status_cpu;
  if(v==seq)return B_OK;
  if((v>>32)&0x80000000u)return B_ERROR;
  if(timeout>=0&&system_time()>=end)return B_TIMED_OUT;
  if(d.irq_installed){
   bigtime_t left=end-system_time();
   if(left>0)rdna4_wait_fence_event(d,left>5000?5000:left);
  } else snooze(10);
 }
}
status_t rdna4_mes_wait_api(rdna4_device&d,uint64 fence,bigtime_t timeout){
 if(!d.mes.ready||fence!=d.mes.status_gpu)return B_BAD_VALUE;
 bigtime_t end=system_time()+timeout;
 for(;;){
  if(*d.mes.status_cpu!=0)return B_OK;
  if(timeout>=0&&system_time()>=end)return B_TIMED_OUT;
  if(d.irq_installed){bigtime_t left=end-system_time();if(left>0)rdna4_wait_fence_event(d,left>5000?5000:left);}
  else snooze(10);
 }
}
status_t rdna4_mes_set_hw_resources(rdna4_device&d,uint32 vmidMM,uint32 vmidGFX,uint32 gfxMask,uint32 computeMask,uint32 sdmaMask){
 if(!d.mes.ready)return B_NO_INIT;uint32 b=alloc_frame(d);uint32*p=d.mes.ring_cpu+b;p[0]=header(OP_SET_HW);
 p[1]=vmidMM;p[2]=vmidGFX;p[3]=0;p[4]=0;
 for(uint32 i=0;i<8;i++)p[5+i]=(computeMask>>i)&1;
 p[13]=gfxMask&1;p[14]=(gfxMask>>1)&1;p[15]=sdmaMask&1;p[16]=(sdmaMask>>1)&1;
 for(uint32 i=0;i<5;i++)p[17+i]=0;
 put64(p,22,d.mes.status_gpu);put64(p,24,d.mes.status_gpu);
 /* enable MES fence interrupt and keep reset/logging defaults. */
 p[54]=(1u<<20);p[55]=0;
 return submit(d,b,50,2100000);
}
status_t rdna4_mes_set_scheduling_config(rdna4_device&d,uint64 quantum,uint64 grace,uint32 yield){
 if(!d.mes.ready||yield>50)return B_BAD_VALUE;uint32 b=alloc_frame(d);uint32*p=d.mes.ring_cpu+b;p[0]=header(OP_SET_SCHED);
 for(uint32 i=0;i<5;i++){put64(p,1+i*2,grace);put64(p,11+i*2,quantum);put64(p,21+i*2,grace);}p[31]=yield;return submit(d,b,32,2100000);
}
status_t rdna4_mes_add_queue(rdna4_device&d,rdna4_mes_queue&q,uint32 pid,uint64 pt,uint64 va0,uint64 va1){
 if(!d.mes.ready||q.mqd==0||q.wptr==0)return B_BAD_VALUE;uint32 b=alloc_frame(d);uint32*p=d.mes.ring_cpu+b;p[0]=header(OP_ADD_QUEUE);
 p[1]=pid;put64(p,2,pt);put64(p,4,va0);put64(p,6,va1);put64(p,8,0);put64(p,10,0);put64(p,12,0);put64(p,14,0);
 p[16]=0;p[17]=1;p[18]=q.doorbell;put64(p,19,q.mqd);put64(p,21,q.wptr);put64(p,23,q.h_context);put64(p,25,q.h_queue);
 p[27]=q.type;p[28]=0;p[29]=0;p[30]=0;p[31]=0;p[32]=0;put64(p,33,0);p[35]=0;p[36]=(1u<<0);q.active=true;
 status_t s=submit(d,b,37,2100000);if(s!=B_OK)q.active=false;return s;
}
status_t rdna4_mes_remove_queue(rdna4_device&d,rdna4_mes_queue&q){
 if(!d.mes.ready||!q.active)return B_ENTRY_NOT_FOUND;uint32 b=alloc_frame(d);uint32*p=d.mes.ring_cpu+b;p[0]=header(OP_REMOVE_QUEUE);p[1]=q.doorbell;put64(p,2,q.h_context);p[4]=0;put64(p,5,d.mes.status_gpu);put64(p,7,d.mes.completion_seq+1);p[9]=0;p[10]=0;p[11]=0;p[13]=0;p[14]=q.type;
 status_t s=submit(d,b,5,2100000);if(s==B_OK)q.active=false;return s;
}
