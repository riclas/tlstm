/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#include "tlstm.h"

#include "tid.h"
#include "memory.h"
#include "transaction.h"
#include "constants.h"
#include "read_write.h"
#include "tlstm_malloc.h"

///////////////////////////
// basic interface start //
///////////////////////////

extern "C" {

void tlstm_global_init(int nb_tasks) {
	tlstm::CurrentTransaction::GlobalInit(nb_tasks);
}

void tlstm_thread_init(int ptid, int taskid) {
	tlstm::CurrentTransaction::ThreadInit(ptid, taskid);
}

void tlstm_start_tx(int commit, int serial, int start, int last) {
	tlstm::CurrentTransaction::TxStart(0, commit, serial, start, last);
}

void tlstm_start_tx_id(int lexical_tx_id) {
	tlstm::CurrentTransaction::TxStart(lexical_tx_id);
}

LONG_JMP_BUF *tlstm_get_long_jmp_buf() {
	return tlstm::CurrentTransaction::GetLongJmpBuf();
}

void tlstm_commit_tx() {
	tlstm::CurrentTransaction::TxCommit();
}

void tlstm_abort_tx() {
	tlstm::CurrentTransaction::TxAbort();
}

void tlstm_restart_tx() {
	tlstm::CurrentTransaction::TxRestart();
}

void tlstm_write_word(Word *address, Word value) {
	tlstm::CurrentTransaction::WriteWord(address, value);
}

#ifdef SUPPORT_LOCAL_WRITES
void tlstm_write_word_local(Word *address, Word value) {
	tlstm::CurrentTransaction::WriteWordLocal(address, value);
}
#endif /* SUPPORT_LOCAL_WRITES */

Word tlstm_read_word(Word *address) {
	return tlstm::CurrentTransaction::ReadWord(address);
}

void tlstm_tx_free(void *ptr, size_t size) {
	tlstm_malloc_tx_free(ptr, size);
}

void *tlstm_tx_malloc(size_t size) {
	return tlstm_malloc_tx_malloc(size);
}

tx_desc *tlstm_get_tx_desc() {
	return (::tx_desc *)tlstm::CurrentTransaction::Get();
}

void tlstm_start_tx_desc(tx_desc *tx) {
	((tlstm::Transaction *)tx)->TxStart();
}

LONG_JMP_BUF *tlstm_get_long_jmp_buf_desc(tx_desc *tx) {
	return &((tlstm::Transaction *)tx)->start_buf;
}

void tlstm_start_tx_id_desc(tx_desc *tx, int lexical_tx_id, int commit, int serial, int start, int last) {
	((tlstm::Transaction *)tx)->TxStart(lexical_tx_id,  commit, serial, start, last);
}

void tlstm_commit_tx_desc(tx_desc *tx) {
	((tlstm::Transaction *)tx)->TxCommit();
}

void tlstm_restart_tx_desc(tx_desc *tx) {
	((tlstm::Transaction *)tx)->TxRestart();
}

void tlstm_abort_tx_desc(tx_desc *tx) {
	((tlstm::Transaction *)tx)->TxAbort();
}

Word tlstm_read_word_desc(tx_desc *tx, Word *address) {
	return ((tlstm::Transaction *)tx)->ReadWord(address);
}

void tlstm_write_word_desc(tx_desc *tx, Word *address, Word value) {
	((tlstm::Transaction *)tx)->WriteWord(address, value);
}

#ifdef SUPPORT_LOCAL_WRITES
void tlstm_write_word_local_desc(tx_desc *tx, Word *address, Word value) {
	((tlstm::Transaction *)tx)->WriteWordLocal(address, value);
}
#endif /* SUPPORT_LOCAL_WRITES */

void tlstm_tx_free_desc(tx_desc *tx, void *ptr, size_t size) {
	((tlstm::Transaction *)tx)->TxFree(ptr, size);
}

void *tlstm_tx_malloc_desc(tx_desc *tx, size_t size) {
	return ((tlstm::Transaction *)tx)->TxMalloc(size);
}

void tlstm_thread_shutdown() {
	tlstm::CurrentTransaction::ThreadShutdown();
}

void tlstm_global_shutdown() {
	tlstm::Transaction::GlobalShutdown();
}

void tlstm_print_stats() {
	tlstm::Transaction::PrintStatistics();
}

void *tlstm_s_malloc(size_t size) {
	return tlstm::MemoryManager::Malloc(size);
}

void tlstm_s_free(void *ptr) {
	tlstm::MemoryManager::Free(ptr);
}

/////////////////////////
// basic interface end //
/////////////////////////


//////////////////////////////
// extended interface start //
//////////////////////////////

uint32_t tlstm_read_32(uint32_t *address) {
	return read32aligned(tlstm::CurrentTransaction::Get(), address);
}

uint32_t tlstm_read_32_desc(tx_desc *tx, uint32_t *address) {
	return read32aligned((tlstm::Transaction *)tx, address);
}

float tlstm_read_float(float *address) {
	return read_float_aligned(tlstm::CurrentTransaction::Get(), address);
}

float tlstm_read_float_desc(tx_desc *tx, float *address) {
	return read_float_aligned((tlstm::Transaction *)tx, address);
}

uint64_t tlstm_read_64(uint64_t *address) {
	return read64aligned(tlstm::CurrentTransaction::Get(), address);
}

uint64_t tlstm_read_64_desc(tx_desc *tx, uint64_t *address) {
	return read64aligned((tlstm::Transaction *)tx, address);
}

double tlstm_read_double(double *address) {
	return read_double_aligned(tlstm::CurrentTransaction::Get(), address);
}

double tlstm_read_double_desc(tx_desc *tx, double *address) {
	return read_double_aligned((tlstm::Transaction *)tx, address);
}

void tlstm_write_32(uint32_t *address, uint32_t value) {
	write32aligned(tlstm::CurrentTransaction::Get(), address, value);
}

void tlstm_write_32_desc(tx_desc *tx, uint32_t *address, uint32_t value) {
	write32aligned((tlstm::Transaction *)tx, address, value);
}

void tlstm_write_float(float *address, float value) {
	write_float_aligned(tlstm::CurrentTransaction::Get(), address, value);
}

void tlstm_write_float_desc(tx_desc *tx, float *address, float value) {
	write_float_aligned((tlstm::Transaction *)tx, address, value);
}

void tlstm_write_64(uint64_t *address, uint64_t value) {
	write64aligned(tlstm::CurrentTransaction::Get(), address, value);
}

void tlstm_write_64_desc(tx_desc *tx, uint64_t *address, uint64_t value) {
	write64aligned((tlstm::Transaction *)tx, address, value);
}

void tlstm_write_double(double *address, double value) {
	write_double_aligned(tlstm::CurrentTransaction::Get(), address, value);
}

void tlstm_write_double_desc(tx_desc *tx, double *address, double value) {
	write_double_aligned((tlstm::Transaction *)tx, address, value);
}

}

//////////////////////////////
// extended interface end //
//////////////////////////////
