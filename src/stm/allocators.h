/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef WLPDSTM_ALLOCATORS_H_
#define WLPDSTM_ALLOCATORS_H_

#include "memory.h"

namespace wlpdstm {

	/**
	 * Use non-tx free/malloc provided by wlpdstm.
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

#endif // WLPDSTM_ALLOCATORS_H_
