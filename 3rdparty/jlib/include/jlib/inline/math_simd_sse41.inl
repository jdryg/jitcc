#ifndef JX_MATH_SIMD_H
#error "Must be included from jx/math_simd.h"
#endif

#ifdef __cplusplus
#define JVEC4F(xmm_reg) { (xmm_reg) }
#define JVEC4I(imm_reg) { (imm_reg) }
#else
#define JVEC4F(xmm_reg) (f32x4_t){ .xmm = (xmm_reg) }
#define JVEC4I(imm_reg) (i32x4_t){ .imm = (imm_reg) }
#endif

static JX_FORCE_INLINE f32x4_t f32x4_zero(void)
{
	return JVEC4F(_mm_setzero_ps());
}

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat(float x)
{
	return JVEC4F(_mm_set_ps1(x));
}

static JX_FORCE_INLINE f32x4_t f32x4_from_i32x4(i32x4_t x)
{
	return JVEC4F(_mm_cvtepi32_ps(x.imm));
}

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat4(float x0, float x1, float x2, float x3)
{
	return JVEC4F(_mm_set_ps(x3, x2, x1, x0));
}

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat4va(const float* arr)
{
	return JVEC4F(_mm_load_ps(arr));
}

static JX_FORCE_INLINE f32x4_t f32x4_fromFloat4vu(const float* arr)
{
	return JVEC4F(_mm_loadu_ps(arr));
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
	return JVEC4F(_mm_cvtepi32_ps(imm_rgba32));
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
	return JVEC4I(_mm_castps_si128(x.xmm));
}

static f32x4_t f32x4_cmpeq(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_cmpeq_ps(a.xmm, b.xmm));
}

static f32x4_t f32x4_cmpgt(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_cmpgt_ps(a.xmm, b.xmm));
}

static f32x4_t f32x4_cmple(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_cmple_ps(a.xmm, b.xmm));
}

