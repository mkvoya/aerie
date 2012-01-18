#ifndef __STAMNOS_CLIENT_SUPERBLOCK_FACTORY_H
#define __STAMNOS_CLIENT_SUPERBLOCK_FACTORY_H

#include "dpo/base/common/obj.h"

namespace client {

class Session;     // forward declaration
class SuperBlock;  // forward declaration

class SuperBlockFactory {
public:
	// FIXME
	virtual int Make(Session* session, SuperBlock** sbp) = 0;
	virtual int Load(Session* session, dpo::common::ObjectId oid, SuperBlock** sbp) = 0;
	virtual int TypeID() = 0;
	virtual std::string TypeStr() = 0;
};

} // namespace client

#endif // __STAMNOS_MFS_CLIENT_SUPERBLOCK_FACTORY_H