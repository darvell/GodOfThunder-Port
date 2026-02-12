#ifndef GOT_NATIVE_IO_H
#define GOT_NATIVE_IO_H

#ifdef _MSC_VER
#include <corecrt_io.h>
#else
#include <unistd.h>
#endif
#include <fcntl.h>

#endif

