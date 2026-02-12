#ifndef GOT_NATIVE_DOS_H
#define GOT_NATIVE_DOS_H

/*
  Minimal <dos.h> compatibility for the native port.

  This is not a DOS emulation layer. It only provides enough surface area
  for the 1994 Turbo C code to compile and run in a modern process.
*/

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turbo C keyword shims */
#ifndef far
#define far
#endif
#ifndef huge
#define huge
#endif
#ifndef interrupt
#define interrupt
#endif

/* Register structs used by int86() call sites. */
union REGS {
  struct {
    uint16_t ax, bx, cx, dx;
    uint16_t si, di, cflag, flags;
  } x;
  struct {
    uint8_t al, ah, bl, bh, cl, ch, dl, dh;
  } h;
};

struct SREGS {
  uint16_t es, cs, ss, ds;
};

/* BIOS/DOS interrupt stubs. */
static inline int int86(int intno, union REGS* in, union REGS* out) {
  (void)intno;
  if (out && in) {
    *out = *in;
  }
  return 0;
}

static inline int int86x(int intno, union REGS* in, union REGS* out, struct SREGS* seg) {
  (void)seg;
  return int86(intno, in, out);
}

/* vect stubs */
typedef void (*voidfunc_t)(void);
static inline voidfunc_t getvect(int v) { (void)v; return (voidfunc_t)0; }
static inline void setvect(int v, voidfunc_t f) { (void)v; (void)f; }

/* Port I/O stubs (never used in the native build's hot paths). */
static inline uint8_t inportb(uint16_t port) { (void)port; return 0; }
static inline void outportb(uint16_t port, uint8_t value) { (void)port; (void)value; }

/* Delay helper (ms) */
void delay(unsigned ms);

/* DOS-style file I/O (used by save/load code). */
int _dos_open(const char* path, int oflag, int* out_handle);
int _dos_close(int handle);
int _dos_read(int handle, void* buffer, unsigned count, unsigned* out_count);
int _dos_write(int handle, const void* buffer, unsigned count, unsigned* out_count);

/* Common Turbo helpers — MSVC CRT already provides these. */
#ifndef _MSC_VER
char* itoa(int value, char* str, int base);
char* ltoa(long value, char* str, int base);
char* ultoa(unsigned long value, char* str, int base);
#endif

#ifdef __cplusplus
}
#endif

#endif
