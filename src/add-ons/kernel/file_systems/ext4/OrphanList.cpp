#include "OrphanList.h"

#include "Volume.h"
#include "Inode.h"
#include "../ext2/Transaction.h"

status_t
Ext4OrphanList::_WriteSuper(Volume& volume, Transaction& transaction)
{
	return volume.WriteSuperBlock(transaction);
}

status_t
Ext4OrphanList::Add(Volume& volume, Inode& inode, Transaction& transaction)
{
	ext2_super_block& sb = volume.SuperBlock();
	inode.Node().SetNextOrphan(sb.LastOrphan());
	sb.SetLastOrphan(inode.ID());
	return _WriteSuper(volume, transaction);
}

status_t
Ext4OrphanList::Remove(Volume& volume, ino_t id, Transaction& transaction)
{
	/*
	 * The legacy ext4 orphan list is singly linked through the inode
	 * deletion_time field.  Removal of an arbitrary node requires walking
	 * the list and is therefore deliberately performed transactionally.
	 */
	ino_t current = volume.SuperBlock().LastOrphan();
	ino_t previous = 0;

	while (current != 0) {
		Inode inode(&volume, current);
		if (inode.InitCheck() != B_OK)
			return B_BAD_DATA;

		ino_t next = inode.Node().NextOrphan();
		if (current == id) {
			if (previous == 0)
				volume.SuperBlock().SetLastOrphan(next);
			else {
				Inode previousInode(&volume, previous);
				if (previousInode.InitCheck() != B_OK)
					return B_BAD_DATA;
				previousInode.Node().SetNextOrphan(next);
				status_t status = previousInode.WriteBack(transaction);
				if (status != B_OK)
					return status;
			}
			return _WriteSuper(volume, transaction);
		}
		previous = current;
		current = next;
	}

	return B_ENTRY_NOT_FOUND;
}

status_t
Ext4OrphanList::Recover(Volume& volume)
{
	ino_t current = volume.SuperBlock().LastOrphan();
	if (current == 0)
		return B_OK;

	/*
	 * Journal replay has already completed.  Each orphan has no directory
	 * entry and must not retain blocks.  Truncating to zero is the safe
	 * recovery operation; inode allocation reclamation is left to the normal
	 * inode lifecycle until the allocator can atomically reclaim it.
	 */
	while (current != 0) {
		Inode inode(&volume, current);
		if (inode.InitCheck() != B_OK)
			return B_BAD_DATA;

		ino_t next = inode.Node().NextOrphan();

		Transaction* transaction = volume.StartTransaction();
		if (transaction == NULL)
			return B_NO_MEMORY;

		status_t status = inode.Resize(*transaction, 0);
		if (status == B_OK) {
			inode.Node().SetNextOrphan(0);
			status = inode.WriteBack(*transaction);
		}
		if (status == B_OK) {
			volume.SuperBlock().SetLastOrphan(next);
			status = volume.WriteSuperBlock(*transaction);
		}
		status = volume.DoneTransaction(transaction, status);
		if (status != B_OK)
			return status;

		current = next;
	}

	return B_OK;
}
