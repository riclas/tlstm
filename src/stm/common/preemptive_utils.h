#ifndef PREEMPTIVE_UTILS_H_
#define PREEMPTIVE_UTILS_H_

#ifdef TLSTM_LINUXOS || defined TLSTM_SOLARIS
#include <pthread.h>

namespace tlstm {

	// give up on this processor slice
	inline void pre_yield() {
		::pthread_yield();
	}
}

#elif defined TLSTM_MACOS

#include <sched.h>

namespace tlstm {
	
	// give up on this processor slice
	inline void pre_yield() {
		::sched_yield();
	}
}

#endif /* TLSTM_LINUXOS */

#endif // PREEMPTIVE_UTILS_H_

