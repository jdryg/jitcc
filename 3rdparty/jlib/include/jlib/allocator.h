#ifndef JX_ALLOCATOR_H
#define JX_ALLOCATOR_H

#include <stdint.h>
#include <stdbool.h>
#include "macros.h"

#ifdef __cplusplus
#include <new.h>
extern "C" {
#endif // __cplusplus

typedef struct jx_allocator_o jx_allocator_o;

typedef struct jx_allocator_i
{
	jx_allocator_o* m_Inst;

	void* (*realloc)(jx_allocator_o* allocator, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line);
} jx_allocator_i;

#define JX_ALLOC(allocator, sz)                        (allocator)->realloc(allocator->m_Inst, NULL, sz, 0, __FILE__, __LINE__)
#define JX_FREE(allocator, ptr)                        (void)(allocator)->realloc(allocator->m_Inst, ptr, 0, 0, __FILE__, __LINE__)
#define JX_REALLOC(allocator, ptr, sz)                 (allocator)->realloc(allocator->m_Inst, ptr, sz, 0, __FILE__, __LINE__)
#define JX_ALIGNED_ALLOC(allocator, sz, align)         (allocator)->realloc(allocator->m_Inst, NULL, sz, align, __FILE__, __LINE__)
#define JX_ALIGNED_FREE(allocator, ptr, align)         (void)(allocator)->realloc(allocator->m_Inst, ptr, 0, align, __FILE__, __LINE__)
#define JX_ALIGNED_REALLOC(allocator, ptr, sz, align)  (allocator)->realloc(allocator->m_Inst, ptr, sz, align, __FILE__, __LINE__)

#ifdef __cplusplus
#define JX_PLACEMENT_NEW(ptr, type)                    ::new(ptr) type
#define JX_NEW(allocator, type)                        JX_PLACEMENT_NEW(JX_ALLOC(allocator, sizeof(type)), type)
#define JX_DELETE(allocator, ptr)                      jx_deleteObject(allocator, ptr)
#endif // __cplusplus

#define JX_FRAME_ALLOC(sz)                             allocator_api->m_FrameAllocator->realloc(allocator_api->m_FrameAllocator->m_Inst, NULL, sz, 0, __FILE__, __LINE__)
#define JX_FRAME_ALIGNED_ALLOC(sz, align)              allocator_api->m_FrameAllocator->realloc(allocator_api->m_FrameAllocator->m_Inst, NULL, sz, align, __FILE__, __LINE__)

typedef struct jx_allocator_api
{
	jx_allocator_i* m_SystemAllocator;
	jx_allocator_i* m_FrameAllocator;

	void            (*frameTick)(void);

	jx_allocator_i* (*createAllocator)(const char* name);
	void            (*destroyAllocator)(jx_allocator_i* allocator);

	// Initialize a linear allocator with an initial capacity of 'chunkSize'.
	// - If 'backingAllocator' is not NULL, the allocator will allocate a new 'chunkSize' buffer
	//   every time the last buffer is full.
	// - If 'backingAllocator' is NULL, all allocations beyond the initial capacity will fail.
	// 
	// 'backingAllocator' is used for allocating all required internal memory. If 'backingAllocator'
	// is NULL, the system allocator will be used instead.
	// 
	// Linear allocators do not support reallocations (will assert in debug mode and will always 
	// return a NULL pointer). Frees are silently ignored. You have to destroy the allocator in order
	// to free the allocated memory.
	//
	// WARNING: Linear allocators are not thread-safe. The caller is expected to limit access to one thread at a time.
	jx_allocator_i* (*createLinearAllocator)(uint32_t chunkSize, const jx_allocator_i* backingAllocator);

	// Same as 'createLinearAllocator' but uses 'buffer' as the initial chunk of memory.
	//
	// The initially available memory from this allocator will be less than the specified 'sz' because
	// part of the buffer is used for the internal representation of the allocator.
	jx_allocator_i* (*createLinearAllocatorWithBuffer)(uint8_t* buffer, uint32_t sz, const jx_allocator_i* backingAllocator);

	// Destroy a linear allocator by deallocating all its buffers.
	void            (*destroyLinearAllocator)(jx_allocator_i* allocator);

	void            (*linearAllocatorReset)(jx_allocator_i* allocator);

	// Initialize a pool allocator for 'numItemsPerChunk' items, each with size 'itemSize'.
	// 
	// If 'backingAllocator' is NULL the system allocator will be used.
	//
	// Pool allocators allow only for single item allocations. Each allocation should be 'itemSize' in bytes.
	// Pool allocators do not support arbitrary alignment. The alignment is assumed to be equal to 'itemSize',
	// i.e. all items are tightly packed into a continuous buffer.
	//
	// Caveat: Pool allocators keep their free list structure inside the item buffers. If 'itemSize' is less than
	// the size of a pointer (sizeof(void*)), it will be set to this value.
	//
	// Pool allocators grow indefinitely, i.e. individual chunks of memory are never freed when they become empty.
	// E.g. If you allocate N items and free them all, the memory footprint of the allocator will remain 
	// equal to that when all objects were alive. In order to completely destroy a pool allocator, you must call
	// destroyPoolAllocator().
	//
	// WARNING: Pool allocators are not thread-safe. The caller is expected to limit access to one thread at a time.
	jx_allocator_i* (*createPoolAllocator)(uint32_t itemSize, uint32_t numItemsPerChunk, const jx_allocator_i* backingAllocator);

	// Same as 'createPoolAllocator' but uses 'buffer' as the initial chunk of memory.
	//
	// The number of items per chunk is calculated by first subtracting the required memory for the internal
	// representation of the allocator.
	jx_allocator_i* (*createPoolAllocatorWithBuffer)(uint8_t* buffer, uint32_t sz, uint32_t itemSize, const jx_allocator_i* backingAllocator);

	// Destroy a pool allocator by deallocating all its buffers.
	void            (*destroyPoolAllocator)(jx_allocator_i* allocator);

	// Returns UINT32_MAX if the item is not in the allocator's allocated memory
	uint32_t        (*poolAllocatorGetItemID)(jx_allocator_i* allocator, void* itemPtr);

	// Returns NULL if the item is not allocated.
	void*           (*poolAllocatorGetItemPtr)(jx_allocator_i* allocator, uint32_t itemID);
} jx_allocator_api;

extern jx_allocator_api* allocator_api;

#ifdef __cplusplus
}

template <typename ObjectT>
inline void jx_deleteObject(jx_allocator_i* allocator, ObjectT* object)
{
	if (object) {
		object->~ObjectT();
		JX_FREE(allocator, object);
	}
}
#endif // __cplusplus

#endif // JX_ALLOCATOR_H
