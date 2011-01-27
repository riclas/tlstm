/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 * TODO: move privatization quiescence to a separate class
 * TODO: move contention management to a separate class
 * TODO: perhaps move definition of logs into a separate file
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "common/word.h"
#include "aborted_exc.h"
#include "common/version_lock.h"
#include "constants.h"
#include "common/log.h"
#include "common/timestamp.h"
#include "memory.h"
#include "tid.h"
#include "common/tls.h"
#include "common/cache_aligned_alloc.h"
#include "common/jmp.h"
#include "stats.h"
#include "common/large_lock_set.h"
#include "common/timing.h"
#include "common/random.h"
#include "common/padded.h"

#ifdef USE_PREEMPTIVE_WAITING
#include "common/preemptive_utils.h"
#endif // USE_PREEMPTIVE_WAITING

#ifdef STACK_PROTECT
#include "common/arch.h"
#endif /* STACK_PROTECT */

#ifdef PRIVATIZATION_QUIESCENCE_TREE
#include "privatization_tree.h"
#endif /* PRIVATIZATION_QUIESCENCE_TREE */

#include "perfcnt/perf_cnt.h"
#include "common/sampling.h"

// default is 2
// how many successive locations (segment size)
#define LOG_DEFAULT_LOCK_EXTENT_SIZE_WORDS 2
#define DEFAULT_LOCK_EXTENT_SIZE (LOG_DEFAULT_LOCK_EXTENT_SIZE_WORDS + LOG_BYTES_IN_WORD)
#define MIN_LOCK_EXTENT_SIZE LOG_BYTES_IN_WORD
#define MAX_LOCK_EXTENT_SIZE 10

#define VERSION_LOCK_TABLE_SIZE (1 << 22)
// two locks are used - write/write and read/write 
#define FULL_VERSION_LOCK_TABLE_SIZE (VERSION_LOCK_TABLE_SIZE << 1)

#ifdef ADAPTIVE_LOCKING
#define CHANGE_TABLE_CONFIGURATION_FREQUENCY 100
#endif /* ADAPTIVE_LOCKING */

// rbtree
//     20 no tx go to second phase
//     10 several tx go to second phase
//      5 significant number of tx go to second phase
// default is 10
#define CM_ACCESS_THRESHOLD 10

// rbtree
//     20 number of FreeDominated calls is small and this parameter does not make difference
// default is 20
#define UPDATE_LOCAL_LAST_OBSERVED_TS_FREQUENCY 20


// The size of start_buf paddig in cache lines.
// This should be bigger than the biggest start_buf, or will cause cache misses.
#define START_BUF_PADDING_SIZE 10

//TLSTM
#define SPECDEPTH 8

namespace wlpdstm {
	
	typedef VersionLock WriteLock;
	
	class TxMixinv : public CacheAlignedAlloc {
		
		//////////////////////////////////
		// log related structures start //
		//////////////////////////////////
		
		struct ReadLogEntry {
			VersionLock *read_lock;
			VersionLock version;
		};

		// mask is needed here to avoid overwriting non-transactional
		// data with their stale values upon commit 
		struct WriteWordLogEntry {
			Word *address;
			Word value;
			Word mask;
			WriteWordLogEntry *next;
		};

		static const Word LOG_ENTRY_UNMASKED = ~0x0;
		
#ifdef SUPPORT_LOCAL_WRITES
		// TODO: local writes are not used in any of the benchmarks,
		// so they are not updated to use mask
		struct WriteWordLocalLogEntry {
			Word *address;
			Word value;
		};
#endif /* SUPPORT_LOCAL_WRITES */
		
		struct WriteLogEntry {
			VersionLock *read_lock;
			WriteLock *write_lock;
			
			VersionLock old_version;
			
			TxMixinv *owner;
			
			WriteWordLogEntry *head;
			
			// methods
			void InsertWordLogEntry(Word *address, Word value, Word mask);
			
//			void InsertWordLogEntryMasked(Word *address, Word value, Word mask);
			
			WriteWordLogEntry *FindWordLogEntry(Word *address);
			
			void ClearWordLogEntries();
		};
		
		typedef Log<ReadLogEntry> ReadLog;
		typedef Log<WriteLogEntry> WriteLog;
#ifdef SUPPORT_LOCAL_WRITES
		typedef Log<WriteWordLocalLogEntry> WriteLocalLog;
#endif /* SUPPORT_LOCAL_WRITES */
		typedef Log<WriteWordLogEntry> WriteWordLogMemPool;
		
		////////////////////////////////
		// log related structures end //
		////////////////////////////////
		
	public:
		// possible CM phases
		enum CmPhase {
			CM_PHASE_INITIAL,
			CM_PHASE_GREEDY
		};
		
		enum RestartCause {
			NO_RESTART = 0,
			RESTART_EXTERNAL,
			RESTART_LOCK,
			RESTART_VALIDATION,
			RESTART_CLOCK_OVERFLOW
		};

		// possible statuses of aborted transactions
		enum TxStatus {
			TX_IDLE,
			TX_EXECUTING,
			TX_ABORTED,
			TX_RESTARTED,
			TX_COMMITTED
		};

#ifdef SIGNALING
		enum TxSignals {
			SIGNAL_EMPTY = 0,
			SIGNAL_PRIVATIZATION_VALIDATE = 0x1
		};
#endif /* SIGNALING */

#ifdef WAIT_ON_SUCC_ABORTS
		static const unsigned SUCC_ABORTS_THRESHOLD = 1;
		static const unsigned SUCC_ABORTS_MAX = 10;
		
		static const unsigned WAIT_CYCLES_MULTIPLICATOR = 8000;
#endif /* WAIT_ON_SUCC_ABORTS */
		
	public:
		static void GlobalInit();
		
		static void InitializeReadLocks();
		
		static void InitializeWriteLocks();

#ifdef PRIVATIZATION_QUIESCENCE		
		static void InitializeQuiescenceTimestamps();
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
		static void InitializeSignaling();
#endif /* SIGNALING */
		
		void ThreadInit();

		/**
		 * Start a transaction.
		 */
		void TxStart(int lex_tx_id = NO_LEXICAL_TX);

		/**
		 * Try to commit a transaction. Return 0 when commit is successful, reason for not succeeding otherwise.
		 */		
		RestartCause TxTryCommit();

		/**
		 * Commit a transaction. If commit is not possible, rollback and restart.
		 */		
		void TxCommit();

		/**
		 * Rollback transaction's effects.
		 */
		void Rollback();

		/**
		 * Rollback transaction's effects and jump to the beginning with flag specifying abort.
		 */		
		void TxAbort();

		/**
		 * Rollback transaction's effects and jump to the beginning with flag specifying restart.
		 */				
		void TxRestart(RestartCause cause = RESTART_EXTERNAL);
		
		void WriteWord(Word *address, Word val, Word mask = LOG_ENTRY_UNMASKED);
		
#ifdef SUPPORT_LOCAL_WRITES
		void WriteWordLocal(Word *address, Word val);
#endif /* SUPPORT_LOCAL_WRITES */
		
		Word ReadWord(Word *address);
		
		void *TxMalloc(size_t size);
		
		void TxFree(void *ptr, size_t size);
		
		// maybe this should be organized slightly different
		void LockMemoryBlock(void *address, size_t size);

		bool IsExecuting() const;

		TxStatus GetTxStatus() const;

		int GetTransactionId() const;

		int GetThreadId();
		
	private:
		void WriteWordInner(Word *address, Word val, Word mask);

		Word ReadWordInner(Word *address);
		
		static Word MaskWord(Word old, Word val, Word mask);

		static Word MaskWord(WriteWordLogEntry *entry);
		
		void RestartJump();
		
		void AbortJump();
		
		unsigned map_address_to_index(Word *address, unsigned le);
		
		unsigned map_address_to_index(Word *address);
		
		VersionLock *map_address_to_read_lock(Word *address);
		
		WriteLock *map_address_to_write_lock(Word *address);
		
		WriteLock *map_read_lock_to_write_lock(VersionLock *lock_address);
		
