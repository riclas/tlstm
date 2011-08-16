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
#include <signal.h>
#include <time.h>

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
#define NONE -1

namespace wlpdstm {
	
	typedef VersionLock WriteLock;
	
	class TxMixinv : public CacheAlignedAlloc {
		
		//////////////////////////////////
		// log related structures start //
		//////////////////////////////////
		
		struct ReadLogEntry {
			VersionLock *read_lock;
			VersionLock version;
			unsigned address_index;
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
			unsigned ptid;
			unsigned address_index;
			
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
		
		struct TransactionState {
			Word valid_ts;
			bool read_only;
			int first_serial;
		};

		struct ProgramThread {
			volatile int last_completed_task, last_commited_task, next_task;
			ReadLog *read_log;
			WriteLog *write_log;
			ReadLog *fw_read_log;
			WriteLogEntry ***store_vector;
			Word **load_vector;
			Word *aborted;
			//TransactionState **tx_state;
		};

		////////////////////////////////
		///// tlstm structures end /////
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

		//static void sigsegv_handler(int sig);

		static void GlobalInit(int nb_tasks);
		
		static void InitializeReadLocks();
		
		static void InitializeWriteLocks();

		static void InitializeProgramThreads();

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
		void TxStart(int lex_tx_id = NO_LEXICAL_TX, bool start_tx = true, bool commit = true, unsigned thread_id = 0, unsigned task_id=0);

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
		void Rollback(unsigned s);

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
		
		//unsigned map_address_to_index(Word *address, unsigned le);
		
		unsigned map_address_to_index(Word *address);
		
		//VersionLock *map_address_to_read_lock(Word *address);
		
		WriteLock *map_address_to_write_lock(Word *address);
		
		WriteLock *map_read_lock_to_write_lock(VersionLock *lock_address);
		
		VersionLock *map_write_lock_to_read_lock(WriteLock *lock_address);

		WriteLock *map_index_to_write_lock(unsigned index);
		
		void LockMemoryStripe(WriteLock *write_lock, Word *address, Word value, Word mask);
		
		bool Validate();
		
		bool ValidateCommit(unsigned serial);
		
		void ReleaseReadLocks();
		
		bool Extend();

		bool ShouldExtend(VersionLock version);
		
		bool LockedByMe(WriteLogEntry *log_entry);
		
		Word IncrementCommitTs();
		
		//////////////////////////
		//Task related functions//
		//////////////////////////

		void AbortEarlySpecReads(unsigned address_index);

		void add_to_fw_read_log(unsigned address_index, VersionLock* read_lock, VersionLock version);

		bool ActiveWriterAfterThisTask(unsigned address_index);

		int PreviousActiveWriter(unsigned address_index);

		void SetLoadVector(unsigned address_index, unsigned s_index, Word value);

		void SetStoreVector(unsigned address_index, unsigned s_index, WriteLogEntry *entry);

		void SigsegvHandler(int sig);

		///////////////////////////
		// contention management //
		///////////////////////////
		
		bool ShouldAbortWrite(WriteLock *write_lock, unsigned address_index);
		
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
		
		//TLSTM
		TransactionState tx_state;

		unsigned serial_index, prog_thread_id;
		int serial;
		bool try_commit;
		bool speculative_task;

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

		//static bool sigsegv_caught;

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
		
		//TLSTM
		static ProgramThread prog_thread[MAX_THREADS];

		static int specdepth;

#ifdef PRIVATIZATION_QUIESCENCE
		static volatile Word quiescence_timestamp_array[MAX_THREADS];
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
		static volatile PaddedWord signaling_array[MAX_THREADS];
#endif /* SIGNALING */		
		
