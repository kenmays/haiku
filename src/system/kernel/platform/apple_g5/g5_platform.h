/* Apple Power Mac G5 platform support. */
#ifndef _KERNEL_PLATFORM_APPLE_G5_H
#define _KERNEL_PLATFORM_APPLE_G5_H

#include <SupportDefs.h>
#include <boot/kernel_args.h>

namespace AppleG5 {

enum chipset_type { CHIPSET_UNKNOWN = 0, CHIPSET_U3, CHIPSET_U3H };
enum cpu_type { CPU_UNKNOWN = 0, CPU_970, CPU_970FX, CPU_970MP };
struct machine_info { chipset_type chipset; cpu_type cpu; uint32 cpuCount; uint64 memorySize; };

static const char* const kU3Compatible = "u3";
static const char* const kU4Compatible = "u4";
static const char* const kU3AGPCompatible = "u3-agp";
static const char* const kU3HTCompatible = "u3-ht";
static const char* const kU4PCIeCompatible = "u4-pcie";
static const char* const kK2MacIOCompatible = "k2-mac-io";
static const char* const kK2MacIOName = "mac-io";
static const uint32 kU3MPICIRQCount = 124;
static const uint32 kK2MPICIRQCount = 120;
static const uint32 kMaxG5CPUs = 4;

bool detect(machine_info& info);
status_t init(kernel_args* args);
status_t init_post_vm(kernel_args* args);

}

#endif
