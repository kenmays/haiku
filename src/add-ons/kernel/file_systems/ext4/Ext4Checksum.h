#ifndef EXT4_CHECKSUM_H
#define EXT4_CHECKSUM_H

#include <SupportDefs.h>

struct ext2_super_block;
struct ext2_block_group;
struct ext2_inode;

namespace Ext4Checksum {

uint32 Seed(const ext2_super_block& superBlock);
uint32 SuperBlock(const ext2_super_block& superBlock);
bool VerifySuperBlock(const ext2_super_block& superBlock);

uint16 GroupDescriptor(const ext2_super_block& superBlock,
	const ext2_block_group& group, uint32 groupNumber, uint16 descriptorSize);
bool VerifyGroupDescriptor(const ext2_super_block& superBlock,
	const ext2_block_group& group, uint32 groupNumber, uint16 descriptorSize);

uint32 Inode(const ext2_super_block& superBlock, const ext2_inode& inode,
	uint32 inodeNumber, uint32 inodeSize);
bool VerifyInode(const ext2_super_block& superBlock, const ext2_inode& inode,
	uint32 inodeNumber, uint32 inodeSize);

}

#endif
