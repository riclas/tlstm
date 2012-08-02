/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#ifndef TLSTM_TIMING_H_
#define TLSTM_TIMING_H_

#include <stdint.h>

// NOTE: AMD64 does not work with a "=A" (ret) version
// have to check if Intel in 64 bit mode would work
inline uint64_t get_clock_count() {
#ifdef TLSTM_X86
#  ifdef TLSTM_32
	uint64_t ret;
	__asm__ volatile ("rdtsc" : "=A" (ret));
	return ret;
#  elif defined TLSTM_64
	uint32_t low, high;
	__asm__ volatile("rdtsc" : "=a" (low), "=d" (high));
	return (uint64_t)low | (((uint64_t)high) << 32);
#  endif /* TLSTM_64 */
#elif defined TLSTM_SPARC && defined TLSTM_64
	uint64_t ret;
	__asm__ __volatile__ ("rd %%tick, %0" : "=r" (ret));
	return ret;
#else
	// avoid compiler complaints
	return 0; 
#endif /* arch */
}

inline void wait_cycles(uint64_t cycles) {
	uint64_t start = get_clock_count();
	uint64_t end;

	while(true) {
		end = get_clock_count();

		if(end - start > cycles) {
			break;
		}
	}
}

#endif /* TLSTM_TIMING_H_ */

