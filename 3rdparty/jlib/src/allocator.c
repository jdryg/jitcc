#include <jlib/allocator.h>
#include <jlib/dbg.h>
#include <jlib/memory.h>
#include <jlib/macros.h>
#include <jlib/memory_tracer.h>
#include <malloc.h>

static void* _jallocator_sysRealloc(jx_allocator_o* a, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line);
static void _jallocator_frameTick(void);
static jx_allocator_i* _jallocator_createAllocator(const char* name);
static void _jallocator_destroyAllocator(jx_allocator_i* alloc);
static jx_allocator_i* _jallocator_createLinearAllocator(uint32_t initialSize, const jx_allocator_i* backingAllocator);
static jx_allocator_i* _jallocator_createLinearAllocatorWithBuffer(uint8_t* buffer, uint32_t sz, const jx_allocator_i* backingAllocator);
static void _jallocator_destroyLinearAllocator(jx_allocator_i* allocator);
static void _jallocator_linearAllocatorReset(jx_allocator_i* allocator);
static jx_allocator_i* _jallocator_createPoolAllocator(uint32_t itemSize, uint32_t itemsPerChunk, const jx_allocator_i* backingAllocator);
static jx_allocator_i* _jallocator_createPoolAllocatorWithBuffer(uint8_t* buffer, uint32_t sz, uint32_t itemSize, const jx_allocator_i* backingAllocator);
static void _jallocator_destroyPoolAllocator(jx_allocator_i* allocator);
static uint32_t _jallocator_poolAllocatorGetItemID(jx_allocator_i* allocator, void* itemPtr);
static void* _jallocator_poolAllocatorGetItemPtr(jx_allocator_i* allocator, uint32_t itemID);
static jx_allocator_i* _jallocator_createTracingAllocator(const char* name);
static void _jallocator_destroyTracingAllocator(jx_allocator_i* allocator);

jx_allocator_i* s_SystemAllocator = &(jx_allocator_i){
	.m_Inst = NULL,
	.realloc = _jallocator_sysRealloc
};

jx_allocator_api* allocator_api = &(jx_allocator_api){
	.m_SystemAllocator = NULL,
	.m_FrameAllocator = NULL,
	.frameTick = _jallocator_frameTick,
	.createAllocator = _jallocator_createAllocator,
	.destroyAllocator = _jallocator_destroyAllocator,
	.createLinearAllocator = _jallocator_createLinearAllocator,
	.createLinearAllocatorWithBuffer = _jallocator_createLinearAllocatorWithBuffer,
	.destroyLinearAllocator = _jallocator_destroyLinearAllocator,
	.linearAllocatorReset = _jallocator_linearAllocatorReset,
	.createPoolAllocator = _jallocator_createPoolAllocator,
	.createPoolAllocatorWithBuffer = _jallocator_createPoolAllocatorWithBuffer,
	.destroyPoolAllocator = _jallocator_destroyPoolAllocator,
	.poolAllocatorGetItemID = _jallocator_poolAllocatorGetItemID,
	.poolAllocatorGetItemPtr = _jallocator_poolAllocatorGetItemPtr
};

bool jx_allocator_initAPI(void)
{
	allocator_api->m_SystemAllocator = _jallocator_createAllocator("system");
	if (!allocator_api->m_SystemAllocator) {
		return false;
	}

	allocator_api->m_FrameAllocator = _jallocator_createLinearAllocator(JX_CONFIG_FRAME_ALLOCATOR_CHUNK_SIZE, allocator_api->m_SystemAllocator);
	if (!allocator_api->m_FrameAllocator) {
		return false;
	}

	return true;
}

