#ifndef JX_ATOMIC_H
#error "Must be included from jlib/atomic.h"
#endif

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <intrin.h>
#pragma intrinsic(_ReadWriteBarrier)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedExchangeAdd)
#pragma intrinsic(_InterlockedExchangeAdd64)
#pragma intrinsic(_mm_pause)
#define JX_MEMORY_BARRIER_ACQUIRE() _ReadWriteBarrier()
#define JX_MEMORY_BARRIER_RELEASE() _ReadWriteBarrier()

#ifdef __cplusplus
extern "C" {
#endif

static inline uint32_t jx_atomic_cmpSwap_u32(volatile uint32_t* dst, uint32_t swap, uint32_t cmp)
{
	return (uint32_t)_InterlockedCompareExchange((volatile long*)(dst), (long)swap, (long)cmp);
}

static inline uint64_t jx_atomic_cmpSwap_u64(volatile uint64_t* dst, uint64_t swap, uint64_t cmp)
{
	return _InterlockedCompareExchange64((volatile int64_t*)dst, swap, cmp);
}

static inline int32_t jx_atomic_add_i32(volatile int32_t* dst, int32_t value)
{
	return _InterlockedExchangeAdd((volatile long*)dst, value);
}

static inline int64_t jx_atomic_add_i64(volatile int64_t* dst, int64_t value)
{
	return _InterlockedExchangeAdd64((volatile long long*)dst, value);
}

static inline void jx_pause(void)
{
	_mm_pause();
}

#ifdef __cplusplus
}
#endif
