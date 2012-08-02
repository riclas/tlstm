/**
 *  Functions that depend on CPU architecture. Reading stack pointer for example.
 *
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#ifndef TLSTM_ARCH_H_
#define TLSTM_ARCH_H_

#include <stdint.h>

namespace tlstm {

	uintptr_t read_sp();

	uintptr_t read_bp();
	
}

inline uintptr_t tlstm::read_sp() {
#ifdef TLSTM_X86
	uintptr_t ret;
	__asm__ volatile ("mov %%esp, %0" : "=A" (ret) : : "%eax");
	return ret;	
#else 
#error Target not supported yet.
#endif  /* arch */
}

inline uintptr_t tlstm::read_bp() {
#ifdef TLSTM_X86
	uintptr_t ret;
	__asm__ volatile ("mov %%ebp, %0" : "=A" (ret) : : "%eax");
	return ret;	
#else 
#error Target not supported yet.
#endif  /* arch */
}

#endif /* TLSTM_ARCH_H_ */
