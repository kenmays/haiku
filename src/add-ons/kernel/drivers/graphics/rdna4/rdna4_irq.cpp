#include "rdna4_irq.h"
#include "driver.h"
#include "rdna4_vm.h"
#include <KernelExport.h>
#include <OS.h>
#include <PCI.h>
#include <string.h>

#define IH_BASE 0x4280
#define IH_RB_CNTL (IH_BASE+0x80)
#define IH_RB_BASE (IH_BASE+0x81)
#define IH_RB_BASE_HI (IH_BASE+0x82)
#define IH_RB_RPTR (IH_BASE+0x83)
#define IH_RB_WPTR (IH_BASE+0x84)
#define IH_RB_WPTR_ADDR_HI (IH_BASE+0x85)
#define IH_RB_WPTR_ADDR_LO (IH_BASE+0x86)
#define IH_DOORBELL_RPTR (IH_BASE+0x87)
#define IH_RB_ENABLE (1u<<0)
#define IH_RB_SIZE_MASK (0x3fu<<1)
#define IH_RB_WPTR_WB (1u<<8)
#define IH_RB_OVERFLOW_EN (1u<<16)
#define IH_RB_ENABLE_INTR (1u<<17)
#define IH_RB_SNOOP (1u<<20)
#define IH_RB_REARM (1u<<21)
#define IH_RB_MC_SPACE (7u<<28)
#define IH_DWORDS 8192
#define IH_ENTRY_DWORDS 8
/* GFX12 CP EOP is source 181. VM protection faults are delivered through
 * the VM client source on current GC12 firmware; the status is also
 * latched in the VM hub and is consumed below when the source arrives. */
#define RDNA4_SRC_CP_EOP 181
#define RDNA4_SRC_MES_HOST 177
#define RDNA4_SRC_VM_FAULT 146

static inline volatile uint32* R(rdna4_device&d,uint32 r){return (volatile uint32*)(d.mmio+((size_t)r<<2));}
static inline uint32 rd(rdna4_device&d,uint32 r){return *R(d,r);}
static inline void wr(rdna4_device&d,uint32 r,uint32 v){*R(d,r)=v;(void)*R(d,r);}

