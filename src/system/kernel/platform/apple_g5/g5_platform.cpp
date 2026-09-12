/* Apple Power Mac G5 platform discovery and bootstrap. */
#include "g5_platform.h"
#include "g5_dart.h"
#include <arch_cpu.h>
#include <platform/openfirmware/openfirmware.h>
#include <boot/kernel_args.h>
#include <debug.h>
#include <vm/vm.h>
#include <string.h>

namespace AppleG5 {
static cpu_type cpu_from_pvr(uint32 pvr){switch((uint16)(pvr>>16)){case 0x0039:return CPU_970;case 0x003c:case 0x0044:return CPU_970FX;case 0x0045:return CPU_970MP;default:return CPU_UNKNOWN;}}
static chipset_type chipset_from_model(const char* model){if(!model)return CHIPSET_UNKNOWN;if(strstr(model,"U4")||strstr(model,"PowerMac11"))return CHIPSET_U3H;if(strstr(model,"U3H"))return CHIPSET_U3H;if(strstr(model,"U3")||strstr(model,"PowerMac"))return CHIPSET_U3;return CHIPSET_UNKNOWN;}
static bool find_compatible(const char* type,const char* compatible,intptr_t& node){intptr_t cookie=0;while(true){char path[B_PATH_NAME_LENGTH];if(of_get_next_device(&cookie,0,type,path,sizeof(path))!=B_OK)break;char b[256];int n=of_getprop(cookie,"compatible",b,sizeof(b)-1);if(n<=0)continue;b[n]='\0';if(strstr(b,compatible)){node=cookie;return true;}}return false;}
static bool get_reg(intptr_t node,phys_addr_t& base,size_t& size){intptr_t parent=of_parent(node);uint32 ac=2,sc=1,value;if(parent>0){if(of_getprop(parent,"#address-cells",&value,sizeof(value))>0)ac=B_BENDIAN_TO_HOST_INT32(value);if(of_getprop(parent,"#size-cells",&value,sizeof(value))>0)sc=B_BENDIAN_TO_HOST_INT32(value);}uint32 reg[8]={};int len=of_getprop(node,"reg",reg,sizeof(reg));if(len<=0||ac==0||ac>2||sc==0||sc>2)return false;const uint32* p=reg;base=0;for(uint32 i=0;i<ac;i++)base=(base<<32)|B_BENDIAN_TO_HOST_INT32(*p++);uint64 s=0;for(uint32 i=0;i<sc;i++)s=(s<<32)|B_BENDIAN_TO_HOST_INT32(*p++);if(!base||!s)return false;size=(size_t)s;return true;}
bool detect(machine_info& info,kernel_args* args){info.chipset=CHIPSET_UNKNOWN;info.cpu=CPU_UNKNOWN;info.cpuCount=args?args->num_cpus:1;info.memorySize=0;if(args)for(uint32 i=0;i<args->num_physical_memory_ranges;i++)info.memorySize+=args->physical_memory_range[i].size;#if defined(__powerpc64__) info.cpu=cpu_from_pvr(get_pvr());char model[128]={};if(of_getprop(gChosen,"model",model,sizeof(model))!=OF_FAILED)info.chipset=chipset_from_model(model);return info.cpu!=CPU_UNKNOWN;#else return false;#endif}
status_t init(kernel_args* args){machine_info info;if(!detect(info,args))return B_BAD_VALUE;dprintf("Apple G5: CPU=%d chipset=%d CPUs=%" B_PRIu32 " memory=%" B_PRIu64 " MB\n",info.cpu,info.chipset,info.cpuCount,info.memorySize/(1024*1024));return B_OK;}
status_t init_post_vm(kernel_args*){intptr_t node=0;bool u4=find_compatible("dart","u4-dart",node);if(!u4&&!find_compatible("dart","u3-dart",node)){dprintf("apple_g5: no DART node in Open Firmware\n");return B_OK;}phys_addr_t physical;size_t size;if(!get_reg(node,physical,size)){dprintf("apple_g5: invalid DART reg property\n");return B_BAD_VALUE;}void* mapped=NULL;area_id area=map_physical_memory("g5-dart-registers",physical,size,B_ANY_KERNEL_ADDRESS,B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA,&mapped);if(area<0)return area;status_t status=AppleG5DART::Init((addr_t)mapped,size,u4);if(status!=B_OK)dprintf("apple_g5: DART initialization failed: %" B_PRId32 "\n",status);return status;}
}
extern "C" status_t apple_g5_platform_init(kernel_args* args){return AppleG5::init(args);}extern "C" status_t apple_g5_platform_init_post_vm(kernel_args* args){return AppleG5::init_post_vm(args);}
