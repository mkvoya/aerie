#ifndef __STAMNOS_MFS_CLIENT_FILE_INODE_H
#define __STAMNOS_MFS_CLIENT_FILE_INODE_H

#include <stdint.h>
#include "pxfs/common/types.h"
#include "pxfs/common/const.h"
#include "pxfs/client/inode.h"
#include "osd/containers/byte/container.h"
#include "osd/main/common/obj.h"
#include <stdio.h>

namespace client {
	class Session; // forward declaration
}

namespace mfs {
namespace client {

class FileInode: public ::client::Inode
{
public:
	FileInode(osd::common::ObjectProxyReference* ref)
	{ 
		//printf("\n @ Creating FileInode.");
		ref_ = ref;
		fs_type_ = kMFS;
		type_ = kFileInode;
	}

	int Read(::client::Session* session, char* dst, uint64_t off, uint64_t n); 
	int Write(::client::Session* session, char* src, uint64_t off, uint64_t n); 
	int Unlink(::client::Session* session, const char* name) { assert(0); }
	int Lookup(::client::Session* session, const char* name, int flags, ::client::Inode** ipp) { assert(0); }
	int xLookup(::client::Session* session, const char* name, int flags, ::client::Inode** ipp) { assert(0); }
	int Link(::client::Session* session, const char* name, ::client::Inode* ip, bool overwrite) { assert(0); }
	int Link(::client::Session* session, const char* name, uint64_t ino, bool overwrite) { assert(0); }
	int Sync(::client::Session* session);

	int nlink();
	int set_nlink(int nlink);

	int Lock(::client::Session* session, Inode* parent_inode, lock_protocol::Mode mode); 
	int Lock(::client::Session* session, lock_protocol::Mode mode); 
	int Unlock(::client::Session* session);
	int xOpenRO(::client::Session* session) { assert(0); }
	int return_dentry(::client::Session* sessio, void *) { return 0;}


	int ioctl(::client::Session* session, int request, void* info);
//	~FileInode() { printf("\n & Destroying FileInode."); }
	  osd::containers::client::ByteContainer::Reference* Rw_ref() {
                return static_cast<osd::containers::client::ByteContainer::Reference*>(ref_);
        }
//	void printme() { printf("\n* Hailing from FileInode."); }

private:
	osd::containers::client::ByteContainer::Reference* rw_ref() {
		return static_cast<osd::containers::client::ByteContainer::Reference*>(ref_);
	}
	int              nlink_;
};


} // namespace client
} // namespace mfs

#endif // __STAMNOS_MFS_CLIENT_FILE_INODE_H
