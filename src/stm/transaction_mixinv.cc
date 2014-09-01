/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#include "transaction_mixinv.h"

tlstm::VersionLock tlstm::TxMixinv::version_lock_table[FULL_VERSION_LOCK_TABLE_SIZE];

CACHE_LINE_ALIGNED tlstm::GlobalTimestamp tlstm::TxMixinv::commit_ts;

CACHE_LINE_ALIGNED tlstm::GlobalTimestamp tlstm::TxMixinv::cm_ts;

CACHE_LINE_ALIGNED tlstm::PaddedWord tlstm::TxMixinv::minimum_observed_ts;

CACHE_LINE_ALIGNED tlstm::PaddedSpinTryLock tlstm::TxMixinv::minimum_observed_ts_lock;

tlstm::TxMixinv *tlstm::TxMixinv::transactions[MAX_THREADS];

Word tlstm::TxMixinv::thread_count;

int tlstm::TxMixinv::specdepth;

pthread_t tlstm::TxMixinv::threadid[MAX_THREADS];

tlstm::TxMixinv::ProgramThread tlstm::TxMixinv::prog_thread[MAX_THREADS];

CACHE_LINE_ALIGNED tlstm::PaddedBool tlstm::TxMixinv::synchronization_in_progress;

#ifdef PRIVATIZATION_QUIESCENCE
CACHE_LINE_ALIGNED volatile Word tlstm::TxMixinv::quiescence_timestamp_array[MAX_THREADS];
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
CACHE_LINE_ALIGNED volatile tlstm::PaddedWord tlstm::TxMixinv::signaling_array[MAX_THREADS];
#endif /* SIGNALING */		

#ifdef ADAPTIVE_LOCKING
CACHE_LINE_ALIGNED tlstm::PaddedUnsigned tlstm::TxMixinv::lock_extent;

unsigned tlstm::TxMixinv::false_sharing_history[FALSE_SHARING_HISTORY_SIZE];
#endif /* ADAPTIVE_LOCKING */
