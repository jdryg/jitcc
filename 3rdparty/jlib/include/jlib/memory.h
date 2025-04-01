#ifndef JX_MEMORY_H
#define JX_MEMORY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jx_mem_api
{
	void (*copy)(void* dst, const void* src, size_t n);
	void (*move)(void* dst, const void* src, size_t n);
	void (*set)(void* dst, uint8_t ch, size_t n);
	int32_t (*cmp)(const void* lhs, const void* rhs, size_t n);
} jx_mem_api;

extern jx_mem_api* mem_api;

static void jx_memcpy(void* dst, const void* src, size_t n);
static void jx_memmove(void* dst, const void* src, size_t n);
static void jx_memset(void* dst, uint8_t ch, size_t n);
static int32_t jx_memcmp(const void* lhs, const void* rhs, size_t n);

static void jx_swap_u32(uint32_t* a, uint32_t* b);

static bool jx_isAlignedPtr(const void* ptr, uint64_t alignment);
static void* jx_alignPtr(void* ptr, uint64_t alignment);

#ifdef __cplusplus
}
#endif

#include "inline/memory.inl"

#endif // JX_MEMORY_H
