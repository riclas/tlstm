/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef TLSTM_ALLOCATORS_H_
#define TLSTM_ALLOCATORS_H_

#include "memory.h"

namespace tlstm {

	/**
	 * Use non-tx free/malloc provided by tlstm.
	 */
	class WlpdstmAlloced {
		public:
			void* operator new(size_t size) {
				return MemoryManager::Malloc(size);
			}

			void* operator new[](size_t size) {
				return MemoryManager::Malloc(size);
			}

			void operator delete(void* ptr) {
				MemoryManager::Free(ptr);
			}
	};
}

#endif // TLSTM_ALLOCATORS_H_
