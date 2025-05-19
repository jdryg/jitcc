#ifndef _STDARG_H
#define _STDARG_H

#include <stdint.h>

typedef char* va_list;

void __va_start(va_list*, ...);

#define va_start(ap, x) (__va_start(&ap, x))
#define va_arg(ap, t)                                                        \
        ((sizeof(t) > sizeof(int64_t) || (sizeof(t) & (sizeof(t) - 1)) != 0) \
            ? **(t**)((ap += sizeof(int64_t)) - sizeof(int64_t))             \
            :  *(t* )((ap += sizeof(int64_t)) - sizeof(int64_t)))
#define va_end(ap)        ((void)(ap = (va_list)0))

#define va_copy(destination, source) ((destination) = (source))

#endif
