#include "rdna4_discovery.h"
#include "driver.h"
#include <string.h>
#pragma pack(1)
struct table_info { uint16 offset, checksum, size, padding; };
struct binary_header { uint32 signature; uint16 version_major,version_minor,binary_checksum,binary_size; table_info tables[6]; };
struct die_info { uint16 die_id,die_offset; };
struct ip_discovery_header { uint32 signature; uint16 version,size; uint32 id; uint16 num_dies; die_info dies[16]; uint8 base_addr_64_bit; uint8 reserved; };
struct die_header { uint16 die_id,num_ips; };
struct ip_v3 { uint16 hw_id; uint8 instance,base_count,major,minor,revision,harvest; uint32 base[1]; };
struct ip_v4 { uint16 hw_id; uint8 instance,base_count,major,minor,revision,harvest; uint64 base[1]; };
#pragma pack()
static bool range_ok(size_t o,size_t n,size_t s){return o<=s&&n<=s-o;}
static uint16 sum16(const uint8*p,size_t n){uint32 s=0;for(size_t i=0;i<n;i++)s+=p[i];return (uint16)s;}
status_t rdna4_discovery_parse(rdna4_device& d,const void*data,size_t size)
{
	if(!data||size<sizeof(binary_header))return B_BAD_DATA;
	const uint8*b=(const uint8*)data;const binary_header*h=(const binary_header*)b;
	if(h->signature!=7&&h->signature!=0x14&&h->signature!=0x21&&h->signature!=0x28)return B_BAD_DATA;
	if(h->binary_size<sizeof(binary_header)||h->binary_size>size)return B_BAD_DATA;
	size_t co=offsetof(binary_header,binary_checksum)+2;
	if(sum16(b+co,h->binary_size-co)!=h->binary_checksum)return B_BAD_DATA;
	const table_info&ti=h->tables[0];
	if(!ti.offset||!range_ok(ti.offset,ti.size,h->binary_size))return B_BAD_DATA;
	const ip_discovery_header*ih=(const ip_discovery_header*)(b+ti.offset);
	if(!ih->num_dies||ih->num_dies>16||ih->signature==0)return B_BAD_DATA;
	memset(&d.discovery,0,sizeof(d.discovery));d.discovery.version=1;
	d.discovery.binary_version_major=h->version_major;d.discovery.binary_version_minor=h->version_minor;
	d.discovery.table_version=ih->version;d.discovery.num_dies=ih->num_dies;
	for(uint32 di=0;di<ih->num_dies;di++){
		uint16 doff=ih->dies[di].die_offset;if(!range_ok(doff,sizeof(die_header),h->binary_size))return B_BAD_DATA;
		const die_header*dh=(const die_header*)(b+doff);if(dh->die_id!=di||dh->num_ips>RDNA4_DISCOVERY_MAX_IPS)return B_BAD_DATA;
		size_t off=doff+sizeof(*dh);
		for(uint32 j=0;j<dh->num_ips;j++){
			if(d.discovery.ip_count>=RDNA4_DISCOVERY_MAX_IPS||!range_ok(off,8,h->binary_size))return B_BUFFER_OVERFLOW;
			const ip_v3*p=(const ip_v3*)(b+off);uint32 n=p->base_count;if(!n||n>RDNA4_DISCOVERY_MAX_BASES)return B_BAD_DATA;
			size_t bytes=8+n*(ih->base_addr_64_bit?8:4);if(!range_ok(off,bytes,h->binary_size))return B_BAD_DATA;
			rdna4_discovered_ip&o=d.discovery.ip[d.discovery.ip_count++];o.hw_id=p->hw_id;o.instance=p->instance;o.harvest=p->harvest;o.major=p->major;o.minor=p->minor;o.revision=p->revision;o.base_count=n;
			if(ih->base_addr_64_bit){const uint64*a=(const uint64*)(b+off+8);for(uint32 k=0;k<n;k++)o.base[k]=a[k];}
			else{const uint32*a=(const uint32*)(b+off+8);for(uint32 k=0;k<n;k++)o.base[k]=a[k];}
			off+=bytes;
		}
	}
	d.discovery.valid=1;return B_OK;
}
status_t rdna4_discovery_scan_vram(rdna4_device&d)
{
	if(!d.framebuffer||d.fb_size<4096)return B_NO_INIT;
	size_t scan=d.fb_size<(16u<<20)?d.fb_size:(16u<<20),start=d.fb_size-scan;
	for(size_t off=start;off+sizeof(binary_header)<=d.fb_size;off+=256){
		const binary_header*h=(const binary_header*)(d.framebuffer+off);
		if(h->signature!=7&&h->signature!=0x14&&h->signature!=0x21&&h->signature!=0x28)continue;
		if(!h->binary_size||h->binary_size>(4u<<20)||off+h->binary_size>d.fb_size)continue;
		if(rdna4_discovery_parse(d,d.framebuffer+off,h->binary_size)==B_OK)return B_OK;
	}
	return B_ENTRY_NOT_FOUND;
}
status_t rdna4_discovery_init(rdna4_device&d){memset(&d.discovery,0,sizeof(d.discovery));return rdna4_discovery_scan_vram(d);}
const rdna4_discovery_state*rdna4_discovery_get(const rdna4_device&d){return &d.discovery;}
