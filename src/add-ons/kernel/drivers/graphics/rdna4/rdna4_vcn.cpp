#include "rdna4_vcn.h"
#include "driver.h"
#include "rdna4_fw.h"
#include "rdna4_vm.h"
#include <OS.h>
#include <string.h>

static const uint32 VCN_BASE = 0x1fc00;
static const uint32 UVD_STATUS=0x0183, UVD_VCPU_CNTL=0x01d6;
static const uint32 UVD_MASTINT_EN=0x0121, UVD_RB_BASE_LO=0x012a, UVD_RB_BASE_HI=0x012b;
static const uint32 UVD_RB_SIZE=0x012c, UVD_RB_RPTR=0x00ac, UVD_RB_WPTR=0x00ad;
static const uint32 VCN_RB_ENABLE=0x0085, VCN_RB1_DB_CTRL=0x0072;
static const uint32 CACHE_BAR_LO=0x02cf, CACHE_BAR_HI=0x02d0, CACHE_OFF0=0x01c0, CACHE_SIZE0=0x01c1;
static const uint32 CACHE1_BAR_LO=0x02fb, CACHE1_BAR_HI=0x02fc, CACHE_OFF1=0x01c2, CACHE_SIZE1=0x01c3;
static const uint32 CACHE2_BAR_LO=0x02ff, CACHE2_BAR_HI=0x0300, CACHE_OFF2=0x01c4, CACHE_SIZE2=0x01c5;
static const uint32 VCN_VCPU_CLOCK=0x00000200u;
static const uint32 VCN_VCPU_BLKRST=0x10000000u;
static const uint32 VCN_VCPU_ENABLE=0x00000002u;
static const uint32 VCN_RB1_ENABLE=0x4u;

static inline volatile uint32* R(rdna4_device&d,uint32 off) {
 return (volatile uint32*)(d.mmio+((size_t)(VCN_BASE+off)<<2));
}
static inline uint32 rd(rdna4_device&d,uint32 off){return *R(d,off);}
static inline void wr(rdna4_device&d,uint32 off,uint32 v){*R(d,off)=v;(void)*R(d,off);}

