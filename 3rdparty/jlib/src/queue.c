#include <jlib/queue.h>
#include <jlib/allocator.h>
#include <jlib/memory.h>
#include <jlib/macros.h>
#include <jlib/os.h>

typedef struct jqueue_node jqueue_node;

typedef struct jqueue_node
{
	void* m_Ptr;
	jqueue_node* m_Next;
} jqueue_node;

typedef struct jx_queue_spsc_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_NodeAllocator;
	jqueue_node* m_First;
	jqueue_node* m_Divider;
	jqueue_node* m_Last;
} jx_queue_spsc_t;

static jqueue_node* _jqueueNodeAlloc(jx_allocator_i* allocator, void* ptr);
static void _jqueueNodeFree(jqueue_node* node, jx_allocator_i* allocator);

jx_queue_spsc_t* jx_queue_spscCreate(jx_allocator_i* allocator)
{
	jx_queue_spsc_t* q = (jx_queue_spsc_t*)JX_ALLOC(allocator, sizeof(jx_queue_spsc_t));
	if (!q) {
		return NULL;
	}

	jx_memset(q, 0, sizeof(jx_queue_spsc_t));
	q->m_Allocator = allocator;
	q->m_NodeAllocator = allocator_api->createPoolAllocator(sizeof(jqueue_node), 64, allocator);
	q->m_First = _jqueueNodeAlloc(q->m_NodeAllocator, NULL);
	q->m_Divider = q->m_First;
	q->m_Last = q->m_First;

	return q;
}

void jx_queue_spscDestroy(jx_queue_spsc_t* queue)
{
	jx_allocator_i* allocator = queue->m_Allocator;
	jx_allocator_i* nodeAllocator = queue->m_NodeAllocator;

	while (queue->m_First != NULL) {
		jqueue_node* node = queue->m_First;
		queue->m_First = node->m_Next;
		_jqueueNodeFree(node, nodeAllocator);
	}

	allocator_api->destroyPoolAllocator(nodeAllocator);
	JX_FREE(allocator, queue);
}

void jx_queue_spscPush(jx_queue_spsc_t* queue, void* ptr)
{
	queue->m_Last->m_Next = _jqueueNodeAlloc(queue->m_NodeAllocator, ptr);
#if 0
	atomicExchangePtr((void**)&queue->m_Last, queue->m_Last->m_Next);
#else
#if JX_COMPILER_MSVC
	_InterlockedExchangePointer((void**)&queue->m_Last, queue->m_Last->m_Next);
#else
	// TODO: 
#endif
#endif
	while (queue->m_First != queue->m_Divider) {
		jqueue_node* node = queue->m_First;
		queue->m_First = queue->m_First->m_Next;
		_jqueueNodeFree(node, queue->m_NodeAllocator);
	}
}

void* jx_queue_spscPeek(jx_queue_spsc_t* queue)
{
	if (queue->m_Divider != queue->m_Last) {
		return queue->m_Divider->m_Next->m_Ptr;
	}

	return NULL;
}

void* jx_queue_spscPop(jx_queue_spsc_t* queue)
{
	if (queue->m_Divider != queue->m_Last) {
		void* ptr = queue->m_Divider->m_Next->m_Ptr;
#if 0
		atomicExchangePtr((void**)&queue->m_Divider, queue->m_Divider->m_Next);
#else
#if JX_COMPILER_MSVC
		_InterlockedExchangePointer((void**)&queue->m_Divider, queue->m_Divider->m_Next);
#else
		// TODO: 
#endif
#endif
		return ptr;
	}

	return NULL;
}

typedef struct jx_queue_spsc_blocking_t
{
	jx_allocator_i* m_Allocator;
	jx_os_semaphore_t* m_Count;
	jx_queue_spsc_t* m_Queue;
} jx_queue_spsc_blocking_t;

jx_queue_spsc_blocking_t* jx_queue_spscBlockingCreate(jx_allocator_i* allocator)
{
	jx_queue_spsc_blocking_t* q = (jx_queue_spsc_blocking_t*)JX_ALLOC(allocator, sizeof(jx_queue_spsc_blocking_t));
	if (!q) {
		return NULL;
	}

	jx_memset(q, 0, sizeof(jx_queue_spsc_blocking_t));
	q->m_Allocator = allocator;
	q->m_Queue = jx_queue_spscCreate(allocator);
	if (!q->m_Queue) {
		jx_queue_spscBlockingDestroy(q);
		return NULL;
	}
	q->m_Count = os_api->semaphoreCreate();
	if (!q->m_Count) {
		jx_queue_spscBlockingDestroy(q);
		return NULL;
	}

	return q;
}

void jx_queue_spscBlockingDestroy(jx_queue_spsc_blocking_t* queue)
{
	jx_allocator_i* allocator = queue->m_Allocator;

	if (queue->m_Count) {
		os_api->semaphoreDestroy(queue->m_Count);
		queue->m_Count = NULL;
	}

	if (queue->m_Queue) {
		jx_queue_spscDestroy(queue->m_Queue);
		queue->m_Queue = NULL;
	}

	JX_FREE(allocator, queue);
}

void jx_queue_spscBlockingPush(jx_queue_spsc_blocking_t* queue, void* ptr)
{
	jx_queue_spscPush(queue->m_Queue, ptr);
	os_api->semaphoreSignal(queue->m_Count, 1);
}

void* jx_queue_spscBlockingPeek(jx_queue_spsc_blocking_t* queue)
{
	return jx_queue_spscPeek(queue->m_Queue);
}

void* jx_queue_spscBlockingPop(jx_queue_spsc_blocking_t* queue, uint32_t msecs)
{
	if (os_api->semaphoreWait(queue->m_Count, msecs)) {
		return jx_queue_spscPop(queue->m_Queue);
	}

	return NULL;
}

static jqueue_node* _jqueueNodeAlloc(jx_allocator_i* allocator, void* ptr)
{
	jqueue_node* node = (jqueue_node*)JX_ALLOC(allocator, sizeof(jqueue_node));
	if (!node) {
		return NULL;
	}

	node->m_Ptr = ptr;
	node->m_Next = NULL;

	return node;
}

static void _jqueueNodeFree(jqueue_node* node, jx_allocator_i* allocator)
{
	JX_FREE(allocator, node);
}
