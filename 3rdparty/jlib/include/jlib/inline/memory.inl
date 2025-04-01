#ifndef JX_MEMORY_H
#error "Must be included from jx/memory.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline void jx_memcpy(void* dst, const void* src, size_t n)
{
	mem_api->copy(dst, src, n);
}

static inline void jx_memmove(void* dst, const void* src, size_t n)
{
	mem_api->move(dst, src, n);
}

static inline void jx_memset(void* dst, uint8_t ch, size_t n)
{
	mem_api->set(dst, ch, n);
}

static inline int32_t jx_memcmp(const void* lhs, const void* rhs, size_t n)
{
	return mem_api->cmp(lhs, rhs, n);
}

static inline void jx_swap_u32(uint32_t* a, uint32_t* b)
{
	uint32_t tmp = *a;
	*a = *b;
	*b = tmp;
}

static inline bool jx_isAlignedPtr(const void* ptr, uint64_t alignment)
{
	const uintptr_t mask = (uintptr_t)(alignment - 1);
	return ((uintptr_t)ptr & mask) == 0;
}

static inline void* jx_alignPtr(void* ptr, uint64_t alignment)
{
	const uintptr_t unalignedPtr = (uintptr_t)ptr;
	const uintptr_t alignMsk = (uintptr_t)(alignment - 1);
	const uintptr_t alignedPtr = (unalignedPtr + alignMsk) & ~alignMsk;
	return (void*)alignedPtr;
}
#ifdef __cplusplus
}
#endif
