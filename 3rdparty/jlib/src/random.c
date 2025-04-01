#include <jlib/random.h>
#include <jlib/allocator.h>
#include <jlib/memory.h>
#include <jlib/math.h>

static jx_qrngf_context_t* _jx_qrngfCreateContext(jx_allocator_i* allocator, uint32_t dims, float seed);
static void _jx_qrngfDestroyContext(jx_qrngf_context_t* ctx);
static const float* _jx_qrngfGenerateNext(jx_qrngf_context_t* ctx);
static void _jx_qrngfGeneratePoint(jx_qrngf_context_t* ctx, uint32_t ptID, float* pt);
static void _jx_qrngfDiscardPoints(jx_qrngf_context_t* ctx, uint32_t n);
static void _jx_qrngfReset(jx_qrngf_context_t* ctx, float seed);
static uint32_t _jx_qrngfGetDimensions(const jx_qrngf_context_t* ctx);

jx_random_api* random_api = &(jx_random_api){
	.qrngfCreateContext = _jx_qrngfCreateContext,
	.qrngfDestroyContext = _jx_qrngfDestroyContext,
	.qrngfGenerateNext = _jx_qrngfGenerateNext,
	.qrngfGeneratePoint = _jx_qrngfGeneratePoint,
	.qrngfDiscardPoints = _jx_qrngfDiscardPoints,
	.qrngfReset = _jx_qrngfReset,
	.qrngfGetDimensions = _jx_qrngfGetDimensions,
};

/////////////////////////////////////////////////////////////////////////
// QuasiRandom Number Generator (float)
//
// Based on https://github.com/KRM7/quasi-random/blob/master/src/quasirand.hpp
//
typedef struct jx_qrngf_context_t
{
	jx_allocator_i* m_Allocator;
	float* m_Alpha;
	float* m_Point;
	float m_Seed;
	uint32_t m_Dimensions;
} jx_qrngf_context_t;

static float qrngfPhi(uint32_t dims, uint32_t n);

static jx_qrngf_context_t* _jx_qrngfCreateContext(jx_allocator_i* allocator, uint32_t dims, float seed)
{
	jx_qrngf_context_t* ctx = (jx_qrngf_context_t*)JX_ALLOC(allocator, sizeof(jx_qrngf_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_qrngf_context_t));
	ctx->m_Allocator = allocator;
	ctx->m_Dimensions = dims;
	ctx->m_Seed = seed;

	float* buffer = (float*)JX_ALLOC(allocator, sizeof(float) * dims * 2);
	if (!buffer) {
		JX_FREE(allocator, ctx);
		return NULL;
	}
	ctx->m_Alpha = &buffer[0];
	ctx->m_Point = &buffer[dims];

	const float phid = qrngfPhi(dims, 30);
	for (uint32_t i = 0; i < dims; ++i) {
		ctx->m_Alpha[i] = 1.0f / jx_powf(phid, (float)(i + 1));
		ctx->m_Point[i] = seed;
	}

	return ctx;
}

static void _jx_qrngfDestroyContext(jx_qrngf_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;
	JX_FREE(allocator, ctx->m_Alpha);
	JX_FREE(allocator, ctx);
}

static const float* _jx_qrngfGenerateNext(jx_qrngf_context_t* ctx)
{
	const uint32_t dim = ctx->m_Dimensions;
	for (uint32_t i = 0; i < dim; ++i) {
		ctx->m_Point[i] += ctx->m_Alpha[i];
		ctx->m_Point[i] -= (uint32_t)ctx->m_Point[i];
	}

	return ctx->m_Point;
}

static void _jx_qrngfGeneratePoint(jx_qrngf_context_t* ctx, uint32_t ptID, float* pt)
{
	const float seed = ctx->m_Seed;
	const uint32_t dim = ctx->m_Dimensions;
	for (uint32_t i = 0; i < dim; ++i) {
		pt[i] = seed + ctx->m_Alpha[i] * ptID;
		pt[i] -= (uint32_t)pt[i];
	}
}

static void _jx_qrngfDiscardPoints(jx_qrngf_context_t* ctx, uint32_t n)
{
	while (n--) {
		_jx_qrngfGenerateNext(ctx);
	}
}

static void _jx_qrngfReset(jx_qrngf_context_t* ctx, float seed)
{
	const uint32_t dim = ctx->m_Dimensions;
	for (uint32_t i = 0; i < dim; ++i) {
		ctx->m_Point[i] = seed;
	}
}

static uint32_t _jx_qrngfGetDimensions(const jx_qrngf_context_t* ctx)
{
	return ctx->m_Dimensions;
}

static float qrngfPhi(uint32_t dim, uint32_t n)
{
	const float exponent = 1.0f / ((float)dim + 1.0f);

	float phid = 1.0f;
	while (n--) {
		phid = jx_powf(1.0f + phid, exponent);
	}

	return phid;
}
