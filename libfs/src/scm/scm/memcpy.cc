#include <string.h>
#include "scm/scm/model.h"

#if 0
// Assumes x86-64: 64B cacheline size and 8B word size
void*
ntmemcpy(void *dst, const void *src, size_t n)
{
	uintptr_t   saddr = (uintptr_t) src;
	uintptr_t   daddr = (uintptr_t) dst;
	uintptr_t   offset;
 	scm_word_t* val;
	size_t      size = n;
	void*       ret;

	if (size == 0) {
		return dst;
	}

	if (size < CACHELINE_SIZE) {
		ret = memcpy(dst, src, n);
		ScmFlush(dst); // insight : Scmflush calls emulate_latency
		return ret;
	}

	// We need to align stream stores at cacheline boundaries
	// Start with a non-aligned cacheline write, then proceed with
	// a bunch of aligned writes, and then do a last non-aligned
	// cacheline for the remaining data.
	
	if ((offset = (uintptr_t) CACHEINDEX_ADDR(daddr)) != 0) {
		val = ((scm_word_t *) saddr);
		asm_sse_write_block64((uintptr_t *) daddr, val);
		saddr += 64 - offset;
		daddr += 64 - offset;
		size -= (64 - offset);
	}
	// insight : This loop can potentially be slowing us down
	// Using the call graph, this is where i come to
	// If i manage to optimize this routine then may be i can
	// optimize the whole stack !!!
	while(size >= 64) {
		val = ((scm_word_t *) saddr);
		asm_sse_write_block64((uintptr_t *) daddr, val);
		saddr+=64;
		daddr+=64;
		size-=64;
	}
	if (size > 0) {
		offset = 64 - size;
		saddr-=offset;
		daddr-=offset;
		val = ((scm_word_t *) saddr);
		asm_sse_write_block64((uintptr_t *) daddr, val);
	}
	return dst;
}
#else

void*
ntmemcpy_noinline(void *dst, const void *src, size_t n)
{
	uintptr_t     saddr = (uintptr_t) src;
	uintptr_t     daddr = (uintptr_t) dst;
	uintptr_t     offset;
	uint64_t*     val;
	size_t        size = n;

	// We need to align stream stores at cacheline boundaries
	// Start with a non-aligned cacheline write, then proceed with
	// a bunch of aligned writes, and then do a last non-aligned
	// cacheline for the remaining data.

	if (size >= CACHE_LINE_SIZE) {
		if ((offset = (uintptr_t) CACHE_LINE_OFFSET(daddr)) != 0) {
			size_t nlivebytes = (CACHE_LINE_SIZE - offset);
			while (nlivebytes >= sizeof(uint64_t)) {
				asm_sse_write((uint64_t *) daddr, *((uint64_t *) saddr));
				saddr+=sizeof(uint64_t);
				daddr+=sizeof(uint64_t);
				size-=sizeof(uint64_t);
				nlivebytes-=sizeof(uint64_t);
			}
		}
		while(size >= CACHE_LINE_SIZE) {
			val = ((uint64_t *) saddr);
			asm_sse_write_block64((uintptr_t *) daddr, val);
			saddr+=CACHE_LINE_SIZE;
			daddr+=CACHE_LINE_SIZE;
			size-=CACHE_LINE_SIZE;
		}
	}
	while (size >= sizeof(uint64_t)) {
		val = ((uint64_t *) saddr);
		asm_sse_write((uint64_t *) daddr, *((uint64_t *) saddr));
		saddr+=sizeof(uint64_t);
		daddr+=sizeof(uint64_t);
		size-=sizeof(uint64_t);
	}

	/* Now make sure data is flushed out */
	asm_mfence();

	return dst;
}

void* ntmemcpy_noinline(void *dst, const void *src, size_t n);

// handle some special cases as inline for speed
void* ntmemcpy(void *dst, const void *src, size_t n)
{
	if (n == sizeof(uint64_t)) {
		uint64_t val = *((uint64_t*) src);
		asm_sse_write(dst, val);
		return dst;
        }
        return ntmemcpy_noinline(dst, src, n);
}
	
#endif

