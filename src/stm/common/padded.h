/**
 *  @author Aleksandar Dragojevic aleksandar.dragojevic@epfl.ch
 *
 */

#ifndef TLSTM_PADDED_H_
#define TLSTM_PADDED_H_

namespace tlstm {
	
	union PaddedUnsigned {
		volatile unsigned val;
		char padding[CACHE_LINE_SIZE_BYTES];
	};
	
	union PaddedWord {
		volatile Word val;
		char padding[CACHE_LINE_SIZE_BYTES];
	};	
	
	union PaddedBool {
		volatile Word val;
		char padding[CACHE_LINE_SIZE_BYTES];
	};	
}

#endif /* TLSTM_PADDED_H_ */