static status_t map_area(rdna4_device&d,area_id area,void*cpu,size_t size,uint64&gpu) {
 physical_entry e; status_t s=get_memory_map(cpu,size,&e,1);
 if(s!=B_OK||e.size<size)return s!=B_OK?s:B_NOT_SUPPORTED;
 rdna4_bo bo={}; bo.area=area;bo.cpu=cpu;bo.size=size;bo.physical=e.address;bo.used=true;
 s=rdna4_vm_map_bo(d,bo,B_PAGE_SIZE); if(s!=B_OK)return s; gpu=bo.gpu; return B_OK;
}
static void free_vcn(rdna4_device&d){
 if(d.vcn.ring_area>=0){rdna4_bo b={};physical_entry e;if(get_memory_map(d.vcn.ring_cpu,d.vcn.ring_dwords*4,&e,1)==B_OK){b.area=d.vcn.ring_area;b.cpu=d.vcn.ring_cpu;b.size=d.vcn.ring_dwords*4;b.physical=e.address;b.gpu=d.vcn.ring_gpu;b.used=true;rdna4_vm_unmap_bo(d,b);}delete_area(d.vcn.ring_area);}
 if(d.vcn.work_area>=0){rdna4_bo b={};physical_entry e;if(get_memory_map(d.vcn.work_cpu,8*1024*1024,&e,1)==B_OK){b.area=d.vcn.work_area;b.cpu=d.vcn.work_cpu;b.size=8*1024*1024;b.physical=e.address;b.gpu=d.vcn.work_gpu;b.used=true;rdna4_vm_unmap_bo(d,b);}delete_area(d.vcn.work_area);}
 d.vcn={}; d.vcn.ring_area=d.vcn.work_area=-1;
}
status_t rdna4_vcn_start(rdna4_device&d){
 const rdna4_firmware_slot& f=d.firmware[RDNA4_FW_VCN];
 if(!f.staged||f.size==0)return B_NO_INIT;
 void*ringCpu=NULL; d.vcn.ring_dwords=4096;
 d.vcn.ring_area=create_area("rdna4 vcn ring",&ringCpu,B_ANY_KERNEL_ADDRESS,d.vcn.ring_dwords*4,B_CONTIGUOUS,B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA);
 if(d.vcn.ring_area<0)return d.vcn.ring_area; d.vcn.ring_cpu=(uint32*)ringCpu; memset(ringCpu,0,d.vcn.ring_dwords*4);
 status_t s=map_area(d,d.vcn.ring_area,ringCpu,d.vcn.ring_dwords*4,d.vcn.ring_gpu); if(s!=B_OK){free_vcn(d);return s;}
 void*workCpu=NULL; d.vcn.work_area=create_area("rdna4 vcn workspace",&workCpu,B_ANY_KERNEL_ADDRESS,8*1024*1024,B_CONTIGUOUS,B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA);
 if(d.vcn.work_area<0){free_vcn(d);return d.vcn.work_area;} d.vcn.work_cpu=workCpu;memset(workCpu,0,8*1024*1024);
 s=map_area(d,d.vcn.work_area,workCpu,8*1024*1024,d.vcn.work_gpu);if(s!=B_OK){free_vcn(d);return s;}

 uint64 firmwareGPU = f.psp_loaded_gpu;
 if (firmwareGPU == 0) return B_NO_INIT;
 uint32 fwSize=(f.payload_size+B_PAGE_SIZE-1)&~(uint32)(B_PAGE_SIZE-1);
 uint32 stackOff=fwSize, ctxOff=stackOff+1024*1024;
 wr(d,CACHE_BAR_LO,(uint32)firmwareGPU);wr(d,CACHE_BAR_HI,(uint32)(firmwareGPU>>32));
 wr(d,CACHE_OFF0,0);wr(d,CACHE_SIZE0,fwSize);
 wr(d,CACHE1_BAR_LO,(uint32)(d.vcn.work_gpu+stackOff));wr(d,CACHE1_BAR_HI,(uint32)((d.vcn.work_gpu+stackOff)>>32));
 wr(d,CACHE_OFF1,0);wr(d,CACHE_SIZE1,1024*1024);
 wr(d,CACHE2_BAR_LO,(uint32)(d.vcn.work_gpu+ctxOff));wr(d,CACHE2_BAR_HI,(uint32)((d.vcn.work_gpu+ctxOff)>>32));
 wr(d,CACHE_OFF2,0);wr(d,CACHE_SIZE2,1024*1024);
 wr(d,UVD_RB_BASE_LO,(uint32)d.vcn.ring_gpu);wr(d,UVD_RB_BASE_HI,(uint32)(d.vcn.ring_gpu>>32));
 wr(d,UVD_RB_SIZE,d.vcn.ring_dwords);
 wr(d,UVD_RB_RPTR,0);wr(d,UVD_RB_WPTR,0);
 uint32 rb=rd(d,VCN_RB_ENABLE)&~VCN_RB1_ENABLE;wr(d,VCN_RB_ENABLE,rb);
 wr(d,UVD_VCPU_CNTL,VCN_VCPU_CLOCK|VCN_VCPU_BLKRST);snooze(10000);
 wr(d,UVD_VCPU_CNTL,VCN_VCPU_CLOCK);snooze(10000);
 wr(d,UVD_MASTINT_EN,VCN_VCPU_ENABLE);
 rb=rd(d,VCN_RB_ENABLE)|VCN_RB1_ENABLE;wr(d,VCN_RB_ENABLE,rb);
 wr(d,VCN_RB1_DB_CTRL,(0x20u<<2)|1u);
 if((rd(d,UVD_STATUS)&0xffff)==0){/* firmware may report status asynchronously */}
 d.vcn.wptr=0;d.vcn.ready=true;if(d.shared)d.shared->feature_mask|=RDNA4_FEATURE_VIDEO_DECODE|RDNA4_FEATURE_VIDEO_ENCODE;return B_OK;
}
static bool valid_codec(rdna4_vcn_codec c){return c<=RDNA4_VCN_JPEG;}
status_t rdna4_vcn_init(rdna4_device&d){if(!d.mmio||!d.mmhub_ready)return B_NO_INIT;d.vcn.ring_area=d.vcn.work_area=-1;return B_OK;}
status_t rdna4_vcn_submit(rdna4_device&d,const rdna4_vcn_job&j){
 if(!d.vcn.ready||!valid_codec(j.codec)||!j.command_gpu||!j.command_dwords||j.command_dwords>2048||!j.fence_gpu)return B_BAD_VALUE;
 if((j.command_gpu&3)||((j.fence_gpu&7)))return B_BAD_VALUE;
 if(d.vcn.wptr+j.command_dwords+8>=d.vcn.ring_dwords)return B_WOULD_BLOCK;
 for(uint32 i=0;i<j.command_dwords;i++){
  uint32 src=0;
  for(uint32 k=0;k<RDNA4_VM_MAX_BOS;k++)if(d.bos[k].used&&j.command_gpu+i*4>=d.bos[k].gpu&&j.command_gpu+(i+1)*4<=d.bos[k].gpu+d.bos[k].size){src=((uint32*)d.bos[k].cpu)[(j.command_gpu+i*4-d.bos[k].gpu)/4];break;}
  d.vcn.ring_cpu[d.vcn.wptr++]=src;
 }
 /* Unified VCN rings consume pre-built VCN packets. The command stream is
    deliberately not rewritten by the kernel; codec-specific packet framing
    remains userspace/VA-API ABI. */
 wr(d,UVD_RB_WPTR,d.vcn.wptr);
	rdna4_doorbell_write32(d, 0x310, d.vcn.wptr);
 return B_OK;
}
status_t rdna4_vcn_wait(rdna4_device&d,uint64 gpu,bigtime_t timeout){
 bigtime_t end=system_time()+timeout;
 for(;;){for(uint32 i=0;i<RDNA4_VM_MAX_BOS;i++)if(d.bos[i].used&&gpu>=d.bos[i].gpu&&gpu+8<=d.bos[i].gpu+d.bos[i].size){volatile uint64*p=(volatile uint64*)((uint8*)d.bos[i].cpu+(gpu-d.bos[i].gpu));if(*p)return B_OK;}if(timeout>=0&&system_time()>=end)return B_TIMED_OUT;snooze(20);}
}
void rdna4_vcn_uninit(rdna4_device&d){if(d.vcn.ready)wr(d,UVD_VCPU_CNTL,VCN_VCPU_CLOCK|VCN_VCPU_BLKRST);free_vcn(d);if(d.shared)d.shared->feature_mask&=~(RDNA4_FEATURE_VIDEO_DECODE|RDNA4_FEATURE_VIDEO_ENCODE);}
