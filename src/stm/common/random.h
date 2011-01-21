/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef WLPDSTM_RANDOM_H_
#define WLPDSTM_RANDOM_H_

#include <stdlib.h>

namespace wlpdstm {

	class Random {
		public:
			unsigned Get() {
				return rand_r(&seed);
			}

		private:
			unsigned seed;
	};
}

#endif /* WLPDSTM_RANDOM_H_ */