void jx_allocator_shutdownAPI(void)
{
	if (allocator_api->m_FrameAllocator) {
		_jallocator_destroyLinearAllocator(allocator_api->m_FrameAllocator);
		allocator_api->m_FrameAllocator = NULL;
	}

	if (allocator_api->m_SystemAllocator) {
		_jallocator_destroyAllocator(allocator_api->m_SystemAllocator);
		allocator_api->m_SystemAllocator = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////
// Internal API
//
static void* _jallocator_sysRealloc(jx_allocator_o* allocator, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line)
{
	JX_UNUSED(allocator, file, line);

	if (sz == 0) {
		if (ptr != NULL) {
			if (align <= JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT) {
				free(ptr);
			} else {
				_aligned_free(ptr);
			}
		}

		return NULL;
	} else if (ptr == NULL) {
		return (align <= JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT)
			? malloc(sz)
			: _aligned_malloc(sz, align)
			;
	}

	return align <= JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT
		? realloc(ptr, sz)
		: _aligned_realloc(ptr, sz, align)
		;
}

static void _jallocator_frameTick(void)
{
	if (allocator_api->m_FrameAllocator) {
		_jallocator_linearAllocatorReset(allocator_api->m_FrameAllocator);
	}
}

static jx_allocator_i* _jallocator_createAllocator(const char* name)
{
	jx_allocator_i* allocator = NULL;

#if JX_CONFIG_TRACE_ALLOCATIONS
	allocator = _jallocator_createTracingAllocator(name);
#else
	JX_UNUSED(name);
	allocator = s_SystemAllocator;
#endif

	return allocator;
}

static void _jallocator_destroyAllocator(jx_allocator_i* allocator)
{
#if JX_CONFIG_TRACE_ALLOCATIONS
	_jallocator_destroyTracingAllocator(allocator);
#else
	JX_UNUSED(allocator);
#endif
}

//////////////////////////////////////////////////////////////////////////
// Linear allocator
//
#define JX_LINEAR_ALLOCATOR_FLAGS_ALLOW_RESIZE    (1u << 0)
#define JX_LINEAR_ALLOCATOR_FLAGS_FREE_ALLOCATOR  (1u << 1)

typedef struct _jx_linear_allocator_chunk
{
	struct _jx_linear_allocator_chunk* m_Next;
	uint8_t* m_Buffer;
	uint32_t m_Pos;
	uint32_t m_Capacity;
} _jx_linear_allocator_chunk;

typedef struct _jx_linear_allocator_o
{
	const jx_allocator_i* m_ParentAllocator;
	_jx_linear_allocator_chunk* m_FirstChunk;
	_jx_linear_allocator_chunk* m_LastChunk;
	uint32_t m_ChunkSize;
	uint32_t m_Flags;
} _jx_linear_allocator_o;

static void* _jallocator_linearRealloc(jx_allocator_o* allocator, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line);

static jx_allocator_i* _jallocator_createLinearAllocator(uint32_t chunkSize, const jx_allocator_i* backingAllocator)
{
	const jx_allocator_i* allocator = backingAllocator == NULL
		? allocator_api->m_SystemAllocator
		: backingAllocator
		;

	const size_t totalMem = 0
		+ sizeof(jx_allocator_i)
		+ sizeof(_jx_linear_allocator_o) 
		+ sizeof(_jx_linear_allocator_chunk) 
		+ (size_t)chunkSize
		;
	uint8_t* buffer = (uint8_t*)JX_ALLOC(allocator, totalMem);
	if (!buffer) {
		return NULL;
	}

	jx_allocator_i* linearAllocatorInterface = _jallocator_createLinearAllocatorWithBuffer(buffer, (uint32_t)totalMem, backingAllocator);
	if (linearAllocatorInterface) {
		_jx_linear_allocator_o* linearAllocator = (_jx_linear_allocator_o*)linearAllocatorInterface->m_Inst;
		linearAllocator->m_Flags |= JX_LINEAR_ALLOCATOR_FLAGS_FREE_ALLOCATOR;
		linearAllocator->m_ParentAllocator = allocator;
	}

	return linearAllocatorInterface;
}

static jx_allocator_i* _jallocator_createLinearAllocatorWithBuffer(uint8_t* buffer, uint32_t sz, const jx_allocator_i* backingAllocator)
{
	const size_t requiredMemory = 0
		+ sizeof(jx_allocator_i)
		+ sizeof(_jx_linear_allocator_o)
		+ sizeof(_jx_linear_allocator_chunk)
		+ 8 // Make sure there is at least 8 bytes of free memory for allocations
		;
	if (sz < requiredMemory) {
		JX_CHECK(false, "Linear allocator buffer too small.", 0);
		return NULL;
	}
	
	uint8_t* ptr = buffer;
	jx_allocator_i* linearAllocatorInterface = (jx_allocator_i*)ptr;
	ptr += sizeof(jx_allocator_i);

	_jx_linear_allocator_o* linearAllocator = (_jx_linear_allocator_o*)ptr;
	ptr += sizeof(_jx_linear_allocator_o);

	_jx_linear_allocator_chunk* linearAllocatorChunk = (_jx_linear_allocator_chunk*)ptr;
	ptr += sizeof(_jx_linear_allocator_chunk);

	linearAllocatorChunk->m_Buffer = ptr;
	linearAllocatorChunk->m_Next = NULL;
	linearAllocatorChunk->m_Pos = 0;
	linearAllocatorChunk->m_Capacity = sz - (uint32_t)(ptr - buffer);

	linearAllocator->m_ParentAllocator = backingAllocator;
	linearAllocator->m_FirstChunk = linearAllocatorChunk;
	linearAllocator->m_LastChunk = linearAllocatorChunk;
	linearAllocator->m_ChunkSize = sz;
	linearAllocator->m_Flags = 0
		| (backingAllocator != NULL ? JX_LINEAR_ALLOCATOR_FLAGS_ALLOW_RESIZE : 0)
		;

	*linearAllocatorInterface = (jx_allocator_i){
		.m_Inst = (jx_allocator_o*)linearAllocator,
		.realloc = _jallocator_linearRealloc
	};

	return linearAllocatorInterface;
}

static void _jallocator_destroyLinearAllocator(jx_allocator_i* allocator)
{
	_jx_linear_allocator_o* linearAllocator = (_jx_linear_allocator_o*)allocator->m_Inst;

	// NOTE: First chunk is always allocated with the allocator itself.
	// If there are any other chunks it means that they have been allocated with 'm_ParentAllocator'.
	_jx_linear_allocator_chunk* chunk = linearAllocator->m_FirstChunk->m_Next;
	while (chunk) {
		_jx_linear_allocator_chunk* nextChunk = chunk->m_Next;
		JX_FREE(linearAllocator->m_ParentAllocator, chunk);
		chunk = nextChunk;
	}

	if ((linearAllocator->m_Flags & JX_LINEAR_ALLOCATOR_FLAGS_FREE_ALLOCATOR) != 0) {
		JX_FREE(linearAllocator->m_ParentAllocator, allocator);
	}
}

static void _jallocator_linearAllocatorReset(jx_allocator_i* allocator)
{
	_jx_linear_allocator_o* linearAllocator = (_jx_linear_allocator_o*)allocator->m_Inst;
	
	_jx_linear_allocator_chunk* chunk = linearAllocator->m_FirstChunk;
	while (chunk) {
		chunk->m_Pos = 0;
		chunk = chunk->m_Next;
	}

	linearAllocator->m_LastChunk = linearAllocator->m_FirstChunk;
}

static void* _jallocator_linearRealloc(jx_allocator_o* inst, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line)
{
	JX_CHECK(sz < UINT32_MAX, "Linear allocator cannot allocate more than 4GB per allocation.", 0);

	if (ptr != NULL) {
		// Realloc or free.
		// - Reallocs are not supported.
		// - Frees are ignored.
		// TODO: Allow resizing/freeing the last allocated block? (see https://www.gingerbill.org/article/2019/02/08/memory-allocation-strategies-002/)
		JX_CHECK(sz == 0, "Linear allocators do not support reallocations.", 0);
		return NULL;
	}

	_jx_linear_allocator_o* allocator = (_jx_linear_allocator_o*)inst;
	_jx_linear_allocator_chunk* lastChunk = allocator->m_LastChunk;

	if (align < JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT) {
		align = JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT;
	}

	// Align buffer pointer to 'align'
	uint8_t* bufferPtr = jx_alignPtr(lastChunk->m_Buffer + lastChunk->m_Pos, align);

	const uint8_t* bufferEnd = lastChunk->m_Buffer + lastChunk->m_Capacity;
	if (bufferPtr + sz > bufferEnd) {
		if ((allocator->m_Flags & JX_LINEAR_ALLOCATOR_FLAGS_ALLOW_RESIZE) == 0) {
			// Resize not allowed.
			return NULL;
		}

		// TODO: Make sure the new 'bufferPtr' is properly aligned.
		if (lastChunk->m_Next == NULL) {
			// Allocate new chunk, insest into list, update 'lastChunk' pointer
			// and let the code below handle the actual allocation.
			const uint32_t chunkSize = allocator->m_ChunkSize < (uint32_t)sz ? (uint32_t)sz : allocator->m_ChunkSize;
			const size_t totalMem = 0
				+ sizeof(_jx_linear_allocator_chunk)
				+ (size_t)chunkSize
				;
			uint8_t* newBuffer = (uint8_t*)allocator->m_ParentAllocator->realloc(allocator->m_ParentAllocator->m_Inst, NULL, totalMem, 0, file, line);
			if (!newBuffer) {
				// Couldn't allocate new chunk. Allocation failed.
				return NULL;
			}

			uint8_t* newBufferPtr = newBuffer;
			_jx_linear_allocator_chunk* linearAllocatorChunk = (_jx_linear_allocator_chunk*)newBufferPtr;
			newBufferPtr += sizeof(_jx_linear_allocator_chunk);

			linearAllocatorChunk->m_Buffer = newBufferPtr;
			linearAllocatorChunk->m_Next = NULL;
			linearAllocatorChunk->m_Pos = 0;
			linearAllocatorChunk->m_Capacity = chunkSize;

			lastChunk->m_Next = linearAllocatorChunk;

			allocator->m_LastChunk = linearAllocatorChunk;
		} else {
			allocator->m_LastChunk = lastChunk->m_Next;
		}

		lastChunk = allocator->m_LastChunk;
		bufferPtr = lastChunk->m_Buffer;
	}

	JX_CHECK(jx_isAlignedPtr(bufferPtr, align), "Buffer is not aligned properly.", 0);
	lastChunk->m_Pos = (uint32_t)((bufferPtr + sz) - lastChunk->m_Buffer);
	return bufferPtr;
}

//////////////////////////////////////////////////////////////////////////
// Pool allocator
//
#define JX_POOL_ALLOCATOR_FLAGS_ALLOW_RESIZE    (1u << 0)
#define JX_POOL_ALLOCATOR_FLAGS_FREE_ALLOCATOR  (1u << 1)

typedef struct _jx_pool_allocator_freelist_item
{
	struct _jx_pool_allocator_freelist_item* m_Next;
} _jx_pool_allocator_freelist_item;

typedef struct _jx_pool_allocator_chunk
{
	uint8_t* m_Buffer;
	struct _jx_pool_allocator_chunk* m_Next;
} _jx_pool_allocator_chunk;

typedef struct _jx_pool_allocator_o
{
	const jx_allocator_i* m_ParentAllocator;
	_jx_pool_allocator_chunk* m_FirstChunk;
	_jx_pool_allocator_freelist_item* m_FirstFreeSlotPtr;
	uint32_t m_ItemSize;
	uint32_t m_NumItemsPerChunk;
	uint32_t m_Flags;
	JX_PAD(4);
} _jx_pool_allocator_o;

static void* _jallocator_poolRealloc(jx_allocator_o* allocator, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line);

static jx_allocator_i* _jallocator_createPoolAllocator(uint32_t itemSize, uint32_t numItemsPerChunk, const jx_allocator_i* backingAllocator)
{
	const jx_allocator_i* allocator = backingAllocator == NULL
		? allocator_api->m_SystemAllocator
		: backingAllocator
		;

	// Make sure there is enough space in each slot for the free list data.
	if (itemSize < sizeof(void*)) {
		itemSize = sizeof(void*);
	}

	const uint64_t chunkSize = (uint64_t)itemSize * (uint64_t)numItemsPerChunk;

	const size_t totalMem = 0
		+ sizeof(jx_allocator_i)
		+ sizeof(_jx_pool_allocator_o)
		+ sizeof(_jx_pool_allocator_chunk)
		+ chunkSize
		;
	uint8_t* buffer = (uint8_t*)JX_ALLOC(allocator, totalMem);
	if (!buffer) {
		return NULL;
	}

	jx_allocator_i* poolAllocatorInterface = _jallocator_createPoolAllocatorWithBuffer(buffer, (uint32_t)totalMem, itemSize, backingAllocator);
	if (poolAllocatorInterface) {
		_jx_pool_allocator_o* poolAllocator = (_jx_pool_allocator_o*)poolAllocatorInterface->m_Inst;
		poolAllocator->m_Flags |= JX_POOL_ALLOCATOR_FLAGS_FREE_ALLOCATOR;
		poolAllocator->m_ParentAllocator = allocator;
	}

	return poolAllocatorInterface;
}

static jx_allocator_i* _jallocator_createPoolAllocatorWithBuffer(uint8_t* buffer, uint32_t sz, uint32_t itemSize, const jx_allocator_i* backingAllocator)
{
	const size_t requiredMemory = 0
		+ sizeof(jx_allocator_i)
		+ sizeof(_jx_pool_allocator_o)
		+ sizeof(_jx_pool_allocator_chunk)
		+ itemSize // The buffer should be able to hold at least 1 item.
		;
	if (sz < requiredMemory) {
		JX_CHECK(false, "Pool allocator buffer too small.", 0);
		return NULL;
	}

	uint8_t* ptr = buffer;
	jx_allocator_i* poolAllocatorInterface = (jx_allocator_i*)ptr;
	ptr += sizeof(jx_allocator_i);

	_jx_pool_allocator_o* poolAllocator = (_jx_pool_allocator_o*)ptr;
	ptr += sizeof(_jx_pool_allocator_o);

	_jx_pool_allocator_chunk* poolAllocatorChunk = (_jx_pool_allocator_chunk*)ptr;
	ptr += sizeof(_jx_pool_allocator_chunk);

	// Initialize free list
	const uint32_t remainingSize = sz - (uint32_t)(ptr - buffer);
	const uint32_t numItemsPerChunk = remainingSize / itemSize;
	for (uint64_t i = 0; i < numItemsPerChunk - 1; ++i) {
		_jx_pool_allocator_freelist_item* fli = (_jx_pool_allocator_freelist_item*)(ptr + i * itemSize);
		fli->m_Next = (_jx_pool_allocator_freelist_item*)(ptr + (i + 1) * itemSize);
	}

	// Last item
	{
		_jx_pool_allocator_freelist_item* fli = (_jx_pool_allocator_freelist_item*)(ptr + (numItemsPerChunk - 1) * itemSize);
		fli->m_Next = NULL;
	}

	poolAllocatorChunk->m_Buffer = ptr;
	poolAllocatorChunk->m_Next = NULL;

	poolAllocator->m_FirstChunk = poolAllocatorChunk;
	poolAllocator->m_FirstFreeSlotPtr = (_jx_pool_allocator_freelist_item*)poolAllocatorChunk->m_Buffer;
	poolAllocator->m_ParentAllocator = backingAllocator;
	poolAllocator->m_ItemSize = itemSize;
	poolAllocator->m_NumItemsPerChunk = numItemsPerChunk;
	poolAllocator->m_Flags = 0
		| (backingAllocator != NULL ? JX_POOL_ALLOCATOR_FLAGS_ALLOW_RESIZE : 0)
		;

	*poolAllocatorInterface = (jx_allocator_i){
		.m_Inst = (jx_allocator_o*)poolAllocator,
		.realloc = _jallocator_poolRealloc
	};

	return poolAllocatorInterface;
}

static void _jallocator_destroyPoolAllocator(jx_allocator_i* allocator)
{
	_jx_pool_allocator_o* poolAllocator = (_jx_pool_allocator_o*)allocator->m_Inst;

	_jx_pool_allocator_chunk* chunk = poolAllocator->m_FirstChunk;
	while (chunk->m_Next) {
		_jx_pool_allocator_chunk* nextChunk = chunk->m_Next;
		JX_FREE(poolAllocator->m_ParentAllocator, chunk);
		chunk = nextChunk;
	}

	if ((poolAllocator->m_Flags & JX_POOL_ALLOCATOR_FLAGS_FREE_ALLOCATOR) != 0) {
		JX_FREE(poolAllocator->m_ParentAllocator, allocator);
	}
}

static uint32_t _jallocator_poolAllocatorGetItemID(jx_allocator_i* allocator, void* itemPtr)
{
	_jx_pool_allocator_o* poolAllocator = (_jx_pool_allocator_o*)allocator->m_Inst;

	const size_t chunkSize = poolAllocator->m_ItemSize * poolAllocator->m_NumItemsPerChunk;

	uint32_t firstChunkItemID = 0;

	uint32_t itemID = UINT32_MAX;

	_jx_pool_allocator_chunk* chunk = poolAllocator->m_FirstChunk;
	while (chunk) {
		if ((uintptr_t)itemPtr >= (uintptr_t)chunk->m_Buffer && (uintptr_t)itemPtr < (uintptr_t)(chunk->m_Buffer + chunkSize)) {
			const uintptr_t itemOffset = (uintptr_t)itemPtr - (uintptr_t)chunk->m_Buffer;
			JX_CHECK((itemOffset % poolAllocator->m_ItemSize) == 0, "Invalid item ptr");
			const uint32_t localItemID = (uint32_t)(itemOffset / poolAllocator->m_ItemSize);
			itemID = firstChunkItemID + localItemID;
			break;
		}

		firstChunkItemID += poolAllocator->m_NumItemsPerChunk;
		chunk = chunk->m_Next;
	}

	return itemID;
}

static void* _jallocator_poolAllocatorGetItemPtr(jx_allocator_i* allocator, uint32_t itemID)
{
	_jx_pool_allocator_o* poolAllocator = (_jx_pool_allocator_o*)allocator->m_Inst;

	void* ptr = NULL;

	_jx_pool_allocator_chunk* chunk = poolAllocator->m_FirstChunk;
	while (chunk) {
		if (itemID < poolAllocator->m_NumItemsPerChunk) {
			// TODO: Check if item is allocated. Return NULL if it's not.
			ptr = chunk->m_Buffer + (itemID * poolAllocator->m_ItemSize);
			break;
		}

		itemID -= poolAllocator->m_NumItemsPerChunk;
		chunk = chunk->m_Next;
	}

	return ptr;
}

static void* _jallocator_poolRealloc(jx_allocator_o* allocator, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line)
{
	JX_UNUSED(align);
	JX_CHECK(align <= JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT, "Pool allocators do not support alignment.", 0);

	_jx_pool_allocator_o* poolAllocator = (_jx_pool_allocator_o*)allocator;

	if (ptr != NULL) {
		// Realloc or free?
		if (sz != 0) {
			JX_CHECK(false, "Pool allocators do not support reallocations.", 0);
			return NULL;
		}

#if JX_CONFIG_DEBUG
		// Make sure this ptr has been allocated from this pool
		{
			bool found = false;
			const uint64_t chunkSize = poolAllocator->m_ItemSize * poolAllocator->m_NumItemsPerChunk;
			_jx_pool_allocator_chunk* chunk = poolAllocator->m_FirstChunk;
			while (chunk) {
				if ((uintptr_t)ptr >= (uintptr_t)chunk->m_Buffer && (uintptr_t)ptr < (uintptr_t)(chunk->m_Buffer + chunkSize)) {
					found = true;
				}

				chunk = chunk->m_Next;
			}

			JX_CHECK(found, "ptr does not belong to this pool.");
		}
#endif

		// Free item
		_jx_pool_allocator_freelist_item* freeSlot = (_jx_pool_allocator_freelist_item*)ptr;
		freeSlot->m_Next = poolAllocator->m_FirstFreeSlotPtr;
		poolAllocator->m_FirstFreeSlotPtr = freeSlot;

		return NULL;
	}

	// Alloc
	if ((uint32_t)sz != poolAllocator->m_ItemSize) {
		JX_CHECK(false, "Pool allocators cannot allocate arbitrary amounts of memory.", 0);
		return NULL;
	}

	_jx_pool_allocator_freelist_item* freeSlot = poolAllocator->m_FirstFreeSlotPtr;
	if (freeSlot == NULL) {
		if ((poolAllocator->m_Flags & JX_POOL_ALLOCATOR_FLAGS_ALLOW_RESIZE) == 0) {
			return NULL;
		}

		const size_t totalMem = 0
			+ sizeof(_jx_pool_allocator_chunk)
			+ (size_t)poolAllocator->m_ItemSize * (size_t)poolAllocator->m_NumItemsPerChunk
			;

		uint8_t* newBuffer = (uint8_t*)poolAllocator->m_ParentAllocator->realloc(poolAllocator->m_ParentAllocator->m_Inst, NULL, totalMem, 0, file, line);
		if (!newBuffer) {
			// Couldn't allocate new chunk. Allocation failed.
			return NULL;
		}

		uint8_t* newBufferPtr = newBuffer;
		_jx_pool_allocator_chunk* poolAllocatorChunk = (_jx_pool_allocator_chunk*)newBufferPtr;
		newBufferPtr += sizeof(_jx_pool_allocator_chunk);

		poolAllocatorChunk->m_Buffer = newBufferPtr;
		poolAllocatorChunk->m_Next = poolAllocator->m_FirstChunk;
		poolAllocator->m_FirstChunk = poolAllocatorChunk;

		// Initialize free list
		const uint32_t itemSize = poolAllocator->m_ItemSize;
		const uint32_t numItemsPerChunk = poolAllocator->m_NumItemsPerChunk;
		for (uint64_t i = 0; i < numItemsPerChunk - 1; ++i) {
			_jx_pool_allocator_freelist_item* fli = (_jx_pool_allocator_freelist_item*)(newBufferPtr + i * itemSize);
			fli->m_Next = (_jx_pool_allocator_freelist_item*)(newBufferPtr + (i + 1) * itemSize);
		}

		// Last item
		{
			_jx_pool_allocator_freelist_item* fli = (_jx_pool_allocator_freelist_item*)(newBufferPtr + (numItemsPerChunk - 1) * itemSize);
			fli->m_Next = NULL;
		}

		poolAllocator->m_FirstFreeSlotPtr = (_jx_pool_allocator_freelist_item*)poolAllocatorChunk->m_Buffer;
		
		freeSlot = poolAllocator->m_FirstFreeSlotPtr;
	}

	poolAllocator->m_FirstFreeSlotPtr = freeSlot->m_Next;
	
	return freeSlot;
}

//////////////////////////////////////////////////////////////////////////
// Tracing allocator
//
#define JX_TRACING_ALLOCATOR_FLAGS_INSIDE_ONREALLOC (1u << 0)

typedef struct _jx_tracing_allocator_o
{
	jx_allocator_handle_t m_AllocatorHandle;
	uint32_t m_Flags;
} _jx_tracing_allocator_o;

static void* _jallocator_tracingRealloc(jx_allocator_o* allocator, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line);

static jx_allocator_i* _jallocator_createTracingAllocator(const char* name)
{
	const size_t requiredMemory = 0
		+ sizeof(jx_allocator_i)
		+ sizeof(_jx_tracing_allocator_o)
		;

	uint8_t* buffer = (uint8_t*)JX_ALLOC(s_SystemAllocator, requiredMemory);
	if (!buffer) {
		return NULL;
	}

	uint8_t* ptr = buffer;
	jx_allocator_i* tracingAllocatorInterface = (jx_allocator_i*)ptr;
	ptr += sizeof(jx_allocator_i);

	_jx_tracing_allocator_o* tracingAllocator = (_jx_tracing_allocator_o*)ptr;
	ptr += sizeof(_jx_tracing_allocator_o);

	tracingAllocator->m_AllocatorHandle = memory_tracer_api->createAllocator(name);
	JX_CHECK(tracingAllocator->m_AllocatorHandle.idx != UINT32_MAX, "Failed to create tracing allocator %s.", name);
	tracingAllocator->m_Flags = 0;
	
	*tracingAllocatorInterface = (jx_allocator_i){
		.m_Inst = (jx_allocator_o*)tracingAllocator,
		.realloc = _jallocator_tracingRealloc
	};
	
	return tracingAllocatorInterface;
}

static void _jallocator_destroyTracingAllocator(jx_allocator_i* allocator)
{
	_jx_tracing_allocator_o* tracingAllocator = (_jx_tracing_allocator_o*)allocator->m_Inst;
	memory_tracer_api->destroyAllocator(tracingAllocator->m_AllocatorHandle);

	JX_FREE(s_SystemAllocator, allocator);
}

static void* _jallocator_tracingRealloc(jx_allocator_o* allocator, void* ptr, uint64_t sz, uint64_t align, const char* file, uint32_t line)
{
	_jx_tracing_allocator_o* tracingAllocator = (_jx_tracing_allocator_o*)allocator;
	void* newPtr = s_SystemAllocator->realloc(NULL, ptr, sz, align, file, line);

	// NOTE: Avoid reentering the memory tracer if this is a recursive call.
	// Might happen if the memory tracer allocates memory from a tracing allocator
	// (i.e. allocate a mutex from the OS API).
	if ((tracingAllocator->m_Flags & JX_TRACING_ALLOCATOR_FLAGS_INSIDE_ONREALLOC) == 0) {
		tracingAllocator->m_Flags |= JX_TRACING_ALLOCATOR_FLAGS_INSIDE_ONREALLOC;
		memory_tracer_api->onRealloc(tracingAllocator->m_AllocatorHandle, ptr, newPtr, sz, file, line);
		tracingAllocator->m_Flags &= ~JX_TRACING_ALLOCATOR_FLAGS_INSIDE_ONREALLOC;
	}

	return newPtr;
}
