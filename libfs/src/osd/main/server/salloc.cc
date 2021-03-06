#include "osd/main/server/salloc.h"
#include <stdio.h>
#include <stddef.h>
#include "scm/const.h"
#include "common/errno.h"
#include "common/util.h"
#include "bcs/bcs.h"
#include "osd/main/common/storage_protocol.h"
#include "osd/containers/set/container.h"
#include "osd/containers/super/container.h"
#include "osd/containers/name/container.h"
#include "osd/containers/byte/container.h"
#include "osd/containers/needle/container.h"
#include "osd/main/server/container.h"
#include "osd/main/server/session.h"
#include "common/prof.h"

//#define PROFILER_SAMPLE __PROFILER_SAMPLE


#define USE_VISTAHEAP

namespace osd {
namespace server {


int
DescriptorPool::Load(OsdSession* session)
{
	osd::common::ObjectId   oid;
	osd::common::ExtentId   eid;
	osd::common::Object*    obj;

	extent_list_.clear();
	for (int i = 0; i < 16; i++) {
		container_list_[i].clear();
	}

	for (int i = 0; i < set_obj_->Size(); i++) {
		set_obj_->Read(session, i, &oid);
		if (oid.type() == osd::containers::T_EXTENT) {
			eid = osd::common::ExtentId(oid);
			extent_list_.push_back(ExtentDescriptor(eid, i));
		} else {
			container_list_[oid.type()].push_back(ContainerDescriptor(oid, i));
		}
	}
	return E_SUCCESS;
}


int
DescriptorPool::Create(OsdSession* session, 
                       osd::common::ObjectId set_oid, 
                       DescriptorPool** poolp)
{
	DescriptorPool*  pool;
	int              ret;

	if ((pool = new DescriptorPool(set_oid)) == NULL) {
		return -E_NOMEM;
	}
	if ((ret = pool->Load(session)) < 0) {
		return ret;
	}
	*poolp = pool;
		
	return E_SUCCESS;
}


int 
DescriptorPool::AllocateContainer(OsdSession* session, StorageAllocator* salloc, int type, osd::common::ObjectId* oid)
{
	int r;
	int ret;

	if (container_list_[type].empty()) {
		if ((ret = salloc->AllocateContainerIntoSet(session, set_obj_, type, 1024)) < 0) {
			return ret;
		}
		if ((ret = Load(session)) < 0) {
			return ret;
		}
	}
	ContainerDescriptor& front = container_list_[type].front();
	*oid = front.oid_;
	//TODO: mark the allocation down in the journal.
	container_list_[type].pop_front();
	return E_SUCCESS;
}


// deprecated: the server shouldn't use this to allocate extents but instead use directly the 
// extent allocator
// the api allows multiple size extents.
// currently the implementation provides only 4K size extents
int
DescriptorPool::AllocateExtent(OsdSession* session, StorageAllocator* salloc, 
                               size_t nbytes, osd::common::ExtentId* eid)
{
	int ret;
	int r;

	if (extent_list_.empty()) {
		if ((ret = salloc->AllocateExtentIntoSet(session, set_obj_, nbytes, 1024)) < 0) {
			return ret;
		}
		if ((ret = Load(session)) < 0) {
			return ret;
		}
	}
	ExtentDescriptor& front = extent_list_.front();
	*eid = front.eid_;
	//TODO: mark the allocation down in the journal
	extent_list_.pop_front();
	return E_SUCCESS;
}


StorageAllocator::StorageAllocator(::server::Ipc* ipc, StoragePool* pool, FreeSet* freeset)
	: ipc_(ipc),
	  pool_(pool),
	  freeset_(freeset)
{ 
	pthread_mutexattr_t    attr;

	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mutex_, &attr);
	objtype2factory_map_.set_empty_key(0);
	aclpoolmap_.set_empty_key(INT_MAX);
	aclpoolmap_.set_deleted_key(INT_MAX-1);
}


int 
StorageAllocator::Load(::server::Ipc* ipc, StoragePool* pool, StorageAllocator** sallocp) 
{
	void*                                               b;
	osd::containers::server::SuperContainer::Object*    super_obj;
	FreeSet*                                            set_obj;
	StorageAllocator*                                   salloc;
	int                                                 ret;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "Load storage pool 0x%lx\n", pool->Identity());

