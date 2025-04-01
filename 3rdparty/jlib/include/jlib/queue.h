#ifndef JX_QUEUE_H
#define JX_QUEUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jx_allocator_i jx_allocator_i;

// Based on bx/spscqueue.h
// Copyright 2010-2025 Branimir Karadzic.
// https://github.com/bkaradzic/bx/blob/d3d403170e0153160227621b69ae75c60b3ae7b0/include/bx/spscqueue.h
typedef struct jx_queue_spsc_t jx_queue_spsc_t;
typedef struct jx_queue_spsc_blocking_t jx_queue_spsc_blocking_t;

jx_queue_spsc_t* jx_queue_spscCreate(jx_allocator_i* allocator);
void jx_queue_spscDestroy(jx_queue_spsc_t* queue);
void jx_queue_spscPush(jx_queue_spsc_t* queue, void* ptr);
void* jx_queue_spscPeek(jx_queue_spsc_t* queue);
void* jx_queue_spscPop(jx_queue_spsc_t* queue);

jx_queue_spsc_blocking_t* jx_queue_spscBlockingCreate(jx_allocator_i* allocator);
void jx_queue_spscBlockingDestroy(jx_queue_spsc_blocking_t* queue);
void jx_queue_spscBlockingPush(jx_queue_spsc_blocking_t* queue, void* ptr);
void* jx_queue_spscBlockingPeek(jx_queue_spsc_blocking_t* queue);
void* jx_queue_spscBlockingPop(jx_queue_spsc_blocking_t* queue, uint32_t msecs);

#ifdef __cplusplus
}
#endif

#endif // JX_QUEUE_H
