#ifndef RDNA4_CURSOR_H
#define RDNA4_CURSOR_H
#include <SupportDefs.h>
struct rdna4_device;
status_t rdna4_cursor_init(rdna4_device&);
status_t rdna4_cursor_set(rdna4_device&,uint64 gpu,uint32 width,uint32 height,uint32 pitch,bool enable);
status_t rdna4_cursor_move(rdna4_device&,int32 x,int32 y);
#endif
