/**
 * Definitions for various memory management functions are here. They can be simply
 * used by different compiler wrappers.
 *
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

// TODO: This is a very ugly file name. Think of something better later.

#ifndef WLPDSTM_MALLOC_H_
#define WLPDSTM_MALLOC_H_

#include "transaction.h"

void *wlpdstm_malloc_tx_malloc(size_t size) {
	return wlpdstm::CurrentTransaction::TxMalloc(size);
}

void wlpdstm_malloc_tx_free(void *ptr, size_t size) {
	wlpdstm::CurrentTransaction::TxFree(ptr, size);
}

#endif /* WLPDSTM_MALLOC_H_ */
