#ifndef JX_MATH_SIMD_H
#define JX_MATH_SIMD_H

#include <stdint.h>
#include <stdbool.h>
#include <immintrin.h>
#include "macros.h"

#if !defined(JX_MATH_SIMD_SSE2) && !defined(JX_MATH_SIMD_SSSE3) && !defined(JX_MATH_SIMD_SSE41) && !defined(JX_MATH_SIMD_AVX) && !defined(JX_MATH_SIMD_AVX2)
#if JX_COMPILER_MSVC
#define JX_MATH_SIMD_SSE2
#pragma message(JX_FILE_LINE_LITERAL "No JX_MATH_SIMD_xxx flag defined. Falling back to SSE2.")
#else
#error "Unknown compiler"
#endif // JX_COMPILER_MSVC
#endif // JX_MATH_SIMD_xxx

#ifdef __cplusplus
extern "C" {
#endif

#define JVEC4_SHUFFLE_MASK(d0_a, d1_a, d2_b, d3_b) (((d3_b) << 6) | ((d2_b) << 4) | ((d1_a) << 2) | ((d0_a)))

typedef enum jvec4_shuffle_mask
{
	JVEC4_SHUFFLE_XXXX = JVEC4_SHUFFLE_MASK(0, 0, 0, 0),
	JVEC4_SHUFFLE_YYYY = JVEC4_SHUFFLE_MASK(1, 1, 1, 1),
	JVEC4_SHUFFLE_ZZZZ = JVEC4_SHUFFLE_MASK(2, 2, 2, 2),
	JVEC4_SHUFFLE_WWWW = JVEC4_SHUFFLE_MASK(3, 3, 3, 3),
	JVEC4_SHUFFLE_XYXY = JVEC4_SHUFFLE_MASK(0, 1, 0, 1),
	JVEC4_SHUFFLE_XZXZ = JVEC4_SHUFFLE_MASK(0, 2, 0, 2),
	JVEC4_SHUFFLE_YWYW = JVEC4_SHUFFLE_MASK(1, 3, 1, 3),
	JVEC4_SHUFFLE_ZWZW = JVEC4_SHUFFLE_MASK(2, 3, 2, 3),
	JVEC4_SHUFFLE_XZYW = JVEC4_SHUFFLE_MASK(0, 2, 1, 3),
	JVEC4_SHUFFLE_XXZZ = JVEC4_SHUFFLE_MASK(0, 0, 2, 2),
	JVEC4_SHUFFLE_YYWW = JVEC4_SHUFFLE_MASK(1, 1, 3, 3),
} jvec4_shuffle_mask;

typedef struct f32x4_t
{
	__m128 xmm;
} f32x4_t;

// TODO: jvec2d

typedef struct i32x4_t
{
	__m128i imm;
} i32x4_t;

#if defined(JX_MATH_SIMD_AVX) || defined(JX_MATH_SIMD_AVX2)
typedef struct f32x8_t
{
	__m256 ymm;
} f32x8_t;

typedef struct i32x8_t
{
	__m256i ymm;
} i32x8_t;

// TODO: jvec4d
#endif

static f32x4_t f32x4_zero(void);
static f32x4_t f32x4_fromFloat(float x);
static f32x4_t f32x4_from_i32x4(i32x4_t x);
#if defined(JX_MATH_SIMD_AVX) || defined(JX_MATH_SIMD_AVX2)
static f32x4_t f32x4_from_f32x8_low(f32x8_t x);
static f32x4_t f32x4_from_f32x8_high(f32x8_t x);
#endif
static f32x4_t f32x4_fromFloat4(float x0, float x1, float x2, float x3);
static f32x4_t f32x4_fromFloat4va(const float* arr);
static f32x4_t f32x4_fromFloat4vu(const float* arr);
static void f32x4_toFloat4va(f32x4_t x, float* arr);
static void f32x4_toFloat4vu(f32x4_t x, float* arr);
static f32x4_t f32x4_fromRGBA8(uint32_t rgba8);
static uint32_t f32x4_toRGBA8(f32x4_t x);
static i32x4_t f32x4_castTo_i32x4(f32x4_t x);
static f32x4_t f32x4_cmpeq(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_cmpgt(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_cmple(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_cmplt(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_ldexp(f32x4_t x, i32x4_t exp);
static f32x4_t f32x4_add(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_sub(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_mul(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_div(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_min(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_max(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_xor(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_and(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_or(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_andnot(f32x4_t a, f32x4_t b);
static f32x4_t f32x4_getSignMask(f32x4_t x);
static f32x4_t f32x4_negate(f32x4_t x);
static f32x4_t f32x4_abs(f32x4_t x);
static f32x4_t f32x4_floor(f32x4_t x);
static f32x4_t f32x4_ceil(f32x4_t x);
static float f32x4_hadd(f32x4_t x);
static f32x4_t f32x4_madd(f32x4_t a, f32x4_t b, f32x4_t c);  // (a * b) + c
static f32x4_t f32x4_msub(f32x4_t a, f32x4_t b, f32x4_t c);  // (a * b) - c
static f32x4_t f32x4_nmadd(f32x4_t a, f32x4_t b, f32x4_t c); // -(a * b) + c
static f32x4_t f32x4_sqrt(f32x4_t x);
static f32x4_t f32x4_log(f32x4_t x);
static f32x4_t f32x4_exp(f32x4_t x);
static f32x4_t f32x4_sin(f32x4_t x);
static f32x4_t f32x4_cos(f32x4_t x);
static void f32x4_sincos(f32x4_t x, f32x4_t* s, f32x4_t* c);
static f32x4_t f32x4_sinh(f32x4_t x);
static f32x4_t f32x4_cosh(f32x4_t x);
static void f32x4_sincosh(f32x4_t x, f32x4_t* s, f32x4_t* c);
static f32x4_t f32x4_atan2(f32x4_t y, f32x4_t x);
static f32x4_t f32x4_atan(f32x4_t x);
static float f32x4_getX(f32x4_t a);
static float f32x4_getY(f32x4_t a);
static float f32x4_getZ(f32x4_t a);
static float f32x4_getW(f32x4_t a);
static f32x4_t f32x4_getXXXX(f32x4_t x);
static f32x4_t f32x4_getYYYY(f32x4_t x);
static f32x4_t f32x4_getZZZZ(f32x4_t x);
static f32x4_t f32x4_getWWWW(f32x4_t x);
static f32x4_t f32x4_getXYXY(f32x4_t x);
static f32x4_t f32x4_getYWYW(f32x4_t x);
static f32x4_t f32x4_getZWZW(f32x4_t x);

static i32x4_t i32x4_zero(void);
static i32x4_t i32x4_fromInt(int32_t x);
static i32x4_t i32x4_from_f32x4(f32x4_t x);
static i32x4_t i32x4_from_f32x4_truncate(f32x4_t x);
static i32x4_t i32x4_fromInt4(int32_t x0, int32_t x1, int32_t x2, int32_t x3);
static i32x4_t i32x4_fromInt4va(const int32_t* arr);
static void i32x4_toInt4vu(i32x4_t x, int32_t* arr);
static void i32x4_toInt4va(i32x4_t x, int32_t* arr);
static void i32x4_toInt4va_masked(i32x4_t x, i32x4_t mask, int32_t* buffer);
static void i32x4_toInt4va_maskedInv(i32x4_t x, i32x4_t maskInv, int32_t* buffer);
static void i32x4_toInt4vu_maskedInv(i32x4_t x, i32x4_t maskInv, int32_t* buffer);
static int32_t i32x4_toInt(i32x4_t x);
static f32x4_t i32x4_castTo_f32x4(i32x4_t x);
static i32x4_t i32x4_add(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_sub(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_mullo(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_and(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_or(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_or3(i32x4_t a, i32x4_t b, i32x4_t c);
static i32x4_t i32x4_andnot(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_xor(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_sar(i32x4_t x, uint32_t shift);
static i32x4_t i32x4_sal(i32x4_t x, uint32_t shift);
static i32x4_t i32x4_slr(i32x4_t x, uint32_t shift);
static i32x4_t i32x4_cmplt(i32x4_t a, i32x4_t b);
static i32x4_t i32x4_packR32G32B32A32_to_RGBA8(i32x4_t r, i32x4_t g, i32x4_t b, i32x4_t a);
static i32x4_t i32x4_cmpeq(i32x4_t a, i32x4_t b);
static bool i32x4_anyNegative(i32x4_t x);
static bool i32x4_allNegative(i32x4_t x);
static uint32_t i32x4_getSignMask(i32x4_t x);
static uint32_t i32x4_getByteSignMask(i32x4_t x);
static int32_t i32x4_getX(i32x4_t a);
static int32_t i32x4_getY(i32x4_t a);
static int32_t i32x4_getZ(i32x4_t a);
static int32_t i32x4_getW(i32x4_t a);

#if defined(JX_MATH_SIMD_AVX) || defined(JX_MATH_SIMD_AVX2)
static f32x8_t f32x8_zero(void);
static f32x8_t f32x8_fromFloat(float x);
static f32x8_t f32x8_from_i32x8(i32x8_t x);
static f32x8_t f32x8_fromFloat8(float x0, float x1, float x2, float x3, float x4, float x5, float x6, float x7);
static f32x8_t f32x8_fromFloat8va(const float* arr);
static f32x8_t f32x8_fromFloat8vu(const float* arr);
static void f32x8_toFloat8va(f32x8_t x, float* arr);
static void f32x8_toFloat8vu(f32x8_t x, float* arr);
static i32x8_t f32x8_castTo_i32x8(f32x8_t x);
static f32x8_t f32x8_cmpeq(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_cmpgt(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_cmple(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_cmplt(f32x8_t a, f32x8_t b);
#if defined(JX_MATH_SIMD_AVX2)
static f32x8_t f32x8_ldexp(f32x8_t x, i32x8_t exp);
#endif
static f32x8_t f32x8_add(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_sub(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_mul(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_div(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_min(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_max(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_xor(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_and(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_or(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_andnot(f32x8_t a, f32x8_t b);
static f32x8_t f32x8_getSignMask(f32x8_t x);
static f32x8_t f32x8_negate(f32x8_t x);
static f32x8_t f32x8_abs(f32x8_t x);
static f32x8_t f32x8_floor(f32x8_t x);
static f32x8_t f32x8_ceil(f32x8_t x);
static float f32x8_hadd(f32x8_t x);
static f32x8_t f32x8_madd(f32x8_t a, f32x8_t b, f32x8_t c);
static f32x8_t f32x8_msub(f32x8_t a, f32x8_t b, f32x8_t c);
static f32x8_t f32x8_nmadd(f32x8_t a, f32x8_t b, f32x8_t c); // -(a * b) + c
static f32x8_t f32x8_sqrt(f32x8_t x);
#if defined(JX_MATH_SIMD_AVX2)
static f32x8_t f32x8_log(f32x8_t x);
static f32x8_t f32x8_exp(f32x8_t x);
static f32x8_t f32x8_sin(f32x8_t x);
static f32x8_t f32x8_cos(f32x8_t x);
static void f32x8_sincos(f32x8_t x, f32x8_t* s, f32x8_t* c);
static f32x8_t f32x8_sinh(f32x8_t x);
static f32x8_t f32x8_cosh(f32x8_t x);
static void f32x8_sincosh(f32x8_t x, f32x8_t* s, f32x8_t* c);
static f32x8_t f32x8_atan2(f32x8_t y, f32x8_t x);
static f32x8_t f32x8_atan(f32x8_t x);
#endif

static i32x8_t i32x8_zero(void);
static i32x8_t i32x8_fromInt(int32_t x);
static i32x8_t i32x8_from_f32x8(f32x8_t x);
static i32x8_t i32x8_from_f32x8_truncate(f32x8_t x);
static i32x8_t i32x8_fromInt8(int32_t x0, int32_t x1, int32_t x2, int32_t x3, int32_t x4, int32_t x5, int32_t x6, int32_t x7);
static i32x8_t i32x8_fromInt8va(const int32_t* arr);
static void i32x8_toInt8vu(i32x8_t x, int32_t* arr);
static void i32x8_toInt8va(i32x8_t x, int32_t* arr);
static f32x8_t i32x8_castToVec8f(i32x8_t x);
#if defined(JX_MATH_SIMD_AVX2)
static void i32x8_toInt8va_masked(i32x8_t x, i32x8_t mask, int32_t* buffer);
static void i32x8_toInt8va_maskedInv(i32x8_t x, i32x8_t maskInv, int32_t* buffer);
static void i32x8_toInt8vu_maskedInv(i32x8_t x, i32x8_t maskInv, int32_t* buffer);
static i32x8_t i32x8_add(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_sub(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_mullo(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_and(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_or(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_or3(i32x8_t a, i32x8_t b, i32x8_t c);
static i32x8_t i32x8_andnot(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_xor(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_sar(i32x8_t x, uint32_t shift);
static i32x8_t i32x8_sal(i32x8_t x, uint32_t shift);
static i32x8_t i32x8_slr(i32x8_t x, uint32_t shift);
static i32x8_t i32x8_sll(i32x8_t x, uint32_t shift);
static i32x8_t i32x8_sllv(i32x8_t x, i32x8_t shift);
static i32x8_t i32x8_cmpeq(i32x8_t a, i32x8_t b);
static i32x8_t i32x8_packR32G32B32A32_to_RGBA8(i32x8_t r, i32x8_t g, i32x8_t b, i32x8_t a);
#endif
static bool i32x8_anyNegative(i32x8_t x);
static bool i32x8_allNegative(i32x8_t x);
static uint32_t i32x8_getSignMask(i32x8_t x);
static uint32_t i32x8_getByteSignMask(i32x8_t x);
#endif

#if defined(JX_MATH_SIMD_SSE2)
#include "inline/math_simd_sse2.inl"
#elif defined(JX_MATH_SIMD_SSSE3)
#include "inline/math_simd_ssse3.inl"
#elif defined(JX_MATH_SIMD_SSE41)
#include "inline/math_simd_sse41.inl"
#elif defined(JX_MATH_SIMD_AVX) || defined(JX_MATH_SIMD_AVX2)
#include "inline/math_simd_avx.inl"
#endif

#include "inline/math_simd_common.inl"

#ifdef __cplusplus
}
#endif

#endif // JX_MATH_SIMD_H
