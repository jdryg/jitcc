#ifndef JX_MATH_SIMD_H
#error "Must be included from jx/math_simd.h"
#endif

#ifdef __cplusplus
#define JFLOAT32x4(xmm_reg) { (xmm_reg) }
#define JINT32x4(imm_reg) { (imm_reg) }
#define JFLOAT32x8(ymm_reg) { (ymm_reg) }
#define JINT32x8(ymm_reg) { (ymm_reg) }
#else
#define JFLOAT32x4(xmm_reg) (f32x4_t){ .xmm = (xmm_reg) }
#define JINT32x4(imm_reg) (i32x4_t){ .imm = (imm_reg) }
#define JFLOAT32x8(ymm_reg) (f32x8_t){ .ymm = (ymm_reg) }
#define JINT32x8(ymm_reg) (i32x8_t){ .ymm = (ymm_reg) }
#endif

static JX_FORCE_INLINE f32x4_t f32x4_zero(void)
{
	return JFLOAT32x4(_mm_setzero_ps());
}

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat(float x)
{
	return JFLOAT32x4(_mm_set_ps1(x));
}

static JX_FORCE_INLINE f32x4_t f32x4_from_i32x4(i32x4_t x)
{
	return JFLOAT32x4(_mm_cvtepi32_ps(x.imm));
}

#if defined(JX_MATH_SIMD_AVX) || defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x4_t f32x4_from_f32x8_low(f32x8_t x)
{
	return JFLOAT32x4(_mm256_extractf128_ps(x.ymm, 0));
}

static JX_FORCE_INLINE f32x4_t f32x4_from_f32x8_high(f32x8_t x)
{
	return JFLOAT32x4(_mm256_extractf128_ps(x.ymm, 1));
}
#endif // defined(JX_MATH_SIMD_AVX) || defined(JX_MATH_SIMD_AVX2)

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat4(float x0, float x1, float x2, float x3)
{
	return JFLOAT32x4(_mm_set_ps(x3, x2, x1, x0));
}

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat4va(const float* arr)
{
	return JFLOAT32x4(_mm_load_ps(arr));
}

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat4vu(const float* arr)
{
	return JFLOAT32x4(_mm_loadu_ps(arr));
}

static JX_FORCE_INLINE void f32x4_toFloat4va(f32x4_t x, float* arr)
{
	_mm_store_ps(arr, x.xmm);
}

static JX_FORCE_INLINE void f32x4_toFloat4vu(f32x4_t x, float* arr)
{
	_mm_storeu_ps(arr, x.xmm);
}

static JX_FORCE_INLINE f32x4_t f32x4_fromRGBA8(uint32_t rgba8)
{
	const __m128i imm_zero = _mm_setzero_si128();
	const __m128i imm_rgba8 = _mm_cvtsi32_si128(rgba8);
	const __m128i imm_rgba16 = _mm_unpacklo_epi8(imm_rgba8, imm_zero);
	const __m128i imm_rgba32 = _mm_unpacklo_epi16(imm_rgba16, imm_zero);
	return JFLOAT32x4(_mm_cvtepi32_ps(imm_rgba32));
}

static JX_FORCE_INLINE uint32_t f32x4_toRGBA8(f32x4_t x)
{
	const __m128i imm_zero = _mm_setzero_si128();
	const __m128i imm_rgba32 = _mm_cvtps_epi32(x.xmm);
	const __m128i imm_rgba16 = _mm_packs_epi32(imm_rgba32, imm_zero);
	const __m128i imm_rgba8 = _mm_packus_epi16(imm_rgba16, imm_zero);
	return (uint32_t)_mm_cvtsi128_si32(imm_rgba8);
}

static i32x4_t f32x4_castTo_i32x4(f32x4_t x)
{
	return JINT32x4(_mm_castps_si128(x.xmm));
}

static f32x4_t f32x4_cmpeq(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_cmpeq_ps(a.xmm, b.xmm));
}

static f32x4_t f32x4_cmpgt(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_cmpgt_ps(a.xmm, b.xmm));
}

static f32x4_t f32x4_cmple(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_cmple_ps(a.xmm, b.xmm));
}

