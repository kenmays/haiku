#ifndef RDNA4_GFX_H
#define RDNA4_GFX_H

#include <SupportDefs.h>

#define RDNA4_PM4_TYPE3 3u
#define RDNA4_PM4_PACKET3(op, count) 	((RDNA4_PM4_TYPE3 << 30) | (((op) & 0xffu) << 8) | (((count) - 1) & 0x3fffu))

enum rdna4_pm4_opcode {
	RDNA4_PM4_NOP = 0x10,
	RDNA4_PM4_INDIRECT_BUFFER = 0x3f,
	RDNA4_PM4_WRITE_DATA = 0x37,
	RDNA4_PM4_EVENT_WRITE = 0x46,
	RDNA4_PM4_RELEASE_MEM = 0x49
};

struct rdna4_command_buffer {
	const uint32* words;
	uint32 word_count;
};

uint32 rdna4_pm4_nop(uint32* out, uint32 count);
uint32 rdna4_pm4_write_data(uint32* out, uint64 address,
	uint32 value);
uint32 rdna4_pm4_release_mem(uint32* out, uint64 address,
	uint64 value);
uint32 rdna4_pm4_indirect_buffer(uint32* out, uint64 address,
	uint32 size_dw);

status_t rdna4_validate_command_buffer(const rdna4_command_buffer& command);

#endif

status_t rdna4_gfx_ring_alloc(rdna4_device& device);
void rdna4_gfx_ring_free(rdna4_device& device);
status_t rdna4_gfx_ring_write(rdna4_device& device, const uint32* packets,
	size_t dwords);
status_t rdna4_gfx_ring_kick(rdna4_device& device);
status_t rdna4_gfx_program_ring(rdna4_device& device);
status_t rdna4_gfx_program_mqd(rdna4_device& device);
