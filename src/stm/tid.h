/**
 * @file tid.h
 * 
 * Declaration of elements required for thread identifier management
 * are in this file.
 * 
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef TLSTM_TID_H_
#define TLSTM_TID_H_

#include "common/atomic.h"

namespace tlstm {

	class Tid {
		public:
			Tid() : tid(GetCounter().getNext()) {
				// do nothing
			}

			unsigned Get() {
				return tid;
			}

		private:
			const unsigned tid;

			// avoid uninitialized data use
			static CounterOF &GetCounter() {
				// global tid counter
				static CounterOF counter;
				return counter;
			}
	};
}

#endif