		VersionLock *map_write_lock_to_read_lock(WriteLock *lock_address);
		
		WriteLogEntry *LockMemoryStripe(WriteLock *write_lock, Word *address);
		
		bool Validate();
		
		bool ValidateCommit();
		
		void ReleaseReadLocks();
		
		bool Extend();

		bool ShouldExtend(VersionLock version);
		
		bool LockedByMe(WriteLogEntry *log_entry);
		
		Word IncrementCommitTs();
		
		///////////////////////////
		// contention management //
		///////////////////////////
		
		bool ShouldAbortWrite(WriteLock *write_lock);
		
		// TODO: isolate CM in a separate class
		bool CMStrongerThan(TxMixinv *other);
		
		void CmStartTx();
		
		void CmOnAccess();
		
#ifdef WAIT_ON_SUCC_ABORTS
		void WaitOnAbort();
#endif /* WAIT_ON_SUCC_ABORTS */
		
		void Register();
		
		void YieldCPU();
		
#ifdef MM_EPOCH
		void InitLastObservedTs();
		
		void UpdateLastObservedTs(Word ts);
		
	public:
		static Word UpdateMinimumObservedTs();
		
		static Word GetMinimumObservedTs();
#endif /* MM_EPOCH */
		
		////////////////////////////////////////
		// synchronize all transactions start //
		////////////////////////////////////////
		
		bool StartSynchronization();
		
		void EndSynchronization();
		
		bool Synchronize();
		
		void RestartCommitTS();
		
		//////////////////////////////////////
		// synchronize all transactions end //
		//////////////////////////////////////
		
	public:
		static void PrintStatistics();
		
		/////////////////////////
		// protect stack start //
		/////////////////////////
		
#ifdef STACK_PROTECT
#if defined STACK_PROTECT_TANGER_BOUND || defined STACK_PROTECT_ICC_BOUND
	public:
		void SetStackHigh(uintptr_t addr);
#elif defined STACK_PROTECT_WLPDSTM_BOUND
	private:
		void SetStackHigh();
#endif /* STACK_PROTECT_TANGER_BOUND */
		
	private:
		bool OnStack(uintptr_t addr);
		
		bool OnStack(uintptr_t addr, uintptr_t current_sp);
#endif /* STACK_PROTECT */
		
#ifdef PRIVATIZATION_QUIESCENCE
		void PrivatizationQuiescenceWait(Word ts);
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
		void SetSignal(unsigned tid, TxSignals signal);
		void ClearSignals();
		bool IsSignalSet(TxSignals signal);
#endif /* SIGNALING */
		
		///////////////////////
		// protect stack end //
		///////////////////////
		
		////////////////
		// data start //
		////////////////

	public:
		// Local, but should be at the start of the descriptor as this is what
		// assembly jump expects.
		CACHE_LINE_ALIGNED union {
			LONG_JMP_BUF start_buf;
			char padding_start_buf[CACHE_LINE_SIZE_BYTES * START_BUF_PADDING_SIZE];
		};
		
	private:
		// shared data aligned as needed
		// assumption here is that the whole descriptor is already aligned
		
		// data accessed by other tx
		// r shared
		CACHE_LINE_ALIGNED union {
			volatile Word greedy_ts;
			char padding_greedy_ts[CACHE_LINE_SIZE_BYTES];
		};
		
		// w shared
		CACHE_LINE_ALIGNED union {
			volatile bool aborted_externally;
			char padding_aborted_extrenally[CACHE_LINE_SIZE_BYTES];
		};
		
#ifdef MM_EPOCH
		// r shared
		CACHE_LINE_ALIGNED union {
			// used for memory management
			volatile Word last_observed_ts;
			char padding_last_observed_ts[CACHE_LINE_SIZE_BYTES];
		};
#endif /* MM_EPOCH */
		
		CACHE_LINE_ALIGNED union {
			volatile Word tx_status;
			char padding_tx_status[CACHE_LINE_SIZE_BYTES];
		};
		
		// tx local data next
		// its alignment is not important, as it is not shared
		
		Word valid_ts;
		
#ifdef PRIVATIZATION_QUIESCENCE
		Word *quiescence_ts;
#elif defined PRIVATIZATION_QUIESCENCE_TREE
		PrivatizationTree privatization_tree;
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
		volatile Word *signals;
#endif /* SIGNALING */

	private:
#ifdef STACK_PROTECT
		// local
		uintptr_t stack_high;
#endif /* STACK_PROTECT */
		/*
		// local
		ReadLog read_log;
		
		// local
		WriteLog write_log;
		*/
		//TLSTM
		// local
		ReadLog[] read_log[SPECDEPTH];
		
		WriteLog[] write_log[SPECDEPTH];
		
		ReadLog[] fw_read_log[SPECDEPTH];

		unsigned lastCommitedTask, lastCompletedTask, nextTask;

#ifdef SUPPORT_LOCAL_WRITES
		// local
		WriteLocalLog write_local_log;
#endif /* SUPPORT_LOCAL_WRITES */
		
		// local
		WriteWordLogMemPool write_word_log_mem_pool;
		
		// contention management
		// local
		unsigned locations_accessed;
		
		// local
		CmPhase cm_phase;
		
		// local
		unsigned succ_aborts;
		
		// memory management support
		// local
		unsigned start_count;
		
	public:
		// local
		Tid tid;
		
		// local
		MemoryManager mm;

	private:
		
		// local
		ThreadStatistics stats;

	private:
		// local
		Random random;

		// local
		// used to prevent multiple rollbacks
		bool rolled_back;

#ifdef PERFORMANCE_COUNTING
	private:
		Sampling perf_cnt_sampling;
		PerfCnt perf_cnt;

#endif /* PERFORMANCE_COUNTING */

		//////////////////////
		// shared variables //
		//////////////////////

	private:
		static VersionLock version_lock_table[FULL_VERSION_LOCK_TABLE_SIZE];
		
		static GlobalTimestamp commit_ts;
		
		static GlobalTimestamp cm_ts;
		
		static PaddedWord minimum_observed_ts;
		
		static PaddedSpinTryLock minimum_observed_ts_lock;
		
		static TxMixinv *transactions[MAX_THREADS];
		
		static Word thread_count;
		
		static PaddedBool synchronization_in_progress;
		
#ifdef PRIVATIZATION_QUIESCENCE
		static volatile Word quiescence_timestamp_array[MAX_THREADS];
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
		static volatile PaddedWord signaling_array[MAX_THREADS];
#endif /* SIGNALING */		

		/////////////////////////////////
		// lock mapping function start //
		/////////////////////////////////
		
#ifdef ADAPTIVE_LOCKING
	private:
		// currently used lock extent
		static PaddedUnsigned lock_extent;
		
		// main parameters of the algorithm
		const static unsigned LOCK_EXTENT_INCREASE_THRESHOLD = 2;
		const static unsigned LOCK_EXTENT_DECREASE_THRESHOLD = 60;
		
		// local
		LargeLockSet<VERSION_LOCK_TABLE_SIZE> larger_lock_extent_table;
		
		// local
		// this is lock_extend for which copy of the table is maintained
		unsigned larger_lock_extent;
		
		const static unsigned NOT_PROPOSING_LOCK_EXTENT = MIN_LOCK_EXTENT_SIZE - 1;
		const static unsigned LOCK_EXTENT_CHANGED = MAX_LOCK_EXTENT_SIZE + 1;
		const static unsigned SIGNIFICANT_STAT_COUNT_ABORTS = 100;
		const static unsigned SIGNIFICANT_STAT_COUNT_WRITES = 100;
		
		// shared
		// proposing new lock extent
		CACHE_LINE_ALIGNED PaddedUnsigned proposed_lock_extent;
		
		unsigned old_lock_extent;
		
