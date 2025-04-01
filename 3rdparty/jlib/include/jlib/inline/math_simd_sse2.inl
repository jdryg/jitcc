#ifndef JX_MATH_SIMD_H
#error "Must be included from jx/math_simd.h"
#endif

#ifdef __cplusplus
#define JFLOAT32x4(xmm_reg) { (xmm_reg) }
#define JINT32x4(imm_reg) { (imm_reg) }
#else
#define JFLOAT32x4(xmm_reg) (f32x4_t){ .xmm = (xmm_reg) }
#define JINT32x4(imm_reg) (i32x4_t){ .imm = (imm_reg) }
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
	static const JX_ALIGN_DECL(16, float kOne[4]) = { 1.0f, 1.0f, 1.0f, 1.0f };
	const __m128i i = _mm_cvttps_epi32(x.xmm);
	const __m128 fi = _mm_cvtepi32_ps(i);
	const __m128 igx = _mm_cmpgt_ps(fi, x.xmm);
	const __m128 j = _mm_and_ps(igx, *(__m128*)kOne);
	return JFLOAT32x4(_mm_sub_ps(fi, j));
}

// http://dss.stephanierct.com/DevBlog/?p=8
static JX_FORCE_INLINE f32x4_t f32x4_ceil(f32x4_t x)
{
	static const JX_ALIGN_DECL(16, float kOne[4]) = { 1.0f, 1.0f, 1.0f, 1.0f };
	const __m128i i = _mm_cvttps_epi32(x.xmm);
	const __m128 fi = _mm_cvtepi32_ps(i);
	const __m128 igx = _mm_cmplt_ps(fi, x.xmm);
	const __m128 j = _mm_and_ps(igx, *(__m128*)kOne);
	return JFLOAT32x4(_mm_add_ps(fi, j));
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
	return JFLOAT32x4(_mm_add_ps(c.xmm, _mm_mul_ps(a.xmm, b.xmm)));
}

static JX_FORCE_INLINE f32x4_t f32x4_msub(f32x4_t a, f32x4_t b, f32x4_t c)
{
	return JFLOAT32x4(_mm_sub_ps(_mm_mul_ps(a.xmm, b.xmm), c.xmm));
}

static JX_FORCE_INLINE f32x4_t f32x4_nmadd(f32x4_t a, f32x4_t b, f32x4_t c)
{
	return JFLOAT32x4(_mm_sub_ps(c.xmm, _mm_mul_ps(a.xmm, b.xmm)));
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
#if 1
	// https://fgiesen.wordpress.com/2016/04/03/sse-mind-the-gap/
	// even and odd lane products
	const __m128i evnp = _mm_mul_epu32(a.imm, b.imm);
	const __m128i odda = _mm_srli_epi64(a.imm, 32);
	const __m128i oddb = _mm_srli_epi64(b.imm, 32);
	const __m128i oddp = _mm_mul_epu32(odda, oddb);

	// merge results
	const __m128i evn_mask = _mm_setr_epi32(-1, 0, -1, 0);
	const __m128i evn_result = _mm_and_si128(evnp, evn_mask);
	const __m128i odd_result = _mm_slli_epi64(oddp, 32);

	return JINT32x4(_mm_or_si128(evn_result, odd_result));
#else
	const __m128i tmp1 = _mm_mul_epu32(a.imm, b.imm);
	const __m128i tmp2 = _mm_mul_epu32(_mm_srli_si128(a.imm, 4), _mm_srli_si128(b.imm, 4));
	return (i32x4_t) { .imm = _mm_unpacklo_epi32(_mm_shuffle_epi32(tmp1, _MM_SHUFFLE(0, 0, 2, 0)), _mm_shuffle_epi32(tmp2, _MM_SHUFFLE(0, 0, 2, 0))) };
#endif
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
	// Pack into uint8_t
	// (uint8_t){ r0, r1, r2, r3, g0, g1, g2, g3, b0, b1, b2, b3, a0, a1, a2, a3 }
	const __m128i imm_r0123_g0123_b0123_a0123_u8 = _mm_packus_epi16(
		_mm_packs_epi32(r.imm, g.imm), _mm_packs_epi32(b.imm, a.imm)
	);

	// https://stackoverflow.com/questions/24595003/permuting-bytes-inside-sse-m128i-register
	// _mm_shuffle_epi8() with SSE2
#if 0
	__m128i mask = _mm_set_epi8(0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF);
#else
	// Avoid warning C4309: 'argument': truncation of constant value
	__m128i mask = _mm_set_epi8(0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1, 0, -1);
#endif

	// (uint8_t){ r0, r2, g0, g2, b0, b2, a0, a2, r1, r3, g1, g3, b1, b3, a1, a3 }
	const __m128i imm_r02_g02_b02_a02_r13_g13_b13_a13_u8 =
		_mm_packus_epi16(
			_mm_and_si128(imm_r0123_g0123_b0123_a0123_u8, mask),
			_mm_srli_epi16(imm_r0123_g0123_b0123_a0123_u8, 8)
		);

	// (uint8_t){ r0, g0, b0, a0, r1, g1, b1, a1, r2, g2, b3, a2, r3, g3, b3, a3 }
	const __m128i imm_rgba_p0123_u8 =
		_mm_packus_epi16(
			_mm_and_si128(imm_r02_g02_b02_a02_r13_g13_b13_a13_u8, mask),
			_mm_srli_epi16(imm_r02_g02_b02_a02_r13_g13_b13_a13_u8, 8)
		);

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

#undef JFLOAT32x4
#undef JINT32x4
