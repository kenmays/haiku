#ifndef RDNA4_RING_H
#define RDNA4_RING_H

#include <SupportDefs.h>

/* Software ring model used by the hardware backend. Packet emission,
 * write pointers and doorbells remain generation-specific. */
class RDNA4Ring {
public:
	RDNA4Ring();
	~RDNA4Ring();

	status_t Initialize(uint32 dwordCapacity);
	void Reset();
	bool IsInitialized() const { return fBuffer != NULL; }
	uint32 Capacity() const { return fCapacity; }
	uint32 Used() const { return fUsed; }
	uint32 Free() const { return fCapacity - fUsed; }

	status_t Reserve(uint32 dwords, uint32* _offset);
	status_t Write(uint32 offset, const uint32* commands, uint32 count);
	status_t Commit(uint32 dwords);
	status_t Read(uint32 offset, uint32* _value) const;

	uint64 Sequence() const { return fSequence; }
	uint64 Signal();

private:
	uint32* fBuffer;
	uint32 fCapacity;
	uint32 fUsed;
	uint32 fWriteOffset;
	uint64 fSequence;
};

#endif