		// local - statistics
		unsigned writes;
		unsigned new_writes;
		unsigned larger_table_hits;
		unsigned larger_table_read_hits;
		unsigned aborts;
		unsigned false_sharing;
		const static unsigned FALSE_SHARING_HISTORY_SIZE = MAX_LOCK_EXTENT_SIZE - MIN_LOCK_EXTENT_SIZE + 1;
		static unsigned false_sharing_history[FALSE_SHARING_HISTORY_SIZE];
		
		unsigned reads;
		
		// local
		unsigned change_table_configuration_counter;
		
		// helper functions
		bool IsFalseConflict(WriteLogEntry *log_entry, Word *address);
		void ClearLocalStatistics();
		
		// if table configuration changed apply it
		void ApplyTableConfigurationChange();
		
		// propose table change if necessary
		void ProposeTableChange();
		void ProposeTableChangeInner();
		
		bool EnoughResizeTableDataAborts();
		bool EnoughResizeTableDataWrites();
		
		unsigned CalculateFalseAbortRate();
		unsigned CalculateLargerTableHitRate();
		static unsigned GetFalseSharingHistory(unsigned le);
		
#endif /* ADAPTIVE_LOCKING */
		
		///////////////////////////////
		// lock mapping function end //
		///////////////////////////////
		
		void IncrementReadAbortStats();
		void IncrementWriteAbortStats();
		
	} __attribute__ ((aligned(CACHE_LINE_SIZE_BYTES)));
}

#ifdef ADAPTIVE_LOCKING
#define LOCK_EXTENT lock_extent.val
#else
#define LOCK_EXTENT DEFAULT_LOCK_EXTENT_SIZE
#endif /* ADAPTIVE_LOCKING */

//////////////////////////
// initialization start //
//////////////////////////

inline void wlpdstm::TxMixinv::GlobalInit() {
	InitializeReadLocks();
	
	InitializeWriteLocks();
	
#ifdef PRIVATIZATION_QUIESCENCE
	InitializeQuiescenceTimestamps();
#elif defined PRIVATIZATION_QUIESCENCE_TREE
	PrivatizationTree::GlobalInit();
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
	InitializeSignaling();
#endif /* SIGNALING */
	
	// initialize shared data
	minimum_observed_ts.val = MINIMUM_TS;
	
	// initialize memory manager
	MemoryManager::GlobalInit();
	
	synchronization_in_progress.val = false;
	
#ifdef ADAPTIVE_LOCKING
	lock_extent.val = DEFAULT_LOCK_EXTENT_SIZE;
	
	for(unsigned i = 0;i < FALSE_SHARING_HISTORY_SIZE;i++) {
		false_sharing_history[i] = 0;
	}
#endif /* ADAPTIVE_LOCKING */

#ifdef PERFORMANCE_COUNTING
	PerfCnt::GlobalInit();
#endif /* PERFORMANCE_COUNTING */
}

inline void wlpdstm::TxMixinv::InitializeReadLocks() {
	VersionLock initial_version = get_version_lock(MINIMUM_TS + 1);
	
	for(int i = 0;i < FULL_VERSION_LOCK_TABLE_SIZE;i += 2) {
		version_lock_table[i] = initial_version;
	}	
}

inline void wlpdstm::TxMixinv::InitializeWriteLocks() {
	for(int i = 1;i < FULL_VERSION_LOCK_TABLE_SIZE;i += 2) {
		version_lock_table[i] = WRITE_LOCK_CLEAR;
	}
}

#ifdef PRIVATIZATION_QUIESCENCE
inline void wlpdstm::TxMixinv::InitializeQuiescenceTimestamps() {
	for(unsigned i = 0;i < MAX_THREADS;i++) {
		quiescence_timestamp_array[i] = MINIMUM_TS;
	}	
}
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
inline void wlpdstm::TxMixinv::InitializeSignaling() {
	for(unsigned i = 0;i < MAX_THREADS;i++) {
		signaling_array[i].val = SIGNAL_EMPTY;
	}	
}
#endif /* SIGNALING */

inline void wlpdstm::TxMixinv::ThreadInit() {
	aborted_externally = false;
	
#ifdef MM_EPOCH
	InitLastObservedTs();
#endif /* MM_EPOCH */
	
	Register();
	
	// locally initialize memory manager
	mm.ThreadInit(tid.Get());
	
#ifdef COLLECT_STATS
	mm.InitStats(&stats);
#endif /* COLLECT_STATS */
	
	tx_status = (Word)TX_IDLE;

#ifdef PRIVATIZATION_QUIESCENCE_TREE
	privatization_tree.ThreadInit(tid.Get(), &thread_count);
#endif /* PRIVATIZATION_QUIESCENCE_TREE */
	
#ifdef ADAPTIVE_LOCKING
	// init change lock table conf
	proposed_lock_extent.val = NOT_PROPOSING_LOCK_EXTENT;
	ClearLocalStatistics();
	change_table_configuration_counter = 0;
	larger_lock_extent = lock_extent.val + 1;
	old_lock_extent = lock_extent.val;
#endif /* ADAPTIVE_LOCKING */
	
	succ_aborts = 0;

	assert((uintptr_t)this == (uintptr_t)(&start_buf));

#ifdef PERFORMANCE_COUNTING
	perf_cnt.ThreadInit();
#endif /* PERFORMANCE_COUNTING */
}

////////////////////////
// initialization end //
////////////////////////

inline wlpdstm::TxMixinv::TxStatus wlpdstm::TxMixinv::GetTxStatus() const {
	return (TxStatus)tx_status;
}

inline bool wlpdstm::TxMixinv::IsExecuting() const {
	return tx_status == TX_EXECUTING;
}

inline int wlpdstm::TxMixinv::GetTransactionId() const {
	return stats.lexical_tx_id;
}

inline int wlpdstm::TxMixinv::GetThreadId() {
	return tid.Get();
}


////////////////////////////
// mapping to locks start //
////////////////////////////

inline unsigned wlpdstm::TxMixinv::map_address_to_index(Word *address) {
	return map_address_to_index(address, LOCK_EXTENT) << 1;
}

inline unsigned wlpdstm::TxMixinv::map_address_to_index(Word *address, unsigned le) {
	return (((uintptr_t)address >> (le)) & (VERSION_LOCK_TABLE_SIZE - 1));
}

inline wlpdstm::VersionLock *wlpdstm::TxMixinv::map_address_to_read_lock(Word *address) {
	return version_lock_table + map_address_to_index(address);
}

inline wlpdstm::WriteLock *wlpdstm::TxMixinv::map_address_to_write_lock(Word *address) {
	return map_address_to_read_lock(address) + 1;
}

inline wlpdstm::WriteLock *wlpdstm::TxMixinv::map_read_lock_to_write_lock(VersionLock *lock_address) {
	return lock_address + 1;
}

inline wlpdstm::VersionLock *wlpdstm::TxMixinv::map_write_lock_to_read_lock(WriteLock *lock_address) {
	return lock_address - 1;
}

//////////////////////////
// mapping to locks end //
//////////////////////////


//////////////////////////
// main algorithm start //
//////////////////////////

inline void wlpdstm::TxMixinv::TxStart(int lex_tx_id) {
#ifdef PERFORMANCE_COUNTING
	perf_cnt_sampling.tx_start();

	if(perf_cnt_sampling.should_sample()) {
		perf_cnt.TxStart();
	}
#endif /* PERFORMANCE_COUNTING */	
	
	atomic_store_full(&tx_status, (Word)TX_EXECUTING);
	
	if(Synchronize()) {
#ifdef ADAPTIVE_LOCKING
		ApplyTableConfigurationChange();
#endif /* ADAPTIVE_LOCKING */
		tx_status = (Word)TX_EXECUTING;
	}
	
#ifdef STACK_PROTECT_WLPDSTM_BOUND
	SetStackHigh();
#endif /* STACK_PROTECT_WLPDSTM_BOUND */
	
	mm.TxStart();
	
	// get validity timestamp
	valid_ts = commit_ts.readCurrentTsAcquire();
#ifdef PRIVATIZATION_QUIESCENCE
	*quiescence_ts = valid_ts;
#elif defined PRIVATIZATION_QUIESCENCE_TREE
	privatization_tree.setNonMinimumTs(valid_ts);
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef MM_EPOCH
	UpdateLastObservedTs(valid_ts);
#endif /* MM_EPOCH */
	
	CmStartTx();
	
	// reset aborted flag
	aborted_externally = false;
	
	// initialize lexical tx id
	stats.lexical_tx_id = lex_tx_id;

	// not rolled back yet
	rolled_back = false;
	
#ifdef ADAPTIVE_LOCKING
	// reset table
	larger_lock_extent_table.Clear();
#endif /* ADAPTIVE_LOCKING */

#ifdef SIGNALING
	ClearSignals();
#endif /* SIGNALING */	
}

