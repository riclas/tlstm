/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef TLSTM_ABORTED_EXC_H_
#define TLSTM_ABORTED_EXC_H_

#include "constants.h"

namespace tlstm {

	struct RestartedException {
		RestartedException(int lvl = TX_NO_LEVEL) : level(lvl) {  }

		int level;
	};

	struct AbortedException {
		AbortedException(int lvl = TX_NO_LEVEL) : level(lvl) {  }

		int level;
	};

}

#endif // TLSTM_ABORTED_EXC_H_
