/**
 * The implementing classes should implement the following interface:
 *
 * - GlobalInit()
 * - ThreadInit()
 * - TxStart()
 * - TxEnd()
 * valid after TxEnd() is called:
 * - uint64_t GetElapsedCycles()
 * - uint64_t GetRetiredInstructions()
 *
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifdef PERFORMANCE_COUNTING

#ifndef TLSTM_PERF_CNT_H_
#define TLSTM_PERF_CNT_H_

#ifdef TLSTM_SOLARIS

#include "perf_cnt_solaris.h"

#else

#undef PERFORMANCE_COUNTING

#endif /* TLSTM_SOLARIS */

#endif /* TLSTM_PERF_CNT_H_ */

#endif /* PERFORMANCE_COUNTING */
