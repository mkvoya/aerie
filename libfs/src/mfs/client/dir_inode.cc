#include "mfs/client/dir_inode.h"
#include <stdint.h>
#include "client/backend.h"
#include "client/inode.h"

namespace mfs {


//TODO thread safety: acquire/release mutex to protect volatile metadata/data structures
//     persistent is protected by the lock. the caller should have already acquire the lock

// TODO: perhaps it makes sense to cache persistent entries to avoid
// the second lookup (persistent inode lookup) for entries that have
// already been looked up in the past. But the benefit might not be much because
// it further slows down lookups as we need to insert an entry in the cache. 
 
// Lookup algorithm
// 1. Find whether there is a volatile directory inode or whether we should
//    query the persistent inode directly 


// If Lookup is successfull, it wraps the persistent inode with a volatile one
// The inode is not locked. 
int 
DirInodeMutable::Lookup(client::Session* session, const char* name, client::Inode** ipp)
{
	int                   ret;
	uint64_t              ino;
	client::Inode*        ip;
	EntryCache::iterator  it;
	
	printf("DirInodeMutable::Lookup %s in inode %lu\n", name, (uint64_t) pnode_);

	// FIXME: call Get to increment the refcount

	// check the private copy first before looking up the global one
	if ((it = entries_.find(name)) != entries_.end()) {
		if (it->second.first == true) {
			ino = it->second.second;
		} else {
			return -E_NOENT;
		}
	} else {
		if ((ret = pnode_->Lookup(session, name, &ino)) < 0) {
			return ret;
		}
	}
	
	sb_->GetInode(ino, &ip);
	*ipp = ip;

	return E_SUCCESS;
}


int 
DirInodeMutable::Link(client::Session* session, const char* name, client::Inode* ip, 
                      bool overwrite)
{
	uint64_t ino = ip->GetInodeNumber();
	
	//FIXME: fix link count
	return Link(session, name, ino, overwrite);
}


// FIXME: How do we shadow . and ..??? In the EntryCache? It should work.
// FIXME: Should Link increment the link count as well? currently the caller 
// must do a separate call but this breaks encapsulation. 
int 
DirInodeMutable::Link(client::Session* session, const char* name, uint64_t ino, 
                      bool overwrite)
{
	assert(overwrite == false);
	EntryCache::iterator   it;
	int                    ret;

	printf("DirInodeMutable::Link (%s)\n", name);

	if (name[0] == '\0') {
		return -1;
	}

	if ((it = entries_.find(name)) != entries_.end()) {
		if (it->second.first == true) {
			// name exists in the volatile cache
			ino = it->second.second;
			return -E_EXIST;
		} else {
			// found a negative entry indicating absence due to a previous 
			// unlink. just overwrite.
			it->second.first = true;
			it->second.second = ino;
			return E_SUCCESS;
		}
	}
	
	if ((ret = pnode_->Lookup(session, name, &ino)) == 0) {
		// name exists in the persistent structure
		return -E_EXIST;
	}
	std::pair<EntryCache::iterator, bool> ret_pair = entries_.insert(std::pair<std::string, std::pair<bool, uint64_t> >(name, std::pair<bool, uint64_t>(true, ino)));
	assert(ret_pair.second == true);
	return 0;
}


int 
DirInodeMutable::Unlink(client::Session* session, const char* name)  
{

	EntryCache::iterator   it;
	int                    ret;
	uint64_t               ino;

	if (name[0] == '\0') {
		return -1;
	}

	if ((it = entries_.find(name)) != entries_.end()) {
		if (it->second.first == true) {
			// name exists
			it->second.first = false;
			return E_SUCCESS;
		} else {
			// a negative entry indicating absence due to a previous unlink
			return -E_NOENT;
		}
	}
	
	if ((ret = pnode_->Lookup(session, name, &ino)) != 0) {
		// name does not exist in the persistent structure
		return -E_EXIST;
	}
	// add a negative directory entry when removing a directory entry from 
	// the persistent data structure
	std::pair<EntryCache::iterator, bool> ret_pair = entries_.insert(std::pair<std::string, std::pair<bool, uint64_t> >(name, std::pair<bool, uint64_t>(false, ino)));
	neg_entries_count_++;
	assert(ret_pair.second == true);

	return E_SUCCESS;
}


int 
DirInodeMutable::nlink() 
{
	// TODO
	return 0;
}

int 
DirInodeMutable::set_nlink(int nlink) 
{
	// TODO
	return 0;
}

/*
//  List all directory entries of a directory
//     This requires coordinating with the immutable directory inode. Simply
//     taking the union of the two inodes is not correct as some entries 
//     that appear in the persistent inode may have been removed and appear
//     as negative entries in the volatile cache. One way is for each entry 
//     in the persistent inode to check whether there is a corresponding negative
//     entry in the volatile cache. But this sounds like an overkill. 
//     Perhaps we need a combination of a bloom filter of just the negative entries 
//     and a counter of the negative entries. As deletes are rare, the bloom filter
//     should quickly provide an answer.

int 
DirInodeMutable::Readdir()
{

}
*/

int 
DirInodeMutable::Publish(client::Session* session)
{
	// FIXME: Currently we publish by simply doing the updates in-place. 
	// Normally this must be done via the trusted server using the journal 

	EntryCache::iterator  it;
	int                   ret;

	for (it = entries_.begin(); it != entries_.end(); it++) {
		if (it->second.first == true) {
			// normal entry -- link
			// FIXME: if a link followed an unlink then the entry is 
			// marked as valid (true). Adding this entry to the persistent 
			// directory without removing the previous one is incorrect.
			// however, currently we don't have a way to detect this case.
			// consider modifying the cache.
			// this case won't be a problem  when doing the publish through the server
			// because there we replay the journal which has the unlink operation.
			// so don't worry for now.
			// TEST TestLinkPublish3 checks this case
			if ((ret = pnode_->Link(session, it->first.c_str(), it->second.second)) != 0) {
				return ret;
			}
		} else {
			// negative entry -- unlink
			if ((ret = pnode_->Unlink(session, it->first.c_str())) != 0) {
				return ret;
			}
		}
	}
	return 0;
}


} // namespace mfs