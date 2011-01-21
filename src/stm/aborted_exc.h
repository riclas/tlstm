/**
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef WLPDSTM_ABORTED_EXC_H_
#define WLPDSTM_ABORTED_EXC_H_

#include "constants.h"

namespace wlpdstm {

	struct RestartedException {
		RestartedException(int lvl = TX_NO_LEVEL) : level(lvl) {  }

		int level;
	};

	struct AbortedException {
		AbortedException(int lvl = TX_NO_LEVEL) : level(lvl) {  }

		int level;
	};

}

#endif // WLPDSTM_ABORTED_EXC_H_
