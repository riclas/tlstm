/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef WLPDSTM_TRANSACTION_H_
#define WLPDSTM_TRANSACTION_H_

#include "memory.h"
#include "common/tls.h"
#include "aborted_exc.h"
#include "common/word.h"

///////////////////////////////////////////
// different transaction implementations //
///////////////////////////////////////////

#include "transaction_mixinv.h"

///////////////////////////////////////
// select transaction implementation //
///////////////////////////////////////

namespace wlpdstm {

	typedef TxMixinv Transaction;
}

/////////////////////////////////////
// thread local transaction object //
/////////////////////////////////////

namespace wlpdstm {

	// A shortcut used for accessing transaction in progress.
	class CurrentTransaction : public Tls<Transaction, true, true> {
		public:
			static void TxStart(int lexical_tx_id = NO_LEXICAL_TX, bool commit=true, int new_serial=0, int start_s=0, int commit_s=0) {
				Tls<Transaction, true, true>::Get()->TxStart(lexical_tx_id, commit, new_serial, start_s, commit_s);
			}

			static LONG_JMP_BUF *GetLongJmpBuf() {
				return &Tls<Transaction, true, true>::Get()->start_buf;
			}

			static void TxCommit() {
				Tls<Transaction, true, true>::Get()->TxCommit();
			}

			static void TxRestart() {
				Tls<Transaction, true, true>::Get()->TxRestart();
			}

			static void TxAbort() {
				Tls<Transaction, true, true>::Get()->TxAbort();
			}

			static void WriteWord(Word *address, Word value) {
				Tls<Transaction, true, true>::Get()->WriteWord(address, value);
			}

#ifdef SUPPORT_LOCAL_WRITES
			static void WriteWordLocal(Word *address, Word value) {
				Tls<Transaction, true, true>::Get()->WriteWordLocal(address, value);
			}
#endif /* SUPPORT_LOCAL_WRITES */

			static Word ReadWord(Word *address) {
				return Tls<Transaction, true, true>::Get()->ReadWord(address);
			}

			static void *TxMalloc(size_t size) {
				return Tls<Transaction, true, true>::Get()->TxMalloc(size);
			}

			static void TxFree(void *address, size_t size) {
				return Tls<Transaction, true, true>::Get()->TxFree(address, size);
			}
	};
}

#endif // WLPDSTM_TRANSACTION_H_
