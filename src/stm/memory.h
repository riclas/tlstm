// this is just a convinient way of switching memory management
// implementations

#ifdef MM_PRIVATIZATION
#include "memory/memory_impl_priv.h"
#elif defined MM_EPOCH
#include "memory/memory_impl.h"
#endif /* mm */
