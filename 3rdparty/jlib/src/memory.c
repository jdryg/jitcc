#include <jlib/memory.h>
#include <jlib/cpu.h>

static void _jmem_copy(void* __restrict dst, const void* __restrict src, size_t n);
static void _jmem_move(void* __restrict dst, const void* __restrict src, size_t n);
static void _jmem_set_ref(void* dst, uint8_t ch, size_t n);
static int32_t _jmem_cmp(const void* __restrict lhs, const void* __restrict rhs, size_t n);

extern void _jmem_set_asm_win64_ermsb(void* dst, uint8_t ch, size_t n);
extern void _jmem_set_asm_win64(void* dst, uint8_t ch, size_t n);

jx_mem_api* mem_api = &(jx_mem_api) {
	.copy = _jmem_copy,
	.move = _jmem_move,
	.set = _jmem_set_ref,
	.cmp = _jmem_cmp
};

bool jx_mem_initAPI(void)
{
	const uint64_t cpuFeatures = cpu_api->getFeatures();
	if ((cpuFeatures & JX_CPU_FEATURE_SSE2) != 0) {
		if ((cpuFeatures & JX_CPU_FEATURE_ERMSB) != 0) {
			mem_api->set = _jmem_set_asm_win64_ermsb;
		} else {
			mem_api->set = _jmem_set_asm_win64;
		}
	}

	return true;
}

static void _jmem_copy(void* __restrict dstPtr, const void* __restrict srcPtr, size_t n)
{
	uint8_t* dst = (uint8_t*)dstPtr;
	const uint8_t* src = (uint8_t*)srcPtr;
	const uint8_t* end = dst + n;
	while (dst != end) {
		*dst++ = *src++;
	}
}

static void _jmem_move(void* __restrict dstPtr, const void* __restrict srcPtr, size_t n)
{
	uint8_t* dst = (uint8_t*)dstPtr;
	const uint8_t* src = (const uint8_t*)srcPtr;

	if (n == 0 || dst == src) {
		return;
	}

	if (dst < src) {
		jx_memcpy(dstPtr, srcPtr, n);
		return;
	}

	for (intptr_t ii = n - 1; ii >= 0; --ii) {
		dst[ii] = src[ii];
	}
}

static void _jmem_set_ref(void* dstPtr, uint8_t ch, size_t n)
{
	uint8_t* dst = (uint8_t*)dstPtr;
	const uint8_t* end = dst + n;
	while (dst != end) {
		*dst++ = ch;
	}
}

static int32_t _jmem_cmp(const void* __restrict lhsPtr, const void* __restrict rhsPtr, size_t n)
{
	if (lhsPtr == rhsPtr) {
		return 0;
	}

	const char* lhs = (const char*)lhsPtr;
	const char* rhs = (const char*)rhsPtr;
	for (; n > 0 && *lhs == *rhs; ++lhs, ++rhs, --n) {
	}

	return n == 0 
		? 0 
		: *lhs - *rhs
		;
}
