#ifndef MODERN_H_
#define MODERN_H_

#ifdef __llvm__

// Erase far / huge when compiling on modern platforms.
#define modern
#define far
#define huge

// Map old function names to new ones.
#define farfree   free
#define farmalloc malloc
#ifdef _WIN32
#define strcmpi   _stricmp
#else
#define strcmpi   strcasecmp
#endif

/* strupr: MSVC CRT already provides this; only declare for other toolchains. */
#ifndef _MSC_VER
#ifdef __cplusplus
extern "C" {
#endif
char* strupr(char* s);
#ifdef __cplusplus
}
#endif
#endif

#define randomize()

#include <stdint.h>

// TODO
// #include <time.h>
// inline void randomize(void) { srand((unsigned) time(NULL)); }

#else

/*
  DOS toolchains vary:
  - Borland/Turbo C doesn't ship <stdint.h>
  - OpenWatcom does (and also defines these via other headers)

  Keep the original lightweight typedefs for Borland-like environments, but
  defer to the standard header for OpenWatcom to avoid redefinition errors.
*/
#ifdef __WATCOMC__
/* OpenWatcom already provides exact-width integers in <sys/types.h>. */
#include <sys/types.h>
#else
typedef unsigned char uint8_t;
typedef unsigned int  uint16_t;
typedef unsigned long uint32_t;
#endif

#endif

#endif
