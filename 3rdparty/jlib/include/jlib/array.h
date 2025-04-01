#ifndef JX_ARRAY_H
#define JX_ARRAY_H

#include <stdint.h>
#include "allocator.h"

#if 1
typedef uint32_t jx_array_size_t;
#else
typedef size_t jx_array_size_t;
#endif

// Based on stb_ds.h (https://github.com/nothings/stb/blob/master/stb_ds.h)
typedef struct jx_array_header
{
	jx_allocator_i* m_Allocator;
	jx_array_size_t m_Length;
	jx_array_size_t m_Capacity;
} jx_array_header;

#define jx_array_hdr(ptr)           ((jx_array_header*)(ptr) - 1)
#define jx_array_reserve(arr,n)     (jx_array_grow(arr, 0, n))
#define jx_array_resize(arr,n)      ((jx_array_capacity(arr) < (jx_array_size_t)(n) ? jx_array_reserve((arr),(jx_array_size_t)(n)),0 : 0), (arr) ? jx_array_hdr(arr)->m_Length = (jx_array_size_t) (n) : 0)
#define jx_array_capacity(arr)      ((arr) ? jx_array_hdr(arr)->m_Capacity : 0)
#define jx_array_size(arr)          ((arr) ? (ptrdiff_t)jx_array_hdr(arr)->m_Length : 0)
#define jx_array_sizeu(arr)         ((arr) ?            jx_array_hdr(arr)->m_Length : 0)
#define jx_array_push_back(arr,...) (jx_array_maybegrow(arr,1),(arr)[jx_array_hdr(arr)->m_Length++] = __VA_ARGS__)
#define jx_array_pop_back(arr)      (jx_array_hdr(arr)->m_Length--, (arr)[jx_array_hdr(arr)->m_Length])
#define jx_array_addnptr(arr,n)     (jx_array_maybegrow(arr,n), (n) ? (jx_array_hdr(arr)->m_Length += (n), &(arr)[jx_array_hdr(arr)->m_Length-(n)]) : (arr))
#define jx_array_addnindex(arr,n)   (jx_array_maybegrow(arr,n), (n) ? (jx_array_hdr(arr)->m_Length += (n), jx_array_hdr(arr)->m_Length-(n)) : jx_array_size(arr))
#define jx_array_last(arr)          ((arr)[jx_array_hdr(arr)->m_Length-1])
#define jx_array_free(arr)          ((void) ((arr) ? JX_FREE(jx_array_hdr(arr)->m_Allocator,jx_array_hdr(arr)) : (void)0), (arr)=NULL)
#define jx_array_del(arr,i)         jx_array_deln(arr,i,1)
#define jx_array_deln(arr,i,n)      (jx_memmove(&(arr)[i], &(arr)[(i)+(n)], sizeof *(arr) * (jx_array_hdr(arr)->m_Length-(n)-(i))), jx_array_hdr(arr)->m_Length -= (n))
#define jx_array_delswap(arr,i)     ((arr)[i] = jx_array_last(arr), jx_array_hdr(arr)->m_Length -= 1)
#define jx_array_insertn(arr,i,n)   (((void)(jx_array_addnindex(arr,n))), jx_memmove(&(arr)[(i)+(n)], &(arr)[i], sizeof *(arr) * (jx_array_hdr(arr)->m_Length-(n)-(i))))
#define jx_array_insert(arr,i,v)    (jx_array_insertn((arr),(i),1), (arr)[i]=(v))

#define jx_array_maybegrow(arr,n)   ((!(arr) || jx_array_hdr(arr)->m_Length + (n) > jx_array_hdr(arr)->m_Capacity) ? (jx_array_grow(arr,n,0),0) : 0)

#define jx_array_grow(a,b,c)        ((a) = jx_array_growf_wrapper((a), sizeof *(a), (b), (c)))

#ifdef __cplusplus
extern "C" static void* jx_array_growf(void* a, jx_array_size_t elemsize, jx_array_size_t addlen, jx_array_size_t min_cap);

template<typename T>
static T* jx_array_growf_wrapper(T* a, jx_array_size_t elemsize, jx_array_size_t addlen, jx_array_size_t min_cap)
{
	return (T*)jx_array_growf((void*)a, elemsize, addlen, min_cap);
}
#else
#define jx_array_growf_wrapper jx_array_growf
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#define jx_array_create(allocator) jx_array_create_ex(allocator, __FILE__, __LINE__)

static inline void* jx_array_create_ex(jx_allocator_i* allocator, const char* file, uint32_t line)
{
	void* buffer = allocator->realloc(allocator->m_Inst, NULL, sizeof(jx_array_header), 0, file, line);
	void* arr = (uint8_t*)buffer + sizeof(jx_array_header);
	
	jx_array_header* hdr = jx_array_hdr(arr);
	hdr->m_Allocator = allocator;
	hdr->m_Length = 0;
	hdr->m_Capacity = 0;
	return arr;
}

static inline void* jx_array_growf(void* a, jx_array_size_t elemsize, jx_array_size_t addlen, jx_array_size_t min_cap)
{
	jx_allocator_i* allocator = a == NULL
		? allocator_api->m_SystemAllocator
		: jx_array_hdr(a)->m_Allocator
		;

	void* b;
	jx_array_size_t min_len = jx_array_size(a) + addlen;

	// compute the minimum capacity needed
	if (min_len > min_cap) {
		min_cap = min_len;
	}

	if (min_cap <= jx_array_capacity(a)) {
		return a;
	}

	// increase needed capacity to guarantee O(1) amortized
	if (min_cap < 2 * jx_array_capacity(a)) {
		min_cap = 2 * jx_array_capacity(a);
	} else if (min_cap < 4) {
		min_cap = 4;
	}

	b = JX_REALLOC(allocator, (a) ? jx_array_hdr(a) : NULL, elemsize * min_cap + sizeof(jx_array_header));
	b = (char*)b + sizeof(jx_array_header);
	if (a == NULL) {
		jx_array_hdr(b)->m_Length = 0;
		jx_array_hdr(b)->m_Allocator = allocator;
	}
	jx_array_hdr(b)->m_Capacity = min_cap;

	return b;
}

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // JX_ARRAY_H
