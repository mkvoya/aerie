#include "bcs/main/server/shbuf.h"
#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>
#include "bcs/main/common/debug.h"
#include "bcs/main/server/shbufmgr.h"
#include "scm/scm.h"
#include "common/util.h"
#include "common/mmapregion.h"


namespace server {


int
SharedBuffer::Init(const char* suffix)
{
	int         ret;
	const char* root = "/tmp/shbuf_";

	path_ = std::string(root) + std::string(suffix);
	size_ = SharedBufferManager::RuntimeConfig::sharedbuffer_size;

	DBG_LOG(DBG_DEBUG, DBG_MODULE(server_bcs), "SharedBuffer: path = %s, size = %" PRIu64 "\n", 
	        path_.c_str(), size_);

	if ((ret = Open(path_.c_str(), size_, kCreate | kMap, this)) < 0) {
		return ret;
	}
	start_ = header_->start_ = 0;
	end_ = header_->end_ = 0;

	return E_SUCCESS;
}


/**
 * \brief returns the number of bytes read
 */
int 
SharedBuffer::Read(char* dst, size_t n)
{
	//printf(">>start-end: [%lu - %lu], size=%lu, count=%lu, n=%d\n", start_, end_, size_, Count(), n);
	//fflush(stdout);
	if (n > Count()) {
		// not enough data 
		return 0;
	}
	void* bptr = (void*) (base_ + start_);
	if (start_ > end_) {
		uint64_t first_part_size = size_ - start_;
		if (n > first_part_size) {
			memcpy(dst, bptr, first_part_size);
			memcpy(&dst[first_part_size], (void*) base_, n-first_part_size);
		} else {
			memcpy(dst, bptr, n);
		}
	} else {
		memcpy(dst, bptr, n);
	}
	start_ = (start_ + n) % size_;
	return n;
}

} // namespace server
