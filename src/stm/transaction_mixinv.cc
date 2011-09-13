/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#include "transaction_mixinv.h"

wlpdstm::VersionLock wlpdstm::TxMixinv::version_lock_table[FULL_VERSION_LOCK_TABLE_SIZE];

CACHE_LINE_ALIGNED wlpdstm::GlobalTimestamp wlpdstm::TxMixinv::commit_ts;

CACHE_LINE_ALIGNED wlpdstm::GlobalTimestamp wlpdstm::TxMixinv::cm_ts;

CACHE_LINE_ALIGNED wlpdstm::PaddedWord wlpdstm::TxMixinv::minimum_observed_ts;

CACHE_LINE_ALIGNED wlpdstm::PaddedSpinTryLock wlpdstm::TxMixinv::minimum_observed_ts_lock;

wlpdstm::TxMixinv *wlpdstm::TxMixinv::transactions[MAX_THREADS];

Word wlpdstm::TxMixinv::thread_count;

int wlpdstm::TxMixinv::specdepth;

wlpdstm::TxMixinv::ProgramThread wlpdstm::TxMixinv::prog_thread[MAX_THREADS];

//int wlpdstm::TxMixinv::serial;

CACHE_LINE_ALIGNED wlpdstm::PaddedBool wlpdstm::TxMixinv::synchronization_in_progress;

#ifdef PRIVATIZATION_QUIESCENCE
CACHE_LINE_ALIGNED volatile Word wlpdstm::TxMixinv::quiescence_timestamp_array[MAX_THREADS];
#endif /* PRIVATIZATION_QUIESCENCE */

#ifdef SIGNALING
CACHE_LINE_ALIGNED volatile wlpdstm::PaddedWord wlpdstm::TxMixinv::signaling_array[MAX_THREADS];
#endif /* SIGNALING */		

#ifdef ADAPTIVE_LOCKING
CACHE_LINE_ALIGNED wlpdstm::PaddedUnsigned wlpdstm::TxMixinv::lock_extent;

unsigned wlpdstm::TxMixinv::false_sharing_history[FALSE_SHARING_HISTORY_SIZE];
#endif /* ADAPTIVE_LOCKING */
