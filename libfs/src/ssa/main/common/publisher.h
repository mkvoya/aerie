#ifndef __STAMNOS_SSA_COMMON_PUBLISHER_H
#define __STAMNOS_SSA_COMMON_PUBLISHER_H

#include "bcs/bcs.h"
#include "ssa/main/server/ssa-opaque.h"

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
		kTransactionEnd,
		kLogicalOperation,
		kPhysicalOperation,
	};
	struct BaseMessage;
	struct TransactionBegin;
	struct TransactionEnd;
	struct LogicalOperationHeader;
	struct PhysicalOperationHeader;
	struct PhysicalOperation;
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


struct Publisher::Messages::TransactionBegin: public BaseMessage {
	TransactionBegin(int id = 0)
		: id_(id),
		  BaseMessage(kTransactionBegin)
	{ 
	}

	static TransactionBegin* Load(void* src) {
		return reinterpret_cast<TransactionBegin*>(src);
	}

	int id_; 
};


struct Publisher::Messages::TransactionEnd: public BaseMessage {
	TransactionEnd()
		: BaseMessage(kTransactionEnd)
	{ }

	static TransactionEnd* Load(void* src) {
		return reinterpret_cast<TransactionEnd*>(src);
	}
};


struct Publisher::Messages::LogicalOperationHeader: public BaseMessage {
	LogicalOperationHeader(char id)
		: id_(id),
		  BaseMessage(kLogicalOperation)
	{ }

	static LogicalOperationHeader* Load(void* src) {
		return reinterpret_cast<LogicalOperationHeader*>(src);
	}

	char id_; 
};


struct Publisher::Messages::PhysicalOperationHeader: public BaseMessage {
	PhysicalOperationHeader(int id)
		: id_(id),
		  BaseMessage(kPhysicalOperation)
	{ }

	static PhysicalOperationHeader* Load(void* src) {
		return reinterpret_cast<PhysicalOperationHeader*>(src);
	}
	
	int  id_; 
};



struct Publisher::Messages::PhysicalOperation {
	enum OperationCode {
		kAllocateExtent = 1,
		kLinkBlock
	};
	struct AllocateExtent;
	struct LinkBlock;
};


struct Publisher::Messages::PhysicalOperation::AllocateExtent: public PhysicalOperationHeader {
	AllocateExtent()
		: PhysicalOperationHeader(1)
	{ }

	static AllocateExtent* Load(void* src) {
		return reinterpret_cast<AllocateExtent*>(src);
	}
};


struct Publisher::Messages::PhysicalOperation::LinkBlock: public PhysicalOperationHeader {
	LinkBlock(uint64_t bn, void* ptr)
		: PhysicalOperationHeader(2),
		  bn_(bn), 
		  ptr_(ptr)
	{ }
	
	static LinkBlock* Load(void* src) {
		return reinterpret_cast<LinkBlock*>(src);
	}
	
	uint64_t bn_;
	void*    ptr_;
};


} // namespace ssa

#endif // __STAMNOS_SSA_COMMON_PUBLISHER_H