	if ((b = pool->root()) == 0) {
		return -E_NOENT;
	}
	if ((super_obj = osd::containers::server::SuperContainer::Object::Load(b)) == NULL) {
		return -E_NOMEM;
	}
	osd::common::ObjectId freelist_oid = super_obj->freelist(NULL); // we don't need journaling
	if ((set_obj = FreeSet::Object::Load(freelist_oid)) == NULL) {
		return -E_NOMEM;
	}
	if ((salloc = new StorageAllocator(ipc, pool, set_obj)) == NULL) {
		return -E_NOMEM;
	}
	if ((ret = salloc->Init()) < 0) {
		return ret;
	}
	salloc->can_commit_suicide_ = true;
	*sallocp = salloc;

	return E_SUCCESS;
}


int 
StorageAllocator::Close()
{
	if (can_commit_suicide_) {
		delete this;
	}
	return E_SUCCESS;
}


int
StorageAllocator::Init()
{
	int ret;

	if ((ret = RegisterBaseTypes()) < 0) {
		return ret;
	}
	if ((ret = LoadFreeMap()) < 0) {
		return ret;
	}
	for (int i=0; i<16; i++) {
		container_list_[i].clear();
	}
	if (ipc_) {
		return ipc_handlers_.Register(this);
	}
	return E_SUCCESS;
}


int
StorageAllocator::LoadFreeMap()
{
	AclObjectPair aclobjpair;
	ObjectIdSet*  oid_set;
	
	for (int i=0; i<freeset_->Size(); i++) {
		freeset_->Read(NULL, i, &aclobjpair);
		if ((oid_set = ObjectIdSet::Load(aclobjpair.oid)) == NULL) {
			break;
		}
		freemap_.insert(std::pair<osd::common::AclIdentifier, ObjectIdSet*>(aclobjpair.acl_id, oid_set));
	}

	return E_SUCCESS;
}


int
StorageAllocator::RegisterBaseTypes()
{
	int ret;

	{
	// NameContainer
	::osd::server::ContainerAbstractFactory* factory = new osd::containers::server::NameContainer::Factory();
    if ((ret = RegisterType(osd::containers::T_NAME_CONTAINER, factory)) < 0) {
		return ret;
	}
	}

	{
	// ByteContainer
	::osd::server::ContainerAbstractFactory* factory = new osd::containers::server::ByteContainer::Factory();
    if ((ret = RegisterType(osd::containers::T_BYTE_CONTAINER, factory)) < 0) {
		return ret;
	}
	}

	{
	// NeedleContainer
	::osd::server::ContainerAbstractFactory* factory = new osd::containers::server::NeedleContainer::Factory();
    if ((ret = RegisterType(osd::containers::T_NEEDLE_CONTAINER, factory)) < 0) {
		return ret;
	}
	}

	return E_SUCCESS;
}


int
StorageAllocator::RegisterType(::osd::common::ObjectType type_id, 
                               ::osd::server::ContainerAbstractFactory* objfactory)
{
	int                          ret = E_SUCCESS;
	ObjectType2Factory::iterator itr; 

	pthread_mutex_lock(&mutex_);

	if ((itr = objtype2factory_map_.find(type_id)) != objtype2factory_map_.end()) {
		ret = -E_EXIST;
		goto done;
	}
	objtype2factory_map_[type_id] = objfactory;

done:
	pthread_mutex_unlock(&mutex_);
	return ret;
}


