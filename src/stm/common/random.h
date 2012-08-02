/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef TLSTM_RANDOM_H_
#define TLSTM_RANDOM_H_

#include <stdlib.h>

namespace tlstm {

	class Random {
		public:
			unsigned Get() {
				return rand_r(&seed);
			}

		private:
			unsigned seed;
	};
}

#endif /* TLSTM_RANDOM_H_ */
