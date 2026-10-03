#include "rdna4_gfx.h"

static inline bool
is_privileged_opcode(uint32 opcode)
{
	/*
	 * Register writes and context-control packets are not accepted from an
	 * untrusted user command stream. The native ABI will construct those
	 * packets in kernel-owned submission paths.
	 */
	switch (opcode) {
		case RDNA4_PM4_WRITE_DATA:
		case RDNA4_PM4_RELEASE_MEM:
		case RDNA4_PM4_INDIRECT_BUFFER:
			return true;
		default:
			return false;
	}
}

uint32
rdna4_pm4_nop(uint32* out, uint32 count)
{
	if (out == NULL || count == 0)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_NOP, count);
	for (uint32 i = 1; i < count; i++)
		out[i] = 0;
	return count;
}

uint32
rdna4_pm4_write_data(uint32* out, uint64 address, uint32 value)
{
	if (out == NULL)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_WRITE_DATA, 4);
	out[1] = 0;
	out[2] = (uint32)address;
	out[3] = (uint32)(address >> 32);
	out[4] = value;
	return 5;
}

uint32
rdna4_pm4_release_mem(uint32* out, uint64 address, uint64 value)
{
	if (out == NULL)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_RELEASE_MEM, 7);
	out[1] = 0;
	out[2] = 0;
	out[3] = (uint32)address;
	out[4] = (uint32)(address >> 32);
	out[5] = (uint32)value;
	out[6] = (uint32)(value >> 32);
	out[7] = 0;
	return 8;
}

uint32
rdna4_pm4_indirect_buffer(uint32* out, uint64 address, uint32 size_dw)
{
	if (out == NULL || size_dw == 0)
		return 0;
	out[0] = RDNA4_PM4_PACKET3(RDNA4_PM4_INDIRECT_BUFFER, 3);
	out[1] = (uint32)address;
	out[2] = (uint32)(address >> 32);
	out[3] = size_dw;
	return 4;
}

status_t
rdna4_validate_command_buffer(const rdna4_command_buffer& command)
{
	if (command.words == NULL || command.word_count == 0)
		return B_BAD_VALUE;
	if (command.word_count > (16u * 1024u * 1024u))
		return B_BAD_VALUE;

	uint32 i = 0;
	while (i < command.word_count) {
		uint32 header = command.words[i];
		if ((header >> 30) != RDNA4_PM4_TYPE3)
			return B_BAD_DATA;

		uint32 opcode = (header >> 8) & 0xff;
		uint32 count = (header & 0x3fff) + 1;
		if (count == 0 || i + 1 + count > command.word_count)
			return B_BAD_DATA;

		if (is_privileged_opcode(opcode))
			return B_NOT_ALLOWED;

		i += 1 + count;
	}
	return B_OK;
}
