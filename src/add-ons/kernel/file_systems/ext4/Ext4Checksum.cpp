#include "Ext4Checksum.h"

#include <stddef.h>
#include <string.h>

#include "CRCTable.h"
#include "../ext2/ext2.h"

namespace Ext4Checksum {

uint32
Seed(const ext2_super_block& superBlock)
{
	if ((superBlock.IncompatibleFeatures() & EXT2_INCOMPATIBLE_FEATURE_CSUM_SEED) != 0)
		return B_LENDIAN_TO_HOST_INT32(superBlock.checksum_seed);

	return calculate_crc32c(0xffffffff, (const uint8*)superBlock.uuid,
		sizeof(superBlock.uuid));
}


uint32
SuperBlock(const ext2_super_block& superBlock)
{
	return calculate_crc32c(Seed(superBlock), (const uint8*)&superBlock,
		offsetof(ext2_super_block, checksum));
}


bool
VerifySuperBlock(const ext2_super_block& superBlock)
{
	if ((superBlock.ReadOnlyFeatures() & EXT4_READ_ONLY_FEATURE_METADATA_CSUM) == 0)
		return true;

	return SuperBlock(superBlock) == B_LENDIAN_TO_HOST_INT32(superBlock.checksum);
}


uint16
GroupDescriptor(const ext2_super_block& superBlock,
	const ext2_block_group& group, uint32 groupNumber, uint16 descriptorSize)
{
	/*
	 * ext4 group descriptor checksums cover:
	 *   checksum seed + little-endian group number + descriptor bytes,
	 * with the checksum field logically zeroed.
	 *
	 * The low 16 bits are stored in bg_checksum.  For 64-byte descriptors
	 * the high bitmap/checksum fields are included as well.
	 */
	uint32 crc = Seed(superBlock);
	uint32 number = B_HOST_TO_LENDIAN_INT32(groupNumber);
	crc = calculate_crc32c(crc, (const uint8*)&number, sizeof(number));

	const uint8* bytes = (const uint8*)&group;
	const size_t checksumOffset = offsetof(ext2_block_group, checksum);
	crc = calculate_crc32c(crc, bytes, checksumOffset);

	uint16 zero = 0;
	crc = calculate_crc32c(crc, (const uint8*)&zero, sizeof(zero));

	const size_t ext4Offset = offsetof(ext2_block_group, block_bitmap_high);
	if (descriptorSize > ext4Offset) {
		size_t length = descriptorSize - ext4Offset;
		/*
		 * Do not read beyond the structure known by this driver.  A larger
		 * descriptor is an unsupported format until its high fields are
		 * represented explicitly.
		 */
		if (length > sizeof(group) - ext4Offset)
			return 0;

		crc = calculate_crc32c(crc, bytes + ext4Offset, length);
	}

	return (uint16)(crc & 0xffff);
}


bool
VerifyGroupDescriptor(const ext2_super_block& superBlock,
	const ext2_block_group& group, uint32 groupNumber, uint16 descriptorSize)
{
	if ((superBlock.ReadOnlyFeatures() & EXT4_READ_ONLY_FEATURE_METADATA_CSUM) == 0
		&& (superBlock.ReadOnlyFeatures() & EXT2_READ_ONLY_FEATURE_GDT_CSUM) == 0)
		return true;

	return GroupDescriptor(superBlock, group, groupNumber, descriptorSize)
		== B_LENDIAN_TO_HOST_INT16(group.checksum);
}


uint32
Inode(const ext2_super_block& superBlock, const ext2_inode& inode,
	uint32 inodeNumber, uint32 inodeSize)
{
	/*
	 * ext4 inode checksum is CRC32C(UUID/seed, inode number, generation,
	 * inode bytes), with the checksum fields cleared while calculating it.
	 */
	uint32 crc = Seed(superBlock);
	uint32 number = B_HOST_TO_LENDIAN_INT32(inodeNumber);
	crc = calculate_crc32c(crc, (const uint8*)&number, sizeof(number));
	crc = calculate_crc32c(crc, (const uint8*)&inode.generation,
		sizeof(inode.generation));

	if (inodeSize > sizeof(ext2_inode))
		return 0;

	uint8 copy[sizeof(ext2_inode)];
	memcpy(copy, &inode, inodeSize);

	const size_t checksumOffset = offsetof(ext2_inode, checksum);
	if (checksumOffset + sizeof(uint16) <= inodeSize)
		memset(copy + checksumOffset, 0, sizeof(uint16));

	const size_t checksumHighOffset = offsetof(ext2_inode, checksum_high);
	if (checksumHighOffset + sizeof(uint16) <= inodeSize)
		memset(copy + checksumHighOffset, 0, sizeof(uint16));

	crc = calculate_crc32c(crc, copy, inodeSize);
	return crc;
}


bool
VerifyInode(const ext2_super_block& superBlock, const ext2_inode& inode,
	uint32 inodeNumber, uint32 inodeSize)
{
	if ((superBlock.ReadOnlyFeatures() & EXT4_READ_ONLY_FEATURE_METADATA_CSUM) == 0)
		return true;

	if (inodeSize < offsetof(ext2_inode, checksum) + sizeof(uint16))
		return true;

	return (uint16)(Inode(superBlock, inode, inodeNumber, inodeSize) & 0xffff)
		== B_LENDIAN_TO_HOST_INT16(inode.checksum);
}

}
