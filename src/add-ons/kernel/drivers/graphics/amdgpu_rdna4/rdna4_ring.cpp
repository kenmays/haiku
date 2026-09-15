#include "rdna4_ring.h"

#include <stdlib.h>
#include <string.h>

RDNA4Ring::RDNA4Ring()
	: fBuffer(NULL), fCapacity(0), fUsed(0), fWriteOffset(0), fSequence(0)
{
}

RDNA4Ring::~RDNA4Ring()
{
	free(fBuffer);
}

status_t
RDNA4Ring::Initialize(uint32 dwordCapacity)
{
	if (dwordCapacity < 64 || dwordCapacity > (1U << 20))
		return B_BAD_VALUE;

	uint32* buffer = (uint32*)malloc((size_t)dwordCapacity * sizeof(uint32));
	if (buffer == NULL)
		return B_NO_MEMORY;

	free(fBuffer);
	fBuffer = buffer;
	fCapacity = dwordCapacity;
	fUsed = 0;
	fWriteOffset = 0;
	fSequence = 0;
	memset(fBuffer, 0, (size_t)fCapacity * sizeof(uint32));
	return B_OK;
}

void
RDNA4Ring::Reset()
{
	if (fBuffer != NULL)
		memset(fBuffer, 0, (size_t)fCapacity * sizeof(uint32));
	fUsed = 0;
	fWriteOffset = 0;
	fSequence = 0;
}

status_t
RDNA4Ring::Reserve(uint32 dwords, uint32* _offset)
{
	if (fBuffer == NULL || _offset == NULL || dwords == 0)
		return B_BAD_VALUE;
	if (dwords > Free())
		return B_WOULD_BLOCK;
	if (fWriteOffset + dwords > fCapacity)
		return B_WOULD_BLOCK;
	*_offset = fWriteOffset;
	return B_OK;
}

status_t
RDNA4Ring::Write(uint32 offset, const uint32* commands, uint32 count)
{
	if (fBuffer == NULL || commands == NULL || count == 0)
		return B_BAD_VALUE;
	if (offset >= fCapacity || count > fCapacity - offset)
		return B_BAD_VALUE;
	memcpy(fBuffer + offset, commands, (size_t)count * sizeof(uint32));
	return B_OK;
}

status_t
RDNA4Ring::Commit(uint32 dwords)
{
	if (fBuffer == NULL || dwords == 0 || dwords > Free())
		return B_BAD_VALUE;
	if (fWriteOffset + dwords > fCapacity)
		return B_BAD_VALUE;
	fWriteOffset += dwords;
	fUsed += dwords;
	return B_OK;
}

status_t
RDNA4Ring::Read(uint32 offset, uint32* _value) const
{
	if (fBuffer == NULL || _value == NULL || offset >= fUsed)
		return B_BAD_VALUE;
	*_value = fBuffer[offset];
	return B_OK;
}

uint64
RDNA4Ring::Signal()
{
	return ++fSequence;
}
