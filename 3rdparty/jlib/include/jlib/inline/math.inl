#ifndef JX_MATH_H
#error "Must be included from jx/math.h"
#endif

#include "../macros.h"
#include "../dbg.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline float jx_sinf(float x)
{
	return math_api->sinf(x);
}

static inline float jx_cosf(float x)
{
	return math_api->cosf(x);
}

static inline float jx_absf(float x)
{
	return x < 0.0f ? -x : x;
}

static inline float jx_roundf(float x)
{
	return math_api->floorf(x + 0.5f);
}

static inline float jx_floorf(float x)
{
	return math_api->floorf(x);
}

static inline float jx_ceilf(float x)
{
	return -math_api->floorf(-x);
}

static inline float jx_fractf(float x)
{
	return x - (float)((int32_t)x);
}

static inline float jx_expf(float x)
{
	return math_api->expf(x);
}

static inline float jx_exp2f(float x)
{
	return math_api->exp2f(x);
}

static inline float jx_logf(float x)
{
	return math_api->logf(x);
}

static inline float jx_log2f(float x)
{
	return math_api->log2f(x);
}

static inline float jx_modf(float numer, float denom)
{
	return numer - denom * jx_floorf(numer / denom);
}

static inline float jx_maxf(float a, float b)
{
	return a > b ? a : b;
}

static inline float jx_minf(float a, float b)
{
	return a < b ? a : b;
}

static inline float jx_max3f(float a, float b, float c)
{
	return jx_maxf(a, jx_maxf(b, c));
}

static inline float jx_min3f(float a, float b, float c)
{
	return jx_minf(a, jx_minf(b, c));
}

static inline float jx_clampf(float x, float min, float max)
{
	return x < min
		? min
		: (x > max ? max : x)
		;
}

static inline float jx_lerpf(float t, float s, float e)
{
	return s + t * (e - s);
}

static inline float jx_signf(float x)
{
	return x < 0.0f ? -1.0f : 1.0f;
}

static inline float jx_toRadf(float deg)
{
	return deg * kPif / 180.0f;
}

static inline float jx_toDegf(float rad)
{
	return rad * 180.0f / kPif;
}

static inline float jx_sqrtf(float x)
{
	return math_api->sqrtf(x);
}

static inline float jx_powf(float x, float e)
{
	return math_api->powf(x, e);
}

static inline float jx_atan2f(float y, float x)
{
	return math_api->atan2f(y, x);
}

static inline float jx_acosf(float x)
{
	return math_api->acosf(x);
}

static inline float jx_asinf(float x)
{
	return math_api->asinf(x);
}

static inline float jx_atanf(float x)
{
	return math_api->atanf(x);
}

static inline float jx_tanf(float x)
{
	return math_api->tanf(x);
}

static inline float jx_wrapf(float a, float wrap)
{
	const float tmp = jx_modf(a, wrap);
	return tmp < 0.0f 
		? wrap + tmp
		: tmp
		;
}

static inline float jx_sqrf(float x)
{
	return x * x;
}

static inline bool jx_isnanf(float x)
{
	const int32_t ix = jx_bitcast_f_i32(x);
	return ((ix & 0x7f800000) == 0x7f800000)
		&& ((ix & 0x007fffff) == 0)
		;
}

static inline bool jx_isinff(float x)
{
	const int32_t ix = jx_bitcast_f_i32(x);
	return ((ix & 0x7f800000) == 0x7f800000)
		&& ((ix & 0x007fffff) != 0)
		;
}

static inline bool jx_isnumf(float x)
{
	const int32_t ix = jx_bitcast_f_i32(x);
	return ((ix & 0x7f800000) != 0x7f800000);
}

static inline double jx_sind(double x)
{
	return math_api->sind(x);
}

static inline double jx_cosd(double x)
{
	return math_api->cosd(x);
}

static inline double jx_absd(double x)
{
	return x < 0 ? -x : x;
}

static inline double jx_roundd(double x)
{
	return math_api->floord(x + 0.5);
}

static inline double jx_floord(double x)
{
	return math_api->floord(x);
}

static inline double jx_ceild(double x)
{
	return -math_api->floord(-x);
}

static inline double jx_modd(double numer, double denom)
{
	return numer - denom * jx_floord(numer / denom);
}

static inline double jx_maxd(double a, double b)
{
	return a > b ? a : b;
}

static inline double jx_mind(double a, double b)
{
	return a < b ? a : b;
}

static inline double jx_max3d(double a, double b, double c)
{
	return jx_maxd(a, jx_maxd(b, c));
}

static inline double jx_min3d(double a, double b, double c)
{
	return jx_mind(a, jx_mind(b, c));
}

