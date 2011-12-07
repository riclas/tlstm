/**
 *  Collect simple execution statistics.
 *
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#ifndef WLPDSTM_STATS_H_
#define WLPDSTM_STATS_H_

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

#ifndef PRIu64

#ifdef WLPDSTM_X86
#define PRIu64 "llu"
#elif defined WLPDSTM_X86_64
#define PRIu64 "llu"
#endif /* ARCH */

#endif /* PRIu64 */

//#include "common/iostream_wrap.h"
#include "common/cache_aligned_alloc.h"
#include "constants.h"

namespace wlpdstm {

	class Statistics : public CacheAlignedAlloc {
	public:
		enum Type {
			COMMIT = 0,
			COMMIT_READ_ONLY,
			ABORT,
			ABORT_INCONSISTENT_READ,
			ABORT_SPEC_READER,
			ABORT_WRITE_LOCKED,
			ABORT_TASK_WRITE_LOCKED,
			ABORT_WRITE_VALIDATE,
			ABORT_READ_VALIDATE,
			ABORT_READ_LOCKED,
			ABORT_COMMIT_VALIDATE,
			CLOCK_OVERFLOWS,
#ifdef INFLUENCE_DIAGRAM_STATS
			READ_LOG_SIZE,
			READ_LOG_SIZE_ABORT_READ,
			READ_LOG_SIZE_ABORT_WRITE,
			WRITE_LOG_SIZE,
			WRITE_LOG_SIZE_ABORT_READ,
			WRITE_LOG_SIZE_ABORT_WRITE,
			FALSE_ABORTS_READ,
			FALSE_ABORTS_WRITE,
#endif /* INFLUENCE_DIAGRAM_STATS */
#ifdef TS_EXTEND_STATS
			EXTEND_SUCCESS,
			EXTEND_FAILURE,
#endif /* TS_EXTEND_STATS */
#ifdef DETAILED_STATS
			FREE_DOMINATED,
			UPDATE_MINIMUM_TS,
			SWITCH_TO_SECOND_CM_PHASE,
			CM_DECIDE,
			LARGER_TABLE_WRITE_HITS,
			LARGER_TABLE_READ_HITS,
			WRITES,
			NEW_WRITES,
			READS,
			NEW_READS,
			FALSE_CONFLICTS,
			LOCK_TABLE_RECONFIGURATIONS,
			MEMORY_DEALLOC_COUNT,
			MEMORY_DEALLOC_SIZE,
			MEMORY_DEALLOC_LOCKS,
			WAIT_ON_ABORT,
#endif /* DETAILED_STATS */
#ifdef PERFORMANCE_COUNTING
			CYCLES,
			RETIRED_INSTRUCTIONS,
			CACHE_MISSES,
#endif /* PERFORMANCE_COUNTING */							
			COUNT
		};

		static const char *GetTypeName(Type type) {
			const char *type_names[] = {
				"Commit",
				"CommitReadOnly",
				"Abort",
				"AbortInconsistentRead",
				"AbortSpecReader",
				"AbortWriteLocked",
				"AbortTaskWriteLocked",
				"AbortWriteValidate",
				"AbortReadValidate",
				"AbortReadLocked",
				"AbortCommitValidate",
				"ClockOverflows",
#ifdef INFLUENCE_DIAGRAM_STATS
				"ReadLogSize",
				"ReadLogSizeAbortRead",
				"ReadLogSizeAbortWrite",
				"WriteLogSize",
				"WriteLogSizeAbortRead",
				"WriteLogSizeAbortWrite",
				"FalseAbortsRead",
				"FalseAbortsWrite",
#endif /* INFLUENCE_DIAGRAM_STATS */
#ifdef TS_EXTEND_STATS
				"ExtendSuccess",
				"ExtendFailure",
#endif /* TS_EXTEND_STATS */
#ifdef DETAILED_STATS
				"FreeDominated",
				"UpdateMinimumTs",
				"SwitchToSecondCMPhase",
				"CMDecide",
				"LargerTableWriteHits",
				"LargerTableReadHits",
				"Writes",
				"NewWrites",
				"Reads",
				"NewReads",
				"FalseConflicts",
				"LockTableReconfigurations",
				"MemoryDeallocCount",
				"MemoryDeallocSize",
				"MemoryDeallocLocks",
				"WaitOnAbort",
#endif /* DETAILED_STATS */
#ifdef PERFORMANCE_COUNTING
				"Cycles",
				"RetiredInstructions",
				"CacheMisses"
#endif /* PERFORMANCE_COUNTING */				
			};

			return type_names[type];
		}
		
