/* Apple Power Mac G5 U3/U3H/U4 DART support. */
#include "g5_dart.h"
#include <KernelExport.h>
#include <ByteOrder.h>
#include <arch/cpu.h>
#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/vm_types.h>
#include <string.h>

namespace {
static volatile uint32* sRegisters;
static uint32* sTable;
static phys_addr_t sTablePhysical;
static uint8* sUsed;
static size_t sTableBytes;
static size_t sEntries;
static bool sEnabled;
static bool sU4;
static const uint32 U3_CNTL=0x00, U3_BASE_SHIFT=12, U3_BASE_MASK=0xfffff;
static const uint32 U3_FLUSH=0x400, U3_ENABLE=0x200, U3_SIZE_MASK=0x1ff;
static const uint32 U4_BASE=0x10, U4_SIZE=0x20, U4_FLUSH=0x20000000U;
static const uint32 U4_ENABLE=0x80000000U, U4_SIZE_MASK=0x1fff;
static const uint32 VALID=0x80000000U, RPNMASK=0x00ffffffU;
static inline uint32 readReg(uint32 o)
	{ return B_BENDIAN_TO_HOST_INT32(sRegisters[o / 4]); }
static inline void writeReg(uint32 o,uint32 v)
	{ sRegisters[o / 4]=B_HOST_TO_BENDIAN_INT32(v); eieio(); }
static void flushTLB()
{
	if (sU4) {
		uint32 v=readReg(0);
		writeReg(0,v|U4_FLUSH);
		for (uint32 i=0;i<100000;i++) { if (!(readReg(0)&U4_FLUSH)) return; asm volatile("sync" ::: "memory"); }
		return;
	}
	uint32 v=readReg(0); writeReg(0,v|U3_FLUSH);
	for (uint32 i=0;i<100000;i++) { if (!(readReg(0)&U3_FLUSH)) return; asm volatile("sync" ::: "memory"); }
	writeReg(0,v&~U3_FLUSH); writeReg(0,v|U3_FLUSH);
}
static bool findRun(size_t pages,size_t& first)
{
	if (pages==0 || pages>=sEntries-1) return false;
	for(size_t p=1;p+pages<sEntries;p++){size_t i=0;for(;i<pages;i++)if(sUsed[p+i])break;if(i==pages){first=p;return true;}p+=i;}
	return false;
}
}

namespace AppleG5DART {
status_t Init(addr_t base,size_t size,bool u4)
{
	if(base==0||size<0x100)return B_BAD_VALUE;
	if(sEnabled)return B_OK;
	sRegisters=(volatile uint32*)base;sU4=u4;sTableBytes=2*1024*1024;sEntries=sTableBytes/4;
	physical_address_restrictions r={};r.low_address=0;r.high_address=0x80000000ULL;r.alignment=16*1024*1024;
	vm_page* run=vm_page_allocate_page_run(PAGE_STATE_WIRED|VM_PAGE_ALLOC_CLEAR,sTableBytes/B_PAGE_SIZE,&r,VM_PRIORITY_SYSTEM);
	if(!run)return B_NO_MEMORY;
	sTablePhysical=run->physical_page_number*B_PAGE_SIZE;
	void* mapped=NULL;area_id area=map_physical_memory("g5-dart-table",sTablePhysical,sTableBytes,B_ANY_KERNEL_ADDRESS,B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA,&mapped);
	if(area<0)return area;sTable=(uint32*)mapped;
	sUsed=(uint8*)kernel_memory_allocate(sEntries,B_PAGE_SIZE,B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA);if(!sUsed)return B_NO_MEMORY;memset(sUsed,0,sEntries);
	physical_address_restrictions dr={};dr.low_address=0;dr.high_address=0x80000000ULL;
	vm_page* dummy=vm_page_allocate_page_run(PAGE_STATE_WIRED|VM_PAGE_ALLOC_CLEAR,1,&dr,VM_PRIORITY_SYSTEM);if(!dummy)return B_NO_MEMORY;
	const uint32 empty=VALID|((uint32)dummy->physical_page_number&RPNMASK);for(size_t i=0;i<sEntries;i++)sTable[i]=B_HOST_TO_BENDIAN_INT32(empty);sUsed[0]=sUsed[sEntries-1]=1;arch_cpu_memory_write_barrier();
	const uint32 basePages=(uint32)(sTablePhysical>>12), tablePages=(uint32)(sTableBytes>>12);
	if(sU4){writeReg(U4_BASE,basePages);writeReg(U4_SIZE,tablePages&U4_SIZE_MASK);writeReg(0,U4_ENABLE);}else writeReg(0,U3_ENABLE|((basePages&U3_BASE_MASK)<<U3_BASE_SHIFT)|(tablePages&U3_SIZE_MASK));
	flushTLB();sEnabled=true;dprintf("apple_g5: DART %s phys=%" B_PRIxPHYSADDR " entries=%" B_PRIuSIZE "\n",sU4?"U4":"U3",sTablePhysical,sEntries);return B_OK;
}
void Shutdown(){if(sRegisters)writeReg(0,readReg(0)&~(sU4?U4_ENABLE:U3_ENABLE));sEnabled=false;}
status_t Map(addr_t physical,size_t size,addr_t* out)
{
	if(!sEnabled||!out||!size)return B_BAD_VALUE;physical&=~(addr_t)(B_PAGE_SIZE-1);size=(size+B_PAGE_SIZE-1)&~(size_t)(B_PAGE_SIZE-1);size_t pages=size/B_PAGE_SIZE,first;if(!findRun(pages,first))return B_NO_MEMORY;
	for(size_t i=0;i<pages;i++){sTable[first+i]=B_HOST_TO_BENDIAN_INT32(VALID|((uint32)(physical>>12)&RPNMASK));sUsed[first+i]=1;physical+=B_PAGE_SIZE;}arch_cpu_memory_write_barrier();flushTLB();*out=first*B_PAGE_SIZE;return B_OK;
}
status_t Unmap(addr_t dma,size_t size)
{
	if(!sEnabled||!size)return B_BAD_VALUE;dma&=~(addr_t)(B_PAGE_SIZE-1);size=(size+B_PAGE_SIZE-1)&~(size_t)(B_PAGE_SIZE-1);size_t first=dma/B_PAGE_SIZE,pages=size/B_PAGE_SIZE;if(first==0||first+pages>=sEntries)return B_BAD_VALUE;for(size_t i=0;i<pages;i++){sTable[first+i]=0;sUsed[first+i]=0;}arch_cpu_memory_write_barrier();flushTLB();return B_OK;
}
addr_t Base(){return 0;}size_t Size(){return sTableBytes;}
}
