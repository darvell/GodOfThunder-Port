#ifndef GOT_NATIVE_MEM_H
#define GOT_NATIVE_MEM_H

#include <string.h>

/* Turbo C far-memory helpers map cleanly to standard libc for our build. */
#ifndef _fmemcpy
#define _fmemcpy memcpy
#endif
#ifndef _fmemset
#define _fmemset memset
#endif
#ifndef _fmemcmp
#define _fmemcmp memcmp
#endif

#ifndef _fstrncpy
#define _fstrncpy strncpy
#endif
#ifndef _fstrlen
#define _fstrlen strlen
#endif
#ifndef _fstrcmp
#define _fstrcmp strcmp
#endif

#endif

