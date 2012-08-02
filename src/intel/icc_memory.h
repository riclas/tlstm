/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#ifndef TLSTM_ICC_MEMORY_H_
#define TLSTM_ICC_MEMORY_H_

#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

__attribute__((tm_wrapping(malloc))) void *tlstm_icc_malloc(size_t size);
__attribute__((tm_wrapping(free))) void tlstm_icc_free(void *ptr);

			  
#ifdef __cplusplus
}
#endif
			  
#endif /* TLSTM_ICC_MEMORY_H_ */
