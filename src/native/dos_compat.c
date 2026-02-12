#include "dos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <time.h>
#include <fcntl.h>

#ifdef _MSC_VER
#include <windows.h>
#include <corecrt_io.h>
typedef long ssize_t;
#define STDIN_FILENO 0
#define open  _open
#define read  _read
#define write _write
#define close _close
#else
#include <unistd.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

void delay(unsigned ms) {
#ifdef __EMSCRIPTEN__
  /* On the web, long busy-waits freeze the tab. Yield with emscripten_sleep()
     (requires ASYNCIFY). For very small delays keep a short spin to avoid
     oversleeping in tight transitions. */
  if (ms <= 2) {
    double start = emscripten_get_now();
    double until = start + (double)ms;
    while (emscripten_get_now() < until) {
      /* spin */
    }
  } else {
    emscripten_sleep(ms);
  }
#elif defined(_MSC_VER)
  Sleep(ms);
#else
  usleep((useconds_t)ms * 1000);
#endif
}

/* MSVC CRT already provides itoa/ltoa/ultoa. */
#ifndef _MSC_VER

static char* itoa_base_u(unsigned long v, char* str, int base, int is_signed, long sv) {
  static const char* digits = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  char tmp[64];
  int i = 0;
  int neg = 0;

  if (base < 2 || base > 36) {
    str[0] = 0;
    return str;
  }

  if (is_signed && sv < 0) {
    neg = 1;
    v = (unsigned long)(-sv);
  }

  do {
    tmp[i++] = digits[v % (unsigned long)base];
    v /= (unsigned long)base;
  } while (v && i < (int)sizeof(tmp));

  {
    int o = 0;
    if (neg) {
      str[o++] = '-';
    }
    while (i > 0) {
      str[o++] = tmp[--i];
    }
    str[o] = 0;
  }

  return str;
}

char* itoa(int value, char* str, int base) {
  return itoa_base_u((unsigned long)value, str, base, 1, (long)value);
}

char* ltoa(long value, char* str, int base) {
  return itoa_base_u((unsigned long)value, str, base, 1, value);
}

char* ultoa(unsigned long value, char* str, int base) {
  return itoa_base_u(value, str, base, 0, 0);
}

#endif /* !_MSC_VER */

int _dos_open(const char* path, int oflag, int* out_handle) {
  int fd = open(path, oflag, 0666);
  if (fd < 0) {
    if (out_handle) *out_handle = -1;
    return errno ? errno : 1;
  }
  if (out_handle) *out_handle = fd;
  return 0;
}

int _dos_close(int handle) {
  if (close(handle) != 0) {
    return errno ? errno : 1;
  }
  return 0;
}

int _dos_read(int handle, void* buffer, unsigned count, unsigned* out_count) {
  ssize_t n = read(handle, buffer, (size_t)count);
  if (n < 0) {
    if (out_count) *out_count = 0;
    return errno ? errno : 1;
  }
  if (out_count) *out_count = (unsigned)n;
  return 0;
}

int _dos_write(int handle, const void* buffer, unsigned count, unsigned* out_count) {
  ssize_t n = write(handle, buffer, (size_t)count);
  if (n < 0) {
    if (out_count) *out_count = 0;
    return errno ? errno : 1;
  }
  if (out_count) *out_count = (unsigned)n;
  return 0;
}

/* MSVC CRT already provides kbhit/getch via <conio.h>. */
#ifndef _MSC_VER

int kbhit(void) {
  /* Not used by the game loop in the native build. */
  return 0;
}

int getch(void) {
  /* Fallback: read a single byte from stdin. */
  unsigned char c = 0;
  if (read(STDIN_FILENO, &c, 1) == 1) {
    return (int)c;
  }
  return 0;
}

#endif /* !_MSC_VER */
