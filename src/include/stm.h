#include "../src/stm/tlstm.h"

#define BEGIN_TRANSACTION             \
	int flag = sigsetjmp(*tlstm_get_long_jmp_buf(), 1); \
	if(flag == LONG_JMP_INCONSISTENT_READ_FLAG) \
		tlstm_inconsistent_read_restart_tx(); \
	if(flag != LONG_JMP_ABORT_FLAG) {   \
        tlstm_start_tx()

#define BEGIN_TRANSACTION_ID(TX_ID)   \
    int flag = sigsetjmp(*tlstm_get_long_jmp_buf(), 1); \
	if(flag == LONG_JMP_INCONSISTENT_READ_FLAG) \
		tlstm_inconsistent_read_restart_tx(); \
	if(flag != LONG_JMP_ABORT_FLAG) {   \
    	tlstm_start_tx_id(TX_ID)

#define END_TRANSACTION					\
		tlstm_commit_tx();			\
	}

#define BEGIN_TRANSACTION_DESC			\
	int flag = sigsetjmp(*tlstm_get_long_jmp_buf(), 1); \
	if(flag == LONG_JMP_INCONSISTENT_READ_FLAG) \
		tlstm_inconsistent_read_restart_tx_desc(tx); \
	if(flag != LONG_JMP_ABORT_FLAG) {   \
    	tlstm_start_tx_desc(tx)

#define BEGIN_TRANSACTION_DESC_ID(TX_ID, serial, start, last)	\
	int flag = sigsetjmp(*tlstm_get_long_jmp_buf(), 1); \
	if(flag == LONG_JMP_INCONSISTENT_READ_FLAG) \
		tlstm_inconsistent_read_restart_tx_desc(tx); \
	if(flag != LONG_JMP_ABORT_FLAG) {   \
    	tlstm_start_tx_id_desc(tx, TX_ID, serial, start, last)

#define END_TRANSACTION_DESC				\
		tlstm_commit_tx_desc(tx);			\
	}