static f32x4_t f32x4_cmplt(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_cmplt_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_ldexp(f32x4_t x, i32x4_t exp)
{
	static const JX_ALIGN_DECL(16, int32_t k127[4]) = { 127, 127, 127, 127 };
	const __m128i imm_exp_adjusted = _mm_add_epi32(exp.imm, *(__m128i*)k127);
	const __m128i imm_pow2n = _mm_slli_epi32(imm_exp_adjusted, 23);
	const __m128 xmm_pow2n = _mm_castsi128_ps(imm_pow2n);
	return JVEC4F(_mm_mul_ps(x.xmm, xmm_pow2n));
}

static JX_FORCE_INLINE f32x4_t f32x4_add(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_add_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_sub(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_sub_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_mul(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_mul_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_div(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_div_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_min(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_min_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_max(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_max_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_xor(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_xor_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_and(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_and_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_or(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_or_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_andnot(f32x4_t a, f32x4_t b)
{
	return JVEC4F(_mm_andnot_ps(a.xmm, b.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_negate(f32x4_t x)
{
	static const JX_ALIGN_DECL(16, uint32_t kSignMsk[4]) = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
	return JVEC4F(_mm_xor_ps(x.xmm, *(__m128*)kSignMsk));
}

static JX_FORCE_INLINE f32x4_t f32x4_abs(f32x4_t x)
{
	static const JX_ALIGN_DECL(16, uint32_t kInvSignMsk[4]) = { 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF };
	return JVEC4F(_mm_and_ps(x.xmm, *(__m128*)kInvSignMsk));
}

static JX_FORCE_INLINE f32x4_t f32x4_getSignMask(f32x4_t x)
{
	static const JX_ALIGN_DECL(16, uint32_t kSignMsk[4]) = { 0x80000000, 0x80000000, 0x80000000, 0x80000000 };
	return JVEC4F(_mm_and_ps(x.xmm, *(__m128*)kSignMsk));
}

static JX_FORCE_INLINE f32x4_t f32x4_floor(f32x4_t x)
{
	return JVEC4F(_mm_round_ps(x.xmm, _MM_FROUND_FLOOR));
}

static JX_FORCE_INLINE f32x4_t f32x4_ceil(f32x4_t x)
{
	return JVEC4F(_mm_round_ps(x.xmm, _MM_FROUND_CEIL));
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
	return JVEC4F(_mm_add_ps(c.xmm, _mm_mul_ps(a.xmm, b.xmm)));
}

static JX_FORCE_INLINE f32x4_t f32x4_msub(f32x4_t a, f32x4_t b, f32x4_t c)
{
	return JVEC4F(_mm_sub_ps(_mm_mul_ps(a.xmm, b.xmm), c.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_nmadd(f32x4_t a, f32x4_t b, f32x4_t c)
{
	return JVEC4F(_mm_sub_ps(c.xmm, _mm_mul_ps(a.xmm, b.xmm)));
}

static JX_FORCE_INLINE f32x4_t f32x4_sqrt(f32x4_t x)
{
	return JVEC4F(_mm_sqrt_ps(x.xmm));
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

#define JVEC4F_GET_FUNC(swizzle) \
static JX_FORCE_INLINE f32x4_t f32x4_get##swizzle(f32x4_t x) \
{ \
	return JVEC4F(_mm_shuffle_ps(x.xmm, x.xmm, (uint32_t)(JVEC4_SHUFFLE_##swizzle))); \
}

JVEC4F_GET_FUNC(XXXX)
JVEC4F_GET_FUNC(YYYY)
JVEC4F_GET_FUNC(ZZZZ)
JVEC4F_GET_FUNC(WWWW)
JVEC4F_GET_FUNC(XYXY)
JVEC4F_GET_FUNC(ZWZW)

static JX_FORCE_INLINE i32x4_t i32x4_zero(void)
{
	return JVEC4I(_mm_setzero_si128());
}

static JX_FORCE_INLINE i32x4_t i32x4_fromInt(int32_t x)
{
	return JVEC4I(_mm_set1_epi32(x));
}

static JX_FORCE_INLINE i32x4_t i32x4_from_f32x4(f32x4_t x)
{
	return JVEC4I(_mm_cvtps_epi32(x.xmm));
}

static JX_FORCE_INLINE i32x4_t i32x4_from_f32x4_truncate(f32x4_t x)
{
	return JVEC4I(_mm_cvttps_epi32(x.xmm));
}

static JX_FORCE_INLINE i32x4_t i32x4_fromInt4(int32_t x0, int32_t x1, int32_t x2, int32_t x3)
{
	return JVEC4I(_mm_set_epi32(x3, x2, x1, x0));
}

static JX_FORCE_INLINE i32x4_t i32x4_fromInt4va(const int32_t* arr)
{
	return JVEC4I(_mm_load_si128((const __m128i*)arr));
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
#if 0
	_mm_maskmoveu_si128(x.imm, mask.imm, (char*)buffer);
#else
	const __m128i old = _mm_load_si128((const __m128i*)buffer);
	const __m128i oldMasked = _mm_andnot_si128(mask.imm, old);
	const __m128i newMasked = _mm_and_si128(mask.imm, x.imm);
	const __m128i final = _mm_or_si128(oldMasked, newMasked);
	_mm_store_si128((__m128i*)buffer, final);
#endif
}

static JX_FORCE_INLINE void i32x4_toInt4va_maskedInv(i32x4_t x, i32x4_t maskInv, int32_t* buffer)
{
#if 0
	_mm_maskmoveu_si128(x.imm, _mm_xor_si128(maskInv.imm, _mm_set1_epi32(-1)), (char*)buffer);
#else
	const __m128i old = _mm_load_si128((const __m128i*)buffer);
	const __m128i oldMasked = _mm_and_si128(maskInv.imm, old);
	const __m128i newMasked = _mm_andnot_si128(maskInv.imm, x.imm);
	const __m128i final = _mm_or_si128(oldMasked, newMasked);
	_mm_store_si128((__m128i*)buffer, final);
#endif
}

static JX_FORCE_INLINE void i32x4_toInt4vu_maskedInv(i32x4_t x, i32x4_t maskInv, int32_t* buffer)
{
#if 0
	_mm_maskmoveu_si128(x.imm, _mm_xor_si128(maskInv.imm, _mm_set1_epi32(-1)), (char*)buffer);
#else
	const __m128i old = _mm_loadu_si128((const __m128i*)buffer);
	const __m128i oldMasked = _mm_and_si128(maskInv.imm, old);
	const __m128i newMasked = _mm_andnot_si128(maskInv.imm, x.imm);
	const __m128i final = _mm_or_si128(oldMasked, newMasked);
	_mm_storeu_si128((__m128i*)buffer, final);
#endif
}

static JX_FORCE_INLINE int32_t i32x4_toInt(i32x4_t x)
{
	return _mm_cvtsi128_si32(x.imm);
}

static JX_FORCE_INLINE f32x4_t i32x4_castTo_f32x4(i32x4_t x)
{
	return JVEC4F(_mm_castsi128_ps(x.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_add(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_add_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_sub(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_sub_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_mullo(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_mullo_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_and(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_and_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_or(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_or_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_or3(i32x4_t a, i32x4_t b, i32x4_t c)
{
	return JVEC4I(_mm_or_si128(a.imm, _mm_or_si128(b.imm, c.imm)));
}

static JX_FORCE_INLINE i32x4_t i32x4_andnot(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_andnot_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_xor(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_xor_si128(a.imm, b.imm));
}

static JX_FORCE_INLINE i32x4_t i32x4_sar(i32x4_t x, uint32_t shift)
{
	return JVEC4I(_mm_srai_epi32(x.imm, shift));
}

static JX_FORCE_INLINE i32x4_t i32x4_sal(i32x4_t x, uint32_t shift)
{
	return JVEC4I(_mm_slli_epi32(x.imm, shift));
}

static JX_FORCE_INLINE i32x4_t i32x4_slr(i32x4_t x, uint32_t shift)
{
	return JVEC4I(_mm_srli_epi32(x.imm, shift));
}

static JX_FORCE_INLINE i32x4_t i32x4_cmplt(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_cmplt_epi32(a.imm, b.imm));
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
	return JVEC4I(imm_rgba_p0123_u8);
}

static JX_FORCE_INLINE i32x4_t i32x4_cmpeq(i32x4_t a, i32x4_t b)
{
	return JVEC4I(_mm_cmpeq_epi32(a.imm, b.imm));
}

static JX_FORCE_INLINE bool i32x4_anyNegative(i32x4_t x)
{
	return (_mm_movemask_epi8(x.imm) & 0x8888) != 0;
}

static JX_FORCE_INLINE bool i32x4_allNegative(i32x4_t x)
{
	return (_mm_movemask_epi8(x.imm) & 0x8888) == 0x8888;
}

static JX_FORCE_INLINE uint32_t i32x4_getSignMask(i32x4_t x)
{
	return _mm_movemask_ps(_mm_castsi128_ps(x.imm));
}

static JX_FORCE_INLINE uint32_t i32x4_getByteSignMask(i32x4_t x)
{
	return _mm_movemask_epi8(x.imm);
}

#define JVEC4I_GET_FUNC(swizzle) \
static JX_FORCE_INLINE i32x4_t i32x4_get##swizzle(i32x4_t x) \
{ \
	return JVEC4I(_mm_shuffle_epi32(x.imm, (uint32_t)(JVEC4_SHUFFLE_##swizzle))); \
}

JVEC4I_GET_FUNC(XXXX);
JVEC4I_GET_FUNC(YYYY);
JVEC4I_GET_FUNC(ZZZZ);
JVEC4I_GET_FUNC(WWWW);
JVEC4I_GET_FUNC(XYXY);
JVEC4I_GET_FUNC(ZWZW);

#undef JVEC4F
#undef JVEC4I
