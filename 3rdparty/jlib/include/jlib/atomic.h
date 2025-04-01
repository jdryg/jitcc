#ifndef JX_ATOMIC_H
#define JX_ATOMIC_H

#include <stdint.h>
#include <jlib/macros.h>

#ifdef __cplusplus
extern "C" {
#endif

// Atomically performs: 
//     if (*dst == cmp) { *dst = swap; }
// returns old *dst (so if sucessfull return cmp)
static uint32_t jx_atomic_cmpSwap_u32(volatile uint32_t* dst, uint32_t swap, uint32_t cmp);
static uint64_t jx_atomic_cmpSwap_u64(volatile uint64_t* dst, uint64_t swap, uint64_t cmp);

// Atomically performs: 
//     tmp = *dst; *dst += value; return tmp;
static int32_t jx_atomic_add_i32(volatile int32_t* dst, int32_t value);
static int64_t jx_atomic_add_i64(volatile int64_t* dst, int64_t value);

static void jx_pause(void);

#ifdef __cplusplus
}
#endif

#if JX_PLATFORM_WINDOWS
#include "inline/atomic_win32.inl"
#else
#error "Unknown platform"
#endif

#endif // JX_ATOMIC_H
