#ifndef __STAMNOS_FS_CLIENT_SUPERBLOCK_H
#define __STAMNOS_FS_CLIENT_SUPERBLOCK_H

#include "common/types.h"
#include "client/inode.h"
#include "dpo/base/client/hlckmgr.h"

/**
 *
 * The SuperBlock provides an indirection layer to the inodes of the 
 * filesystem and plays the role of an inode manager.
 *
 * We need indirection because a client cannot directly write to persistent 
 * inodes. Instead we wrap a persistent inode into a local/private one that
 * performs writes using copy-on-write (either logical or physical). 
 * 
 * The SuperBlock is responsible for keeping track the index of inode number 
 * to (volatile) inode. 
 *
 */

namespace client {

class Session; // forward declaration
class InodeMap; // forward declaration

class SuperBlock: public HLockUser {
public:
	SuperBlock(Session* session);
	
	virtual void* GetPSuperBlock() = 0;
	int AllocInode(Session* session, int type, Inode* parent, Inode** ipp);
	int GetInode(InodeNumber ino, Inode** ipp);
	int PutInode(Inode* ip);

	inline client::Inode* RootInode() {	return root_; }

	// callbacks
	void OnRelease(HLock* hlock);
	void OnConvert(HLock* hlock);
	int Revoke(HLock* hlock) { return 0; }

protected:
	pthread_mutex_t mutex_;
	InodeMap*       imap_;
	Inode*          root_;

private:
	// FIXME: these functions should really be replaced with an inode factory 
	virtual int MakeInode(Session* session, int type, Inode** ipp) = 0;
	virtual int LoadInode(InodeNumber ino, client::Inode** ipp) = 0;
};



} // namespace client

#endif // __STAMNOS_FS_CLIENT_SUPERBLOCK_H
