#ifndef __JITCC_H
#define __JITCC_H

#undef __cdecl
#undef _X86_
#undef WIN32

#include <stddef.h>
#include <stdarg.h>

//#define __int8 char
//#define __int16 short
//#define __int32 int
//#define __int64 long long
#define _HAVE_INT64

#define __cdecl
#define __declspec(x) __attribute__((x))

#define __MSVCRT__ 1
#undef _MSVCRT_

#define __CRT_INLINE extern inline

#define DECLSPEC_ALIGN(x) __attribute__((aligned(x)))
#define _CRT_PACKING 8
#define __CRT_UNALIGNED

#define __CRT_STRINGIZE(_Value) #_Value
#define _CRT_STRINGIZE(_Value) __CRT_STRINGIZE(_Value)
#define __CRT_WIDE(_String) L ## _String
#define _CRT_WIDE(_String) __CRT_WIDE(_String)

#define __stdcall
#define _AMD64_ 1
#define __x86_64 1
#define _M_X64 100   // Visual Studio
#define _M_AMD64 100 // Visual Studio
#define __TRY__

// in stddef.h
#define _SIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#define _PTRDIFF_T_DEFINED
#define _WCHAR_T_DEFINED
#define _UINTPTR_T_DEFINED
#define _INTPTR_T_DEFINED
#define _INTEGRAL_MAX_BITS 64

#ifndef _TIME32_T_DEFINED
#define _TIME32_T_DEFINED
typedef long __time32_t;
#endif

#ifndef _TIME64_T_DEFINED
#define _TIME64_T_DEFINED
typedef long long __time64_t;
#endif

#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
#ifdef _USE_32BIT_TIME_T
typedef __time32_t time_t;
#else
typedef __time64_t time_t;
#endif
#endif

#ifndef _WCTYPE_T_DEFINED
#define _WCTYPE_T_DEFINED
typedef wchar_t wctype_t;
#endif

// for winapi
#define DECLSPEC_NORETURN
#define NOSERVICE 1
#define NOMCX 1
#define NOIME 1
#ifndef WINVER
# define WINVER 0x0502
#endif
#ifndef _WIN32_WINNT
# define _WIN32_WINNT 0x502
#endif

#define WINAPI_FAMILY_PARTITION(X) 1

#endif // __JITCC_H
