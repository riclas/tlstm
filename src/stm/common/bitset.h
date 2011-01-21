/**
 * Bitmasks support up to 32 visible readers. If there are more
 * concurrent threads in the system, this class should be changed.
 *
 * @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 */

#ifndef WLPDSTM_BITMASKS_H_
#define WLPDSTM_BITMASKS_H_

#include "atomic.h"

namespace wlpdstm {

	class BitsetBase {
		public:
			BitsetBase() : bits(0) { }

		public:
			static unsigned getBitmask(unsigned pos) {
				unsigned const bitMasks[] = {
					0x00000001, 0x00000002,	0x00000004,	0x00000008,
					0x00000010,	0x00000020,	0x00000040,	0x00000080,
					0x00000100,	0x00000200,	0x00000400,	0x00000800,
					0x00001000,	0x00002000,	0x00004000,	0x00008000,
					0x00010000,	0x00020000,	0x00040000,	0x00080000,
					0x00100000,	0x00200000,	0x00400000,	0x00800000,
					0x01000000,	0x02000000,	0x04000000,	0x08000000,
					0x10000000,	0x20000000,	0x40000000,	0x80000000};
				return bitMasks[pos];
			}

		unsigned getBits() const {
			return (unsigned)bits;
		}

		protected:
			void *bits;
	};

	class Bitset : public BitsetBase {
		public:
			void setBit(unsigned bitmask) {
				unsigned oldval = (unsigned)bits;
				unsigned newval = oldval | bitmask;
				bits = (void *)newval;
			}

			void setBitAtomicAcquire(unsigned bitmask) {
				unsigned oldval;
				unsigned newval;

				do {
					oldval = (unsigned)bits;
					newval = oldval | bitmask;
				} while(!atomic_cas_acquire(&bits, oldval, newval));
			}

			void unsetBit(unsigned bitmask) {
				bitmask = ~bitmask;
				unsigned oldval = (unsigned)bits;
				unsigned newval = oldval & bitmask;
				bits = (void *)newval;
			}

			void unsetBitAtomicNoBarrier(unsigned bitmask) {
				bitmask = ~bitmask;
				unsigned oldval;
				unsigned newval;

				do {
					oldval = (unsigned)bits;
					newval = oldval & bitmask;
				} while(!atomic_cas_no_barrier(&bits, oldval, newval));
			}

			void unsetBitAtomicAcquire(unsigned bitmask) {
				bitmask = ~bitmask;
				unsigned oldval;
				unsigned newval;

				do {
					oldval = (unsigned)bits;
					newval = oldval & bitmask;
				} while(!atomic_cas_acquire(&bits, oldval, newval));
			}

			bool isBitSet(unsigned bitmask) {
				unsigned oldval = (unsigned)bits;
				return (oldval & bitmask);
			}

			bool isOnlyBitSet(unsigned bitmask) {
				unsigned oldval = (unsigned)bits;
				return oldval == bitmask;
			}

			bool isEmpty() {
				return bits == 0;
			}

			bool operator[](int i) const {
				return getBitmask(i) & (unsigned)bits;
			}
	};
}

#endif // WLPDSTM_BITMASKS_H_