	public:
		Statistics() {
			Reset();
		}

		void Reset() {
			memset(&stat_counts, 0, sizeof(stat_counts));
		}

		void Increment(Type stat, uint64_t inc = 1) {
			stat_counts[stat] += inc;
		}

		void Merge(Statistics *other) {
			for(unsigned i = 0;i < COUNT;i++) {
				Increment((Type)i, other->Get((Type)i));
			}			
		}

		uint64_t Get(Type stat) {
			return stat_counts[stat];
		}

	private:
		uint64_t stat_counts[COUNT];
	};

	// hold statistics for a single thread and for lexical transactions
	struct ThreadStatistics {
		static const int MAX_LEXICAL_STATS = 64;

		ThreadStatistics() : stats(new Statistics()), next(NULL), lexical_tx_id(NO_LEXICAL_TX) {
			for(int i = 0;i < MAX_LEXICAL_STATS;i++) {
				lexical_tx_stats[i] = new Statistics();
				lexical_tx_stat_used[i] = false;
			}
		}

		void Merge(const ThreadStatistics *tstat) {
			stats->Merge(tstat->stats);

			for(int i = 0;i < MAX_LEXICAL_STATS;i++) {
				lexical_tx_stats[i]->Merge(tstat->lexical_tx_stats[i]);
				lexical_tx_stat_used[i] |= tstat->lexical_tx_stat_used[i];
			}
		}

		inline void IncrementStatistics(Statistics::Type type, uint64_t inc = 1) {
#ifdef COLLECT_STATS
			stats->Increment(type, inc);

			if(lexical_tx_id != -1) {
				lexical_tx_stats[lexical_tx_id]->Increment(type, inc);
				lexical_tx_stat_used[lexical_tx_id] = true;
			}
#endif
		}
		
		void Print(FILE *out_file, unsigned indent = 1);
		void PrintIndent(FILE *out_file, unsigned indent);

		Statistics *stats;

		Statistics *lexical_tx_stats[MAX_LEXICAL_STATS];
		bool lexical_tx_stat_used[MAX_LEXICAL_STATS];

		// this is used just for finalization
		ThreadStatistics *next;

		int lexical_tx_id;
	};

	// class for manipulating group of statistics
	class ThreadStatisticsCollection {
		public:
			ThreadStatisticsCollection() : head(NULL), last(NULL) {
				// empty
			}

			void Add(ThreadStatistics *stat) {
				if(last == NULL) {
					head = last = stat;
				} else {
					last->next = stat;
					last = stat;
				}
			}

			ThreadStatistics MergeAll() {
				ThreadStatistics ret;
				ThreadStatistics *curr;

				for(curr = head;curr != NULL;curr = curr->next) {
					ret.Merge(curr);
				}

				return ret;
			}

		private:
			ThreadStatistics *head;
			ThreadStatistics *last;
	};
}

inline void wlpdstm::ThreadStatistics::PrintIndent(FILE *out_file, unsigned indent) {	
	for(unsigned u = 0;u < indent;u++) {
		fprintf(out_file, "\t");
	}
}
		
inline void wlpdstm::ThreadStatistics::Print(FILE *out_file, unsigned indent) {	
	for(unsigned i = 0;i < Statistics::COUNT;i++) {
		PrintIndent(out_file, indent);
		fprintf(out_file, "%s: %" PRIu64 "\n", Statistics::GetTypeName((Statistics::Type)i),
				stats->Get((Statistics::Type)i));
	}

	for(int ls = 0;ls < MAX_LEXICAL_STATS;ls++) {
		if(lexical_tx_stat_used[ls]) {
			PrintIndent(out_file, indent);
			fprintf(out_file, "Tx %d:\n", ls);

			for(unsigned i = 0;i < Statistics::COUNT;i++) {
				PrintIndent(out_file, indent + 1);
				fprintf(out_file, "%s: %" PRIu64 "\n", Statistics::GetTypeName((Statistics::Type)i),
						lexical_tx_stats[ls]->Get((Statistics::Type)i));
			}
		}
	}
}

#endif /* WLPDSTM_STATS_H_ */
