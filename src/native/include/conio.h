#ifndef GOT_NATIVE_CONIO_H
#define GOT_NATIVE_CONIO_H

#ifdef _MSC_VER
/* MSVC CRT provides _kbhit/_getch. Map old Turbo C names. */
int __cdecl _kbhit(void);
int __cdecl _getch(void);
#define kbhit _kbhit
#define getch _getch
#else

#ifdef __cplusplus
extern "C" {
#endif

int kbhit(void);
int getch(void);

#ifdef __cplusplus
}
#endif

#endif /* _MSC_VER */

#endif

