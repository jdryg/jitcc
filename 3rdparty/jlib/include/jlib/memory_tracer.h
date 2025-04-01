#ifndef JX_MEMORY_TRACER_H
#define JX_MEMORY_TRACER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jx_allocator_handle_t
{
	uint32_t idx;
} jx_allocator_handle_t;

typedef struct jx_memory_tracer_api
{
	jx_allocator_handle_t(*createAllocator)(const char* name);
	void (*destroyAllocator)(jx_allocator_handle_t allocatorID);
	void (*onRealloc)(jx_allocator_handle_t allocatorID, void* ptr, void* newPtr, size_t newSize, const char* file, uint32_t line);
	void (*onModuleLoaded)(char* modulePath, uint64_t baseAddr);
} jx_memory_tracer_api;

extern jx_memory_tracer_api* memory_tracer_api;

#ifdef __cplusplus
}
#endif

#endif // JX_MEMORY_TRACER_H
