/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#include <stdlib.h>

#include "tanger-stm-std-string.h"

#include "../stm/read_write.h"
#include "../stm/transaction.h"

extern "C" {

//	extern void *tanger_stm_std_memset(void *dest, int c, size_t n) __attribute__((weak));
	extern void *tanger_stm_std_memset(void *dest, int c, size_t n) {
		return tlstm::memset_tx(tlstm::CurrentTransaction::Get(), dest, c, n);
	}

//	extern void *tanger_stm_std_memcpy(void *dest, const void *src, size_t n) __attribute__((weak));
	extern void *tanger_stm_std_memcpy(void *dest, const void *src, size_t n) {
		return tlstm::memcpy_tx(tlstm::CurrentTransaction::Get(), dest, src, n);
	}

//	extern void *tanger_stm_std_memmove(void *dest, const void *src, size_t n) __attribute__((weak));
	extern void *tanger_stm_std_memmove(void *dest, const void *src, size_t n) {
		return tlstm::memmove_tx(tlstm::CurrentTransaction::Get(), dest, src, n);
	}

	extern int tanger_stm_std_strcmp(const char *str1, const char *str2) __attribute__((weak));
	extern int tanger_stm_std_strcmp(const char *str1, const char *str2) {
		return tlstm::strcmp_tx(tlstm::CurrentTransaction::Get(), str1, str2);
	}

	extern int tanger_stm_std_strncmp(const char *str1, const char *str2, size_t num) __attribute__((weak));
	extern int tanger_stm_std_strncmp(const char *str1, const char *str2, size_t num) {
		return tlstm::strncmp_tx(tlstm::CurrentTransaction::Get(), str1, str2, num);
	}	
}
