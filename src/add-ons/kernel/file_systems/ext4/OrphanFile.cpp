#include "OrphanFile.h"

#include "../ext2/CachedBlock.h"
#include "../ext2/Inode.h"
#include "../ext2/Transaction.h"
#include "../ext2/Volume.h"

#include <ByteOrder.h>

#include "CRCTable.h"

static const uint32 kOrphanMagic = 0x0b10ca04;
static const uint32 kMaxOrphanFileBlocks = 512;

struct orphan_file_tail {
	uint32 magic;
	uint32 checksum;
} _PACKED;

bool
Ext4OrphanFile::Enabled(const Volume& volume)
{
	return (volume.SuperBlock().CompatibleFeatures() & 0x1000) != 0;
}

bool
Ext4OrphanFile::Present(const Volume& volume)
{
	return (volume.SuperBlock().ReadOnlyFeatures() & 0x10000) != 0;
}

uint32
Ext4OrphanFile::_ChecksumSeed(Volume& volume, const Inode& inode)
{
	uint32 seed = volume.ChecksumSeed();
	uint32 number = B_HOST_TO_LENDIAN_INT32((uint32)inode.ID());
	uint32 generation = B_HOST_TO_LENDIAN_INT32(inode.Node().generation);
	seed = calculate_crc32c(seed, (uint8*)&number, sizeof(number));
	return calculate_crc32c(seed, (uint8*)&generation, sizeof(generation));
}

uint32
Ext4OrphanFile::_Checksum(Volume& volume, const Inode& orphanFile,
	fsblock_t block, const uint8* data)
{
	uint32 seed = _ChecksumSeed(volume, orphanFile);
	uint64 diskBlock = B_HOST_TO_LENDIAN_INT64((uint64)block);
	uint32 entries = (volume.BlockSize() - sizeof(orphan_file_tail))
		/ sizeof(uint32);
	seed = calculate_crc32c(seed, (uint8*)&diskBlock, sizeof(diskBlock));
	return calculate_crc32c(seed, data, entries * sizeof(uint32));
}

status_t
Ext4OrphanFile::_Block(Volume& volume, Inode& orphanFile, uint32 index,
	fsblock_t& block)
{
	if (index >= kMaxOrphanFileBlocks)
		return B_BAD_VALUE;

	uint32 count = 0;
	status_t status = orphanFile.FindBlock(
		(off_t)index * volume.BlockSize(), block, &count);
	if (status != B_OK || count == 0)
		return status == B_OK ? B_BAD_DATA : status;
	return B_OK;
}

status_t
Ext4OrphanFile::_UpdateChecksum(Volume& volume, const Inode& orphanFile,
	Transaction& transaction, fsblock_t block, uint8* data)
{
	uint32 offset = volume.BlockSize() - sizeof(orphan_file_tail);
	orphan_file_tail* tail = (orphan_file_tail*)(data + offset);
	tail->magic = B_HOST_TO_LENDIAN_INT32(kOrphanMagic);
	tail->checksum = B_HOST_TO_LENDIAN_INT32(
		_Checksum(volume, orphanFile, block, data));
	return B_OK;
}

status_t
Ext4OrphanFile::MarkPresent(Volume& volume, Transaction& transaction,
	bool present)
{
	uint32 features = volume.SuperBlock().ReadOnlyFeatures();
	if (present)
		features |= 0x10000;
	else
		features &= ~0x10000U;
	volume.SuperBlock().SetReadOnlyFeatures(features);
	return volume.WriteSuperBlock(transaction);
}

status_t
Ext4OrphanFile::Add(Volume& volume, Inode& inode, Transaction& transaction)
{
	if (!Enabled(volume))
		return B_UNSUPPORTED;

	Inode orphanFile(&volume, volume.SuperBlock().OrphanFileInode());
	if (orphanFile.InitCheck() != B_OK)
		return B_BAD_DATA;

	uint32 entries = (volume.BlockSize() - sizeof(orphan_file_tail))
		/ sizeof(uint32);
	uint32 blocks = (orphanFile.Size() + volume.BlockSize() - 1)
		/ volume.BlockSize();
	if (blocks == 0 || blocks > kMaxOrphanFileBlocks)
		return B_BAD_DATA;

	uint32 start = (uint32)inode.ID() % blocks;
	for (uint32 n = 0; n < blocks; n++) {
		uint32 index = (start + n) % blocks;
		fsblock_t block;
		status_t status = _Block(volume, orphanFile, index, block);
		if (status != B_OK)
			return status;

		CachedBlock cached(&volume);
		const uint8* current = cached.SetTo(block);
		if (current == NULL)
			return B_IO_ERROR;

		orphan_file_tail* tail = (orphan_file_tail*)
			((uint8*)current + volume.BlockSize() - sizeof(orphan_file_tail));
		if (B_LENDIAN_TO_HOST_INT32(tail->magic) != kOrphanMagic)
			return B_BAD_DATA;

		uint32* list = (uint32*)current;
		for (uint32 j = 0; j < entries; j++) {
			if (B_LENDIAN_TO_HOST_INT32(list[j]) == (uint32)inode.ID())
				return B_OK;
			if (list[j] != 0)
				continue;

			CachedBlock writable(&volume);
			uint8* data = writable.SetToWritable(transaction, block);
			if (data == NULL)
				return B_IO_ERROR;
			((uint32*)data)[j] =
				B_HOST_TO_LENDIAN_INT32((uint32)inode.ID());
			return _UpdateChecksum(volume, orphanFile, transaction, block,
				data);
		}
	}

	return B_DEVICE_FULL;
}