// for local use only: when the server needs to allocate storage for itself
int 
StorageAllocator::GetDescriptorPool(OsdSession* session, osd::common::AclIdentifier acl_id, 
                                    DescriptorPool** poolp)
{
	int                   ret;
	AclPoolMap::iterator  it;
	DescriptorPool*       pool;
	osd::common::ObjectId set_oid;
	
	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "Allocate container set of ACL %d\n", acl_id);

	if ((it = aclpoolmap_.find(acl_id)) == aclpoolmap_.end()) {
		if ((ret = AllocateObjectIdSet(session, acl_id, &set_oid)) < 0) {
			return ret;
		}
		if ((ret = DescriptorPool::Create(session, set_oid, &pool)) < 0) {
			return ret;
		}
		aclpoolmap_.insert(std::pair<osd::common::AclIdentifier, DescriptorPool*>(acl_id, pool));
	} else {
		pool = it->second;	
	}
	*poolp = pool;
	
	return E_SUCCESS;
}




// OBSOLETE
int 
StorageAllocator::Alloc(size_t nbytes, std::type_info const& typid, void** ptr)
{
	assert(0);
}
 

// OBSOLETE
int 
StorageAllocator::Alloc(OsdSession* session, size_t nbytes, std::type_info const& typid, void** ptr)
{
	assert(0);
}


// this interface is for use by the server 
int 
StorageAllocator::AllocateExtent(OsdSession* session, size_t size, int flags, void** ptr)
{
	int ret;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "Allocating Extent: size=%lu (0x%lx)...\n", size, size);

	pthread_mutex_lock(&mutex_);

	if ((ret = pool_->AllocateExtent(size, ptr)) < 0) {
		goto done;
	}
	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc),
	        "Allocating Extent: base=%p, size=%lu (0x%lx): SUCCESS \n", *ptr, size, size);

	ret = E_SUCCESS;
done:
	pthread_mutex_unlock(&mutex_);
	return ret;
}


int
StorageAllocator::FreeExtent(OsdSession* session, osd::common::ExtentId eid)
{
	int ret;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "Freeing Extent: %p ...\n", eid.u64());
	
	pthread_mutex_lock(&mutex_);

	if ((ret = pool_->FreeExtent(eid.addr())) < 0) {
		goto done;
	}
	ret = E_SUCCESS;
done:
	pthread_mutex_unlock(&mutex_);
	return ret;
}


int 
StorageAllocator::AllocateExtentIntoSet(OsdSession* session, ObjectIdSet* set, 
                                           int size, int count)
{
	PROFILER_PREAMBLE
	int                     ret;
	size_t                  extent_size;
	char*                   buffer;
	::osd::common::ExtentId eid;
	PROFILER_SAMPLE

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "[%d] Allocate %d extents(s) of size %d\n", session->clt(), count, size);

	pthread_mutex_lock(&mutex_);

	PROFILER_SAMPLE
#ifdef USE_VISTAHEAP
	for (int i = 0; i<count; i++) {
		if ((ret = pool_->AllocateExtent(size, (void**) &buffer)) < 0) {
			pool_->PrintStats();
			assert(0 && "EXTENT: OUT OF STORAGE: PANIC!");
		}
		eid = ::osd::common::ExtentId(buffer, size);
		set->Insert(session, eid);
	}	
	ret = E_SUCCESS;
	goto done;
#else
	extent_size = count * size;
	if ((ret = pool_->AllocateExtent(extent_size, (void**) &buffer)) < 0) {
		//perhaps the allocator is too fragmented. try size instead
		for (int i = 0; i<count; i++) {
			if ((ret = pool_->AllocateExtent(size, (void**) &buffer)) < 0) {
				pool_->PrintStats();
				assert(0 && "EXTENT: OUT OF STORAGE: PANIC!");
			}
			eid = ::osd::common::ExtentId(buffer, size);
			set->Insert(session, eid);
		}	
		ret = E_SUCCESS;
		goto done;
	}
#endif
	PROFILER_SAMPLE
	for (size_t s=0; s<extent_size; s+=size) {
		char* b = (char*) buffer + s;
		eid = ::osd::common::ExtentId(b, size);
		set->Insert(session, eid);
	}
	PROFILER_SAMPLE
	ret = E_SUCCESS;

done:	
	pthread_mutex_unlock(&mutex_);
	return ret;
}


