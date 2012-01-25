#include "mfs/client/inode_factory.h"
#include "common/debug.h"
#include "client/backend.h"
#include "dpo/containers/typeid.h"
#include "mfs/client/dir_inode.h"

namespace mfs {
namespace client {

pthread_mutex_t InodeFactory::mutex_ = PTHREAD_MUTEX_INITIALIZER;;

int
InodeFactory::LoadDirInode(::client::Session* session, dpo::common::ObjectId oid, 
                           ::client::Inode** ipp)
{
	int                                ret = E_SUCCESS;
	dpo::common::ObjectProxyReference* ref;
	DirInode*                          dip;

	// atomically get a reference to the persistent object and 
	// create the in-core Inode 
	pthread_mutex_lock(&mutex_);
	if ((ret = session->omgr_->FindObject(oid, &ref)) == E_SUCCESS) {
		if (ref->owner()) {
			// the in-core inode already exists; just return this and 
			// we are done
			dip = reinterpret_cast<DirInode*>(ref->owner());
		} else {
			dip = new DirInode(ref);
		}
	} else {
		dip = new DirInode(ref);
	}
	pthread_mutex_unlock(&mutex_);
	*ipp = dip;
	return E_SUCCESS;
}


int
InodeFactory::MakeDirInode(::client::Session* session, ::client::Inode** ipp)
{
	int                                               ret = E_SUCCESS;
	dpo::containers::client::NameContainer::Object*   obj;

	if ((obj = new(session) dpo::containers::client::NameContainer::Object) == NULL) {
		return -E_NOMEM;
	}
	if ((ret = LoadDirInode(session, obj->oid(), ipp)) < 0) {
		// FIXME: deallocate the allocated persistent object (container)
		return ret;
	}
	return ret;
}


int
InodeFactory::LoadFileInode(::client::Session* session, dpo::common::ObjectId oid, 
                            ::client::Inode** ipp)
{
	/* FIXME: FileContainer, FileInode
	int                                               ret = E_SUCCESS;
	::client::Inode*                                  ip;
	dpo::containers::client::FileContainer::Reference rw_ref;

	if ((ret = session->omgr_->GetObject(oid, &rw_ref)) < 0) {
		//FIXME: deallocate the allocated object
		return ret;
	}
	ip = new FileInode(rw_ref);
	*ipp = ip;
	return ret;
	*/
}


int
InodeFactory::MakeFileInode(::client::Session* session, ::client::Inode** ipp)
{
	int                                               ret = E_SUCCESS;
	dpo::containers::client::NameContainer::Object*   obj;

	// FIXME: FileContainer
	//if ((obj = new(session) dpo::containers::client::FileContainer::Object) == NULL) {
	//	return -E_NOMEM;
	//}
	if ((ret = LoadDirInode(session, obj->oid(), ipp)) < 0) {
		// FIXME: deallocate the allocated object
		return ret;
	}
	return ret;
}


int
InodeFactory::Make(::client::Session* session, int type, ::client::Inode** ipp)
{
	int ret = E_SUCCESS;

	switch (type) {
		case ::client::type::kDirInode:
			ret = MakeDirInode(session, ipp);
			break;
		case ::client::type::kFileInode:	
			ret = MakeFileInode(session, ipp);
			break;
		default:
			dbg_log (DBG_CRITICAL, "Unknown inode type\n");
	}
	return ret;
}


int 
InodeFactory::Load(::client::Session* session, dpo::common::ObjectId oid, 
                   ::client::Inode** ipp)
{
	int ret = E_SUCCESS;

	switch (oid.type()) {
		case dpo::containers::T_NAME_CONTAINER: // directory
			ret = LoadDirInode(session, oid, ipp);
			break;
		case dpo::containers::T_FILE_CONTAINER:
			ret = LoadFileInode(session, oid, ipp);
			break;
		default: 
			dbg_log (DBG_CRITICAL, "Unknown container type\n");
	}
	
	return ret;
}

} // namespace client 
} // namespace mfs
