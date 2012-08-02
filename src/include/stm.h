#include "../src/stm/tlstm.h"

#define BEGIN_TRANSACTION             \
	if(sigsetjmp(*tlstm_get_long_jmp_buf(), 0) != LONG_JMP_ABORT_FLAG) {   \
        tlstm_start_tx()

#define BEGIN_TRANSACTION_ID(TX_ID)   \
    if(sigsetjmp(*tlstm_get_long_jmp_buf(), 0) != LONG_JMP_ABORT_FLAG) {   \
		tlstm_start_tx_id(TX_ID)

#define END_TRANSACTION					\
		tlstm_commit_tx();			\
	}

#define BEGIN_TRANSACTION_DESC			\
	if(sigsetjmp(*tlstm_get_long_jmp_buf_desc(tx), 0) != LONG_JMP_ABORT_FLAG) {	\
		tlstm_start_tx_desc(tx)

#define BEGIN_TRANSACTION_DESC_ID(TX_ID, commit, serial, start, last)	\
	if(sigsetjmp(*tlstm_get_long_jmp_buf_desc(tx), 0) != LONG_JMP_ABORT_FLAG) {	\
		tlstm_start_tx_id_desc(tx, TX_ID, commit, serial, start, last)

#define END_TRANSACTION_DESC				\
		tlstm_commit_tx_desc(tx);			\
	}