static inline double jx_clampd(double x, double min, double max)
{
	return x < min
		? min
		: (x > max ? max : x)
		;
}

static inline double jx_lerpd(double t, double s, double e)
{
	return s + t * (e - s);
}

static inline double jx_sqrtd(double x)
{
	return math_api->sqrtd(x);
}

static inline double jx_powd(double x, double e)
{
	return math_api->powd(x, e);
}

static inline double jx_atan2d(double y, double x)
{
	return math_api->atan2d(y, x);
}

static inline double jx_wrapd(double a, double wrap)
{
	const double tmp = jx_modd(a, wrap);
	return tmp < 0.0
		? wrap + tmp
		: tmp
		;
}

static inline void jx_doubleToFloat(float* dst, const double* src, uint32_t n)
{
	math_api->doubleToFloat(dst, src, n);
}

static inline void jx_floatToDouble(double* dst, const float* src, uint32_t n)
{
	math_api->floatToDouble(dst, src, n);
}

static inline uint32_t jx_nextPowerOf2_u32(uint32_t x)
{
	// https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
	x--;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x++;
	return x;
}

static inline uint32_t jx_nextMultipleOf_u32(uint32_t x, uint32_t div)
{
	JX_CHECK(jx_isPow2_u32(div), "Invalid value");
	const uint32_t mask = div - 1;
	return (x & (~mask)) + ((x & mask) != 0 ? div : 0);
}

static inline uint32_t jx_max_u32(uint32_t a, uint32_t b)
{
	return a > b ? a : b;
}

static inline uint32_t jx_min_u32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static inline uint32_t jx_clamp_u32(uint32_t x, uint32_t min, uint32_t max)
{
	return x < min
		? min
		: (x > max ? max : x)
		;
}


static inline uint16_t jx_max_u16(uint16_t a, uint16_t b)
{
	return a > b ? a : b;
}

static inline uint16_t jx_min_u16(uint16_t a, uint16_t b)
{
	return a < b ? a : b;
}

static inline uint16_t jx_clamp_u16(uint16_t x, uint16_t min, uint16_t max)
{
	return x < min
		? min
		: (x > max ? max : x)
		;
}

static inline int64_t jx_abs_i64(int64_t x)
{
	return x < 0 ? -x : x;
}

static inline int64_t jx_max_i64(int64_t a, int64_t b)
{
	return a > b ? a : b;
}

static inline int64_t jx_min_i64(int64_t a, int64_t b)
{
	return a < b ? a : b;
}

static inline int64_t jx_clamp_i64(int64_t x, int64_t min, int64_t max)
{
	return x < min
		? min
		: (x > max ? max : x)
		;
}

static inline int32_t jx_abs_i32(int32_t x)
{
	return x < 0 ? -x : x;
}

static inline int32_t jx_max_i32(int32_t a, int32_t b)
{
	return a > b ? a : b;
}

static inline int32_t jx_min_i32(int32_t a, int32_t b)
{
	return a < b ? a : b;
}

static inline int32_t jx_clamp_i32(int32_t x, int32_t min, int32_t max)
{
	return x < min
		? min
		: (x > max ? max : x)
		;
}

static inline int16_t jx_abs_i16(int16_t x)
{
	return (int16_t)((uint16_t)x & 0x7FFF);
}

static inline int16_t jx_min_i16(int16_t a, int16_t b)
{
	return a < b ? a : b;
}

static inline int16_t jx_max_i16(int16_t a, int16_t b)
{
	return a > b ? a : b;
}

static inline int16_t jx_clamp_i16(int16_t x, int16_t min, int16_t max)
{
	return x < min
		? min
		: (x > max ? max : x)
		;
}

static inline int32_t jx_roundup_i32(int32_t x, int32_t round_to)
{
	return (x + (round_to - 1)) & ~(round_to - 1);
}

static inline uint32_t jx_roundup_u32(uint32_t x, uint32_t round_to)
{
	return (x + (round_to - 1)) & ~(round_to - 1);
}

static inline uint64_t jx_roundup_u64(uint64_t x, uint64_t round_to)
{
	return (x + (round_to - 1)) & ~(round_to - 1);
}

static inline bool jx_isPow2_u32(uint32_t x)
{
	return ((x & (x - 1)) == 0);
}

static inline bool jx_isMultipleOf_u64(uint64_t x, uint64_t of)
{
	return (x & (of - 1)) == 0;
}

static inline uint32_t jx_bisectf(const float* arr, uint32_t n, uint32_t stride, float x)
{
	return math_api->bisectf(arr, n, stride, x);
}

static inline uint32_t jx_bisectd(const double* arr, uint32_t n, uint32_t stride, double x)
{
	return math_api->bisectd(arr, n, stride, x);
}

