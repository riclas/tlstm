/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#include <itm.h>

#include "memory.h"
#include "../stm/transaction.h"

#ifdef __cplusplus
extern "C"
{
#endif

// NOTE: this works only on linux, so be careful
#if defined TLSTM_LINUXOS
#include <malloc.h>

	inline size_t malloc_mem_size(void *ptr) {
		return malloc_usable_size(ptr);
	}
#endif /* TLSTM_LINUXOS */

	void *tlstm_icc_malloc(size_t size) {
		tlstm::TxMixinv *tx = (tlstm::TxMixinv *)_ITM_getTransaction();
		return tx->TxMalloc(size);
	}

	void tlstm_icc_free(void *ptr) {
		tlstm::TxMixinv *tx = (tlstm::TxMixinv *)_ITM_getTransaction();
		tx->TxFree(ptr, malloc_mem_size(ptr));
	}

#ifdef __cplusplus
}
#endif
