/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef TLSTM_PERF_CNT_SOLARIS_H_
#define TLSTM_PERF_CNT_SOLARIS_H_

#include "../common/timing.h"

#include <libcpc.h>
#include <errno.h>
#include <sys/lwp.h>
#include <stdint.h>

#define INSTRUCTION_COUNT_EVENT_NAME "Instr_cnt"
#define CACHE_MISSES_EVENT_NAME "DC_miss"

namespace tlstm {

	class PerfCntSolaris {
		public:
			void ThreadInit();
			void TxStart();
			void TxEnd();

			uint64_t GetElapsedCycles();
			uint64_t GetRetiredInstructions();
			uint64_t GetCacheMisses();

		public:
			static void GlobalInit();

		private:
			uint64_t cycles;
			uint64_t ret_inst;
			uint64_t cache_misses;

			cpc_t *cpc;
			cpc_set_t *set;
			int ret_inst_ind;
			int cache_misses_ind;
			cpc_buf_t *diff, *after, *before;
	};

	typedef PerfCntSolaris PerfCnt;
}

inline void tlstm::PerfCntSolaris::GlobalInit() {
	/* nothing */
}

inline void tlstm::PerfCntSolaris::ThreadInit() {
	// initialize local state
	cycles = 0;
	ret_inst = 0;	

	// I skip error handling (maybe not the best thing to do, but I assume Niagara2 CPU here)
	cpc = cpc_open(CPC_VER_CURRENT);
	set = cpc_set_create(cpc);
	ret_inst_ind = cpc_set_add_request(cpc, set, INSTRUCTION_COUNT_EVENT_NAME, 0, CPC_COUNT_USER, 0, NULL);
	cache_misses_ind = cpc_set_add_request(cpc, set, CACHE_MISSES_EVENT_NAME, 0, CPC_COUNT_USER, 0, NULL);
	
	diff = cpc_buf_create(cpc, set);
	after = cpc_buf_create(cpc, set);
	before = cpc_buf_create(cpc, set);
	
	cpc_bind_curlwp(cpc, set, 0);
}

inline void tlstm::PerfCntSolaris::TxStart() {
	// read elapsed cycles
	cycles = get_clock_count();

	// read performance counters
	cpc_set_sample(cpc, set, before);
}

inline void tlstm::PerfCntSolaris::TxEnd() {
	// read elapsed cycles
	cycles = get_clock_count() - cycles;

	// read performance counters
	cpc_set_sample(cpc, set, after);
	cpc_buf_sub(cpc, diff, after, before);
	cpc_buf_get(cpc, diff, ret_inst_ind, &ret_inst);
	cpc_buf_get(cpc, diff, cache_misses_ind, &cache_misses);
}

inline uint64_t tlstm::PerfCntSolaris::GetElapsedCycles() {
	return cycles;
}

inline uint64_t tlstm::PerfCntSolaris::GetRetiredInstructions() {
	return ret_inst;
}

inline uint64_t tlstm::PerfCntSolaris::GetCacheMisses() {
	return cache_misses;
}


#endif /* TLSTM_PERF_CNT_SOLARIS_H_ */
