#ifndef _CLIENT_INODE_H_JAK129
#define _CLIENT_INODE_H_JAK129

#include <stdint.h>
#include <stdio.h>
#include "client/const.h"
#include "client/hlckmgr.h"

namespace client {

extern HLockManager* global_hlckmgr;

typedef uint64_t InodeNumber;

class Session;
class SuperBlock;


class Inode {
public:
	Inode();
	Inode(SuperBlock* sb, InodeNumber ino);

	virtual int Init(client::Session* session, InodeNumber ino) = 0;
	virtual int Open(client::Session* session, const char* path, int flags) = 0;
	virtual int Write(client::Session* session, char* src, uint64_t off, uint64_t n) = 0;
	virtual int Read(client::Session* session, char* dst, uint64_t off, uint64_t n) = 0;
	virtual int Lookup(client::Session* session, const char* name, Inode** ipp) = 0;
	virtual int Link(client::Session* session, const char* name, Inode* inode, bool overwrite) = 0;
	virtual int Link(client::Session* session, const char* name, uint64_t ino, bool overwrite) = 0;
	virtual int Unlink(client::Session* session, const char* name) = 0;

	virtual int Publish(client::Session* session) = 0;

	virtual client::SuperBlock* GetSuperBlock() = 0;
	virtual void SetSuperBlock(client::SuperBlock* sb) = 0;

	InodeNumber ino() { return ino_; };
	void SetInodeNumber(InodeNumber ino) { ino_ = ino; };

	virtual int nlink() { assert(0 && "Not implemented by subclass"); };
	virtual int set_nlink(int nlink) { assert(0 && "Not implemented by subclass"); };

	int Get();
	int Put();
	int Lock(Inode* parent_inode, lock_protocol::Mode mode); 
	int Lock(lock_protocol::Mode mode); 
	int Unlock();

	int type() {
		//FIXME: where do we store the type?
		return client::type::kDirInode;
	}

//protected:
	//! process-wide mutex; used for synchronizing access to the
	//! volatile inode metadata
	pthread_mutex_t     mutex_;
	//! dynamic reference count; number of objects referencing the
	//! volatile inode object
	int                 refcnt_; 
	client::SuperBlock* sb_;                // volatile file system superblock
	InodeNumber         ino_;
	//! system-wide public lock; used for inter- and intra process 
	//! synchronization to the persistent inode data structure
	client::Lock*       lock_;  

};


} // namespace client


#endif /* _CLIENT_INODE_H_JAK129 */
