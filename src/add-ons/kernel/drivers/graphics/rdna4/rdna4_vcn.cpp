#include "rdna4_vcn.h"
#include "driver.h"
#include <OS.h>
#include <string.h>
/* VCN5 uses the VCN ring/firmware interface. Keep command construction in
   userspace-compatible GPU memory and let the kernel validate/submit it.
   Firmware names are supplied by the standard amdgpu firmware package. */
static const uint32 VCN_STATUS=0x000, VCN_GPCOM_DATA0=0x001, VCN_GPCOM_DATA1=0x002, VCN_GPCOM_CMD=0x003;
static bool valid_codec(rdna4_vcn_codec c){return c<=RDNA4_VCN_JPEG;}
status_t rdna4_vcn_init(rdna4_device& d){if(!d.mmio||!d.gfxhub_ready)return B_NO_INIT;return B_OK;}
status_t rdna4_vcn_submit(rdna4_device& d,const rdna4_vcn_job& j){
 if(!valid_codec(j.codec)||!j.command_gpu||!j.command_dwords||j.command_dwords>4096||!j.fence_gpu)return B_BAD_VALUE;
 /* VCN command buffers are consumed through the firmware command ring. Until
    PSP/VCN firmware is authenticated, do not expose a false RUNNING state. */
 if(!d.shared || !(d.shared->feature_mask&RDNA4_FEATURE_VIDEO_DECODE) && !j.encode)return B_NOT_SUPPORTED;
 return B_NOT_SUPPORTED;
}
status_t rdna4_vcn_wait(rdna4_device& d,uint64 gpu,bigtime_t timeout){
 if(!d.shared)return B_NO_INIT; bigtime_t end=system_time()+timeout;
 for(;;){for(uint32 i=0;i<RDNA4_VM_MAX_BOS;i++)if(d.bos[i].used&&gpu>=d.bos[i].gpu&&gpu+8<=d.bos[i].gpu+d.bos[i].size){volatile uint64* p=(volatile uint64*)((uint8*)d.bos[i].cpu+(gpu-d.bos[i].gpu));if(*p)return B_OK;}if(timeout>=0&&system_time()>=end)return B_TIMED_OUT;snooze(20);}
}
void rdna4_vcn_uninit(rdna4_device& d){if(d.shared)d.shared->feature_mask&=~(RDNA4_FEATURE_VIDEO_DECODE|RDNA4_FEATURE_VIDEO_ENCODE);}
