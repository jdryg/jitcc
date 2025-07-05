#ifndef _STDDEF
#define _STDDEF

#define NULL ((void*)0)

typedef unsigned long long size_t;
typedef long long ptrdiff_t;
typedef unsigned short wchar_t;
typedef long long max_align_t;

typedef long long ptrdiff_t;
typedef long long intptr_t;
typedef unsigned long long uintptr_t;

#define offsetof(type, field) ((size_t)&((type*)0)->field)

#endif // _STDDEF
