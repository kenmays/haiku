#ifndef EXT4_ORPHAN_LIST_H
#define EXT4_ORPHAN_LIST_H

#include <SupportDefs.h>
#include <SupportDefs.h>

class Inode;
class Transaction;
class Volume;

class Ext4OrphanList {
public:
	static status_t Add(Volume& volume, Inode& inode, Transaction& transaction);
	static status_t Remove(Volume& volume, ino_t inode, Transaction& transaction);
	static status_t Recover(Volume& volume);

private:
	static status_t _WriteSuper(Volume& volume, Transaction& transaction);
};

#endif
