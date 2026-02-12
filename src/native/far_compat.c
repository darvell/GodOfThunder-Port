#include <string.h>

/*
  Turbo C far-memory helpers.

  On modern flat memory, "far" pointers are just normal pointers. The original
  code uses `_f*` routines; provide them for the native build.
*/

void* _fmemset(void* s, int c, unsigned n) {
  return memset(s, c, (size_t)n);
}

void* _fmemcpy(void* dst, const void* src, unsigned n) {
  return memcpy(dst, src, (size_t)n);
}

unsigned _fstrlen(const char* s) {
  return (unsigned)strlen(s);
}

char* _fstrcpy(char* dst, const char* src) {
  return strcpy(dst, src);
}

char* _fstrcat(char* dst, const char* src) {
  return strcat(dst, src);
}

int _fstrcmp(const char* a, const char* b) {
  return strcmp(a, b);
}

int _fstrncmp(const char* a, const char* b, unsigned n) {
  return strncmp(a, b, (size_t)n);
}

char* _fstrncpy(char* dst, const char* src, unsigned n) {
  return strncpy(dst, src, (size_t)n);
}

