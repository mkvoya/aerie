/// \file lckmgr.h 
///
/// \brief Client lock manager interface.

#ifndef _CLIENT_LOCK_MANAGER_H_AFG819
#define _CLIENT_LOCK_MANAGER_H_AFG819

#include <string>
#include <google/sparsehash/sparseconfig.h>
#include <google/dense_hash_map>
#include "rpc/rpc.h"
#include "client/lock_protocol.h"

namespace client {

// Classes that inherit lock_release_user can override dorelease so that 
// that they will be called when lock_client releases a lock.
// You will not need to do anything with this class until Lab 6.
class lock_release_user {
public:
	virtual void dorelease(lock_protocol::LockId) = 0;
	virtual ~lock_release_user() {};
};



// SUGGESTED LOCK CACHING IMPLEMENTATION PLAN:
//
// All the requests on the server run till completion and threads wait 
// on condition variables on the client to wait for a lock. This allows 
// the server to:
//  - be replicated using the replicated state machine approach (Schneider 1990)
//  - serve requests using a single core in an event-based fashion
//
// On the client a lock can be in several states:
//  - xfree: client owns the lock (exclusively) and no thread has it
//  - sfree: client shares the lock and no thread has it
//  - xlocked: client owns the lock (exclusively) and a thread has it
//  - slocked: client shares the lock and one thread or more have it
//  - acquiring: the client is acquiring ownership
//  - releasing: the client is releasing ownership
//
// in the state acquiring and xlocked there may be several threads
// waiting for the lock, but the first thread in the list interacts
// with the server and wakes up the threads when its done (released
// the lock).  a thread in the list is identified by its thread id
// (tid).
//
// a thread is in charge of getting a lock: if the server cannot grant
// it the lock, the thread will receive a retry reply.  at some point
// later, the server sends the thread a retry RPC, encouraging the client
// thread to ask for the lock again.
//
// once a thread has acquired a lock, its client obtains ownership of
// the lock. the client can grant the lock to other threads on the client 
// without interacting with the server. 
//
// the server must send the client a revoke request to get the lock back. this
// request tells the client to send the lock back to the
// server when the lock is released or right now if no thread on the
// client is holding the lock.  when receiving a revoke request, the
// client adds it to a list and wakes up a releaser thread, which returns
// the lock the server as soon it is free.
//
// the releasing is done in a separate a thread to avoid
// deadlocks and to ensure that revoke and retry RPCs from the server
// run to completion (i.e., the revoke RPC cannot do the release when
// the lock is free.
//
// a challenge in the implementation is that retry and revoke requests
// can be out of order with the acquire and release requests. That
// is, a client may receive a revoke request before it has received
// the positive acknowledgement on its acquire request.  Similarly, a
// client may receive a retry before it has received a response on its
// initial acquire request.  A flag field is used to record if a retry
// has been received.
//

class Lock {

public:
	enum LockStatus {
		NONE, 
		FREE_X, 
		FREE_S, 
		LOCKED_X, 
		LOCKED_S, 
		LOCKED_XS, 
		ACQUIRING_X, 
		ACQUIRING_S, 
		/* RELEASING (unused) */
		/* DOWNGRADING (unused) */
	};

	//Lock();
	Lock(lock_protocol::LockId);
	~Lock();

	// changes the status of this lock
	void set_status(LockStatus);
	LockStatus status() const;

	lock_protocol::LockId lid_;

	// we use only a single cv to monitor changes of the lock's status
	// this may be less efficient because there would be many spurious
	// wake-ups, but it's simple anyway
	pthread_cond_t        status_cv_;

	// condvar that is signaled when the ``used'' field is set to true
	pthread_cond_t        used_cv_;

	pthread_cond_t        got_acq_reply_cv_;
  
	// condvar that is signaled when the server informs the client to retry
	pthread_cond_t        retry_cv_;

	pthread_t             owner_;

	// the sequence number of the latest *COMPLETED* acquire request made
	// by the client to obtain this lock
	// by completed, we mean the remote acquire() call returns with a value.
	int                   seq_;
	bool                  used_; // set to true after first use
	bool                  can_retry_; // set when a retry message from the server is received

private:
	LockStatus status_;

};


class LockManager {
public:
	LockManager(rpcc*, rpcs*, std::string, class lock_release_user* l = 0);
	~LockManager();
	Lock* GetOrCreateLock(lock_protocol::LockId);
	lock_protocol::status AcquireShared(Lock*);
	lock_protocol::status AcquireShared(lock_protocol::LockId);
	lock_protocol::status AcquireExclusive(Lock*);
	lock_protocol::status AcquireExclusive(lock_protocol::LockId);
	lock_protocol::status Acquire(Lock*);
	lock_protocol::status Acquire(lock_protocol::LockId);
	lock_protocol::status Release(Lock*);
	lock_protocol::status Release(lock_protocol::LockId);
	lock_protocol::status stat(lock_protocol::LockId);
	void releaser();

	rlock_protocol::status revoke_release(lock_protocol::LockId, int, int&);
	rlock_protocol::status revoke_downgrade(lock_protocol::LockId, int, int&);
	// Tell this client to retry requesting the lock in which this client
	// was interest when that lock just became available.
	rlock_protocol::status retry(lock_protocol::LockId, int, int&);

private:
	int do_acquire(Lock*);
	int do_release(Lock*);
	Lock* GetOrCreateLockInternal(lock_protocol::LockId);
	lock_protocol::status AcquireExclusiveInternal(Lock*);
	lock_protocol::status AcquireSharedInternal(Lock*);
	lock_protocol::status ReleaseInternal(Lock*);

	class lock_release_user*                             lu;
	std::string                                          hostname_;
	std::string                                          id_;
	/// the RPC object through which we receive callbacks from the server
	rpcs*                                                srv2cl_;
	/// the RPC object through which we make calls to the server
	rpcc*                                                cl2srv_;

	int                                                  last_seq_;
	bool                                                 running_;

	google::dense_hash_map<lock_protocol::LockId, Lock*> Locks_;

	// key: lock id; value: seq no. of the corresponding acquire
	std::map<lock_protocol::LockId, int>                 revoke_map_;
	// global lock
	pthread_mutex_t                                      mutex_;
	// controls access to the revoke_map
	pthread_mutex_t                                      revoke_mutex_;
	pthread_cond_t                                       revoke_cv;
};


} // namespace client

#endif  // _CLIENT_LOCK_MANAGER_H_AFG819