// not thread safe
int
StorageAllocator::CreateObjectIdSet(OsdSession* session, osd::common::AclIdentifier acl_id, 
                                    ObjectIdSet** obj_set)
{
	char*   buffer;
	size_t  extent_size;
	int     ret;

	extent_size = sizeof(ObjectIdSet);
	if ((ret = pool_->AllocateExtent(extent_size, (void**) &buffer)) < 0) {
		return ret;
	}
	if ((*obj_set = ObjectIdSet::Make(session, buffer)) == NULL) {
		return -E_NOMEM;
	}

	return E_SUCCESS;
}


// allocates a set of containers (actually a set of object ids of containers)
// set may already contain containers if instantiated from an existing set
int
StorageAllocator::AllocateObjectIdSet(OsdSession* session, osd::common::AclIdentifier  acl_id, 
                                      osd::common::ObjectId* set_oid)
{
	int                   ret;
	FreeMap::iterator     it;
	ObjectIdSet*          obj_set;
	osd::common::ObjectId oid;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "[%d] Allocate ObjectID Set of ACL %d\n", session->clt(), acl_id);

	pthread_mutex_lock(&mutex_);

	// find an existing set matching the acl or create a new one
	it = freemap_.find(acl_id);
	if (it == freemap_.end()) {
		if ((ret = CreateObjectIdSet(session, acl_id, &obj_set)) < 0) {
			goto done;
		}
	} else {
		oid = it->second->oid();
		if ((obj_set = osd::containers::server::SetContainer<osd::common::ObjectId>::Object::Load(oid)) == NULL) {
			ret = -E_NOMEM;
			goto done;
		}
		freemap_.erase(it);
	}
	*set_oid = obj_set->oid();
	ret = E_SUCCESS;
done:
	pthread_mutex_unlock(&mutex_);
	return ret;
}


int
StorageAllocator::AllocateObjectIdSet(OsdSession* session, osd::common::AclIdentifier  acl_id, 
                                      ::osd::StorageProtocol::ContainerReply& reply)
{
	int                   ret;
	osd::common::ObjectId oid;

	if ((ret = AllocateObjectIdSet(session, acl_id, &oid)) < 0) {
		return ret;
	}
	// Assign the set to the client
	reply.oid = oid;
	reply.index = session->sets_.size(); // location to index the table of assigned free sets
	session->sets_.push_back(reply.oid);
	return E_SUCCESS;
}


// allocate container -- for local use only: for serving the storage needs of the server
int 
StorageAllocator::AllocateContainer(OsdSession* session, 
                                    osd::common::AclIdentifier acl_id, int type, 
                                    osd::common::ObjectId* oidp)
{
	int                   ret;
	DescriptorPool*       pool;
	osd::common::ObjectId oid;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), "Allocate container of type %d\n", type);

	if ((ret = GetDescriptorPool(session, acl_id, &pool)) < 0) {
		return ret;
	}
	if ((ret = pool->AllocateContainer(session, this, type, &oid)) < 0) {
		return ret;
	}
	*oidp = oid;
	return E_SUCCESS;
}
	

int
StorageAllocator::FreeContainer(OsdSession* session, osd::common::ObjectId oid)
{
	int type = oid.type();
	assert(type < 16);
	// BUG: Recycling other types of containers is problematic because we have
	// a bug somewhere that causes a problem in reinitializing recycled storage
	// We hardcode the if-check below to be able to run KVFS
	if (type == osd::containers::T_NEEDLE_CONTAINER) {
		container_list_[type].push_back(oid);
	}
	return E_SUCCESS;
}