static status_t setup_ih(rdna4_device&d)
{
 void*cpu=NULL;d.ih_area=create_area("rdna4 IH ring",&cpu,B_ANY_KERNEL_ADDRESS,IH_DWORDS*4,B_CONTIGUOUS,B_KERNEL_READ_AREA|B_KERNEL_WRITE_AREA);
 if(d.ih_area<0)return d.ih_area;
 d.ih_cpu=(uint32*)cpu;memset(cpu,0,IH_DWORDS*4);physical_entry e;
 status_t s=get_memory_map(cpu,IH_DWORDS*4,&e,1);if(s!=B_OK||e.size<IH_DWORDS*4){delete_area(d.ih_area);d.ih_area=-1;return s!=B_OK?s:B_NOT_SUPPORTED;}
 d.ih_phys=e.address;d.ih_gpu=e.address;
 rdna4_bo b={};b.area=d.ih_area;b.cpu=cpu;b.size=IH_DWORDS*4;b.physical=e.address;b.used=true;
 s=rdna4_vm_map_bo(d,b,B_PAGE_SIZE);if(s!=B_OK){delete_area(d.ih_area);d.ih_area=-1;return s;}d.ih_gpu=b.gpu;
 uint32 c=rd(d,IH_RB_CNTL);c&=~(IH_RB_SIZE_MASK|IH_RB_ENABLE|IH_RB_ENABLE_INTR);
 c|=(13u<<1)|IH_RB_WPTR_WB|IH_RB_OVERFLOW_EN|IH_RB_SNOOP|(4u<<28);
 wr(d,IH_RB_BASE,(uint32)(d.ih_gpu>>8));wr(d,IH_RB_BASE_HI,(uint32)(d.ih_gpu>>40)&0xff);
 wr(d,IH_RB_RPTR,0);wr(d,IH_RB_WPTR,0);wr(d,IH_RB_WPTR_ADDR_LO,(uint32)e.address);wr(d,IH_RB_WPTR_ADDR_HI,(uint32)(e.address>>32)&0xffff);
 /* Keep IH RPTR MMIO-driven until the NBIO CAM doorbell route is enabled.
  * This is the same hardware ring, with no dependency on doorbell aperture. */
 wr(d,IH_DOORBELL_RPTR,0);
 wr(d,IH_RB_CNTL,c|IH_RB_ENABLE|IH_RB_ENABLE_INTR);d.ih_rptr=0;d.ih_enabled=true;return B_OK;
}
static void process_ih(rdna4_device&d)
{
 if(!d.ih_enabled)return;
 uint32 wp=rd(d,IH_RB_WPTR)&((IH_DWORDS*4)-4),rp=d.ih_rptr;
 while(rp!=wp){
  uint32*e=&d.ih_cpu[rp>>2];
  uint32 source=e[1]&0xff,client=e[0]&0xff;
  if(source==RDNA4_SRC_CP_EOP||source==RDNA4_SRC_MES_HOST){d.interrupt_count++;d.mes_last_irq_data=e[4];if(d.fence_sem>=0)release_sem(d.fence_sem);}
  else if(source==RDNA4_SRC_VM_FAULT){
   d.vm_fault_count++;d.vm_fault_address=((uint64)e[5]<<32)|e[4];d.vm_fault_status=e[6];d.vm_fault_vmid=(e[0]>>24)&0xf;
   if(d.fence_sem>=0)release_sem(d.fence_sem);
  } else {d.interrupt_count++;if(d.fence_sem>=0)release_sem(d.fence_sem);}
  rp=(rp+IH_ENTRY_DWORDS*4)%(IH_DWORDS*4);
 }
 d.ih_rptr=rp;wr(d,IH_RB_RPTR,rp);
}
static int32 rdna4_gpu_interrupt(void*cookie)
{
 rdna4_device*d=(rdna4_device*)cookie;if(!d||!d->mmio)return B_UNHANDLED_INTERRUPT;process_ih(*d);return B_INVOKE_SCHEDULER;
}
status_t rdna4_irq_init(rdna4_device&d)
{
 if(!d.pci)return B_NO_INIT;
 if(d.fence_sem<0)d.fence_sem=create_sem(0,"rdna4 fence irq");
 if(d.fence_sem<0)return d.fence_sem;
 status_t s=setup_ih(d);if(s!=B_OK){delete_sem(d.fence_sem);d.fence_sem=-1;return s;}
 s=install_io_interrupt_handler(d.pci->u.h0.interrupt_line,rdna4_gpu_interrupt,&d,0);
 if(s!=B_OK){rdna4_irq_uninit(d);return s;}d.irq_installed=true;return B_OK;
}
void rdna4_irq_uninit(rdna4_device&d)
{
 if(d.irq_installed&&d.pci){remove_io_interrupt_handler(d.pci->u.h0.interrupt_line,rdna4_gpu_interrupt,&d);d.irq_installed=false;}
 if(d.ih_enabled){wr(d,IH_RB_CNTL,rd(d,IH_RB_CNTL)&~IH_RB_ENABLE);d.ih_enabled=false;}
 if(d.ih_area>=0){
  rdna4_bo b={};b.area=d.ih_area;b.cpu=d.ih_cpu;b.size=IH_DWORDS*4;b.physical=d.ih_phys;b.gpu=d.ih_gpu;b.used=true;
  if(d.vm_ready&&d.ih_gpu)rdna4_vm_unmap_bo(d,b);
  delete_area(d.ih_area);d.ih_area=-1;
 }
 d.ih_cpu=NULL;d.ih_gpu=0;d.ih_rptr=0;
 if(d.fence_sem>=0){delete_sem(d.fence_sem);d.fence_sem=-1;}
}
status_t rdna4_wait_fence_event(rdna4_device&d,bigtime_t timeout){if(d.fence_sem<0)return B_NO_INIT;return acquire_sem_etc(d.fence_sem,1,B_RELATIVE_TIMEOUT,timeout);}