static inline uint16_t jx_crc16(const uint8_t* buffer, uint32_t len)
{
	return math_api->crc16(buffer, len);
}

// http://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetKernighan
static inline uint32_t jx_bitcount_u32(uint32_t x)
{
	uint32_t c;
	for (c = 0; x; c++) {
		x &= x - 1;
	}
	return c;
}

static uint32_t jx_ctntz_u64(uint64_t x)
{
#if JX_COMPILER_MSVC
	uint32_t r;
	_BitScanForward64(&r, x);
	return r;
#else
	JX_NOT_IMPLEMENTED();
#endif
}

static inline jx_vec2f_t jx_vec2f_add(jx_vec2f_t a, jx_vec2f_t b) 
{ 
	return JX_VEC2F(a.x + b.x, a.y + b.y);
}

static inline jx_vec2f_t jx_vec2f_sub(jx_vec2f_t a, jx_vec2f_t b) 
{
	return JX_VEC2F(a.x - b.x, a.y - b.y);
}

static inline jx_vec2f_t jx_vec2f_scale(jx_vec2f_t a, float s) 
{ 
	return JX_VEC2F(a.x * s, a.y * s);
}

// Direction from a to b
static inline jx_vec2f_t jx_vec2f_dir(jx_vec2f_t a, jx_vec2f_t b)
{
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	const float lenSqr = dx * dx + dy * dy;
	const float invLen = lenSqr < JX_FLOAT_EPSILON 
		? 0.0f 
		: (1.0f / jx_sqrtf(lenSqr))
		;
	return JX_VEC2F(dx * invLen, dy * invLen);
}

static inline jx_vec2f_t jx_vec2f_perpCCW(jx_vec2f_t a) 
{ 
	return JX_VEC2F(-a.y, a.x);
}

static inline jx_vec2f_t jx_vec2f_perpCW(jx_vec2f_t a) 
{ 
	return JX_VEC2F(a.y, -a.x);
}

static inline float jx_vec2f_dot(jx_vec2f_t a, jx_vec2f_t b) 
{ 
	return a.x * b.x + a.y * b.y; 
}

static inline float jx_vec2f_cross(jx_vec2f_t a, jx_vec2f_t b)
{
	return a.x * b.y - b.x * a.y;
}

static inline float jx_vec2f_distanceSqr(jx_vec2f_t a, jx_vec2f_t b)
{
	const float dx = a.x - b.x;
	const float dy = a.y - b.y;
	return dx * dx + dy * dy;
}

static inline jx_moving_averagef_t* jx_movAvgfCreate(uint32_t n, jx_allocator_i* allocator)
{
	return math_api->movAvgfCreate(n, allocator);
}

static inline void jx_movAvgfDestroy(jx_moving_averagef_t* ma, jx_allocator_i* allocator)
{
	math_api->movAvgfDestroy(ma, allocator);
}

static inline float jx_movAvgfPush(jx_moving_averagef_t* ma, float val)
{
	return math_api->movAvgfPush(ma, val);
}

static inline float jx_movAvgfGetAverage(const jx_moving_averagef_t* ma)
{
	return math_api->movAvgfGetAverage(ma);
}

static inline float jx_movAvgfGetStdDev(const jx_moving_averagef_t* ma)
{
	return math_api->movAvgfGetStdDev(ma);
}

static inline void jx_movAvgfGetBounds(const jx_moving_averagef_t* ma, float* minVal, float* maxVal)
{
	math_api->movAvgfGetBounds(ma, minVal, maxVal);
}

static inline const float* jx_movAvgfGetValues(const jx_moving_averagef_t* ma)
{
	return math_api->movAvgfGetValues(ma);
}

static inline uint32_t jx_movAvgfGetNumValues(const jx_moving_averagef_t* ma)
{
	return math_api->movAvgfGetNumValues(ma);
}
#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
JX_PRAGMA_DIAGNOSTIC_PUSH
JX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(4116) // unnamed type definition in parentheses
static inline int32_t jx_bitcast_f_i32(float x)
{
	return ((union { float f; int32_t i; }) { x }).i;
}

static inline float jx_bitcast_i32_f(int32_t x)
{
	return ((union { int32_t i; float f; }) { x }).f;
}

static inline int64_t jx_bitcast_d_i64(double x)
{
	return ((union { double d; int64_t i; }) { x }).i;
}

static inline double jx_bitcast_i64_d(int64_t x)
{
	return ((union { int64_t i; double d; }) { x }).d;
}
JX_PRAGMA_DIAGNOSTIC_POP
#else // __cplusplus
static inline int32_t jx_bitcast_f_i32(float x)
{
	return *(int32_t*)&x;
}

static inline float jx_bitcast_i32_f(int32_t x)
{
	return *(float*)&x;
}
#endif
