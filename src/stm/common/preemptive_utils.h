#ifndef PREEMPTIVE_UTILS_H_
#define PREEMPTIVE_UTILS_H_

#ifdef WLPDSTM_LINUXOS || defined WLPDSTM_SOLARIS
#include <pthread.h>

namespace wlpdstm {

	// give up on this processor slice
	inline void pre_yield() {
		::pthread_yield();
	}
}

#elif defined WLPDSTM_MACOS

#include <sched.h>

namespace wlpdstm {
	
	// give up on this processor slice
	inline void pre_yield() {
		::sched_yield();
	}
}

#endif /* WLPDSTM_LINUXOS */

#endif // PREEMPTIVE_UTILS_H_