// Allocate containers into the set:
int 
StorageAllocator::AllocateContainerIntoSet(OsdSession* session, ObjectIdSet* set, 
                                           int type, int count)
{
	int                     ret;
	char*                   buffer;
	size_t                  static_size;
	size_t                  extent_size;
	::osd::common::Object*  obj;
	::osd::common::ObjectId oid;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "[%d] Allocate %d container(s) of type %d\n", session->clt(), count, type);

	pthread_mutex_lock(&mutex_);

	//FIXME: use storage from local pool. currently we keep this in a list but we
	//should really have a local pool based on DescriptorPool
	if (container_list_[type].size() > count) {
		for (int i = 0; i<count; i++) {
			oid = container_list_[type].front();
			container_list_[type].pop_front();
			set->Insert(session, oid);
			// reinitialize object to make sure we start at clean state
			if ((obj = objtype2factory_map_[type]->Make(session, (char*) oid.addr())) == NULL) {
				ret = -E_NOMEM;
				goto done;
			}
		}
		ret = E_SUCCESS;
		goto done;
	}

	// Create objects and insert them into the set 
	static_size = objtype2factory_map_[type]->StaticSize();
	if (static_size < kBlockSize) {
		static_size = RoundUpSize(static_size, ::osd::common::kObjectAlignSize);
	} else {
		static_size = NumOfBlocks(static_size, kBlockSize) * kBlockSize;
	}
	extent_size = count * static_size;
	if ((ret = pool_->AllocateExtent(extent_size, (void**) &buffer)) < 0) {
		// perhaps the allocator is too fragmented. try size instead
		for (int i = 0; i<count; i++) {
			if ((ret = pool_->AllocateExtent(4096, (void**) &buffer)) < 0) {
				pool_->PrintStats();
				assert(0 && "CONTAINER: OUT OF STORAGE: PANIC!");
			}
			for (size_t s=0; s<4096; s+=static_size) {
				char* b = (char*) buffer + s;
				if ((obj = objtype2factory_map_[type]->Make(session, b)) == NULL) {
					ret = -E_NOMEM;
					goto done;
				}
				set->Insert(session, obj->oid());
			}
		}	
		ret = E_SUCCESS;
		goto done;
	}
	for (size_t s=0; s<extent_size; s+=static_size) {
		char* b = (char*) buffer + s;
		if ((obj = objtype2factory_map_[type]->Make(session, b)) == NULL) {
			ret = -E_NOMEM;
			goto done;
		}
		set->Insert(session, obj->oid());
	}
	ret = E_SUCCESS;

done:	
	pthread_mutex_unlock(&mutex_);

	return ret;
}


// Allocate Container: called by the publisher/validator to allocate container from a set
int 
StorageAllocator::AllocateContainerFromSet(OsdSession* session, osd::common::ObjectId set_oid, osd::common::ObjectId oid, int index_hint)
{
	int                     ret;
	char*                   buffer;
	size_t                  static_size;
	size_t                  extent_size;
	::osd::common::Object*  obj;
	ObjectIdSet*            obj_set;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "[%d] Allocate container %p from set %p (hint=%d)\n", session->clt(), 
	        (void*) oid.u64(), (void*) set_oid.u64(), index_hint);

	if ((obj_set = osd::containers::server::SetContainer<osd::common::ObjectId>::Object::Load(set_oid)) == NULL) {
		return -1;
	}

	obj_set->Write(session, index_hint, 0);
	return E_SUCCESS;
}


// Allocate Extent: called by the publisher/validator to allocate extent from a set
int 
StorageAllocator::AllocateExtentFromSet(OsdSession* session, osd::common::ObjectId set_oid, osd::common::ExtentId eid, int index_hint)
{
	int                     ret;
	char*                   buffer;
	size_t                  static_size;
	size_t                  extent_size;
	::osd::common::Object*  obj;
	ObjectIdSet*            obj_set;

	DBG_LOG(DBG_INFO, DBG_MODULE(server_salloc), 
	        "[%d] Allocate extent %p from set %p (hint=%d)\n", session->clt(), 
	        (void*) eid.u64(), (void*) set_oid.u64(), index_hint);

	if ((obj_set = osd::containers::server::SetContainer<osd::common::ObjectId>::Object::Load(set_oid)) == NULL) {
		return -1;
	}

	obj_set->Write(session, index_hint, 0);
	return E_SUCCESS;
}




