#ifndef EXT4_INTEGRITY_H
#define EXT4_INTEGRITY_H

#include <SupportDefs.h>

class Ext4Integrity {
public:
	static status_t VerifySuperBlock(const void* superBlock);
	static status_t VerifyGroupDescriptor(const void* superBlock,
		const void* group, uint32 groupNumber, uint16 descriptorSize);
	static status_t VerifyInode(const void* superBlock, const void* inode,
		uint32 inodeNumber, uint32 inodeSize);
};

#endif
