#ifndef RDNA4_DISCOVERY_H
#define RDNA4_DISCOVERY_H
#include <SupportDefs.h>
struct rdna4_device;
#define RDNA4_DISCOVERY_MAX_IPS 96
#define RDNA4_DISCOVERY_MAX_BASES 8
struct rdna4_discovered_ip {
	uint16 hw_id; uint8 instance; uint8 harvest; uint8 major; uint8 minor; uint8 revision; uint8 base_count; uint8 reserved;
	uint64 base[RDNA4_DISCOVERY_MAX_BASES];
};
struct rdna4_discovery_state {
	uint32 version; uint32 binary_version_major; uint32 binary_version_minor; uint32 table_version;
	uint32 num_dies; uint32 ip_count; uint32 valid; uint32 reserved;
	rdna4_discovered_ip ip[RDNA4_DISCOVERY_MAX_IPS];
};
status_t rdna4_discovery_init(rdna4_device& d);
status_t rdna4_discovery_parse(rdna4_device& d,const void* data,size_t size);
status_t rdna4_discovery_scan_vram(rdna4_device& d);
const rdna4_discovery_state* rdna4_discovery_get(const rdna4_device& d);
#endif
