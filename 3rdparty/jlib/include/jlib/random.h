#ifndef JX_RANDOM_H
#define JX_RANDOM_H

#include <stdint.h>

typedef struct jx_allocator_i jx_allocator_i;

#ifdef __cplusplus
extern "C" {
#endif

// QuasiRandom Number Generator (float values)
typedef struct jx_qrngf_context_t jx_qrngf_context_t;

typedef struct jx_random_api
{
	jx_qrngf_context_t* (*qrngfCreateContext)(jx_allocator_i* allocator, uint32_t dims, float seed);
	void                (*qrngfDestroyContext)(jx_qrngf_context_t* ctx);
	const float*        (*qrngfGenerateNext)(jx_qrngf_context_t* ctx);
	void                (*qrngfGeneratePoint)(jx_qrngf_context_t* ctx, uint32_t ptID, float* pt);
	void                (*qrngfDiscardPoints)(jx_qrngf_context_t* ctx, uint32_t n);
	void                (*qrngfReset)(jx_qrngf_context_t* ctx, float seed);
	uint32_t            (*qrngfGetDimensions)(const jx_qrngf_context_t* ctx);
} jx_random_api;

extern jx_random_api* random_api;

#ifdef __cplusplus
}
#endif

#endif // JX_RANDOM_H
