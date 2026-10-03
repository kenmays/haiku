#ifndef RDNA4_VCN_H
#define RDNA4_VCN_H
#include <SupportDefs.h>
struct rdna4_device;
enum rdna4_vcn_codec { RDNA4_VCN_H264=0, RDNA4_VCN_HEVC, RDNA4_VCN_VP9, RDNA4_VCN_AV1, RDNA4_VCN_JPEG };
struct rdna4_vcn_job { rdna4_vcn_codec codec; bool encode; uint64 command_gpu; uint32 command_dwords; uint64 fence_gpu; uint64 fence_value; };
status_t rdna4_vcn_init(rdna4_device&);
status_t rdna4_vcn_start(rdna4_device&);
status_t rdna4_vcn_submit(rdna4_device&,const rdna4_vcn_job&);
status_t rdna4_vcn_wait(rdna4_device&,uint64,bigtime_t);
void rdna4_vcn_uninit(rdna4_device&);
#endif
