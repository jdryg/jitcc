#ifndef JX_MATH_H
#define JX_MATH_H

#include <stdint.h>
#include <stdbool.h>

typedef struct jx_allocator_i jx_allocator_i;

#ifdef __cplusplus
extern "C" {
#endif

#define JX_FLOAT_EPSILON 1e-8f
#define JX_FLOAT_MAX     3.40282346638528859812e+38f
#define JX_INT_MIN       (-2147483647 - 1)
#define JX_INT_MAX       2147483647

static const float  kPif  = 3.14159274101257324219f; // Pi value from Windows calculator rounded by https://float.exposed/ to float
static const double kPid = 3.141592653589793116;    // Pi value from Windows calculator rounded by https://float.exposed/ to double
static const float  kLogNat10f = 2.3025850929940456840179914546844f;

typedef struct jx_rectf_t
{
	float x;
	float y;
	float w;
	float h;
} jx_rectf_t;

typedef struct jx_recti_t
{
	int32_t x;
	int32_t y;
	uint32_t w;
	uint32_t h;
} jx_recti_t;

typedef struct jx_vec2f_t
{
	float x;
	float y;
} jx_vec2f_t;

#ifdef __cplusplus
#define JX_VEC2F(x, y) { (x), (y) }
#else
#define JX_VEC2F(_x, _y) (jx_vec2f_t){ .x = (_x), .y = (_y) }
#endif

typedef struct jx_moving_averagef_t jx_moving_averagef_t;

typedef struct jx_math_api
{
	// Float
	float (*sinf)(float x);
	float (*cosf)(float x);
	float (*floorf)(float x);
	float (*expf)(float x);
	float (*exp2f)(float x);
	float (*logf)(float x);
	float (*log2f)(float x);
	float (*sqrtf)(float x);
	float (*powf)(float x, float e);
	float (*atan2f)(float y, float x);
	float (*acosf)(float x);
	float (*asinf)(float x);
	float (*atanf)(float x);
	float (*tanf)(float x);

	// Double
	double (*sind)(double x);
	double (*cosd)(double x);
	double (*floord)(double x);
	double (*sqrtd)(double x);
	double (*powd)(double x, double e);
	double (*atan2d)(double y, double x);

	// Conversions
	void (*doubleToFloat)(float* dst, const double* src, uint32_t n);
	void (*floatToDouble)(double* dst, const float* src, uint32_t n);

	// NOTE: These do not belong here but I don't currently have a suitable API.
	// Returns the index at which x should be inserted in order to keep arr sorted.
	// arr is assumed to be already sorted.
	uint32_t (*bisectf)(const float* arr, uint32_t n, uint32_t stride, float x);
	uint32_t (*bisectd)(const double* arr, uint32_t n, uint32_t stride, double x);

	uint16_t (*crc16)(const uint8_t* buffer, uint32_t len);

	jx_moving_averagef_t* (*movAvgfCreate)(uint32_t n, jx_allocator_i* allocator);
	void                  (*movAvgfDestroy)(jx_moving_averagef_t* ma, jx_allocator_i* allocator);
	float                 (*movAvgfPush)(jx_moving_averagef_t* ma, float val);
	float                 (*movAvgfGetAverage)(const jx_moving_averagef_t* ma);
	float                 (*movAvgfGetStdDev)(const jx_moving_averagef_t* ma);
	void                  (*movAvgfGetBounds)(const jx_moving_averagef_t* ma, float* minVal, float* maxVal);
	const float*          (*movAvgfGetValues)(const jx_moving_averagef_t* ma);
	uint32_t              (*movAvgfGetNumValues)(const jx_moving_averagef_t* ma);
} jx_math_api;

extern jx_math_api* math_api;

static float jx_sinf(float x);
static float jx_cosf(float x);
static float jx_absf(float x);
static float jx_roundf(float x);
static float jx_floorf(float x);
static float jx_ceilf(float x);
static float jx_fractf(float x);
static float jx_expf(float x);
static float jx_exp2f(float x);
static float jx_logf(float x);
static float jx_log2f(float x);
static float jx_modf(float numer, float denom);
static float jx_maxf(float a, float b);
static float jx_minf(float a, float b);
static float jx_max3f(float a, float b, float c);
static float jx_min3f(float a, float b, float c);
static float jx_clampf(float x, float min, float max);
static float jx_lerpf(float t, float s, float e);
static float jx_signf(float x);
static float jx_toRadf(float deg);
static float jx_toDegf(float rad);
static float jx_sqrtf(float x);
static float jx_powf(float x, float e);
static float jx_atan2f(float y, float x);
static float jx_acosf(float x);
static float jx_asinf(float x);
static float jx_atanf(float x);
static float jx_tanf(float x);
static float jx_wrapf(float a, float wrap);
static float jx_sqrf(float x);
static bool jx_isnanf(float x);
static bool jx_isinff(float x);
static bool jx_isnumf(float x); // Not inf or nan; proper floating point value.

static double jx_sind(double x);
static double jx_cosd(double x);
static double jx_absd(double x);
static double jx_roundd(double x);
static double jx_floord(double x);
static double jx_ceild(double x);
static double jx_modd(double numer, double denom);
static double jx_maxd(double a, double b);
static double jx_mind(double a, double b);
static double jx_max3d(double a, double b, double c);
static double jx_min3d(double a, double b, double c);
static double jx_clampd(double x, double min, double max);
static double jx_lerpd(double t, double s, double e);
static double jx_sqrtd(double x);
static double jx_powd(double x, double e);
static double jx_atan2d(double y, double x);
static double jx_wrapd(double a, double wrap);

static void jx_doubleToFloat(float* dst, const double* src, uint32_t n);
static void jx_floatToDouble(double* dst, const float* src, uint32_t n);

// NOTE: If 'x' is 0 it returns 0.
static uint32_t jx_nextPowerOf2_u32(uint32_t x);
static uint32_t jx_nextMultipleOf_u32(uint32_t x, uint32_t div);

static uint32_t jx_max_u32(uint32_t a, uint32_t b);
static uint32_t jx_min_u32(uint32_t a, uint32_t b);
static uint32_t jx_clamp_u32(uint32_t x, uint32_t min, uint32_t max);

static uint16_t jx_max_u16(uint16_t a, uint16_t b);
static uint16_t jx_min_u16(uint16_t a, uint16_t b);
static uint16_t jx_clamp_u16(uint16_t x, uint16_t min, uint16_t max);

static int64_t jx_abs_i64(int64_t x);
static int64_t jx_max_i64(int64_t a, int64_t b);
static int64_t jx_min_i64(int64_t a, int64_t b);
static int64_t jx_clamp_i64(int64_t x, int64_t min, int64_t max);

static int32_t jx_abs_i32(int32_t x);
static int32_t jx_max_i32(int32_t a, int32_t b);
static int32_t jx_min_i32(int32_t a, int32_t b);
static int32_t jx_clamp_i32(int32_t x, int32_t min, int32_t max);

static int16_t jx_abs_i16(int16_t x);
static int16_t jx_min_i16(int16_t a, int16_t b);
static int16_t jx_max_i16(int16_t a, int16_t b);
static int16_t jx_clamp_i16(int16_t x, int16_t min, int16_t max);

static int32_t jx_roundup_i32(int32_t x, int32_t round_to);
static uint32_t jx_roundup_u32(uint32_t x, uint32_t round_to);
static uint64_t jx_roundup_u64(uint64_t x, uint64_t round_to);

static bool jx_isPow2_u32(uint32_t x);
static bool jx_isMultipleOf_u64(uint64_t x, uint64_t of);

static int32_t jx_bitcast_f_i32(float x);
static float jx_bitcast_i32_f(int32_t x);

static uint32_t jx_bisectf(const float* arr, uint32_t n, uint32_t stride, float x);
static uint32_t jx_bisectd(const double* arr, uint32_t n, uint32_t stride, double x);

static uint16_t jx_crc16(const uint8_t* buffer, uint32_t len);

static jx_moving_averagef_t* jx_movAvgfCreate(uint32_t n, jx_allocator_i* allocator);
static void jx_movAvgfDestroy(jx_moving_averagef_t* ma, jx_allocator_i* allocator);
static float jx_movAvgfPush(jx_moving_averagef_t* ma, float val);
static float jx_movAvgfGetAverage(const jx_moving_averagef_t* ma);
static float jx_movAvgfGetStdDev(const jx_moving_averagef_t* ma);
static void jx_movAvgfGetBounds(const jx_moving_averagef_t* ma, float* minVal, float* maxVal);
static const float* jx_movAvgfGetValues(const jx_moving_averagef_t* ma);
static uint32_t jx_movAvgfGetNumValues(const jx_moving_averagef_t* ma);

static uint32_t jx_bitcount_u32(uint32_t x);
static uint32_t jx_ctntz_u64(uint64_t x);

static jx_vec2f_t jx_vec2f_add(jx_vec2f_t a, jx_vec2f_t b);
static jx_vec2f_t jx_vec2f_sub(jx_vec2f_t a, jx_vec2f_t b);
static jx_vec2f_t jx_vec2f_scale(jx_vec2f_t a, float s);
static jx_vec2f_t jx_vec2f_dir(jx_vec2f_t a, jx_vec2f_t b);
static jx_vec2f_t jx_vec2f_perpCCW(jx_vec2f_t a);
static jx_vec2f_t jx_vec2f_perpCW(jx_vec2f_t a);
static float jx_vec2f_dot(jx_vec2f_t a, jx_vec2f_t b);
static float jx_vec2f_cross(jx_vec2f_t a, jx_vec2f_t b);
static float jx_vec2f_distanceSqr(jx_vec2f_t a, jx_vec2f_t b);

#ifdef __cplusplus
}
#endif