inline Word wlpdstm::TxMixinv::IncrementCommitTs() {
#ifdef COMMIT_TS_INC
	return commit_ts.getNextTsRelease() + 1;
#elif defined COMMIT_TS_GV4
	return commit_ts.GenerateTsGV4();
#endif /* commit_ts */
}

inline void wlpdstm::TxMixinv::TxCommit() {
	RestartCause ret = TxTryCommit();

	if(ret) {
		TxRestart(ret);
	}

#ifdef PERFORMANCE_COUNTING
	if(perf_cnt_sampling.should_sample()) {
		// if tx is restarted, this code is not reached
		perf_cnt.TxEnd();
		stats.IncrementStatistics(Statistics::CYCLES, perf_cnt.GetElapsedCycles());
		stats.IncrementStatistics(Statistics::RETIRED_INSTRUCTIONS, perf_cnt.GetRetiredInstructions());
		stats.IncrementStatistics(Statistics::CACHE_MISSES, perf_cnt.GetCacheMisses());
	}
#endif /* PERFORMANCE_COUNTING */	
}

inline wlpdstm::TxMixinv::RestartCause wlpdstm::TxMixinv::TxTryCommit() {
	Word ts = valid_ts;
	bool read_only = write_log.empty();
	
	if(!read_only) {
		// first lock all read locks
		for(WriteLog::iterator iter = write_log.begin();iter.hasNext();iter.next()) {
			WriteLogEntry &entry = *iter;
			*(entry.read_lock) = READ_LOCK_SET;
		}
		
		// now get a commit timestamp
		ts = IncrementCommitTs();
		
		// if global time overflows restart
		if(ts >= MAXIMUM_TS) {
			//executing = false;
			tx_status = (Word)TX_ABORTED;
#ifdef PRIVATIZATION_QUIESCENCE
			*quiescence_ts = MINIMUM_TS;
#elif defined PRIVATIZATION_QUIESCENCE_TREE
			privatization_tree.setNonMinimumTs(MINIMUM_TS);
#endif /* PRIVATIZATION_QUIESCENCE */
			ReleaseReadLocks();
			Rollback();
			
			if(StartSynchronization()) {
				RestartCommitTS();
				EndSynchronization();
				stats.IncrementStatistics(Statistics::CLOCK_OVERFLOWS);
			}

			return RESTART_CLOCK_OVERFLOW;
		}

		// if there is no validation in GV4, these the read set of one transaction could
		// overlap with the write set of another and this would pass unnoticed
#ifdef COMMIT_TS_INC
		if(ts != valid_ts + 1 && !ValidateCommit()) {
#elif defined COMMIT_TS_GV4
		if(!ValidateCommit()) {
#endif /* commit_ts */
			ReleaseReadLocks();
			stats.IncrementStatistics(Statistics::ABORT_COMMIT_VALIDATE);
			IncrementReadAbortStats();
#ifdef ADAPTIVE_LOCKING
			++aborts;
#endif /* ADAPTIVE_LOCKING */
			return RESTART_VALIDATION;
		}
		
		VersionLock commitVersion = get_version_lock(ts);
		
		// now update all written values
		for(WriteLog::iterator iter = write_log.begin();iter.hasNext();iter.next()) {
			WriteLogEntry &entry = *iter;
			
			// now update actual values
			WriteWordLogEntry *word_log_entry = entry.head;

			while(word_log_entry != NULL) {
				*word_log_entry->address = MaskWord(word_log_entry);
				word_log_entry = word_log_entry->next;
			}

			// release locks
			atomic_store_release(entry.read_lock, commitVersion);
			atomic_store_release(entry.write_lock, WRITE_LOCK_CLEAR);
		}

	} else {
		stats.IncrementStatistics(Statistics::COMMIT_READ_ONLY);
	}
	
	atomic_store_release(&tx_status, TX_COMMITTED);
		
#ifdef PRIVATIZATION_QUIESCENCE
	atomic_store_release(quiescence_ts, MINIMUM_TS);
	PrivatizationQuiescenceWait(ts);
#elif defined PRIVATIZATION_QUIESCENCE_TREE
//	privatization_tree.setMinimumTs();
	privatization_tree.setNonMinimumTs(MINIMUM_TS);
	privatization_tree.wait(ts);
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef INFLUENCE_DIAGRAM_STATS
	stats.IncrementStatistics(Statistics::READ_LOG_SIZE, read_log.get_size());
	stats.IncrementStatistics(Statistics::WRITE_LOG_SIZE, write_log.get_size());
#endif /* INFLUENCE_DIAGRAM_STATS */

	if(!read_only) {
		write_log.clear();
		write_word_log_mem_pool.clear();
	}

	read_log.clear();
#ifdef SUPPORT_LOCAL_WRITES
	write_local_log.clear();
#endif /* SUPPORT_LOCAL_WRITES */
	
	// commit mem
	mm.TxCommit<TxMixinv>(ts);
	
	stats.IncrementStatistics(Statistics::COMMIT);
	
	succ_aborts = 0;
	
#ifdef ADAPTIVE_LOCKING
	if(!read_only) {
		ProposeTableChange();
	}
#endif /* ADAPTIVE_LOCKING */

	return NO_RESTART;
}

inline void wlpdstm::TxMixinv::ReleaseReadLocks() {
	for(WriteLog::iterator iter = write_log.begin();iter.hasNext();iter.next()) {
		WriteLogEntry &entry = *iter;
		*(entry.read_lock) = entry.old_version;
	}			
}

// updates only write locks
inline void wlpdstm::TxMixinv::Rollback() {
	if(rolled_back) {
		return;
	}

	rolled_back = true;

#ifdef SUPPORT_LOCAL_WRITES
	// rollback local writes
	for(WriteLocalLog::iterator iter = write_local_log.begin();iter.hasNext();iter.next()) {
		WriteWordLocalLogEntry &entry = *iter;
		*entry.address = entry.value;
	}
#endif /* SUPPORT_LOCAL_WRITES */

	// drop locks
	for(WriteLog::iterator iter = write_log.begin();iter.hasNext();iter.next()) {
		WriteLogEntry &entry = *iter;
		*entry.write_lock = WRITE_LOCK_CLEAR;
	}
	
	// empty logs
	read_log.clear();
	write_log.clear();
#ifdef SUPPORT_LOCAL_WRITES
	write_local_log.clear();
#endif /* SUPPORT_LOCAL_WRITES */
	write_word_log_mem_pool.clear();
	
	YieldCPU();
	
	mm.TxAbort();
	
	stats.IncrementStatistics(Statistics::ABORT);
}

inline void wlpdstm::TxMixinv::IncrementReadAbortStats() {
#ifdef INFLUENCE_DIAGRAM_STATS
	stats.IncrementStatistics(Statistics::READ_LOG_SIZE_ABORT_READ, read_log.get_size());
	stats.IncrementStatistics(Statistics::WRITE_LOG_SIZE_ABORT_READ, write_log.get_size());
#endif /* INFLUENCE_DIAGRAM_STATS */
}

inline void wlpdstm::TxMixinv::IncrementWriteAbortStats() {
#ifdef INFLUENCE_DIAGRAM_STATS
	stats.IncrementStatistics(Statistics::READ_LOG_SIZE_ABORT_WRITE, read_log.get_size());
	stats.IncrementStatistics(Statistics::WRITE_LOG_SIZE_ABORT_WRITE, write_log.get_size());
#endif /* INFLUENCE_DIAGRAM_STATS */
}

inline wlpdstm::TxMixinv::WriteLogEntry *wlpdstm::TxMixinv::LockMemoryStripe(WriteLock *write_lock, Word *address) {
#ifdef DETAILED_STATS
	stats.IncrementStatistics(Statistics::WRITES);
#endif /* DETAILED_STATS */
#ifdef ADAPTIVE_LOCKING
	++writes;
#endif /* ADAPTIVE_LOCKING */
	
	// read lock value
	WriteLock lock_value = (WriteLock)atomic_load_no_barrier(write_lock);
	bool locked = is_write_locked(lock_value);
	
	if(locked) {
		WriteLogEntry *log_entry = (WriteLogEntry *)lock_value;
		
		if(LockedByMe(log_entry)) {
			return log_entry;
		}
	}
	
#ifdef DETAILED_STATS
	stats.IncrementStatistics(Statistics::NEW_WRITES);
#endif /* DETAILED_STATS */
#ifdef ADAPTIVE_LOCKING
	++new_writes;
	bool false_conflict_detected = false;
#endif /* ADAPTIVE_LOCKING */
	
	while(true) {		
		if(locked) {
#ifdef ADAPTIVE_LOCKING
			if(!false_conflict_detected) {
				false_conflict_detected = true;
				
				if(IsFalseConflict((WriteLogEntry *)lock_value, address)) {
					stats.IncrementStatistics(Statistics::FALSE_CONFLICTS);
					++false_sharing;
				}
			}
#endif /* ADAPTIVE_LOCKING */
			
			if(ShouldAbortWrite(write_lock)) {
				stats.IncrementStatistics(Statistics::ABORT_WRITE_LOCKED);
				IncrementWriteAbortStats();
#ifdef ADAPTIVE_LOCKING
				++aborts;
#endif /* ADAPTIVE_LOCKING */
				
				//				if(IsFalseConflict((WriteLogEntry *)lock_value, address)) {
				//					stats.IncrementStatistics(Statistics::FALSE_CONFLICTS);
				//					++false_sharing;
				//				}
				
				TxRestart(RESTART_LOCK);
			} else {
				lock_value = (WriteLock)atomic_load_acquire(write_lock);
				locked = is_write_locked(lock_value);
				YieldCPU();
			}
		}
		
		// prepare write log entry
		WriteLogEntry *log_entry = write_log.get_next();
		log_entry->write_lock = write_lock;
		log_entry->ClearWordLogEntries(); // need this here TODO - maybe move this to commit/abort time
		log_entry->owner = this; // this is for CM - TODO: try to move it out of write path
		
		// now try to lock it
		if(atomic_cas_release(write_lock, WRITE_LOCK_CLEAR, log_entry)) {
			// need to check read set validity if this address was read before
			// we skip this read_before() check TODO: maybe do that
			VersionLock *read_lock = map_write_lock_to_read_lock(write_lock);
			VersionLock version = (VersionLock)atomic_load_acquire(read_lock);
			
//			if(get_value(version) > valid_ts) {
			if(ShouldExtend(version)) {
				if(!Extend()) {
					stats.IncrementStatistics(Statistics::ABORT_WRITE_VALIDATE);
					IncrementReadAbortStats();
#ifdef ADAPTIVE_LOCKING
					++aborts;
#endif /* ADAPTIVE_LOCKING */
					TxRestart(RESTART_VALIDATION);
				}
			}
			
			// success
			log_entry->read_lock = read_lock;
			log_entry->old_version = version;
			CmOnAccess();
			
#ifdef ADAPTIVE_LOCKING
			if(larger_lock_extent_table.Set(map_address_to_index(address, larger_lock_extent))) {
				stats.IncrementStatistics(Statistics::LARGER_TABLE_WRITE_HITS);
				++larger_table_hits;
			}
#endif /* ADAPTIVE_LOCKING */
			
			return log_entry;
		}
		
		// someone locked it in the meantime
		// return last element back to the log
		write_log.delete_last();
		
		// read version again
		lock_value = (WriteLock)atomic_load_acquire(write_lock);
		locked = is_write_locked(lock_value);
		YieldCPU();
	}
	
	// this can never happen
	return NULL;
}

inline void wlpdstm::TxMixinv::WriteWord(Word *address, Word value, Word mask) {
#ifdef STACK_PROTECT_ON_WRITE
	if(OnStack((uintptr_t)address)) {
		*address = MaskWord(*address, value, mask);
	} else {
		WriteWordInner(address, value, mask);
	}
#else
	WriteWordInner(address, value, mask);
#endif /* STACK_PROTECT_ON_WRITE */
}

inline void wlpdstm::TxMixinv::WriteWordInner(Word *address, Word value, Word mask) {
	// map address to the lock
	VersionLock *write_lock = map_address_to_write_lock(address);
	
	// try to lock the address - it will abort if address cannot be locked
	WriteLogEntry *log_entry = LockMemoryStripe(write_lock, address);
	
	// insert (address, value) pair into the log
	log_entry->InsertWordLogEntry(address, value, mask);
}

#ifdef SUPPORT_LOCAL_WRITES
inline void wlpdstm::TxMixinv::WriteWordLocal(Word *address, Word val) {
	WriteWordLocalLogEntry *log_entry = write_local_log.get_next();
	
	log_entry->address = address;
	log_entry->value = *address;
	
	// update location
	*address = val;
}
#endif /* SUPPORT_LOCAL_WRITES*/

inline wlpdstm::TxMixinv::WriteWordLogEntry *wlpdstm::TxMixinv::WriteLogEntry::FindWordLogEntry(Word *address) {
	WriteWordLogEntry *curr = head;
	
	while(curr != NULL) {
		if(curr->address == address) {
			break;
		}
		
		curr = curr->next;
	}
	
	return curr;
}

inline void wlpdstm::TxMixinv::WriteLogEntry::InsertWordLogEntry(Word *address, Word value, Word mask) {
	WriteWordLogEntry *entry = FindWordLogEntry(address);

	// new entry
	if(entry == NULL) {
		entry = owner->write_word_log_mem_pool.get_next();
		entry->address = address;
		entry->next = head;
		entry->value = value;
		entry->mask = mask;
		head = entry;
	} else {
		entry->value = MaskWord(entry->value, value, mask);
		entry->mask |= mask;
	}
}

// mask contains ones where value bits are valid
inline Word wlpdstm::TxMixinv::MaskWord(Word old, Word val, Word mask) {
	if(mask == LOG_ENTRY_UNMASKED) {
		return val;
	}
	
	return (old & ~mask) | (val & mask);
}

inline Word wlpdstm::TxMixinv::MaskWord(WriteWordLogEntry *entry) {
	return MaskWord(*entry->address, entry->value, entry->mask);
}

inline void wlpdstm::TxMixinv::WriteLogEntry::ClearWordLogEntries() {
	head = NULL;
}

inline Word wlpdstm::TxMixinv::ReadWord(Word *address) {
	Word ret;
	
#ifdef STACK_PROTECT_ON_READ
	if(OnStack((uintptr_t)address)) {
		ret = *address;
	} else {
		ret = ReadWordInner(address);
	}
#else
	ret = ReadWordInner(address);
#endif /* STACK_PROTECT_ON_READ */
	
	return ret;
}

inline Word wlpdstm::TxMixinv::ReadWordInner(Word *address) {
#ifdef ADAPTIVE_LOCKING
	++reads;
#endif /* ADAPTIVE_LOCKING */
	WriteLock *write_lock = map_address_to_write_lock(address);
	WriteLogEntry *log_entry = (WriteLogEntry *)atomic_load_no_barrier(write_lock);
	
	// if locked by me return quickly
	if(LockedByMe(log_entry)) {
		WriteLogEntry *log_entry = (WriteLogEntry *)*write_lock;
		WriteWordLogEntry *word_log_entry = log_entry->FindWordLogEntry(address);
		
		if(word_log_entry != NULL) {
			// if it was written return from log
			return MaskWord(word_log_entry);
		} else {
			// if it was not written return from memory
			return (Word)atomic_load_no_barrier(address);
		}		
	}

#ifdef ADAPTIVE_LOCKING
	//	if(larger_lock_extent_table.Contains(map_address_to_index(address, larger_lock_extent))) {
	//		stats.IncrementStatistics(Statistics::LARGER_TABLE_READ_HITS);
	//		++larger_table_read_hits;
	//	}
#endif /* ADAPTIVE_LOCKING */

	VersionLock *read_lock = map_write_lock_to_read_lock(write_lock);
	VersionLock version = (VersionLock)atomic_load_acquire(read_lock);
	Word value;

	while(true) {
		if(is_read_locked(version)) {
			version = (VersionLock)atomic_load_acquire(read_lock);
			YieldCPU();
			continue;
		}

		value = (Word)atomic_load_acquire(address);
		VersionLock version_2 = (VersionLock)atomic_load_acquire(read_lock);

		if(version != version_2) {
			version = version_2;
			YieldCPU();
			continue;
		}

		ReadLogEntry *entry = read_log.get_next();
		entry->read_lock = read_lock;
		entry->version = version;		

		if(ShouldExtend(version)) {
			if(!Extend()) {
				// need to restart here
				stats.IncrementStatistics(Statistics::ABORT_READ_VALIDATE);
				IncrementReadAbortStats();
#ifdef ADAPTIVE_LOCKING
				++aborts;
#endif /* ADAPTIVE_LOCKING */
				TxRestart(RESTART_VALIDATION);
			}
		}
		
		break;
	}
	
	return value;
}

// TODO add here a check for a signal from another thread
inline bool wlpdstm::TxMixinv::ShouldExtend(VersionLock version) {
	if(get_value(version) > valid_ts) {
		return true;
	}

#ifdef SIGNALING
	if(IsSignalSet(SIGNAL_PRIVATIZATION_VALIDATE)) {
		ClearSignals();
		return true;
	}
#endif /* SIGNALING */

	return false;
}

// this is validate that occurs during reads and writes not during commit
inline bool wlpdstm::TxMixinv::Validate() {
	ReadLog::iterator iter;
	
	for(iter = read_log.begin();iter.hasNext();iter.next()) {
		ReadLogEntry &entry = *iter;
		VersionLock currentVersion = (VersionLock)atomic_load_no_barrier(entry.read_lock);
		
		if(currentVersion != entry.version) {
			return false;
		}
	}
	
	return true;
}

// validate invoked at commit time
inline bool wlpdstm::TxMixinv::ValidateCommit() {
	ReadLog::iterator iter;
	
	for(iter = read_log.begin();iter.hasNext();iter.next()) {
		ReadLogEntry &entry = *iter;
		VersionLock currentVersion = (VersionLock)atomic_load_no_barrier(entry.read_lock);
		
		if(currentVersion != entry.version) {
			if(is_read_locked(currentVersion)) {
				WriteLock *write_lock = map_read_lock_to_write_lock(entry.read_lock);
				WriteLogEntry *log_entry = (WriteLogEntry *)atomic_load_no_barrier(write_lock);
				
				if(LockedByMe(log_entry)) {
					continue;
				}
			}
			
			return false;
		}
	}
	
	return true;
}

inline bool wlpdstm::TxMixinv::Extend() {
	unsigned ts = commit_ts.readCurrentTsAcquire();
	
	if(Validate()) {
		valid_ts = ts;
#ifdef PRIVATIZATION_QUIESCENCE
		*quiescence_ts = ts;
#elif defined PRIVATIZATION_QUIESCENCE_TREE
		privatization_tree.setNonMinimumTs(ts);
#endif /* PRIVATIZATION_QUIESCENCE */
		
#ifdef TS_EXTEND_STATS
		stats.IncrementStatistics(Statistics::EXTEND_SUCCESS);
#endif /* TS_EXTEND_STATS */
		return true;
	}
	
#ifdef TS_EXTEND_STATS
	stats.IncrementStatistics(Statistics::EXTEND_FAILURE);
#endif /* TS_EXTEND_STATS */

	return false;
}

// this function knows maping from addresses to locks
inline void wlpdstm::TxMixinv::LockMemoryBlock(void *address, size_t size) {
	//
	// Old version
	//
	uintptr_t start = (uintptr_t)address;
	uintptr_t end = start + size;
	VersionLock *old = NULL;
	VersionLock *curr;
	
	for(uintptr_t address = start;address < end;address++) {
		curr = map_address_to_write_lock((Word *)address);
		
		if(curr != old) {
			LockMemoryStripe(curr, (Word *)address);
			old = curr;
		}
	}
}

inline void *wlpdstm::TxMixinv::TxMalloc(size_t size) {
	return mm.TxMalloc(size);
}

inline void wlpdstm::TxMixinv::TxFree(void *ptr, size_t size) {
#ifdef DETAILED_STATS
	stats.IncrementStatistics(Statistics::MEMORY_DEALLOC_COUNT);
	stats.IncrementStatistics(Statistics::MEMORY_DEALLOC_SIZE, size);
#endif /* DETAILED_STATS */
	LockMemoryBlock(ptr, size);
	mm.TxFree(ptr);
}

inline bool wlpdstm::TxMixinv::LockedByMe(WriteLogEntry *log_entry) {
	// this is much faster than going through the log or checking address ranges
	return log_entry != NULL && log_entry->owner == this;
}

inline void wlpdstm::TxMixinv::TxRestart(RestartCause cause) {
#ifdef PRIVATIZATION_QUIESCENCE
	*quiescence_ts = MINIMUM_TS;
#elif defined PRIVATIZATION_QUIESCENCE_TREE
	privatization_tree.setNonMinimumTs(MINIMUM_TS);
#endif /* PRIVATIZATION_QUIESCENCE */

	Rollback();
	atomic_store_release(&tx_status, (Word)TX_RESTARTED);

#ifdef WAIT_ON_SUCC_ABORTS
	if(cause == RESTART_LOCK) {
		if(++succ_aborts > SUCC_ABORTS_MAX) {
			succ_aborts = SUCC_ABORTS_MAX;
		}
		
		if(succ_aborts >= SUCC_ABORTS_THRESHOLD) {
			WaitOnAbort();
		}
	}
#endif /* WAIT_ON_SUCC_ABORTS */	

#ifdef PERFORMANCE_COUNTING
	if(perf_cnt_sampling.should_sample()) {
		perf_cnt.TxEnd();
		stats.IncrementStatistics(Statistics::CYCLES, perf_cnt.GetElapsedCycles());
		stats.IncrementStatistics(Statistics::RETIRED_INSTRUCTIONS, perf_cnt.GetRetiredInstructions());
		stats.IncrementStatistics(Statistics::CACHE_MISSES, perf_cnt.GetCacheMisses());
	}
#endif /* PERFORMANCE_COUNTING */	
	
	RestartJump();
}

inline void wlpdstm::TxMixinv::RestartJump() {
#ifdef WLPDSTM_ICC
//	jmp_to_begin_transaction(&start_buf, LONG_JMP_RESTART_FLAG);
	jmp_to_begin_transaction(&start_buf);
#else
	siglongjmp(start_buf, LONG_JMP_RESTART_FLAG);
#endif /* WLPDSTM_ICC */
}

inline void wlpdstm::TxMixinv::TxAbort() {
#ifdef PRIVATIZATION_QUIESCENCE
	*quiescence_ts = MINIMUM_TS;
#elif defined PRIVATIZATION_QUIESCENCE_TREE
//	privatization_tree.setMinimumTs();
	privatization_tree.setNonMinimumTs(MINIMUM_TS);
#endif /* PRIVATIZATION_QUIESCENCE */

	Rollback();
	atomic_store_release(&tx_status, (Word)TX_ABORTED);

#ifdef PERFORMANCE_COUNTING
	if(perf_cnt_sampling.should_sample()) {
		perf_cnt.TxEnd();
		stats.IncrementStatistics(Statistics::CYCLES, perf_cnt.GetElapsedCycles());
		stats.IncrementStatistics(Statistics::RETIRED_INSTRUCTIONS, perf_cnt.GetRetiredInstructions());
		stats.IncrementStatistics(Statistics::CACHE_MISSES, perf_cnt.GetCacheMisses());
	}
#endif /* PERFORMANCE_COUNTING */	
	
	AbortJump();
}

inline void wlpdstm::TxMixinv::AbortJump() {
#ifdef WLPDSTM_ICC
//	jmp_to_begin_transaction(&start_buf, LONG_JMP_ABORT_FLAG);
	jmp_to_begin_transaction(&start_buf);
#else	
	siglongjmp(start_buf, LONG_JMP_ABORT_FLAG);
#endif /* WLPDSTM_ICC */
}


inline bool wlpdstm::TxMixinv::ShouldAbortWrite(WriteLock *write_lock) {
	if(aborted_externally) {
		return true;
	}
	
	if(greedy_ts == MINIMUM_TS) {
		return true;
	}
	
	WriteLock lock_value = (WriteLock)atomic_load_no_barrier(write_lock);
	
	if(is_write_locked(lock_value)) {
		WriteLogEntry *log_entry = (WriteLogEntry *)lock_value;
		TxMixinv *owner = log_entry->owner;
		
		if(CMStrongerThan(owner)) {
			if(!owner->aborted_externally) {
				owner->aborted_externally = true;
#ifdef DETAILED_STATS
				stats.IncrementStatistics(Statistics::CM_DECIDE);
#endif /* DETAILED_STATS */
			}
			
			return false;
		}
		
		return true;
	}
	
	return false;
}

inline bool wlpdstm::TxMixinv::CMStrongerThan(TxMixinv *other) {
	if(greedy_ts == MINIMUM_TS) {
		return false;
	}
	
	unsigned other_ts = other->greedy_ts;
	return other_ts == MINIMUM_TS || greedy_ts < other_ts;
}

inline void wlpdstm::TxMixinv::CmStartTx() {
#ifdef SIMPLE_GREEDY
	if(succ_aborts == 0) {
		cm_phase = CM_PHASE_GREEDY;
		greedy_ts = cm_ts.getNextTs() + 1;
	}
#else /* two phase greedy */
	cm_phase = CM_PHASE_INITIAL;
	greedy_ts = MINIMUM_TS;
	locations_accessed = 0;
#endif /* SIMPLE_GREEDY */
}

inline void wlpdstm::TxMixinv::CmOnAccess() {
#ifndef SIMPLE_GREEDY
	if(cm_phase == CM_PHASE_INITIAL) {
		if(++locations_accessed > CM_ACCESS_THRESHOLD) {
#ifdef DETAILED_STATS
			stats.IncrementStatistics(Statistics::SWITCH_TO_SECOND_CM_PHASE);
#endif /* DETAILED_STATS */
			cm_phase = CM_PHASE_GREEDY;
			greedy_ts = cm_ts.getNextTs() + 1;
		}
	}
#endif /* SIMPLE_GREEDY */
}

#ifdef WAIT_ON_SUCC_ABORTS
inline void wlpdstm::TxMixinv::WaitOnAbort() {
	uint64_t cycles_to_wait = random.Get() % (succ_aborts * WAIT_CYCLES_MULTIPLICATOR);
	wait_cycles(cycles_to_wait);
#ifdef DETAILED_STATS
	stats.IncrementStatistics(Statistics::WAIT_ON_ABORT);
#endif /* DETAILED_STATS */
}
#endif /* WAIT_ON_SUCC_ABORTS */

#ifdef MM_EPOCH
inline void wlpdstm::TxMixinv::InitLastObservedTs() {
	last_observed_ts = MINIMUM_TS;
	start_count = 0;	
}

inline void wlpdstm::TxMixinv::UpdateLastObservedTs(Word ts) {
	if(++start_count >= UPDATE_LOCAL_LAST_OBSERVED_TS_FREQUENCY) {
		last_observed_ts = ts - 1;
		start_count = 0;
	}
}

inline Word wlpdstm::TxMixinv::UpdateMinimumObservedTs() {
	Word ret = MINIMUM_TS;
	
	if(minimum_observed_ts_lock.try_lock()) {
		if(transactions[0]) {
			unsigned minimum_ts = transactions[0]->last_observed_ts;
			
			for(unsigned i = 1;i < thread_count;i++) {
				if(transactions[i] != NULL) {
					unsigned ts = transactions[i]->last_observed_ts;
					
					if(ts < minimum_ts) {
						minimum_ts = ts;
					}
				}
			}
			
			minimum_observed_ts.val = minimum_ts;
			ret = minimum_ts;
		}
		
		minimum_observed_ts_lock.release();
	} else {
		ret = minimum_observed_ts.val;
	}
	
	return ret;
}

inline Word wlpdstm::TxMixinv::GetMinimumObservedTs() {
	return minimum_observed_ts.val;
}
#endif /* MM_EPOCH */

inline void wlpdstm::TxMixinv::Register() {
	// add itself to the transaction array
	transactions[tid.Get()] = this;

#ifdef PRIVATIZATION_QUIESCENCE
	quiescence_ts = (Word *)quiescence_timestamp_array + tid.Get();
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
	signals = &(signaling_array[tid.Get()].val);
#endif /* SIGNALING */
	
	// update thread count
	Word my_count = tid.Get() + 1;
	Word curr_count = atomic_load_acquire(&thread_count);
	
	while(my_count > curr_count) {
		if(atomic_cas_full(&thread_count, curr_count, my_count)) {
			break;
		}
		
		curr_count = atomic_load_acquire(&thread_count);
	}
}

inline void wlpdstm::TxMixinv::YieldCPU() {
#ifdef USE_PREEMPTIVE_WAITING
	pre_yield();
#endif	
}

#ifdef STACK_PROTECT

#if defined STACK_PROTECT_TANGER_BOUND || defined STACK_PROTECT_ICC_BOUND
inline void wlpdstm::TxMixinv::SetStackHigh(uintptr_t addr) {
	stack_high = addr;
}
#elif defined STACK_PROTECT_WLPDSTM_BOUND
// This is safe to do because start_tx should have its own stack.
inline void wlpdstm::TxMixinv::SetStackHigh() {
	stack_high = read_bp();
}
#endif /* STACK_PROTECT_TANGER_BOUND */

inline bool wlpdstm::TxMixinv::OnStack(uintptr_t addr) {
	return OnStack(addr, read_sp());
}

inline bool wlpdstm::TxMixinv::OnStack(uintptr_t addr, uintptr_t current_sp) {
	if(addr < current_sp) {
		return false;
	}
	
	if(addr > stack_high) {
		return false;
	}
	
	return true;
}
#endif /* STACK_PROTECT */

inline void wlpdstm::TxMixinv::PrintStatistics() {
#ifdef COLLECT_STATS
	FILE *out_file = stdout;
	fprintf(out_file, "\n");
	fprintf(out_file, "STM internal statistics: \n");
	fprintf(out_file, "========================\n");
	
	// collect stats in a single collection
	ThreadStatisticsCollection stat_collection;
	
	for(unsigned i = 0;i < thread_count;i++) {
		// these should all be initialized at this point
		stat_collection.Add(&(transactions[i]->stats));
		fprintf(out_file, "Thread %d: \n", i + 1);
		transactions[i]->stats.Print(out_file, 1);
		fprintf(out_file, "\n");
		
		//		fprintf(out_file, "\tFalse sharing history: [");
		//
		//		for(unsigned j = 0;j < FALSE_SHARING_HISTORY_SIZE;j++) {
		//			fprintf(out_file, "%d ", transactions[i]->false_sharing_history[j]);
		//		}
		//
		//		fprintf(out_file, "]\n");
	}

	fprintf(out_file, "Total stats: \n");
	ThreadStatistics total_stats = stat_collection.MergeAll();
	total_stats.Print(out_file, 1);
	fprintf(out_file, "\nConfiguration:\n");
	fprintf(out_file, "\tLockExtent: %d\n", LOCK_EXTENT);
#endif /* COLLECT_STATS */
}

inline bool wlpdstm::TxMixinv::StartSynchronization() {
	if(!atomic_cas_acquire(&synchronization_in_progress.val, false, true)) {
		Synchronize();
		return false;
	}
	
	for(unsigned i = 0;i < thread_count;i++) {
		//while(atomic_load_acquire(&transactions[i]->executing)) {
		while(transactions[i]->IsExecuting()) {
			// do nothing
		}
	}
	
	return true;
}

inline void wlpdstm::TxMixinv::EndSynchronization() {
	atomic_store_release(&synchronization_in_progress.val, false);
}

inline bool wlpdstm::TxMixinv::Synchronize() {
	bool ret = false;
	
	if(atomic_load_acquire(&synchronization_in_progress.val)) {
		tx_status = TX_IDLE;
		
		while(atomic_load_acquire(&synchronization_in_progress.val)) {
			// do nothing
		}
		
		ret = true;
	}
	
	return ret;
}

inline void wlpdstm::TxMixinv::RestartCommitTS() {
	commit_ts.restart();
	InitializeReadLocks();
}

#ifdef ADAPTIVE_LOCKING
inline bool wlpdstm::TxMixinv::IsFalseConflict(WriteLogEntry *log_entry, Word *address) {
	WriteWordLogEntry *word_log_entry = log_entry->head;
	
	while(word_log_entry != NULL) {
		if(word_log_entry->address == address) {
			return false;
		}
		
		word_log_entry = word_log_entry->next;
	}
	
	return true;
}

inline void wlpdstm::TxMixinv::ClearLocalStatistics() {
	writes = 0;
	reads = 0;
	new_writes = 0;
	larger_table_hits = 0;
	aborts = 0;
	false_sharing = 0;
}

inline void wlpdstm::TxMixinv::ApplyTableConfigurationChange() {
	if(proposed_lock_extent.val == LOCK_EXTENT_CHANGED) {
		if(lock_extent.val + 1 < MAX_LOCK_EXTENT_SIZE) {
			larger_lock_extent = lock_extent.val + 1;
		}
		
		ClearLocalStatistics();
		change_table_configuration_counter = 0;
		old_lock_extent = lock_extent.val;
		proposed_lock_extent.val = NOT_PROPOSING_LOCK_EXTENT;
	}
}

inline void wlpdstm::TxMixinv::ProposeTableChange() {
	if(++change_table_configuration_counter > CHANGE_TABLE_CONFIGURATION_FREQUENCY) {
		ProposeTableChangeInner();
		change_table_configuration_counter = 0;
	}
}

inline void wlpdstm::TxMixinv::ProposeTableChangeInner() {
	return;
	/*
	 bool proposing = proposed_lock_extent.val != NOT_PROPOSING_LOCK_EXTENT;
	 unsigned my_proposition;
	 
	 if(!proposing) {
	 bool enough_data_aborts = EnoughResizeTableDataAborts();
	 bool enough_data_writes = EnoughResizeTableDataWrites();
	 
	 if(!enough_data_aborts && !enough_data_writes) {
	 return;
	 }
	 
	 if(enough_data_aborts && lock_extent.val > MIN_LOCK_EXTENT_SIZE) {
	 if(CalculateFalseAbortRate() > LOCK_EXTENT_DECREASE_THRESHOLD) {
	 my_proposition = lock_extent.val - 1;
	 proposing = true;
	 }
	 }
	 
	 if(enough_data_writes && !proposing && lock_extent.val < MAX_LOCK_EXTENT_SIZE) {
	 if(CalculateLargerTableHitRate() > LOCK_EXTENT_INCREASE_THRESHOLD
	 && GetFalseSharingHistory(larger_lock_extent) < LOCK_EXTENT_DECREASE_THRESHOLD) {
	 my_proposition = larger_lock_extent;
	 proposing = true;
	 }
	 }
	 }
	 
	 // if no need to change, but there is enough statistical data
	 if(!proposing) {
	 // increase larger table lock extent to avoid local minima
	 if(larger_lock_extent < MAX_LOCK_EXTENT_SIZE &&
	 GetFalseSharingHistory(larger_lock_extent + 1) < LOCK_EXTENT_DECREASE_THRESHOLD) {
	 ++larger_lock_extent;
	 ClearLocalStatistics();
	 }
	 
	 return;
	 }
	 
	 // check if enough threads want this change
	 unsigned threads_wanting_change = 0;
	 
	 for(unsigned i = 0;i < thread_count;i++) {
	 if(transactions[i] && transactions[i]->proposed_lock_extent.val == my_proposition) {
	 ++threads_wanting_change;
	 }
	 }
	 
	 if(threads_wanting_change + 1 < (thread_count + 1) / 2) {
	 return;
	 }
	 
	 // try to start synchronization
	 if(!StartSynchronization()) {
	 ApplyTableConfigurationChange();
	 return;
	 }
	 
	 // now perform the actual change
	 stats.IncrementStatistics(Statistics::LOCK_TABLE_RECONFIGURATIONS);
	 lock_extent.val = my_proposition;
	 
	 // inform everyone of the change
	 for(unsigned i = 0;i < thread_count;i++) {
	 if(transactions[i]) {
	 transactions[i]->proposed_lock_extent.val = LOCK_EXTENT_CHANGED;
	 }
	 }	
	 
	 EndSynchronization();
	 
	 // accept the change myself
	 ApplyTableConfigurationChange();
	 */
}

inline unsigned wlpdstm::TxMixinv::CalculateFalseAbortRate() {
	return aborts ? false_sharing * 100 / aborts : 0;
}

inline unsigned wlpdstm::TxMixinv::CalculateLargerTableHitRate() {
	return larger_table_hits * 100 / writes;
}

//inline unsigned wlpdstm::TxMixinv::GetFalseSharingHistoryIndex(unsigned le) {
//	return le - MIN_LOCK_EXTENT_SIZE;
//}

//inline bool wlpdstm::TxMixinv::EnoughResizeTableDataAborts() {
//	return aborts > SIGNIFICANT_STAT_COUNT_ABORTS;// && writes > SIGNIFICANT_STAT_COUNT_WRITES;
//	return writes > SIGNIFICANT_STAT_COUNT_WRITES;
//}

inline bool wlpdstm::TxMixinv::EnoughResizeTableDataWrites() {
	return writes > SIGNIFICANT_STAT_COUNT_WRITES;
}

//inline unsigned wlpdstm::TxMixinv::GetFalseSharingHistory(unsigned le) {
//	return false_sharing_history[GetFalseSharingHistoryIndex(le)];
//}
#endif /* ADAPTIVE_LOCKING */

#ifdef PRIVATIZATION_QUIESCENCE

inline void wlpdstm::TxMixinv::PrivatizationQuiescenceWait(Word ts) {
	unsigned tc = thread_count;

	for(unsigned i = 0;i < tc;i++) {
#ifdef SIGNALING
		if(quiescence_timestamp_array[i] != MINIMUM_TS && quiescence_timestamp_array[i] < ts) { 
			SetSignal(i, SIGNAL_PRIVATIZATION_VALIDATE);
		} else {
			continue;
		}
#endif /* SIGNALING */

		while(quiescence_timestamp_array[i] != MINIMUM_TS && quiescence_timestamp_array[i] < ts) {
			// do nothing
		}
	}
}

#endif /* PRIVATIZATION_QUIESCENCE */		

#ifdef SIGNALING
inline void wlpdstm::TxMixinv::SetSignal(unsigned tid, TxSignals signal) {
	signaling_array[tid].val |= (Word)signal;
}
	
inline void wlpdstm::TxMixinv::ClearSignals() {
	*signals = SIGNAL_EMPTY;
}

inline bool wlpdstm::TxMixinv::IsSignalSet(TxSignals signal) {
	return (*signals) & signal;
}

#endif /* SIGNALING */
	