status_t
Ext4OrphanFile::Remove(Volume& volume, ino_t inodeNumber,
	Transaction& transaction)
{
	if (!Enabled(volume))
		return B_UNSUPPORTED;

	Inode orphanFile(&volume, volume.SuperBlock().OrphanFileInode());
	if (orphanFile.InitCheck() != B_OK)
		return B_BAD_DATA;

	uint32 entries = (volume.BlockSize() - sizeof(orphan_file_tail))
		/ sizeof(uint32);
	uint32 blocks = (orphanFile.Size() + volume.BlockSize() - 1)
		/ volume.BlockSize();

	for (uint32 i = 0; i < blocks && i < kMaxOrphanFileBlocks; i++) {
		fsblock_t block;
		status_t status = _Block(volume, orphanFile, i, block);
		if (status != B_OK)
			return status;

		CachedBlock cached(&volume);
		const uint8* current = cached.SetTo(block);
		if (current == NULL)
			return B_IO_ERROR;

		uint32* list = (uint32*)current;
		for (uint32 j = 0; j < entries; j++) {
			if (B_LENDIAN_TO_HOST_INT32(list[j]) != (uint32)inodeNumber)
				continue;

			CachedBlock writable(&volume);
			uint8* data = writable.SetToWritable(transaction, block);
			if (data == NULL)
				return B_IO_ERROR;
			((uint32*)data)[j] = 0;
			return _UpdateChecksum(volume, orphanFile, transaction, block,
				data);
		}
	}
	return B_ENTRY_NOT_FOUND;
}

status_t
Ext4OrphanFile::IsEmpty(Volume& volume, bool& empty)
{
	empty = true;
	if (!Enabled(volume))
		return B_OK;

	Inode orphanFile(&volume, volume.SuperBlock().OrphanFileInode());
	if (orphanFile.InitCheck() != B_OK)
		return B_BAD_DATA;

	uint32 entries = (volume.BlockSize() - sizeof(orphan_file_tail))
		/ sizeof(uint32);
	uint32 blocks = (orphanFile.Size() + volume.BlockSize() - 1)
		/ volume.BlockSize();

	for (uint32 i = 0; i < blocks && i < kMaxOrphanFileBlocks; i++) {
		fsblock_t block;
		status_t status = _Block(volume, orphanFile, i, block);
		if (status != B_OK)
			return status;
		CachedBlock cached(&volume);
		const uint8* data = cached.SetTo(block);
		if (data == NULL)
			return B_IO_ERROR;
		const uint32* list = (const uint32*)data;
		for (uint32 j = 0; j < entries; j++) {
			if (list[j] != 0) {
				empty = false;
				return B_OK;
			}
		}
	}
	return B_OK;
}

status_t
Ext4OrphanFile::Recover(Volume& volume)
{
	if (!Enabled(volume) || !Present(volume))
		return B_OK;

	Inode orphanFile(&volume, volume.SuperBlock().OrphanFileInode());
	if (orphanFile.InitCheck() != B_OK)
		return B_BAD_DATA;

	uint32 entries = (volume.BlockSize() - sizeof(orphan_file_tail))
		/ sizeof(uint32);
	uint32 blocks = (orphanFile.Size() + volume.BlockSize() - 1)
		/ volume.BlockSize();
	if (blocks > kMaxOrphanFileBlocks)
		return B_BAD_DATA;

	for (uint32 i = 0; i < blocks; i++) {
		fsblock_t block;
		status_t status = _Block(volume, orphanFile, i, block);
		if (status != B_OK)
			return status;

		CachedBlock cached(&volume);
		const uint8* data = cached.SetTo(block);
		if (data == NULL)
			return B_IO_ERROR;

		const uint32* list = (const uint32*)data;
		for (uint32 j = 0; j < entries; j++) {
			ino_t id = (ino_t)B_LENDIAN_TO_HOST_INT32(list[j]);
			if (id == 0)
				continue;

			Inode inode(&volume, id);
			if (inode.InitCheck() != B_OK)
				return B_BAD_DATA;

			Transaction transaction(volume.GetJournal());
			if (!transaction.IsStarted())
				return B_ERROR;

			status = inode.Resize(transaction,
				inode.Node().NumLinks() == 0 ? 0 : inode.Size());
			if (status == B_OK)
				status = Remove(volume, id, transaction);
			if (status == B_OK && inode.Node().NumLinks() == 0)
				status = volume.FreeInode(transaction, inode.ID(), inode.IsDirectory());
			if (status == B_OK)
				status = transaction.Done(true);
			else
				transaction.Done(false);
			if (status != B_OK)
				return status;
		}
	}

	bool empty;
	status_t status = IsEmpty(volume, empty);
	if (status != B_OK)
		return status;
	if (empty) {
		Transaction transaction(volume.GetJournal());
		if (!transaction.IsStarted())
			return B_ERROR;
		status = MarkPresent(volume, transaction, false);
		status = transaction.Done(status == B_OK);
	}
	return status;
}
