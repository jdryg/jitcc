/**
 * This file has no copyright assigned and is placed in the Public Domain.
 * This file is part of the w64 mingw-runtime package.
 * No warranty is given; refer to the file DISCLAIMER within this package.
 */
#ifndef _BASETSD_H_
#define _BASETSD_H_

typedef unsigned __int64 POINTER_64_INT;

#define POINTER_32
#define POINTER_64
#define FIRMWARE_PTR

typedef signed char INT8,*PINT8;
typedef signed short INT16,*PINT16;
typedef signed int INT32,*PINT32;
typedef signed __int64 INT64,*PINT64;
typedef unsigned char UINT8,*PUINT8;
typedef unsigned short UINT16,*PUINT16;
typedef unsigned int UINT32,*PUINT32;
typedef unsigned __int64 UINT64,*PUINT64;
typedef signed int LONG32,*PLONG32;
typedef unsigned int ULONG32,*PULONG32;
typedef unsigned int DWORD32,*PDWORD32;

#ifndef _W64
#define _W64
#endif

typedef __int64 INT_PTR,*PINT_PTR;
typedef unsigned __int64 UINT_PTR,*PUINT_PTR;
typedef __int64 LONG_PTR,*PLONG_PTR;
typedef unsigned __int64 ULONG_PTR,*PULONG_PTR;
#define __int3264 __int64

#define ADDRESS_TAG_BIT 0x40000000000ULL
typedef __int64 SHANDLE_PTR;
typedef unsigned __int64 HANDLE_PTR;
typedef unsigned int UHALF_PTR,*PUHALF_PTR;
typedef int HALF_PTR,*PHALF_PTR;

static inline unsigned long HandleToULong(const void *h) { return((unsigned long) (ULONG_PTR) h); }
static inline long HandleToLong(const void *h) { return((long) (LONG_PTR) h); }
static inline void *ULongToHandle(const unsigned long h) { return((void *) (UINT_PTR) h); }
static inline void *LongToHandle(const long h) { return((void *) (INT_PTR) h); }
static inline unsigned long PtrToUlong(const void *p) { return((unsigned long) (ULONG_PTR) p); }
static inline unsigned int PtrToUint(const void *p) { return((unsigned int) (UINT_PTR) p); }
static inline unsigned short PtrToUshort(const void *p) { return((unsigned short) (unsigned long) (ULONG_PTR) p); }
static inline long PtrToLong(const void *p) { return((long) (LONG_PTR) p); }
static inline int PtrToInt(const void *p) { return((int) (INT_PTR) p); }
static inline short PtrToShort(const void *p) { return((short) (long) (LONG_PTR) p); }
static inline void *IntToPtr(const int i) { return((void *)(INT_PTR)i); }
static inline void *UIntToPtr(const unsigned int ui) { return((void *)(UINT_PTR)ui); }
static inline void *LongToPtr(const long l) { return((void *)(LONG_PTR)l); }
static inline void *ULongToPtr(const unsigned long ul) { return((void *)(ULONG_PTR)ul); }

#define PtrToPtr64(p) ((void *) p)
#define Ptr64ToPtr(p) ((void *) p)
#define HandleToHandle64(h) (PtrToPtr64(h))
#define Handle64ToHandle(h) (Ptr64ToPtr(h))

static inline void *Ptr32ToPtr(const void *p) { return (void *)p; }
static inline void *Handle32ToHandle(const void *h) { return((void *) h); }
static inline void *PtrToPtr32(const void *p) { return((void *) (ULONG_PTR) p); }

#define HandleToHandle32(h) (PtrToPtr32(h))

#define HandleToUlong(h) HandleToULong(h)
#define UlongToHandle(ul) ULongToHandle(ul)
#define UlongToPtr(ul) ULongToPtr(ul)
#define UintToPtr(ui) UIntToPtr(ui)

#define MAXUINT_PTR (~((UINT_PTR)0))
#define MAXINT_PTR ((INT_PTR)(MAXUINT_PTR >> 1))
#define MININT_PTR (~MAXINT_PTR)

#define MAXULONG_PTR (~((ULONG_PTR)0))
#define MAXLONG_PTR ((LONG_PTR)(MAXULONG_PTR >> 1))
#define MINLONG_PTR (~MAXLONG_PTR)

#define MAXUHALF_PTR ((UHALF_PTR)~0)
#define MAXHALF_PTR ((HALF_PTR)(MAXUHALF_PTR >> 1))
#define MINHALF_PTR (~MAXHALF_PTR)

typedef ULONG_PTR SIZE_T,*PSIZE_T;
typedef LONG_PTR SSIZE_T,*PSSIZE_T;
typedef ULONG_PTR DWORD_PTR,*PDWORD_PTR;
typedef __int64 LONG64,*PLONG64;
typedef unsigned __int64 ULONG64,*PULONG64;
typedef unsigned __int64 DWORD64,*PDWORD64;
typedef ULONG_PTR KAFFINITY;
typedef KAFFINITY *PKAFFINITY;

#endif // _BASETSD_H_