static f32x4_t f32x4_cmplt(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_cmplt_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_ldexp(f32x4_t x, i32x4_t exp)
{
	static const JX_ALIGN_DECL(16, int32_t k127[4]) = { 127, 127, 127, 127 };
	const __m128i imm_exp_adjusted = _mm_add_epi32(exp.imm, *(__m128i*)k127);
	const __m128i imm_pow2n = _mm_slli_epi32(imm_exp_adjusted, 23);
	const __m128 xmm_pow2n = _mm_castsi128_ps(imm_pow2n);
	return JFLOAT32x4(_mm_mul_ps(x.xmm, xmm_pow2n));
}

static JX_FORCE_INLINE f32x4_t f32x4_add(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_add_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_sub(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_sub_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_mul(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_mul_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_div(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_div_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_min(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_min_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_max(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_max_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_xor(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_xor_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_and(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_and_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_or(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_or_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_andnot(f32x4_t a, f32x4_t b)
{
	return JFLOAT32x4(_mm_andnot_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_negate(f32x4_t x)
{
	static const JX_ALIGN_DECL(16, uint32_t kSignMsk[4]) = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
	return JFLOAT32x4(_mm_xor_ps(x.xmm, *(__m128*)kSignMsk));
}

static JX_FORCE_INLINE f32x4_t f32x4_abs(f32x4_t x)
{
	static const JX_ALIGN_DECL(16, uint32_t kInvSignMsk[4]) = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };
	return JFLOAT32x4(_mm_and_ps(x.xmm, *(__m128*)kInvSignMsk));
}

static JX_FORCE_INLINE f32x4_t f32x4_getSignMask(f32x4_t x)
{
	static const JX_ALIGN_DECL(16, uint32_t kSignMsk[4]) = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
	return JFLOAT32x4(_mm_and_ps(x.xmm, *(__m128*)kSignMsk));
}

// http://dss.stephanierct.com/DevBlog/?p=8
static JX_FORCE_INLINE f32x4_t f32x4_floor(f32x4_t x)
{
	return JFLOAT32x4(_mm_round_ps(x.xmm, _MM_FROUND_FLOOR));
}

// http://dss.stephanierct.com/DevBlog/?p=8
static JX_FORCE_INLINE f32x4_t f32x4_ceil(f32x4_t x)
{
	return JFLOAT32x4(_mm_round_ps(x.xmm, _MM_FROUND_CEIL));
}

// https://stackoverflow.com/questions/6996764/fastest-way-to-do-horizontal-sse-vector-sum-or-other-reduction
static JX_FORCE_INLINE float f32x4_hadd(f32x4_t x)
{
	__m128 shuf = _mm_shuffle_ps(x.xmm, x.xmm, _MM_SHUFFLE(2, 3, 0, 1));  // [ C D | A B ]
	__m128 sums = _mm_add_ps(x.xmm, shuf);      // sums = [ D+C C+D | B+A A+B ]
	shuf = _mm_movehl_ps(shuf, sums);           //  [   C   D | D+C C+D ]  // let the compiler avoid a mov by reusing shuf
	sums = _mm_add_ss(sums, shuf);
	return _mm_cvtss_f32(sums);
}

static JX_FORCE_INLINE f32x4_t f32x4_madd(f32x4_t a, f32x4_t b, f32x4_t c)
{
#if defined(JX_MATH_SIMD_FMA)
	return JFLOAT32x4(_mm_fmadd_ps(a.xmm, b.xmm, c.xmm));
#else
	return JFLOAT32x4(_mm_add_ps(c.xmm, _mm_mul_ps(a.xmm, b.xmm)));
#endif
}

static JX_FORCE_INLINE f32x4_t f32x4_msub(f32x4_t a, f32x4_t b, f32x4_t c)
{
#if defined(JX_MATH_SIMD_FMA)
	return JFLOAT32x4(_mm_fmsub_ps(a.xmm, b.xmm, c.xmm));
#else
	return JFLOAT32x4(_mm_sub_ps(_mm_mul_ps(a.xmm, b.xmm), c.xmm));
#endif
}

static JX_FORCE_INLINE f32x4_t f32x4_nmadd(f32x4_t a, f32x4_t b, f32x4_t c)
{
#if defined(JX_MATH_SIMD_FMA)
	return JFLOAT32x4(_mm_fnmadd_ps(a.xmm, b.xmm, c.xmm));
#else
	return JFLOAT32x4(_mm_sub_ps(c.xmm, _mm_mul_ps(a.xmm, b.xmm)));
#endif
}

static JX_FORCE_INLINE f32x4_t f32x4_sqrt(f32x4_t x)
{
	return JFLOAT32x4(_mm_sqrt_ps(x.xmm));
}

static JX_FORCE_INLINE float f32x4_getX(f32x4_t a)
{
	return _mm_cvtss_f32(a.xmm);
}

static JX_FORCE_INLINE float f32x4_getY(f32x4_t a)
{
	return _mm_cvtss_f32(_mm_shuffle_ps(a.xmm, a.xmm, _MM_SHUFFLE(1, 1, 1, 1)));
}

static JX_FORCE_INLINE float f32x4_getZ(f32x4_t a)
{
	return _mm_cvtss_f32(_mm_shuffle_ps(a.xmm, a.xmm, _MM_SHUFFLE(2, 2, 2, 2)));
}

static JX_FORCE_INLINE float f32x4_getW(f32x4_t a)
{
	return _mm_cvtss_f32(_mm_shuffle_ps(a.xmm, a.xmm, _MM_SHUFFLE(3, 3, 3, 3)));
}

#define f32x4_shuffle(a, b, mask) (f32x4_t){ .xmm = _mm_shuffle_ps(a.xmm, b.xmm, mask) }

#define JFLOAT32x4_GET_FUNC(swizzle) \
static JX_FORCE_INLINE f32x4_t f32x4_get##swizzle(f32x4_t x) \
{ \
	return JFLOAT32x4(_mm_shuffle_ps(x.xmm, x.xmm, (uint32_t)(JVEC4_SHUFFLE_##swizzle))); \
}

JFLOAT32x4_GET_FUNC(XXXX)
JFLOAT32x4_GET_FUNC(YYYY)
JFLOAT32x4_GET_FUNC(ZZZZ)
JFLOAT32x4_GET_FUNC(WWWW)
JFLOAT32x4_GET_FUNC(XYXY)
JFLOAT32x4_GET_FUNC(ZWZW)

static JX_FORCE_INLINE i32x4_t i32x4_zero(void)
{
	return JINT32x4(_mm_setzero_si128());
}

static JX_FORCE_INLINE i32x4_t i32x4_fromInt(int32_t x)
{
	return JINT32x4(_mm_set1_epi32(x));
}

static JX_FORCE_INLINE i32x4_t i32x4_from_f32x4(f32x4_t x)
{
	return JINT32x4(_mm_cvtps_epi32(x.xmm));
}

static JX_FORCE_INLINE i32x4_t i32x4_from_f32x4_truncate(f32x4_t x)
{
	return JINT32x4(_mm_cvttps_epi32(x.xmm));
}

static JX_FORCE_INLINE i32x4_t i32x4_fromInt4(int32_t x0, int32_t x1, int32_t x2, int32_t x3)
{
	return JINT32x4(_mm_set_epi32(x3, x2, x1, x0));
}

static JX_FORCE_INLINE i32x4_t i32x4_fromInt4va(const int32_t* arr)
{
	return JINT32x4(_mm_load_si128((const __m128i*)arr));
}

static JX_FORCE_INLINE void i32x4_toInt4vu(i32x4_t x, int32_t* arr)
{
	_mm_storeu_si128((__m128i*)arr, x.imm);
}

static JX_FORCE_INLINE void i32x4_toInt4va(i32x4_t x, int32_t* arr)
{
	_mm_store_si128((__m128i*)arr, x.imm);
}

static JX_FORCE_INLINE void i32x4_toInt4va_masked(i32x4_t x, i32x4_t mask, int32_t* buffer)
{
	// TODO: _mm_maskstore_epi32?
	const __m128i old = _mm_load_si128((const __m128i*)buffer);
	const __m128i oldMasked = _mm_andnot_si128(mask.imm, old);
	const __m128i newMasked = _mm_and_si128(mask.imm, x.imm);
	const __m128i final = _mm_or_si128(oldMasked, newMasked);
	_mm_store_si128((__m128i*)buffer, final);
}

static JX_FORCE_INLINE void i32x4_toInt4va_maskedInv(i32x4_t x, i32x4_t maskInv, int32_t* buffer)
{
	// TODO: _mm_maskstore_epi32?
	const __m128i old = _mm_load_si128((const __m128i*)buffer);
	const __m128i oldMasked = _mm_and_si128(maskInv.imm, old);
	const __m128i newMasked = _mm_andnot_si128(maskInv.imm, x.imm);
	const __m128i final = _mm_or_si128(oldMasked, newMasked);
	_mm_store_si128((__m128i*)buffer, final);
}

static JX_FORCE_INLINE void i32x4_toInt4vu_maskedInv(i32x4_t x, i32x4_t maskInv, int32_t* buffer)
{
	// TODO: _mm_maskstore_epi32?
	const __m128i old = _mm_lddqu_si128((const __m128i*)buffer);
	const __m128i oldMasked = _mm_and_si128(maskInv.imm, old);
	const __m128i newMasked = _mm_andnot_si128(maskInv.imm, x.imm);
	const __m128i final = _mm_or_si128(oldMasked, newMasked);
	_mm_storeu_si128((__m128i*)buffer, final);
}

static JX_FORCE_INLINE int32_t i32x4_toInt(i32x4_t x)
{
	return _mm_cvtsi128_si32(x.imm);
}

static JX_FORCE_INLINE f32x4_t i32x4_castTo_f32x4(i32x4_t x)
{
	return JFLOAT32x4(_mm_castsi128_ps(x.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_add(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_add_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_sub(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_sub_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_mullo(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_mullo_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_and(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_and_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_or(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_or_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_or3(i32x4_t a, i32x4_t b, i32x4_t c)
{
	return JINT32x4(_mm_or_si128(a.imm, _mm_or_si128(b.imm, c.imm)));
}

static JX_FORCE_INLINE i32x4_t i32x4_andnot(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_andnot_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_xor(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_xor_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_sar(i32x4_t x, uint32_t shift)
{
	return JINT32x4(_mm_srai_epi32(x.imm, shift));
}

static JX_FORCE_INLINE i32x4_t i32x4_sal(i32x4_t x, uint32_t shift)
{
	return JINT32x4(_mm_slli_epi32(x.imm, shift));
}

static JX_FORCE_INLINE i32x4_t i32x4_slr(i32x4_t x, uint32_t shift)
{
	return JINT32x4(_mm_srli_epi32(x.imm, shift));
}

static JX_FORCE_INLINE i32x4_t i32x4_cmplt(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_cmplt_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_packR32G32B32A32_to_RGBA8(i32x4_t r, i32x4_t g, i32x4_t b, i32x4_t a)
{
	const __m128i mask = _mm_set_epi8(15, 11, 7, 3, 14, 10, 6, 2, 13, 9, 5, 1, 12, 8, 4, 0);

	// Pack into uint8_t
	// (uint8_t){ r0, r1, r2, r3, g0, g1, g2, g3, b0, b1, b2, b3, a0, a1, a2, a3 }
	const __m128i imm_r0123_g0123_b0123_a0123_u8 = _mm_packus_epi16(
		_mm_packs_epi32(r.imm, g.imm), _mm_packs_epi32(b.imm, a.imm)
	);
	const __m128i imm_rgba_p0123_u8 = _mm_shuffle_epi8(imm_r0123_g0123_b0123_a0123_u8, mask);
	return JINT32x4(imm_rgba_p0123_u8);
}

static JX_FORCE_INLINE i32x4_t i32x4_cmpeq(i32x4_t a, i32x4_t b)
{
	return JINT32x4(_mm_cmpeq_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE bool i32x4_anyNegative(i32x4_t x)
{
	return (_mm_movemask_epi8(x.imm) & 0x8888) != 0;
}

static JX_FORCE_INLINE bool i32x4_allNegative(i32x4_t x)
{
#if 0
	return (_mm_movemask_epi8(x.imm) & 0x8888) == 0x8888;
#else
	return (_mm_movemask_ps(_mm_castsi128_ps(x.imm)) == 0x0F);
#endif
}

static JX_FORCE_INLINE uint32_t i32x4_getSignMask(i32x4_t x)
{
	return _mm_movemask_ps(_mm_castsi128_ps(x.imm));
}

#define JINT32x4_GET_FUNC(swizzle) \
static JX_FORCE_INLINE i32x4_t i32x4_get##swizzle(i32x4_t x) \
{ \
	return JINT32x4(_mm_shuffle_epi32(x.imm, (uint32_t)(JVEC4_SHUFFLE_##swizzle))); \
}

JINT32x4_GET_FUNC(XXXX);
JINT32x4_GET_FUNC(YYYY);
JINT32x4_GET_FUNC(ZZZZ);
JINT32x4_GET_FUNC(WWWW);
JINT32x4_GET_FUNC(XYXY);
JINT32x4_GET_FUNC(ZWZW);

static JX_FORCE_INLINE int32_t i32x4_getX(i32x4_t a)
{
	return _mm_cvtsi128_si32(a.imm);
}

static JX_FORCE_INLINE int32_t i32x4_getY(i32x4_t a)
{
	return _mm_cvtsi128_si32(_mm_shuffle_epi32(a.imm, JVEC4_SHUFFLE_YYYY));
}

static JX_FORCE_INLINE int32_t i32x4_getZ(i32x4_t a)
{
	return _mm_cvtsi128_si32(_mm_shuffle_epi32(a.imm, JVEC4_SHUFFLE_ZZZZ));
}

static JX_FORCE_INLINE int32_t i32x4_getW(i32x4_t a)
{
	return _mm_cvtsi128_si32(_mm_shuffle_epi32(a.imm, JVEC4_SHUFFLE_WWWW));
}

static JX_FORCE_INLINE f32x8_t f32x8_zero(void)
{
	return JFLOAT32x8(_mm256_setzero_ps());
}

static JX_FORCE_INLINE f32x8_t f32x8_fromFloat(float x)
{
	return JFLOAT32x8(_mm256_set1_ps(x));
}

static JX_FORCE_INLINE f32x8_t f32x8_from_i32x8(i32x8_t x)
{
	return JFLOAT32x8(_mm256_cvtepi32_ps(x.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_fromFloat8(float x0, float x1, float x2, float x3, float x4, float x5, float x6, float x7)
{
	return JFLOAT32x8(_mm256_set_ps(x7, x6, x5, x4, x3, x2, x1, x0));
}

static JX_FORCE_INLINE f32x8_t f32x8_fromFloat8va(const float* arr)
{
	return JFLOAT32x8(_mm256_load_ps(arr));
}

static JX_FORCE_INLINE f32x8_t f32x8_fromFloat8vu(const float* arr)
{
	return JFLOAT32x8(_mm256_loadu_ps(arr));
}

static JX_FORCE_INLINE void f32x8_toFloat8va(f32x8_t x, float* arr)
{
	_mm256_store_ps(arr, x.ymm);
}

static JX_FORCE_INLINE void f32x8_toFloat8vu(f32x8_t x, float* arr)
{
	_mm256_storeu_ps(arr, x.ymm);
}

static JX_FORCE_INLINE i32x8_t f32x8_castTo_i32x8(f32x8_t x)
{
	return JINT32x8(_mm256_castps_si256(x.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_cmpeq(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_cmp_ps(a.ymm, b.ymm, _CMP_EQ_OQ));
}

static JX_FORCE_INLINE f32x8_t f32x8_cmpgt(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_cmp_ps(a.ymm, b.ymm, _CMP_NLE_US));
}

static JX_FORCE_INLINE f32x8_t f32x8_cmple(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_cmp_ps(a.ymm, b.ymm, _CMP_LE_OS));
}

static JX_FORCE_INLINE f32x8_t f32x8_cmplt(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_cmp_ps(a.ymm, b.ymm, _CMP_LT_OS));
}

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE f32x8_t f32x8_ldexp(f32x8_t x, i32x8_t exp)
{
	static const JX_ALIGN_DECL(32, int32_t k127[8]) = { 127, 127, 127, 127, 127, 127, 127, 127 };
	const __m256i imm_exp_adjusted = _mm256_add_epi32(exp.ymm, *(__m256i*)k127);
	const __m256i imm_pow2n = _mm256_slli_epi32(imm_exp_adjusted, 23);
	const __m256 ymm_pow2n = _mm256_castsi256_ps(imm_pow2n);
	return JFLOAT32x8(_mm256_mul_ps(x.ymm, ymm_pow2n));
}
#endif

static JX_FORCE_INLINE f32x8_t f32x8_add(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_add_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_sub(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_sub_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_mul(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_mul_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_div(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_div_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_min(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_min_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_max(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_max_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_xor(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_xor_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_and(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_and_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_or(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_or_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_andnot(f32x8_t a, f32x8_t b)
{
	return JFLOAT32x8(_mm256_andnot_ps(a.ymm, b.ymm));
}

static JX_FORCE_INLINE f32x8_t f32x8_getSignMask(f32x8_t x)
{
	static const JX_ALIGN_DECL(32, uint32_t kSignMsk[8]) = { 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
	return JFLOAT32x8(_mm256_and_ps(x.ymm, *(__m256*)kSignMsk));
}

static JX_FORCE_INLINE f32x8_t f32x8_negate(f32x8_t x)
{
	static const JX_ALIGN_DECL(32, uint32_t kSignMsk[8]) = { 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
	return JFLOAT32x8(_mm256_xor_ps(x.ymm, *(__m256*)kSignMsk));
}

static JX_FORCE_INLINE f32x8_t f32x8_abs(f32x8_t x)
{
	static const JX_ALIGN_DECL(32, uint32_t kInvSignMsk[8]) = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };
	return JFLOAT32x8(_mm256_and_ps(x.ymm, *(__m256*)kInvSignMsk));
}

static JX_FORCE_INLINE f32x8_t f32x8_floor(f32x8_t x)
{
	return JFLOAT32x8(_mm256_round_ps(x.ymm, _MM_FROUND_FLOOR));
}

static JX_FORCE_INLINE f32x8_t f32x8_ceil(f32x8_t x)
{
	return JFLOAT32x8(_mm256_round_ps(x.ymm, _MM_FROUND_CEIL));
}

// https://stackoverflow.com/questions/6996764/fastest-way-to-do-horizontal-sse-vector-sum-or-other-reduction
static JX_FORCE_INLINE float f32x8_hadd(f32x8_t x)
{
	__m128 vlow = _mm256_castps256_ps128(x.ymm);
	__m128 vhigh = _mm256_extractf128_ps(x.ymm, 1); // high 128
	vlow = _mm_add_ps(vlow, vhigh);     // add the low 128

	__m128 shuf = _mm_movehdup_ps(vlow);        // broadcast elements 3,1 to 2,0
	__m128 sums = _mm_add_ps(vlow, shuf);
	shuf = _mm_movehl_ps(shuf, sums); // high half -> low half
	sums = _mm_add_ss(sums, shuf);
	return _mm_cvtss_f32(sums);
}

static JX_FORCE_INLINE f32x8_t f32x8_madd(f32x8_t a, f32x8_t b, f32x8_t c)
{
#if defined(JX_MATH_SIMD_FMA)
	return JFLOAT32x8(_mm256_fmadd_ps(a.ymm, b.ymm, c.ymm));
#else
	return JFLOAT32x8(_mm256_add_ps(c.ymm, _mm256_mul_ps(a.ymm, b.ymm)));
#endif
}

static f32x8_t f32x8_msub(f32x8_t a, f32x8_t b, f32x8_t c)
{
#if defined(JX_MATH_SIMD_FMA)
	return JFLOAT32x8(_mm256_fmsub_ps(a.ymm, b.ymm, c.ymm));
#else
	return JFLOAT32x8(_mm256_sub_ps(_mm256_mul_ps(a.ymm, b.ymm), c.ymm));
#endif
}

static JX_FORCE_INLINE f32x8_t f32x8_nmadd(f32x8_t a, f32x8_t b, f32x8_t c)
{
#if defined(JX_MATH_SIMD_FMA)
	return JFLOAT32x8(_mm256_fnmadd_ps(a.ymm, b.ymm, c.ymm));
#else
	return JFLOAT32x8(_mm256_sub_ps(c.ymm, _mm256_mul_ps(a.ymm, b.ymm)));
#endif
}

static JX_FORCE_INLINE f32x8_t f32x8_sqrt(f32x8_t x)
{
	return JFLOAT32x8(_mm256_sqrt_ps(x.ymm));
}

#define f32x8_permute(x, mask4) (f32x8_t){ .ymm = _mm256_permute_ps(x.ymm, mask4) }

static JX_FORCE_INLINE i32x8_t i32x8_zero(void)
{
	return JINT32x8(_mm256_setzero_si256());
}

static JX_FORCE_INLINE i32x8_t i32x8_fromInt(int32_t x)
{
	return JINT32x8(_mm256_set1_epi32(x));
}

static JX_FORCE_INLINE i32x8_t i32x8_from_f32x8(f32x8_t x)
{
	return JINT32x8(_mm256_cvtps_epi32(x.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_from_f32x8_truncate(f32x8_t x)
{
	return JINT32x8(_mm256_cvttps_epi32(x.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_fromInt8(int32_t x0, int32_t x1, int32_t x2, int32_t x3, int32_t x4, int32_t x5, int32_t x6, int32_t x7)
{
	return JINT32x8(_mm256_set_epi32(x7, x6, x5, x4, x3, x2, x1, x0));
}

static JX_FORCE_INLINE i32x8_t i32x8_fromInt8va(const int32_t* arr)
{
	return JINT32x8(_mm256_load_si256((const __m256i*)arr));
}

static JX_FORCE_INLINE void i32x8_toInt8vu(i32x8_t x, int32_t* arr)
{
	_mm256_storeu_si256((__m256i*)arr, x.ymm);
}

static JX_FORCE_INLINE void i32x8_toInt8va(i32x8_t x, int32_t* arr)
{
	_mm256_store_si256((__m256i*)arr, x.ymm);
}

static JX_FORCE_INLINE f32x8_t i32x8_castToVec8f(i32x8_t x)
{
	return JFLOAT32x8(_mm256_castsi256_ps(x.ymm));
}

#if defined(JX_MATH_SIMD_AVX2)
static JX_FORCE_INLINE void i32x8_toInt8va_masked(i32x8_t x, i32x8_t mask, int32_t* buffer)
{
#if 1
	_mm256_maskstore_epi32(buffer, mask.ymm, x.ymm);
#else
	const __m256i old = _mm256_load_si256((const __m256i*)buffer);
#if 1
	const __m256i final = _mm256_blendv_epi8(old, x.ymm, mask.ymm);
#else
	const __m256i oldMasked = _mm256_andnot_si256(mask.ymm, old);
	const __m256i newMasked = _mm256_and_si256(mask.ymm, x.ymm);
	const __m256i final = _mm256_or_si256(oldMasked, newMasked);
#endif
	_mm256_store_si256((__m256i*)buffer, final);
#endif
}

static JX_FORCE_INLINE void i32x8_toInt8va_maskedInv(i32x8_t x, i32x8_t maskInv, int32_t* buffer)
{
#if 1
	_mm256_maskstore_epi32(buffer, _mm256_xor_si256(maskInv.ymm, _mm256_set1_epi32(-1)), x.ymm);
#else
	const __m256i old = _mm256_load_si256((const __m256i*)buffer);
#if 1
	const __m256i final = _mm256_blendv_epi8(x.ymm, old, maskInv.ymm);
#else
	const __m256i oldMasked = _mm256_and_si256(maskInv.ymm, old);
	const __m256i newMasked = _mm256_andnot_si256(maskInv.ymm, x.ymm);
	const __m256i final = _mm256_or_si256(oldMasked, newMasked);
#endif
	_mm256_store_si256((__m256i*)buffer, final);
#endif
}

static JX_FORCE_INLINE void i32x8_toInt8vu_maskedInv(i32x8_t x, i32x8_t maskInv, int32_t* buffer)
{
#if 1
	_mm256_maskstore_epi32(buffer, _mm256_xor_si256(maskInv.ymm, _mm256_set1_epi32(-1)), x.ymm);
#else
	const __m256i old = _mm256_lddqu_si256((const __m256i*)buffer);
#if 0
	const __m256i final = _mm256_blendv_epi8(x.ymm, old, maskInv.ymm);
#else
	const __m256i oldMasked = _mm256_and_si256(maskInv.ymm, old);
	const __m256i newMasked = _mm256_andnot_si256(maskInv.ymm, x.ymm);
	const __m256i final = _mm256_or_si256(oldMasked, newMasked);
#endif
	_mm256_storeu_si256((__m256i*)buffer, final);
#endif
}

static JX_FORCE_INLINE i32x8_t i32x8_add(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_add_epi32(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_sub(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_sub_epi32(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_mullo(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_mullo_epi32(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_and(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_and_si256(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_or(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_or_si256(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_or3(i32x8_t a, i32x8_t b, i32x8_t c)
{
	return i32x8_or(a, i32x8_or(b, c));
}

static JX_FORCE_INLINE i32x8_t i32x8_andnot(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_andnot_si256(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_xor(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_xor_si256(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_sar(i32x8_t x, uint32_t shift)
{
	return JINT32x8(_mm256_srai_epi32(x.ymm, shift));
}

static JX_FORCE_INLINE i32x8_t i32x8_sal(i32x8_t x, uint32_t shift)
{
	return JINT32x8(_mm256_slli_epi32(x.ymm, shift));
}

static JX_FORCE_INLINE i32x8_t i32x8_slr(i32x8_t x, uint32_t shift)
{
	return JINT32x8(_mm256_srli_epi32(x.ymm, shift));
}

static JX_FORCE_INLINE i32x8_t i32x8_sll(i32x8_t x, uint32_t shift)
{
	return JINT32x8(_mm256_slli_epi32(x.ymm, shift));
}

static JX_FORCE_INLINE i32x8_t i32x8_sllv(i32x8_t x, i32x8_t shift)
{
	return JINT32x8(_mm256_sllv_epi32(x.ymm, shift.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_cmpeq(i32x8_t a, i32x8_t b)
{
	return JINT32x8(_mm256_cmpeq_epi32(a.ymm, b.ymm));
}

static JX_FORCE_INLINE i32x8_t i32x8_packR32G32B32A32_to_RGBA8(i32x8_t r, i32x8_t g, i32x8_t b, i32x8_t a)
{
#if 0
	i32x8_t rg = i32x8_or(r, i32x8_sll(g, 8));
	i32x8_t ba = i32x8_or(i32x8_sll(b, 16), i32x8_sll(a, 24));
	return i32x8_or(rg, ba);
#else
	// (uint16_t){
	//   r0, r1, r2, r3,
	//   g0, g1, g2, g3,
	//   r4, r5, r6, r7,
	//   g4, g5, g6, g7
	// }
	const __m256i r03_g03_r47_g47_u16 = _mm256_packs_epi32(r.ymm, g.ymm);
	
	// (uint16_t){
	//   b0, b1, b2, b3,
	//   a0, a1, a2, a3,
	//   b4, b5, b6, b7,
	//   a4, a5, a6, a7
	// }
	const __m256i b03_a03_b47_a47_u16 = _mm256_packs_epi32(b.ymm, a.ymm);

	// Pack into uint8_t
	// (uint8_t){ 
	//   r0, r1, r2, r3, 
	//   g0, g1, g2, g3, 
	//   b0, b1, b2, b3, 
	//   a0, a1, a2, a3, 
	//   r4, r5, r6, r7,
	//   g4, g5, g6, g7,
	//   b4, b5, b6, b7,
	//   a4, a5, a6, a7
	// };
	// 
	const __m256i r03_g03_b03_a03_r47_g47_b47_a47_u8 = _mm256_packus_epi16(r03_g03_r47_g47_u16, b03_a03_b47_a47_u16);

	static const uint8_t mask_u8[] = {
		0, 4, 8, 12,
		1, 5, 9, 13,
		2, 6, 10, 14,
		3, 7, 11, 15,
		
		0, 4, 8, 12,
		1, 5, 9, 13,
		2, 6, 10, 14,
		3, 7, 11, 15,
	};
	return JINT32x8(_mm256_shuffle_epi8(r03_g03_b03_a03_r47_g47_b47_a47_u8, _mm256_load_si256((const __m256i*)mask_u8)));
#endif
}
#endif // defined(JX_MATH_SIMD_AVX2)

static JX_FORCE_INLINE bool i32x8_anyNegative(i32x8_t x)
{
	return i32x8_getSignMask(x) != 0;
}

static JX_FORCE_INLINE bool i32x8_allNegative(i32x8_t x)
{
	return i32x8_getSignMask(x) == 0xFF;
}

static JX_FORCE_INLINE uint32_t i32x8_getSignMask(i32x8_t x)
{
	return _mm256_movemask_ps(_mm256_castsi256_ps(x.ymm));
}

static JX_FORCE_INLINE uint32_t i32x8_getByteSignMask(i32x8_t x)
{
	return _mm256_movemask_epi8(x.ymm);
}

#undef JFLOAT32x4
#undef JINT32x4
#undef JFLOAT32x8
#undef JINT32x8
