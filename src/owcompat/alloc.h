#ifndef GOT_OWCOMPAT_ALLOC_H
#define GOT_OWCOMPAT_ALLOC_H

/*
 * Compatibility shim for Borland <alloc.h> when building with OpenWatcom.
 *
 * The original codebase uses Borland's far heap helpers `farmalloc`/`farfree`.
 * OpenWatcom provides `_fmalloc`/`_ffree` in <malloc.h>.
 */

#include <malloc.h>

#ifndef farmalloc
#define farmalloc _fmalloc
#endif

#ifndef farfree
#define farfree _ffree
#endif

#endif

