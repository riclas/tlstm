/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef TLSTM_TLSTM_H_
#define TLSTM_TLSTM_H_

#include <stdlib.h>
#include <stdint.h>

#include "common/word.h"
#include "common/jmp.h"

//namespace tlstm {

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

	///////////////////////////
	// basic interface start //
	///////////////////////////

	typedef void *tx_desc;

	// initialization
	void tlstm_global_init(int nb_tasks);
	void tlstm_thread_init(int ptid, int taskid);

	// cleanup
	void tlstm_thread_shutdown();
	void tlstm_global_shutdown();

	// start/end tx
	void tlstm_start_tx(int commit, int serial, int start_serial, int commit_serial) __attribute__ ((noinline));
	void tlstm_start_tx_id(int lexical_tx_id) __attribute__ ((noinline));

	LONG_JMP_BUF *tlstm_get_long_jmp_buf();

	void tlstm_commit_tx();

	void tlstm_abort_tx();

	void tlstm_restart_tx();

	// read/write word
	Word tlstm_read_word(Word *address);

	void tlstm_write_word(Word *address, Word value);

#ifdef SUPPORT_LOCAL_WRITES
	void tlstm_write_word_local(Word *address, Word value);
#endif /* SUPPORT_LOCAL_WRITES */

	// memory management
	void *tlstm_tx_malloc(size_t size);

	void tlstm_tx_free(void *ptr, size_t size);

	///////////////////////////////////////////////////////
	// a separate set of methods if current tx is cached //
	///////////////////////////////////////////////////////

	tx_desc *tlstm_get_tx_desc();

	// start/end tx
	void tlstm_start_tx_desc(tx_desc *tx);
	void tlstm_start_tx_id_desc(tx_desc *tx, int lexical_tx_id, int commit, int serial, int start, int last);

	LONG_JMP_BUF *tlstm_get_long_jmp_buf_desc(tx_desc *tx);

	void tlstm_commit_tx_desc(tx_desc *tx);

	void tlstm_abort_tx_desc(tx_desc *tx);

	void tlstm_restart_tx_desc(tx_desc *tx);

	// read/write word
	Word tlstm_read_word_desc(tx_desc *tx, Word *address);

	void tlstm_write_word_desc(tx_desc *tx, Word *address, Word value);

#ifdef SUPPORT_LOCAL_WRITES
	void tlstm_write_word_local_desc(tx_desc *tx, Word *address, Word value);
#endif /* SUPPORT_LOCAL_WRITES */

	// memory management
	void *tlstm_tx_malloc_desc(tx_desc *tx, size_t size);

	void tlstm_tx_free_desc(tx_desc *tx, void *ptr, size_t size);

	unsigned tlstm_inc_serial(tx_desc *tx, unsigned ptid);

	void tlstm_print_stats();

	// use for non-tx code to be able to simply switch
	// the whole memory management scheme
	void *tlstm_s_malloc(size_t size);
	void tlstm_s_free(void *ptr);

	/////////////////////////
	// basic interface end //
	/////////////////////////

	//////////////////////////////
	// extended interface start //
	//////////////////////////////

	uint32_t tlstm_read_32(uint32_t *address);
	uint32_t tlstm_read_32_desc(tx_desc *tx, uint32_t *address);
	float tlstm_read_float(float *address);
	float tlstm_read_float_desc(tx_desc *tx, float *address);
	uint64_t tlstm_read_64(uint64_t *address);
	uint64_t tlstm_read_64_desc(tx_desc *tx, uint64_t *address);
	double tlstm_read_double(double *address);
	double tlstm_read_double_desc(tx_desc *tx, double *address);

	void tlstm_write_32(uint32_t *address, uint32_t value);
	void tlstm_write_32_desc(tx_desc *tx, uint32_t *address, uint32_t value);
	void tlstm_write_float(float *address, float value);
	void tlstm_write_float_desc(tx_desc *tx, float *address, float value);
	void tlstm_write_64(uint64_t *address, uint64_t value);
	void tlstm_write_64_desc(tx_desc *tx, uint64_t *address, uint64_t value);
	void tlstm_write_double(double *address, double value);
	void tlstm_write_double_desc(tx_desc *tx, double *address, double value);

	////////////////////////////
	// extended interface end //
	////////////////////////////
#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif // TLSTM_TLSTM_H_

