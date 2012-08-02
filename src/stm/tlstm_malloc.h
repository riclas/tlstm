/**
 * Definitions for various memory management functions are here. They can be simply
 * used by different compiler wrappers.
 *
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

// TODO: This is a very ugly file name. Think of something better later.

#ifndef TLSTM_MALLOC_H_
#define TLSTM_MALLOC_H_

#include "transaction.h"

void *tlstm_malloc_tx_malloc(size_t size) {
	return tlstm::CurrentTransaction::TxMalloc(size);
}

void tlstm_malloc_tx_free(void *ptr, size_t size) {
	tlstm::CurrentTransaction::TxFree(ptr, size);
}

#endif /* TLSTM_MALLOC_H_ */
