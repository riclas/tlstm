#ifndef TLSTM_CONSTANTS_H_
#define TLSTM_CONSTANTS_H_

#ifdef TLSTM_X86
#define CACHE_LINE_SIZE_BYTES 64
#elif defined TLSTM_SPARC
#define CACHE_LINE_SIZE_BYTES 64
#endif /* arch */

#define CACHE_LINE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE_BYTES)))

#include "common/word.h"

namespace tlstm {

#ifdef TLSTM_32
	const unsigned ADDRESS_SPACE_SIZE = 32;
	const unsigned LOG_BYTES_IN_WORD = 2;
#elif defined TLSTM_64
	const unsigned ADDRESS_SPACE_SIZE = 64;
	const unsigned LOG_BYTES_IN_WORD = 3;
#endif /* X86_64 */
	
	// this constant is useful for defining various data structures, but it should be possible
	// to replace it with a higher number with no problems
	const unsigned MAX_THREADS = 64;

	// the smallest timestamp used
	const unsigned MINIMUM_TS = 0;

	const unsigned WORD_LOG_SIZE = 22;

	const unsigned LOCK_RESERVED_BITS = 2;

	// -1 to get the maximum number
	const Word MAXIMUM_TS = (1l << (ADDRESS_SPACE_SIZE - LOCK_RESERVED_BITS)) - 1 - MAX_THREADS;

	// top level transaction
	const int TX_TOP_LEVEL = 0;
	const int TX_NO_LEVEL = (TX_TOP_LEVEL - 1);

	enum OperationType {
		NO_OP = 0,
		READ_OP = 1,
		WRITE_OP = 2,
		DELETE_OP = 3
	};

	inline const char *getOperationName(OperationType optype) {
		const char *names[] = {
			"noop",
			"read",
			"write",
			"delete"
		};

		return names[optype];
	}
}

#define LSB 1u

#define MINIMUM_VERSION ((Word *)0)

//TLSTM
//#define WRITE_LOCK_CLEAR ((Word)999)
//#define WRITE_LOCK_COMMIT ((Word)998)
//SwissTM
#define WRITE_LOCK_CLEAR ((Word)0)

#define READ_LOCK_SET ((Word)LSB)

#define NO_LEXICAL_TX -1

#endif
