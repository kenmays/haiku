#ifndef EXT4_ORPHAN_FILE_H
#define EXT4_ORPHAN_FILE_H

#include <SupportDefs.h>
#include <fs_interface.h>

#include "../ext2/ext2.h"

class Inode;
class Transaction;
class Volume;

class Ext4OrphanFile {
public:
	static bool Enabled(const Volume& volume);
	static bool Present(const Volume& volume);
	static status_t Add(Volume& volume, Inode& inode, Transaction& transaction);
	static status_t Remove(Volume& volume, ino_t inodeNumber,
		Transaction& transaction);
	static status_t Recover(Volume& volume);
	static status_t IsEmpty(Volume& volume, bool& empty);
	static status_t MarkPresent(Volume& volume, Transaction& transaction,
		bool present);

private:
	static status_t _Block(Volume& volume, Inode& orphanFile,
		uint32 index, fsblock_t& block);
	static uint32 _ChecksumSeed(Volume& volume, const Inode& orphanFile);
	static uint32 _Checksum(Volume& volume, const Inode& orphanFile,
		fsblock_t block, const uint8* data);
	static status_t _UpdateChecksum(Volume& volume, const Inode& orphanFile,
		Transaction& transaction, fsblock_t block, uint8* data);
};

#endif
