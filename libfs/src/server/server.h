#ifndef __STAMNOS_PXFS_SERVER_H
#define __STAMNOS_PXFS_SERVER_H

#include "dpo/dpo-opaque.h"
#include "ipc/ipc-opaque.h"

class ChunkServer;       // forward declaration

namespace server {

class FileSystemManager; // forward declaration

class Server {
public:
	static Server* Instance();
	void Start(int port);

private:
	int                port_;
	ChunkServer*       chunk_server_;
	dpo::server::Dpo*  dpo_layer_;
	Ipc*               ipc_layer_;
	FileSystemManager* fsmgr_;
	static Server*     instance_;
};


} // namespace server

#endif // __STAMNOS_PXFS_SERVER_H