		void IncrementReadAbortStats();
		void IncrementWriteAbortStats();
		
	} __attribute__ ((aligned(CACHE_LINE_SIZE_BYTES)));
}

#define LOCK_EXTENT DEFAULT_LOCK_EXTENT_SIZE

//////////////////////////
// initialization start //
//////////////////////////

/*inline void wlpdstm::TxMixinv::sigsegv_handler(int sig){
	wlpdstm::TxMixinv::sigsegv_caught = true;
}*/

inline void wlpdstm::TxMixinv::GlobalInit(int nb_tasks) {

	//sigsegv_caught = false;

	//signal(SIGSEGV, wlpdstm::TxMixinv::sigsegv_handler);

	specdepth = nb_tasks;

	InitializeProgramThreads();

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

inline void wlpdstm::TxMixinv::InitializeProgramThreads() {

	for(unsigned i=0; i < MAX_THREADS / specdepth; i++){
		prog_thread[i].read_log = new ReadLog[specdepth];
		prog_thread[i].write_log = new WriteLog[specdepth];
		prog_thread[i].fw_read_log = new ReadLog[specdepth];
		prog_thread[i].store_vector = (WriteLogEntry***)malloc(sizeof(WriteLogEntry**) * specdepth);
		prog_thread[i].load_vector = (Word**)malloc(sizeof(Word*) * specdepth);

		prog_thread[i].aborted = (Word*)calloc(specdepth, sizeof(Word));
		//prog_thread[i].tx_state = (TransactionState**)malloc(sizeof(TransactionState*) * specdepth);

		prog_thread[i].last_commited_task = -1;
		prog_thread[i].last_completed_task = -1;
		prog_thread[i].next_task = 0;

		for(int j=0; j < specdepth; j++){
			prog_thread[i].store_vector[j] = (WriteLogEntry**)calloc(VERSION_LOCK_TABLE_SIZE, sizeof(WriteLogEntry*));
			prog_thread[i].load_vector[j] = (Word*)calloc(VERSION_LOCK_TABLE_SIZE, sizeof(Word));
			//prog_thread[i].load_vector[j] = (unsigned*)malloc(VERSION_LOCK_TABLE_SIZE * sizeof(unsigned));
			//prog_thread[i].aborted[j] = 0;
			//for(int k=0; k < VERSION_LOCK_TABLE_SIZE; k++){
				//prog_thread[i].load_vector[j][k] = 0;
				//prog_thread[i].store_vector[k][j] = NULL;
			//}
			//prog_thread[i].stv_count[j] = 0;
			//prog_thread[i].ldv_count[j] = 0;
		}
		//memset(prog_thread[i].load_vector, 0, VERSION_LOCK_TABLE_SIZE * specdepth);
		//memset(prog_thread[i].store_vector, NULL, VERSION_LOCK_TABLE_SIZE * specdepth);
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

	rolled_back = false;

	serial_index = 0;

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

//SWISSTM
/*
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
*/

inline unsigned wlpdstm::TxMixinv::map_address_to_index(Word *address) {
	return (((uintptr_t)address >> (LOCK_EXTENT)) & (VERSION_LOCK_TABLE_SIZE - 1));
}

inline wlpdstm::WriteLock *wlpdstm::TxMixinv::map_index_to_write_lock(unsigned index) {
	return version_lock_table + (index << 1) + 1;
}

inline wlpdstm::WriteLock *wlpdstm::TxMixinv::map_address_to_write_lock(Word *address) {
	return map_index_to_write_lock(map_address_to_index(address));
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

inline void wlpdstm::TxMixinv::TxStart(int lex_tx_id, bool start_tx, bool commit, unsigned thread_id, unsigned task_id) {
	prog_thread_id = thread_id;

	//if it is not an aborted task we give it a new serial
	if(!rolled_back){
		//while(prog_thread[prog_thread_id].next_task - prog_thread[prog_thread_id].last_commited_task > specdepth);

		serial = task_id;

		if(serial+1 > prog_thread[prog_thread_id].next_task)
			prog_thread[prog_thread_id].next_task = serial+1;

		//serial = prog_thread[prog_thread_id].next_task++;
		serial_index = serial & (specdepth - 1);
		//printf("txstart %d %d\n",serial, thread_id);

		//create new transaction state to be shared by all tasks of a transaction
		if(start_tx){
			//tx_state = (TransactionState*)malloc(sizeof(TransactionState));
			tx_state.first_serial = serial;
			tx_state.read_only = true;

			//prog_thread[prog_thread_id].last_tx_state = new_tx_state;
		}

		//this doesn't work because there can be several tasks starting transactions in one program thread
		//tx_state = prog_thread[prog_thread_id].last_tx_state;
	}

#ifdef PERFORMANCE_COUNTING
	perf_cnt_sampling.tx_start();

	if(perf_cnt_sampling.should_sample()) {
		perf_cnt.TxStart();
	}
#endif /* PERFORMANCE_COUNTING */	

	atomic_store_full(&tx_status, (Word)TX_EXECUTING);
	
	if(Synchronize()) {
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


	tx_state.valid_ts = valid_ts;

	//clean load vector
	for(ReadLog::iterator iter = prog_thread[prog_thread_id].read_log[serial_index].begin();iter.hasNext();iter.next()) {
		ReadLogEntry &entry = *iter;

		SetLoadVector(entry.address_index, serial_index, 0);
	}

	//cleanup readlogs
	prog_thread[prog_thread_id].read_log[serial_index].clear();
	prog_thread[prog_thread_id].fw_read_log[serial_index].clear();

	write_word_log_mem_pool.clear();

	try_commit = commit;

	prog_thread[prog_thread_id].aborted[serial_index] = 0;

	speculative_task = true;

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
//int lct = prog_thread[prog_thread_id].last_completed_task;
//printf("lct %d commit %d\n",lct, serial);
	while(prog_thread[prog_thread_id].last_completed_task != serial-1);
	/*	if(lct != prog_thread[prog_thread_id].last_completed_task){
			printf("lct %d serial %d\n",prog_thread[prog_thread_id].last_completed_task,serial);

		}
		lct = prog_thread[prog_thread_id].last_completed_task;
	}*/

	RestartCause ret = TxTryCommit();

	/*for(int s = tx_state.first_serial; s <= serial; s++){
			for(WriteLog::iterator iter = prog_thread[prog_thread_id].write_log[s & (specdepth - 1)].begin();iter.hasNext();iter.next()) {
				WriteLogEntry &entry = *iter;
				if(*(entry.read_lock) == READ_LOCK_SET)
					printf("%p locked! ret=%d\n", entry.read_lock, ret);
			}
	}*/

	if(ret) {
		//printf("956\n");
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
	//printf("trycommit %d\n",serial);
	//if this task was told to abort in the meantime we abort and rollback
	if(atomic_load_acquire(&prog_thread[prog_thread_id].aborted[serial_index]) == 1){
		//printf("987\n");
		Rollback();
		return RESTART_EXTERNAL;
	}

	Word ts = valid_ts;

	//TLSTM
	//Check valid_ts of task, validate or rollback
	//Only needed when dividing transactions in tasks
	/*if(tx_state.valid_ts > ts){
		if(!ValidateCommit(serial)){
			//printf("1066\n");
			Rollback(serial);

			return RESTART_VALIDATION;
		}
	} else {
		if(tx_state.valid_ts < ts){
			for(int s = tx_state.first_serial; s < serial; s++){
				if(!ValidateCommit(s)){
					//printf("1075\n");
					Rollback(s);

					return RESTART_VALIDATION;
				}
			}
			tx_state.valid_ts = ts;
		}
	}
*/
	//if the transaction is read only until now, we check if it has become a writer with this task
	if(tx_state.read_only == true)
		tx_state.read_only = prog_thread[prog_thread_id].write_log[serial_index].empty();

	if(try_commit){
		//commit a write transaction
		if(!tx_state.read_only) {
			// first lock all read locks
			for(int s = tx_state.first_serial; s <= serial; s++){
				for(WriteLog::iterator iter = prog_thread[prog_thread_id].write_log[s & (specdepth - 1)].begin();iter.hasNext();iter.next()) {
					WriteLogEntry &entry = *iter;
					*(entry.read_lock) = READ_LOCK_SET;
				}
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
				Rollback(serial);

				if(StartSynchronization()) {
					RestartCommitTS();
					EndSynchronization();
					stats.IncrementStatistics(Statistics::CLOCK_OVERFLOWS);
				}

				return RESTART_CLOCK_OVERFLOW;
			}

			// if there is no validation in GV4, these the read set of one transaction could
			// overlap with the write set of another and this would pass unnoticed
	/*#ifdef COMMIT_TS_INC
			if(ts != valid_ts + 1 && !ValidateCommit(serial)) {
	#elif defined COMMIT_TS_GV4
			if(!ValidateCommit(serial)) {
	#endif*/ /* commit_ts */
	/*			ReleaseReadLocks();
				stats.IncrementStatistics(Statistics::ABORT_COMMIT_VALIDATE);
				IncrementReadAbortStats();

				Rollback(serial);
				return RESTART_VALIDATION;
			}
	*/
			if(ts > tx_state.valid_ts+1){
				for(int s = tx_state.first_serial; s <= serial; s++){
					if(!ValidateCommit(s)){
						ReleaseReadLocks();
						stats.IncrementStatistics(Statistics::ABORT_COMMIT_VALIDATE);
						IncrementReadAbortStats();

						//printf("1143\n");
						Rollback(tx_state.first_serial);

						return RESTART_VALIDATION;
					}
				}
			}

			VersionLock commitVersion = get_version_lock(ts);

			// now update all written values
			for(int s = tx_state.first_serial; s <= serial; s++){
				//printf("wlog size %d \n", prog_thread[prog_thread_id].write_log[s % specdepth].get_size());
				for(WriteLog::iterator iter = prog_thread[prog_thread_id].write_log[s & (specdepth - 1)].begin();iter.hasNext();iter.next()) {
					WriteLogEntry &entry = *iter;

					// now update actual values
					WriteWordLogEntry *word_log_entry = entry.head;

					while(word_log_entry != NULL) {
						*word_log_entry->address = MaskWord(word_log_entry);
						word_log_entry = word_log_entry->next;
					}

					// release locks
					atomic_store_release(entry.read_lock, commitVersion);

					atomic_store_release(entry.write_lock, WRITE_LOCK_COMMIT);

					//if there is a future active writer for this address we leave its writelock locked
					if(!ActiveWriterAfterThisTask(entry.address_index)){
						atomic_store_release(entry.write_lock, WRITE_LOCK_CLEAR);
					} else {
						atomic_store_release(entry.write_lock, prog_thread_id);
					}

					//add this entry to the forward read logs
					add_to_fw_read_log(entry.address_index, entry.read_lock, commitVersion);

					//clean the store vector entry
					SetStoreVector(entry.address_index, s & (specdepth - 1), NULL);
				}
				//clean write log of this task
				prog_thread[prog_thread_id].write_log[s & (specdepth - 1)].clear();
			}
		} else {
			stats.IncrementStatistics(Statistics::COMMIT_READ_ONLY);
		}
		//free(tx_state);
		prog_thread[prog_thread_id].last_commited_task = serial;
	}

	prog_thread[prog_thread_id].last_completed_task = serial;

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
	stats.IncrementStatistics(Statistics::prog_thread[prog_thread_id].read_log[serial_index]_SIZE, prog_thread[prog_thread_id].read_log[serial_index].get_size());
	stats.IncrementStatistics(Statistics::prog_thread[prog_thread_id].write_log[serial_index]_SIZE, prog_thread[prog_thread_id].write_log[serial_index].get_size());
#endif /* INFLUENCE_DIAGRAM_STATS */

#ifdef SUPPORT_LOCAL_WRITES
	write_local_log.clear();
#endif /* SUPPORT_LOCAL_WRITES */
	
	// commit mem
	mm.TxCommit<TxMixinv>(ts);
	
	stats.IncrementStatistics(Statistics::COMMIT);
	
	succ_aborts = 0;

//printf("commited %d\n",serial);
	return NO_RESTART;
}

// updates only write locks
inline void wlpdstm::TxMixinv::ReleaseReadLocks() {
	for(int s = tx_state.first_serial; s <= serial; s++){
		for(WriteLog::iterator iter = prog_thread[prog_thread_id].write_log[s & (specdepth - 1)].begin();iter.hasNext();iter.next()) {
			WriteLogEntry &entry = *iter;
			*(entry.read_lock) = entry.old_version;
		}
	}
}

inline void wlpdstm::TxMixinv::Rollback(unsigned start_serial) {
	for(int s = start_serial; s < prog_thread[prog_thread_id].next_task; s++){
		//printf("rollback %d\n",s);
		//prog_thread[prog_thread_id].aborted[s & (specdepth - 1)] = true;
		atomic_store_release(&prog_thread[prog_thread_id].aborted[s % specdepth], 1);
		//atomic_cas_full(&prog_thread[prog_thread_id].aborted[s & (specdepth - 1)], false, true);

	}
//printf("rollback %d s-index %d\n", serial, serial_index);
	Rollback();
}

inline void wlpdstm::TxMixinv::Rollback() {
	if(rolled_back) {
		return;
	}
	//printf("rollback %d\n",serial);
	rolled_back = true;

#ifdef SUPPORT_LOCAL_WRITES
	// rollback local writes
	for(WriteLocalLog::iterator iter = write_local_log.begin();iter.hasNext();iter.next()) {
		WriteWordLocalLogEntry &entry = *iter;
		*entry.address = entry.value;
	}
#endif /* SUPPORT_LOCAL_WRITES */

	// drop locks
	for(WriteLog::iterator iter = prog_thread[prog_thread_id].write_log[serial_index].begin();iter.hasNext();iter.next()) {
		WriteLogEntry &entry = *iter;

		*entry.write_lock = WRITE_LOCK_CLEAR;

		SetStoreVector(entry.address_index, serial_index, NULL);
	}

	prog_thread[prog_thread_id].write_log[serial_index].clear();

#ifdef SUPPORT_LOCAL_WRITES
	write_local_log.clear();
#endif /* SUPPORT_LOCAL_WRITES */
	
	YieldCPU();
	
	mm.TxAbort();
	
	stats.IncrementStatistics(Statistics::ABORT);
}

inline void wlpdstm::TxMixinv::IncrementReadAbortStats() {
#ifdef INFLUENCE_DIAGRAM_STATS
	stats.IncrementStatistics(Statistics::prog_thread[prog_thread_id].read_log[serial_index]_SIZE_ABORT_READ, prog_thread[prog_thread_id].read_log[serial_index].get_size());
	stats.IncrementStatistics(Statistics::prog_thread[prog_thread_id].write_log[serial_index]_SIZE_ABORT_READ, prog_thread[prog_thread_id].write_log[serial_index].get_size());
#endif /* INFLUENCE_DIAGRAM_STATS */
}

inline void wlpdstm::TxMixinv::IncrementWriteAbortStats() {
#ifdef INFLUENCE_DIAGRAM_STATS
	stats.IncrementStatistics(Statistics::prog_thread[prog_thread_id].read_log[serial_index]_SIZE_ABORT_WRITE, prog_thread[prog_thread_id].read_log[serial_index].get_size());
	stats.IncrementStatistics(Statistics::prog_thread[prog_thread_id].write_log[serial_index]_SIZE_ABORT_WRITE, prog_thread[prog_thread_id].write_log[serial_index].get_size());
#endif /* INFLUENCE_DIAGRAM_STATS */
}

inline void wlpdstm::TxMixinv::LockMemoryStripe(WriteLock *write_lock, Word *address, Word value, Word mask) {
#ifdef DETAILED_STATS
	stats.IncrementStatistics(Statistics::WRITES);
#endif /* DETAILED_STATS */
	
	unsigned address_index = map_address_to_index(address);

	//TLSTM
	WriteLogEntry* log_entry = prog_thread[prog_thread_id].store_vector[serial_index][address_index];

	//If locked by my program thread and furthermore by my task, return quickly
	if(log_entry != NULL) {

		// insert (address, value) pair into the log
		log_entry->InsertWordLogEntry(address, value, mask);
		return;
	}
	
#ifdef DETAILED_STATS
	stats.IncrementStatistics(Statistics::NEW_WRITES);
#endif /* DETAILED_STATS */

	// prepare write log entry
	log_entry = prog_thread[prog_thread_id].write_log[serial_index].get_next();
	log_entry->write_lock = write_lock;
	log_entry->ClearWordLogEntries(); // need this here TODO - maybe move this to commit/abort time
	log_entry->owner = this; // this is for CM - TODO: try to move it out of write path
	log_entry->address_index = address_index;

	// insert (address, value) pair into the log
	log_entry->InsertWordLogEntry(address, value, mask);

	SetStoreVector(address_index, serial_index, log_entry);

	// read lock value
	WriteLock lock_value = (WriteLock)atomic_load_no_barrier(write_lock);

	while(true) {
		if(lock_value == prog_thread_id || atomic_cas_release(write_lock, WRITE_LOCK_CLEAR, prog_thread_id)) {
			break;

		} else if(lock_value == WRITE_LOCK_COMMIT){
			// read version again
			lock_value = (WriteLock)atomic_load_acquire(write_lock);
			YieldCPU();
			continue;
			
		} else if(lock_value != prog_thread_id && ShouldAbortWrite(write_lock, address_index)) {
				stats.IncrementStatistics(Statistics::ABORT_WRITE_LOCKED);
				IncrementWriteAbortStats();
				
				//				if(IsFalseConflict((WriteLogEntry *)lock_value, address)) {
				//					stats.IncrementStatistics(Statistics::FALSE_CONFLICTS);
				//					++false_sharing;
				//				}
				//printf("1201\n");
				Rollback(serial);
				TxRestart(RESTART_LOCK);
		} else {
			lock_value = (WriteLock)atomic_load_acquire(write_lock);
			YieldCPU();
		}
	}

	// need to check read set validity if this address was read before
	// we skip this read_before() check TODO: maybe do that
	VersionLock *read_lock = map_write_lock_to_read_lock(write_lock);
	VersionLock version = (VersionLock)atomic_load_acquire(read_lock);

	while(is_read_locked(version)) {
		version = (VersionLock)atomic_load_acquire(read_lock);
		YieldCPU();
	}

	//			if(get_value(version) > valid_ts) {
	if(ShouldExtend(version)) {
		if(!Extend()) {
			stats.IncrementStatistics(Statistics::ABORT_WRITE_VALIDATE);
			IncrementReadAbortStats();
			//printf("1259\n");
			Rollback(serial);

			TxRestart(RESTART_VALIDATION);
		}
	}
	
	log_entry->read_lock = read_lock;
	log_entry->old_version = version;
	CmOnAccess();

	//tell the future tasks that have read from this address to abort because the value was updated
	AbortEarlySpecReads(address_index);
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
	LockMemoryStripe(write_lock, address, value, mask);
	
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

	unsigned address_index = map_address_to_index(address);

	if(serial > prog_thread[prog_thread_id].last_completed_task+1){
		if(address < (Word *)0xffff){
			//printf("inconsistent read at %p on task %d\n", address, serial);
			//if it's a speculative task we have an inconsistent read
			stats.IncrementStatistics(Statistics::ABORT_READ_VALIDATE);
			IncrementReadAbortStats();
			//printf("1398\n");
			Rollback(serial);
			TxRestart(RESTART_VALIDATION);
		} else {
			SetLoadVector(address_index, serial_index, 1);
		}
	} else if(speculative_task){
		if(atomic_load_acquire(&prog_thread[prog_thread_id].aborted[serial_index]) == 1){
			//printf("987\n");
			Rollback(serial);
			TxRestart(RESTART_VALIDATION);
		}
		speculative_task = false;
	}

	WriteLock *write_lock = map_index_to_write_lock(address_index);

/*
	WriteLogEntry* log_entry = prog_thread[prog_thread_id].store_vector[serial_index][address_index];

	// if locked by me return quickly
	if(LockedByMe(log_entry)) {
		WriteWordLogEntry *word_log_entry = log_entry->FindWordLogEntry(address);
		
		if(word_log_entry != NULL) {
			// if it was written return from log
			return MaskWord(word_log_entry);
		} else {
			// if it was not written return from memory
			return (Word)atomic_load_no_barrier(address);
		}		
	}
*/

	int writer = PreviousActiveWriter(address_index);

	VersionLock *read_lock = map_write_lock_to_read_lock(write_lock);
	VersionLock version = (VersionLock)atomic_load_acquire(read_lock);
	Word value;

	while(true) {
		if(is_read_locked(version)) {
			version = (VersionLock)atomic_load_acquire(read_lock);
			writer = PreviousActiveWriter(address_index);
			YieldCPU();
			continue;
		}

		if(writer != NONE){
			//We find the value in the previous writer’s store vector
			WriteLogEntry* log_entry = (WriteLogEntry*)atomic_load_acquire(&prog_thread[prog_thread_id].store_vector[writer & (specdepth - 1)][address_index]);

			//the writer may have rolledback in the meantime
			if(log_entry == NULL) {
				value = (Word)atomic_load_no_barrier(address);
			} else {
				WriteWordLogEntry *word_log_entry = log_entry->FindWordLogEntry(address);

				if(word_log_entry != NULL) {
					// if it was written return from log
					value = MaskWord(word_log_entry);
				} else {
					// if it was not written return from memory
					value = (Word)atomic_load_no_barrier(address);
				}
			}
		} else {
			// if it was not written return from memory
			value = (Word)atomic_load_no_barrier(address);
		}

		VersionLock version_2 = (VersionLock)atomic_load_acquire(read_lock);

		if(version != version_2) {
			version = version_2;
			writer = PreviousActiveWriter(address_index);
			YieldCPU();
			continue;
		}

		//add entry to read log
		ReadLogEntry *entry = prog_thread[prog_thread_id].read_log[serial_index].get_next();
		entry->read_lock = read_lock;
		entry->version = version;
		entry->address_index = address_index;

		if(ShouldExtend(version)) {
			if(!Extend()) {
				// need to restart here
				stats.IncrementStatistics(Statistics::ABORT_READ_VALIDATE);
				IncrementReadAbortStats();
				//printf("1487\n");
				Rollback(serial);
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
	
	for(iter = prog_thread[prog_thread_id].read_log[serial_index].begin();iter.hasNext();iter.next()) {
		ReadLogEntry &entry = *iter;
		VersionLock currentVersion = (VersionLock)atomic_load_no_barrier(entry.read_lock);
		
		if(currentVersion != entry.version) {
			return false;
		}
	}
	
	return true;
}

// validate invoked at commit time
inline bool wlpdstm::TxMixinv::ValidateCommit(unsigned s) {
	ReadLog::iterator iter;
	
	for(iter = prog_thread[prog_thread_id].read_log[s & (specdepth - 1)].begin();iter.hasNext();iter.next()) {
		ReadLogEntry &entry = *iter;
		VersionLock currentVersion = (VersionLock)atomic_load_no_barrier(entry.read_lock);
		
		if(currentVersion != entry.version) {
			if(is_read_locked(currentVersion)) {
				WriteLock *write_lock = map_read_lock_to_write_lock(entry.read_lock);
				WriteLock lock_value = (WriteLock)atomic_load_no_barrier(write_lock);
				
				if(lock_value == prog_thread_id) {
					continue;
				}
			}
			
			return false;
		}
	}
	
	for(iter = prog_thread[prog_thread_id].fw_read_log[s & (specdepth - 1)].begin();iter.hasNext();iter.next()) {
		ReadLogEntry &entry = *iter;
		VersionLock currentVersion = (VersionLock)atomic_load_no_barrier(entry.read_lock);

		if(currentVersion != entry.version) {
			if(is_read_locked(currentVersion)) {
				WriteLock *write_lock = map_read_lock_to_write_lock(entry.read_lock);
				WriteLock lock_value = (WriteLock)atomic_load_no_barrier(write_lock);

				if(lock_value == prog_thread_id) {
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
			LockMemoryStripe(curr, (Word *)address, 0, LOG_ENTRY_UNMASKED);
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


inline bool wlpdstm::TxMixinv::ShouldAbortWrite(WriteLock *write_lock, unsigned address_index) {
	if(aborted_externally) {
		return true;
	}
	
	if(greedy_ts == MINIMUM_TS) {
		return true;
	}
	
	WriteLock lock_value = (WriteLock)atomic_load_no_barrier(write_lock);
	
	if(lock_value != WRITE_LOCK_CLEAR && lock_value != WRITE_LOCK_COMMIT) {
		//TLSTM
		for(int s = 0; s < specdepth; s++){
			WriteLogEntry* log_entry = (WriteLogEntry*)atomic_load_acquire(&prog_thread[lock_value].store_vector[s][address_index]);

			if(log_entry != NULL){
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
			}
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

inline void wlpdstm::TxMixinv::AbortEarlySpecReads(unsigned address_index){
	int future_writer = prog_thread[prog_thread_id].next_task-1;

	for(int i = serial+1; i < prog_thread[prog_thread_id].next_task-1; i++){
		if(atomic_load_acquire(&prog_thread[prog_thread_id].store_vector[i & (specdepth - 1)][address_index])){
			future_writer = i;
			break;
		}
	}

	for(int i = serial+1; i <= future_writer; i++){
		if(atomic_load_acquire(&prog_thread[prog_thread_id].load_vector[i & (specdepth - 1)][address_index]) == 1){
			//printf("found early spec readers\n");
			for(int j=i; j < prog_thread[prog_thread_id].next_task; j++){
				stats.IncrementStatistics(Statistics::ABORT_SPEC_READERS);
				//printf("abortearlyspecread %d index %d value %d\n", j, j%specdepth, prog_thread[prog_thread_id].aborted[j % specdepth]);
				//prog_thread[prog_thread_id].aborted[j & (specdepth - 1)] = true;
				atomic_store_release(&prog_thread[prog_thread_id].aborted[j % specdepth], 1);
				//atomic_cas_full(&prog_thread[prog_thread_id].aborted[j & (specdepth - 1)], false, true);
			}
			return;
		}
	}
}

inline void wlpdstm::TxMixinv::add_to_fw_read_log(unsigned address_index, VersionLock* read_lock, VersionLock version) {
	int future_writer = prog_thread[prog_thread_id].next_task-1;

	//we search the future writer of this address
	for(int i = serial+1; i < prog_thread[prog_thread_id].next_task-1; i++){
		if(atomic_load_acquire(&prog_thread[prog_thread_id].store_vector[i & (specdepth - 1)][address_index])){
			future_writer = i;
			break;
		}
	}

	//and add a forward read log entry to all tasks from the present to the future writer
	for(int i = serial+1; i <= future_writer; i++){
		if(atomic_load_acquire(&prog_thread[prog_thread_id].aborted[i & (specdepth - 1)]) == 0 && atomic_load_acquire(&prog_thread[prog_thread_id].load_vector[i & (specdepth - 1)][address_index]) == 1){
			//printf("add to fw read log serial %d i %d read_lock %p version %d\n",serial,i, read_lock, version);
			ReadLogEntry *read_entry = prog_thread[prog_thread_id].fw_read_log[i & (specdepth - 1)].get_next();
			read_entry->read_lock = read_lock;
			read_entry->version = version;
		}
	}
}

inline bool wlpdstm::TxMixinv::ActiveWriterAfterThisTask(unsigned address_index){
	for(int i = serial+1; i < prog_thread[prog_thread_id].next_task; i++){
		if(atomic_load_acquire(&prog_thread[prog_thread_id].store_vector[i & (specdepth - 1)][address_index])){
			return true;
		}
	}
	return false;
}

inline int wlpdstm::TxMixinv::PreviousActiveWriter(unsigned address_index){
	for(int i = serial; i > prog_thread[prog_thread_id].last_commited_task; i--){
		if(atomic_load_acquire(&prog_thread[prog_thread_id].store_vector[i & (specdepth - 1)][address_index])){
			return i;
		}
	}
	return NONE;
}

inline void wlpdstm::TxMixinv::SetLoadVector(unsigned address_index, unsigned s_index, Word value){

	//printf("slv serial %d writelockindex %d value %d\n", serial, address_index, value);
	//if(value) {
		//prog_thread[prog_thread_id].load_vector[s_index][address_index] = serial;
	//} else{
		//if(prog_thread[prog_thread_id].load_vector[s_index][address_index] != value)
			//prog_thread[prog_thread_id].load_vector[s_index][address_index] = value;
	//}
	//if(value == true && prog_thread[prog_thread_id].load_vector[s_index][address_index] == 0) {
		atomic_store_release(&prog_thread[prog_thread_id].load_vector[s_index][address_index], value);
		//prog_thread[prog_thread_id].ldv_count[s_index]++;
	//} else if(prog_thread[prog_thread_id].load_vector[s_index][address_index] != 0){
		//atomic_store_release(&prog_thread[prog_thread_id].load_vector[s_index][address_index], 0);
		//prog_thread[prog_thread_id].ldv_count[s_index]--;
	//}
}

inline void wlpdstm::TxMixinv::SetStoreVector(unsigned address_index, unsigned s_index, WriteLogEntry *entry){
	/*if(entry == NULL)
		prog_thread[prog_thread_id].stv_count[s_index]--;
	else if(prog_thread[prog_thread_id].store_vector[s_index][address_index] == NULL)
		prog_thread[prog_thread_id].stv_count[s_index]++;
*/
	//prog_thread[prog_thread_id].store_vector[s_index][address_index] = entry;

	atomic_store_release(&prog_thread[prog_thread_id].store_vector[s_index][address_index], entry);
	//printf("ssv serial %d writelockindex %d value %08x\n", serial, address_index, entry);
}

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