#ifndef __cplusplus
#define jx_min(a, b) _Generic((a), \
	float: jx_minf(a, b),          \
	uint32_t: jx_min_u32(a, b),    \
	int32_t: jx_min_i32(a, b),     \
	int16_t: jx_min_i16(a, b)      \
	)

#define jx_max(a, b) _Generic((a), \
	float: jx_maxf(a, b),          \
	uint32_t: jx_max_u32(a, b),    \
	int32_t: jx_max_i32(a, b),     \
	int16_t: jx_max_i16(a, b)      \
	)

#define jx_abs(a) _Generic((a), \
	float: jx_absf(a),          \
	double: jx_absd(a),         \
	int32_t: jx_abs_i32(a),     \
	int16_t: jx_abs_i16(a)      \
	)

#define jx_clamp(a, low, hi) _Generic((a), \
	float: jx_clampf(a, low, hi),          \
	uint32_t: jx_clamp_u32(a, low, hi),    \
	int32_t: jx_clamp_i32(a, low, hi),     \
	int16_t: jx_clamp_i16(a, low, hi)      \
	)

#define jx_bisect(arr, n, stride, x) _Generic((x), \
	float: jx_bisectf,          \
	double: jx_bisectd          \
	)(arr, n, stride, x)
#else // __cplusplus
template<typename T>
T jx_abs(T x)
{
	return x < 0 ? -x : x;
}

template<typename T>
T jx_min(T a, T b)
{
	return a < b ? a : b;
}

template<typename T>
T jx_max(T a, T b)
{
	return a > b ? a : b;
}
#endif // __cplusplus

#include "inline/math.inl"

#endif // JX_MATH_H
