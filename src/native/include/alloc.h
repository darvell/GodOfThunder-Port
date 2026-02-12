#ifndef GOT_NATIVE_ALLOC_H
#define GOT_NATIVE_ALLOC_H

#include <stdlib.h>

/* modern.h already maps farmalloc/farfree when __llvm__ is set, but some
   translation units include <alloc.h> and expect these symbols to exist. */
#ifndef far
#define far
#endif
#ifndef huge
#define huge
#endif
#ifndef interrupt
#define interrupt
#endif

#ifndef farmalloc
#define farmalloc malloc
#endif
#ifndef farfree
#define farfree free
#endif

#endif
