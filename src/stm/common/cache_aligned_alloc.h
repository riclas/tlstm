/**
 * Allocate memory aligned on a boundary. Boundary is a power of two.
 * 
 * @author Aleksandar Dragojevic
 */

#ifndef TLSTM_CACHE_ALIGNED_ALLOC_H_
#define TLSTM_CACHE_ALIGNED_ALLOC_H_

#include <stdint.h>

namespace tlstm {

	template<uintptr_t BOUNDARY>
	class AlignedAlloc {
		public:
			void *operator new(size_t size) {
				size_t actual_size = size + BOUNDARY + sizeof(void *);
				uintptr_t mem_block = (uintptr_t)malloc(actual_size);
				uintptr_t ret = ((mem_block + sizeof(void *) + BOUNDARY) & ~(BOUNDARY - 1));
				void **back_ptr = (void **)(ret - sizeof(void *));
				*back_ptr = (void *)mem_block;
				return (void *)ret;
			}

			void operator delete(void *ptr) {
				void **back_ptr = (void **)((uintptr_t)ptr - sizeof(void *));
				free(*back_ptr);
			}
	};

	typedef AlignedAlloc<CACHE_LINE_SIZE_BYTES> CacheAlignedAlloc;
}

#endif /* TLSTM_CACHE_ALIGNED_ALLOC_H_ */