int
StorageAllocator::IpcHandlers::Register(StorageAllocator* salloc)
{
	salloc_ = salloc;
    salloc_->ipc_->reg(::osd::StorageProtocol::kAllocateObjectIdSet, this, 
	                   &::osd::server::StorageAllocator::IpcHandlers::AllocateObjectIdSet);
    salloc_->ipc_->reg(::osd::StorageProtocol::kAllocateExtentIntoSet, this, 
	                   &::osd::server::StorageAllocator::IpcHandlers::AllocateExtentIntoSet);
    salloc_->ipc_->reg(::osd::StorageProtocol::kAllocateContainerIntoSet, this, 
	                   &::osd::server::StorageAllocator::IpcHandlers::AllocateContainerIntoSet);
    salloc_->ipc_->reg(::osd::StorageProtocol::kAllocateContainerVector, this, 
	                   &::osd::server::StorageAllocator::IpcHandlers::AllocateContainerVector);

	return E_SUCCESS;
}


int 
StorageAllocator::IpcHandlers::AllocateObjectIdSet(int clt, osd::common::AclIdentifier acl_id, 
                                                   ::osd::StorageProtocol::ContainerReply& r)
{
	int                    ret;
	::server::BaseSession* session;
	
	if ((ret = salloc_->ipc_->session_manager()->Lookup(clt, &session)) < 0) {
		return -ret;
	}
	if ((ret = salloc_->AllocateObjectIdSet(static_cast<OsdSession*>(session), 
	                                         acl_id, r)) < 0) {
		return -ret;
	}
	return E_SUCCESS;
}


// set_capability is an index into the table of container sets allocates to the client
// new containers will be allocated into the set pointed by the set_capability
int 
StorageAllocator::IpcHandlers::AllocateContainerIntoSet(int clt, int set_capability, int type, int num, 
                                                        int& reply)
{
	int                    ret;
	::server::BaseSession* session;
	OsdSession*            osdsession;
	ObjectIdSet*           obj_set;
	osd::common::ObjectId  oid;
	
	if ((ret = salloc_->ipc_->session_manager()->Lookup(clt, &session)) < 0) {
		return -ret;
	}
	osdsession = static_cast<OsdSession*>(session);
	assert(set_capability < osdsession->sets_.size());
	oid = osdsession->sets_[set_capability];

	if ((obj_set = osd::containers::server::SetContainer<osd::common::ObjectId>::Object::Load(oid)) == NULL) {
		DBG_LOG(DBG_CRITICAL, DBG_MODULE(server_salloc), "[%d] Cannot load persistent object\n", clt);
	}
	if ((ret = salloc_->AllocateContainerIntoSet(osdsession, obj_set, type, num)) < 0) {
		return -ret;
	}
	return E_SUCCESS;
}


// set_capability is an index into the table of container sets allocates to the client
// new containers will be allocated into the set pointed by the set_capability
int 
StorageAllocator::IpcHandlers::AllocateExtentIntoSet(int clt, int set_capability, int size, int num, 
                                                     int& reply)
{
	int                    ret;
	::server::BaseSession* session;
	OsdSession*            osdsession;
	ObjectIdSet*           obj_set;
	osd::common::ObjectId  oid;
	
	if ((ret = salloc_->ipc_->session_manager()->Lookup(clt, &session)) < 0) {
		return -ret;
	}
	osdsession = static_cast<OsdSession*>(session);
	assert(set_capability < osdsession->sets_.size());
	oid = osdsession->sets_[set_capability];

	if ((obj_set = osd::containers::server::SetContainer<osd::common::ObjectId>::Object::Load(oid)) == NULL) {
		DBG_LOG(DBG_CRITICAL, DBG_MODULE(server_salloc), "[%d] Cannot load persistent object\n", clt);
	}
	if ((ret = salloc_->AllocateExtentIntoSet(osdsession, obj_set, size, num)) < 0) {
		return -ret;
	}
	return E_SUCCESS;
}



int 
StorageAllocator::IpcHandlers::AllocateContainerVector(int clt,
                                                       std::vector< ::osd::StorageProtocol::ContainerRequest> container_req_vec, 
                                                       std::vector<int>& result)
{
	std::vector< ::osd::StorageProtocol::ContainerRequest>::iterator vit;
	
	for (vit = container_req_vec.begin(); vit != container_req_vec.end(); vit++) {
		//printf("type: %d, num=%d\n", (*vit).type, (*vit).num);
		
		result.push_back(1);
	}

	return E_SUCCESS;
}


} // namespace server
} // namespace osd
