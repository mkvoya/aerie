#include "client/client_i.h"
#include <sys/resource.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <iostream>
#include "rpc/rpc.h"
#include "client/file.h"
#include "common/debug.h"
#include "server/api.h"
#include "client/config.h"
#include "client/fsomgr.h"
#include "dpo/base/client/dpo.h"
#include "dpo/base/client/stm.h"
#include "chunkstore/registry.h"
#include "mfs/client/mfs.h"

// FIXME: Client should be a singleton. otherwise we lose control 
// over instantiation and destruction of the global variables below, which
// should really be members of client
namespace client {

__thread Session* thread_session;

FileSystemObjectManager* global_fsomgr;
FileManager*             global_fmgr;
NameSpace*               global_namespace;
Registry*                global_registry;
Session*                 global_session;

dpo::client::DpoLayer*   global_dpo_layer;



int 
Client::Init(const char* xdst) 
{
	struct rlimit      rlim_nofile;

	Config::Init();
	
	global_ipc_layer = new IpcLayer(xdst);
	global_ipc_layer->Init();


	global_dpo_layer = new dpo::client::DpoLayer(global_ipc_layer);
	global_dpo_layer->Init();
	global_session = new Session(global_dpo_layer);
	// file manager should allocate file descriptors outside OS's range
	// to avoid collisions
	getrlimit(RLIMIT_NOFILE, &rlim_nofile);
	global_fmgr = new FileManager(rlim_nofile.rlim_max, 
	                              rlim_nofile.rlim_max+client::limits::kFileN);
								  
	global_registry = new Registry(global_ipc_layer);

	// register known file system backends
	global_fsomgr = new FileSystemObjectManager();
	mfs::client::RegisterBackend(global_fsomgr);

	global_namespace = new NameSpace("GLOBAL");
	return global_namespace->Init(global_session);
}


int 
Client::Shutdown() 
{
	// TODO: properly destroy any state created
	delete global_dpo_layer;
	return 0;
}


Session*
Client::CurrentSession()
{
	if (thread_session) {
		return thread_session;
	}

	thread_session = new Session(global_dpo_layer);
	thread_session->tx_ = dpo::stm::client::Self();
	return thread_session;
}

int 
Client::Mount(const char* source, 
              const char* target, 
              const char* fstype, 
              uint32_t flags)
{
	int                   ret;
	client::SuperBlock*   sb;
	char*                 path = const_cast<char*>(target);
	uint64_t              u64;

	if (target == NULL || source == NULL || fstype == NULL) {
		return -1;
	}
	if (global_registry->Lookup(source, &u64) < 0) {
		return -1;
	}
	dpo::common::ObjectId oid = dpo::common::ObjectId(u64);
	if ((ret = global_fsomgr->LoadSuperBlock(global_session, oid, fstype, &sb)) < 0) {
		return -1;
	}
	dbg_log (DBG_INFO, "Mount %s (%lx) to %s\n", source, u64, target);
	return global_namespace->Mount(global_session, path, sb);
}


int 
Client::Mkfs(const char* target, 
             const char* fstype, 
             uint32_t flags)
{
	int                 ret;
	client::SuperBlock* sb;
	char*               path = const_cast<char*>(target);

	if (target == NULL || fstype == NULL) {
		return -1;
	}
	
	if ((ret = global_fsomgr->CreateSuperBlock(global_session, fstype, &sb)) < 0) {
		return -1;
	}
	global_registry->Add(target, sb->oid().u64());
	dbg_log (DBG_INFO, "Mkfs %s (%lx)\n", target, sb->oid().u64());
	return E_SUCCESS;
}


// TODO: This must be parameterized by file system because it has to 
// allocate an inode specific to the file system
// One way would be to keep a pointer to the superblock in the inode 
// TODO: Support O_EXCL with O_CREAT
static inline int
create(::client::Session* session, const char* path, Inode** ipp, int mode, int type)
{
	char          name[128];
	Inode*        dp;
	Inode*        ip;
	SuperBlock*   sb;
	int           ret;

	// when Nameiparent returns successfully, dp is 
	// locked for writing. we release the lock on dp after we get the lock
	// on its child
	if ((ret = global_namespace->Nameiparent(session, path, 
	                                         lock_protocol::Mode::XL, 
	                                         name, &dp)) < 0) 
	{
		return ret;
	}
	printf("create: dp=%p\n", dp);
	if ((ret = dp->Lookup(session, name, 0, &ip)) == E_SUCCESS) {
		// FIXME: if we create a file, do we need XR?
		ip->Lock(session, dp, lock_protocol::Mode::XR); 
		if (type == client::type::kFileInode && 
		    ip->type() == client::type::kFileInode) 
		{
			*ipp = ip;
			dp->Put();
			dp->Unlock(session);
			return E_SUCCESS;
		}
		ip->Put();
		ip->Unlock(session);
		dp->Put();
		dp->Unlock(session);
		return -E_EXIST;
	}
	
	// allocated inode is write locked and referenced (refcnt=1)
	if ((ret = global_fsomgr->CreateInode(session, dp, type, &ip)) < 0) {
		//TODO: handle error; release directory inode
		assert(0 && "PANIC");
	}

	printf("allocated inode\n");
	ip->set_nlink(1);
	if (type == client::type::kDirInode) {
		assert(dp->set_nlink(dp->nlink() + 1) == 0); // for child's ..
		assert(ip->Link(session, ".", ip, false) == 0 );
		assert(ip->Link(session, "..", dp, false) == 0);
	}
	assert(dp->Link(session, name, ip, false) == 0);
	dp->Put();
	dp->Unlock(session);
	*ipp = ip;
	return 0;
}


// TODO: implement file open
int 
Client::Open(const char* path, int flags, int mode)
{
	Inode* ip;
	int    ret;
	int    fd;
	File*  fp;

	if ((ret = global_fmgr->AllocFile(&fp)) < 0) {
		return ret;
	}
	if ((fd = global_fmgr->AllocFd(fp)) < 0) {
		global_fmgr->ReleaseFile(fp);
		return fd;
	}
	
	// TODO: Initialize file object 
	return fd;

	if (flags & O_CREAT) {
		if ((ret = create(global_session, path, &ip, mode, client::type::kFileInode)) < 0) {
			return ret;
		}	
	} else {
		lock_protocol::Mode lock_mode; // FIXME: what lock_mode?

		if ((ret = global_namespace->Namei(global_session, path, lock_mode, &ip)) < 0) {
			return ret;
		}	
		printf("do_open: path=%s, ret=%d, ip=%p\n", path, ret, ip);
		//ilock(ip);
		//if(ip->type == client::type::kFileInode && flags != O_RDONLY){
		//  iunlockput(ip);
		//  return -1;
		//}
	}	

	return fd;
}


int
Client::Close(int fd)
{
	return global_fmgr->Put(fd);
}


int
Client::Duplicate(int oldfd)
{
	File* fp;
	return global_fmgr->Get(oldfd, &fp);
}


int
Client::Duplicate(int oldfd, int newfd)
{
	dbg_log (DBG_CRITICAL, "Unimplemented functionality\n");
}


int 
Client::Write(int fd, const char* src, uint64_t n)
{
	File* fp;
	int   ret;

	if ((ret = global_fmgr->Lookup(fd, &fp)) < 0) {
		return ret;
	}
	return fp->Write(global_session, src, n);
}


int 
Client::Read(int fd, char* dst, uint64_t n)
{
	int   ret;
	File* fp;

	if ((ret = global_fmgr->Lookup(fd, &fp)) < 0) {
		return ret;
	}
	return fp->Read(global_session, dst, n);
}


uint64_t 
Client::Seek(int fd, uint64_t offset, int whence)
{
	dbg_log (DBG_CRITICAL, "Unimplemented functionality\n");	
}


int
Client::CreateDir(const char* path, int mode)
{
	int    ret;
	Inode* ip;

	dbg_log (DBG_INFO, "Create Directory: %s\n", path);	

	if ((ret = create(global_session, path, &ip, mode, client::type::kDirInode)) < 0) {
		return ret;
	}
	assert(ip->Put() == E_SUCCESS);
	assert(ip->Unlock(global_session) == E_SUCCESS);
	return 0;
}


int
Client::DeleteDir(const char* pathname)
{
	dbg_log (DBG_INFO, "Delete Directory: %s\n", pathname);	

	return Client::Unlink(pathname);
}


int
Client::SetCurWrkDir(const char* path)
{

}


int
Client::GetCurWrkDir(const char* path, size_t size)
{

}


int 
Client::Rename(const char* oldpath, const char* newpath)
{
	

}


int 
Client::Link(const char* oldpath, const char* newpath)
{
	return global_namespace->Link(global_session, oldpath, newpath);
}


int 
Client::Unlink(const char* pathname)
{
	return global_namespace->Unlink(global_session, pathname);
}


/// Check whether we can communicate with the server
int
Client::TestServerIsAlive()
{
	int r;

	rpc_client->call(RPC_SERVER_IS_ALIVE, 0, r);
	return r;
}




} // namespace client
