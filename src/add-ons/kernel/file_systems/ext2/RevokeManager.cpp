/*
 * Copyright 2001-2010, Haiku Inc. All rights reserved.
 * This file may be used under the terms of the MIT License.
 *
 * Authors:
 *		Janito V. Ferreira Filho
 */


#include "RevokeManager.h"


//#define TRACE_EXT2
#ifdef TRACE_EXT2
#	define TRACE(x...) dprintf("\33[34mext2:\33[0m " x)
#else
#	define TRACE(x...) ;
#endif


RevokeManager::RevokeManager(bool has64bits)
	:
	fRevokeCount(0),
	fHas64bits(has64bits)
{
}


RevokeManager::~RevokeManager()
{
}


status_t
RevokeManager::ScanRevokeBlock(JournalRevokeHeader* revokeBlock,
	uint32 commitID)
{
	TRACE("RevokeManager::ScanRevokeBlock(): Commit ID: %" B_PRIu32 "\n",
		commitID);
	uint32 bytes = revokeBlock->NumBytes();
	const uint32 headerSize = sizeof(JournalRevokeHeader);
	uint32 entrySize = fHas64bits ? sizeof(uint64) : sizeof(uint32);
	if (bytes < headerSize || ((bytes - headerSize) % entrySize) != 0)
		return B_BAD_DATA;

	uint32 count = (bytes - headerSize) / entrySize;
	uint8* entries = (uint8*)revokeBlock->revoke_blocks;
	for (uint32 i = 0; i < count; i++) {
		uint64 block;
		if (fHas64bits)
			block = B_BENDIAN_TO_HOST_INT64(*(uint64*)(entries + i * entrySize));
		else
			block = B_BENDIAN_TO_HOST_INT32(*(uint32*)(entries + i * entrySize));
		status_t status = Insert(block, commitID);
		if (status != B_OK)
			return status;
	}
	return B_OK;
}

