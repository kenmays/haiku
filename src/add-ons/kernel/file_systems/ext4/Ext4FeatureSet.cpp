#include "Ext4FeatureSet.h"

#include <KernelExport.h>

#define EXT4_COMPAT_DIR_PREALLOC       0x0001
#define EXT4_COMPAT_IMAGIC_INODES     0x0002
#define EXT4_COMPAT_HAS_JOURNAL       0x0004
#define EXT4_COMPAT_EXT_ATTR          0x0008
#define EXT4_COMPAT_RESIZE_INODE      0x0010
#define EXT4_COMPAT_DIR_INDEX         0x0020
#define EXT4_COMPAT_SPARSE_SUPER2     0x0200

#define EXT4_RO_SPARSE_SUPER          0x0001
#define EXT4_RO_LARGE_FILE            0x0002
#define EXT4_RO_BTREE_DIR             0x0004
#define EXT4_RO_HUGE_FILE             0x0008
#define EXT4_RO_GDT_CSUM              0x0010
#define EXT4_RO_DIR_NLINK             0x0020
#define EXT4_RO_EXTRA_ISIZE           0x0040
#define EXT4_RO_METADATA_CSUM         0x0400
#define EXT4_RO_READONLY              0x1000

#define EXT4_INCOMPAT_COMPRESSION      0x0001
#define EXT4_INCOMPAT_FILETYPE        0x0002
#define EXT4_INCOMPAT_RECOVER         0x0004
#define EXT4_INCOMPAT_JOURNAL         0x0008
#define EXT4_INCOMPAT_JOURNAL_DEV     0x0008
#define EXT4_INCOMPAT_META_BG         0x0010
#define EXT4_INCOMPAT_EXTENTS         0x0040
#define EXT4_INCOMPAT_64BIT           0x0080
#define EXT4_INCOMPAT_MMP             0x0100
#define EXT4_INCOMPAT_FLEX_BG         0x0200
#define EXT4_INCOMPAT_EA_INODE        0x0400
#define EXT4_INCOMPAT_DIRDATA         0x1000
#define EXT4_INCOMPAT_CSUM_SEED       0x2000
#define EXT4_INCOMPAT_LARGEDIR        0x4000
#define EXT4_INCOMPAT_INLINE_DATA     0x8000
#define EXT4_INCOMPAT_ENCRYPT         0x10000
#define EXT4_INCOMPAT_CASEFOLD        0x20000

uint32
Ext4FeatureSet::SupportedCompat()
{
	return EXT4_COMPAT_DIR_PREALLOC
		| EXT4_COMPAT_IMAGIC_INODES
		| EXT4_COMPAT_HAS_JOURNAL
		| EXT4_COMPAT_EXT_ATTR
		| EXT4_COMPAT_RESIZE_INODE
		| EXT4_COMPAT_DIR_INDEX
		| EXT4_COMPAT_SPARSE_SUPER2;
}

uint32
Ext4FeatureSet::SupportedReadOnly()
{
	return EXT4_RO_SPARSE_SUPER
		| EXT4_RO_LARGE_FILE
		| EXT4_RO_HUGE_FILE
		| EXT4_RO_DIR_NLINK
		| EXT4_RO_EXTRA_ISIZE
		| EXT4_RO_GDT_CSUM
		| EXT4_RO_METADATA_CSUM;
}

uint32
Ext4FeatureSet::SupportedIncompat()
{
	return EXT4_INCOMPAT_FILETYPE
		| EXT4_INCOMPAT_RECOVER
		| EXT4_INCOMPAT_JOURNAL
		| EXT4_INCOMPAT_EXTENTS
		| EXT4_INCOMPAT_64BIT
		| EXT4_INCOMPAT_FLEX_BG
		| EXT4_INCOMPAT_CSUM_SEED;
}

bool
Ext4FeatureSet::IsExt4(const ext2_super_block& superBlock)
{
	return (superBlock.IncompatibleFeatures() & EXT4_INCOMPAT_EXTENTS) != 0
		|| (superBlock.IncompatibleFeatures() & EXT4_INCOMPAT_64BIT) != 0
		|| (superBlock.ReadOnlyFeatures() & EXT4_RO_METADATA_CSUM) != 0;
}

status_t
Ext4FeatureSet::Validate(const ext2_super_block& superBlock, bool readOnly)
{
	if (!IsExt4(superBlock))
		return B_BAD_VALUE;

	/* The current Haiku on-disk structures represent ext4 group descriptors
	 * through the complete 64-byte Linux ext4 descriptor.  Never index past
	 * that structure when presented with a newer descriptor format. */
	uint16 descriptorSize = superBlock.GroupDescriptorSize();
	if (descriptorSize != 0 && (descriptorSize < 32 || descriptorSize > 64))
		return B_UNSUPPORTED;

	if (superBlock.BlockShift() > 12)
		return B_UNSUPPORTED;

	if ((superBlock.IncompatibleFeatures() & ~SupportedIncompat()) != 0)
		return B_UNSUPPORTED;

	uint32 unsupportedRO = superBlock.ReadOnlyFeatures() & ~SupportedReadOnly();
	if (!readOnly && unsupportedRO != 0)
		return B_UNSUPPORTED;

	/*
	 * These features require filesystem semantics that the current Haiku
	 * implementation does not yet provide.  Explicitly reject them instead
	 * of mounting a filesystem with potentially corrupting semantics.
	 */
	if (superBlock.IncompatibleFeatures()
			& (EXT4_INCOMPAT_COMPRESSION | EXT4_INCOMPAT_JOURNAL_DEV
				| EXT4_INCOMPAT_META_BG | EXT4_INCOMPAT_MMP
				| EXT4_INCOMPAT_EA_INODE | EXT4_INCOMPAT_DIRDATA
				| EXT4_INCOMPAT_LARGEDIR | EXT4_INCOMPAT_INLINE_DATA
				| EXT4_INCOMPAT_ENCRYPT | EXT4_INCOMPAT_CASEFOLD))
		return B_UNSUPPORTED;

	if (superBlock.ReadOnlyFeatures() & (0x0200 /* BIGALLOC */
			| 0x0100 /* QUOTA */ | 0x2000 /* PROJECT */
			| 0x8000 /* VERITY */ | 0x10000 /* ORPHAN_PRESENT */))
		return B_UNSUPPORTED;

	/* The modern orphan-file feature needs its dedicated inode/table
	 * implementation.  Do not mistake it for the legacy s_last_orphan list. */
	if ((superBlock.CompatibleFeatures() & 0x1000) != 0
			|| (superBlock.ReadOnlyFeatures() & 0x10000) != 0)
		return B_UNSUPPORTED;

	return B_OK;
}
