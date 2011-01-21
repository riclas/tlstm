/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#ifndef WLPDSTM_SAMPLING_H_
#define WLPDSTM_SAMPLING_H_

#define SAMPLING_THRESHOLD 101

namespace wlpdstm {

	class Sampling {
		public:
			Sampling() : sample(false), counter(0) {
				// empty
			}

			bool should_sample() {
				return sample;
			}

			void tx_start() {
				if(++counter == SAMPLING_THRESHOLD) {
					counter = 0;
					sample = true;
				} else {
					sample = false;
				}
			}

		private:
			bool sample;
			unsigned counter;
	};
}

#endif /* WLPDSTM_SAMPLING_H_ */
