#ifndef OWCOMPAT_OWPRE_H
#define OWCOMPAT_OWPRE_H

/*
  Forced-include header for OpenWatcom builds.

  The original codebase targets Borland/Turbo C and makes heavy use of:
  - `asm ...` inline assembly statements
  - `inportb` / `outportb` I/O helpers

  OpenWatcom is close, but not identical. Rather than touching every file,
  we force-include this header from the build script with `wcc -fi=...`.
*/

#ifdef __WATCOMC__

/* Map Borland's `asm` keyword to OpenWatcom's inline assembler. */
#ifndef asm
#define asm __asm
#endif

/*
  The original DOS codebase is a mix of C and hand-written ASM.

  OpenWatcom's default 16-bit calling convention is `__watcall` (register
  args, callee pops stack, trailing underscore). Our `g_asm.asm` routines are
  written in a classic stack-frame style (cdecl-like) and are easiest to keep
  that way.

  We therefore declare the graphics ASM entrypoints as `__cdecl` on the C side,
  without changing the rest of the program/toolchain.
*/
#ifndef GOT_GFXCALL
#define GOT_GFXCALL __cdecl
#endif

/* Pull in far heap helpers used all over the original code. */
#include <alloc.h>

/* Borland `randomize()` seeds the RNG; OpenWatcom doesn't provide it. */
#include <stdlib.h>
#include <time.h>
#ifndef randomize
#define randomize() srand((unsigned)time(NULL))
#endif

/*
  Borland provides inportb/outportb. OpenWatcom provides inp/outp.
  Keep the signature/behaviour close to the original.
*/
/*
  Avoid including <conio.h> here: it pulls in <stdbool.h> (and defines `bool`)
  in some OpenWatcom configurations, which collides with the game's own `bool`
  typedefs (and breaks `digisnd.h`).

  Declare `inp/outp` directly instead and mark them intrinsic, mirroring what
  <conio.h> does.
*/
extern unsigned inp(unsigned __port);
extern unsigned outp(unsigned __port, unsigned __value);
#pragma intrinsic(inp,outp)

#ifndef inportb
#define inportb(port) ((unsigned char)inp((unsigned)(port)))
#endif

#ifndef outportb
#define outportb(port, val) (outp((unsigned)(port), (unsigned char)(val)))
#endif

/*
  Borland getvect/setvect helpers are used by some of the original code.
  OpenWatcom provides _dos_getvect/_dos_setvect.
*/
#include <dos.h>
#ifndef getvect
#define getvect(n) _dos_getvect((n))
#endif
#ifndef setvect
#define setvect(n, h) _dos_setvect((n), (h))
#endif

#endif /* __WATCOMC__ */

#endif /* OWCOMPAT_OWPRE_H */
