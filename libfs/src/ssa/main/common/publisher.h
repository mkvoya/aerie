#ifndef __STAMNOS_SSA_COMMON_PUBLISHER_H
#define __STAMNOS_SSA_COMMON_PUBLISHER_H

#include "bcs/bcs.h"
#include "ssa/main/server/ssa-opaque.h"
#include "ssa/main/common/obj.h"
#include "ssa/main/common/cc.h"

// All structures assume a little endian machine

namespace ssa {

class Publisher { 
public:
	class Protocol;
	class Messages;
};


class Publisher::Protocol {
public:
	enum RpcNumbers {
		DEFINE_RPC_NUMBER(SSA_PUBLISHER_PROTOCOL)
	};
};


class Publisher::Messages {
public:
	enum MessageType {
		kTransactionBegin = 0x1,
		kTransactionCommit,
		kLogicalOperation,
		kContainerOperation,
	};
	struct BaseMessage;
	template<typename T> struct BaseMessageT;
	struct TransactionBegin;
	struct TransactionCommit;
	struct LogicalOperationHeader;
	template<typename T> struct LogicalOperationHeaderT;
	struct ContainerOperationHeader;
	struct ContainerOperation;
};


struct Publisher::Messages::BaseMessage {
	BaseMessage(char type = 0)
		: type_(type)
	{ }

	static BaseMessage* Load(void* src) {
		return reinterpret_cast<BaseMessage*>(src);
	}

	char type_;
};


template<typename T>
struct Publisher::Messages::BaseMessageT: public BaseMessage {
	BaseMessageT(char type = 0)
		: BaseMessage(type)
	{ }

	static T* Load(void* src) {
		return reinterpret_cast<T*>(src);
	}
};


struct Publisher::Messages::TransactionBegin: public BaseMessageT<TransactionBegin> {
	TransactionBegin(int id = 0)
		: id_(id),
		  BaseMessageT<TransactionBegin>(kTransactionBegin)
	{ 
	}

	int id_; 
};


struct Publisher::Messages::TransactionCommit: public BaseMessageT<TransactionCommit> {
	TransactionCommit()
		: BaseMessageT<TransactionCommit>(kTransactionCommit)
	{ }
};


struct Publisher::Messages::LogicalOperationHeader: public BaseMessageT<LogicalOperationHeader> {
	LogicalOperationHeader(char id, size_t total_size)
		: id_(id),
		  payload_size_(total_size - sizeof(LogicalOperationHeader)),
		  BaseMessageT<LogicalOperationHeader>(kLogicalOperation)
	{ }

	char   id_; 
	size_t payload_size_;
};


template<typename T>
struct Publisher::Messages::LogicalOperationHeaderT: public LogicalOperationHeader {
	LogicalOperationHeaderT(char id, size_t total_size)
		: LogicalOperationHeader(id, total_size)
	{ }

	static T* Load(void* src) {
		return reinterpret_cast<T*>(src);
	}
};


struct Publisher::Messages::ContainerOperationHeader: public BaseMessageT<ContainerOperationHeader> {
	ContainerOperationHeader(char id, short total_size)
		: id_(id),
		  payload_size_(total_size - sizeof(ContainerOperationHeader)),
		  BaseMessageT<ContainerOperationHeader>(kContainerOperation)
	{ }
	
	short payload_size_;
	char  id_; 
};



struct Publisher::Messages::ContainerOperation {
	enum OperationCode {
		kAllocateExtent = 1,
		kLinkBlock,
		kLockCertificate
	};
	struct AllocateExtent;
	struct LinkBlock;
	struct LockCertificate;
};


struct Publisher::Messages::ContainerOperation::AllocateExtent: public ContainerOperationHeader {
	AllocateExtent()
		: ContainerOperationHeader(kAllocateExtent, sizeof(AllocateExtent))
	{ }

	static AllocateExtent* Load(void* src) {
		return reinterpret_cast<AllocateExtent*>(src);
	}
};


struct Publisher::Messages::ContainerOperation::LinkBlock: public ContainerOperationHeader {
	LinkBlock(ssa::common::ObjectId oid, uint64_t bn, void* ptr)
		: ContainerOperationHeader(kLinkBlock, sizeof(LinkBlock)),
		  oid_(oid),
		  bn_(bn), 
		  ptr_(ptr)
	{ }
	
	static LinkBlock* Load(void* src) {
		return reinterpret_cast<LinkBlock*>(src);
	}
	
	ssa::common::ObjectId oid_;
	uint64_t              bn_;
	void*                 ptr_;
};


struct Publisher::Messages::ContainerOperation::LockCertificate: public ContainerOperationHeader {
	LockCertificate(int nlocks)
		: ContainerOperationHeader(kLockCertificate, sizeof(LockCertificate) + nlocks*sizeof(ssa::cc::common::LockId))
	{ }
	
	static LockCertificate* Load(void* src) {
		return reinterpret_cast<LockCertificate*>(src);
	}
	
	ssa::cc::common::LockId locks_[0];
};


} // namespace ssa

#endif // __STAMNOS_SSA_COMMON_PUBLISHER_H
