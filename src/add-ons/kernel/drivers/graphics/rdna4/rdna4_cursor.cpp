#include "rdna4_cursor.h"
#include "driver.h"
#include <OS.h>
/* DCN4/HUBP cursor fields are common across DCN4.0/4.01.  The exact
   per-HUBP base is selected from the display instance by the accelerant. */
#define HUBP_STRIDE 0x800
#define CURSOR_BASE 0x3c0
#define CUR_ADDR 0x00
#define CUR_ADDR_HI 0x01
#define CUR_SIZE 0x02
#define CUR_CTL 0x03
#define CUR_POS 0x04
#define CUR_HOT 0x05
static inline volatile uint32* r(rdna4_device& d,uint32 hub,uint32 n){return (volatile uint32*)(d.mmio+((0x2c000+hub*HUBP_STRIDE+CURSOR_BASE+n)<<2));}
status_t rdna4_cursor_init(rdna4_device& d){if(!d.mmio)return B_NO_INIT;return B_OK;}
status_t rdna4_cursor_set(rdna4_device& d,uint64 gpu,uint32 w,uint32 h,uint32 pitch,bool en){
 if(!d.mmio||!w||!h||w>256||h>256||pitch<4*w)return B_BAD_VALUE;
 *r(d,0,CUR_ADDR)=(uint32)gpu; *r(d,0,CUR_ADDR_HI)=(uint32)(gpu>>32);
 *r(d,0,CUR_SIZE)=((w-1)&0x1ff)|(((h-1)&0x1ff)<<16);
 *r(d,0,CUR_CTL)=(1u<<0)|((pitch/64)&0xff)<<8; *r(d,0,CUR_POS)=0; *r(d,0,CUR_HOT)=0;
 uint32 ctl=*r(d,0,CUR_CTL); if(!en) ctl&=~1u; *r(d,0,CUR_CTL)=ctl; return B_OK;
}
status_t rdna4_cursor_move(rdna4_device& d,int32 x,int32 y){if(!d.mmio)return B_NO_INIT;*r(d,0,CUR_POS)=((uint32)x&0xffff)|(((uint32)y&0xffff)<<16);return B_OK;}
