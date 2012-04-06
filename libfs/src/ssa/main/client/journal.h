#ifndef __STAMNOS_SSA_CLIENT_JOURNAL_H
#define __STAMNOS_SSA_CLIENT_JOURNAL_H

#include <stdio.h>
#include <stdint.h>
#include "ssa/main/client/ssa-opaque.h"
#include "ssa/main/client/shbuf.h"
#include "ssa/main/common/publisher.h"


namespace ssa {
namespace client {

class Buffer {
public:
	Buffer() 
		: size_(1024),
		  count_(0)
	{ }

	inline int Write(const void* src, size_t n);
	inline int Flush(SsaSharedBuffer* dst);

private:
	char   buf_[1024];
	size_t size_;
	size_t count_;
};


int
Buffer::Write(const void* src, size_t n)
{
	memcpy(&buf_[count_], src, n);
	count_ += n;
	return E_SUCCESS;
}


int
Buffer::Flush(SsaSharedBuffer* dst)
{
	int ret;

	if ((ret = dst->Write(buf_, count_)) < 0) {
		return ret;
	}
	count_ = 0;
	return E_SUCCESS;
}


class Journal {
public:
	Journal(SsaSession* session)
		: session_(session)
	{ }

	int TransactionBegin(int id = 0);
	int TransactionCommit();
	int Write(const void* buf, size_t size) {
		return buffer_.Write(buf, size);
	}

	inline friend Journal* operator<< (Journal* journal, const ssa::Publisher::Messages::ContainerOperationHeader& header) {
		journal->Write(&header, sizeof(header) + header.payload_size_);
		return journal;
    }
	
	inline friend Journal* operator<< (Journal* journal, const ssa::Publisher::Messages::LogicalOperationHeader& header) {
		journal->Write(&header, sizeof(header) + header.payload_size_);
		return journal;
    }

private:
	SsaSession* session_;
	Buffer      buffer_;
};


} // namespace client
} // namespace ssa

#endif // __STAMNOS_SSA_CLIENT_JOURNAL_H