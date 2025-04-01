#include <jlib/math.h>
#include <jlib/macros.h>
#include <jlib/memory.h>
#include <jlib/allocator.h>
#include <jlib/dbg.h>
#include <math.h> // INFINITY

static float _jx_sinf(float x);
static float _jx_cosf(float x);
static float _jx_floorf(float x);
static float _jx_expf(float x);
static float _jx_exp2f(float x);
static float _jx_logf(float x);
static float _jx_log2f(float x);
static float _jx_sqrtf(float x);
static float _jx_powf(float x, float e);
static float _jx_atan2f(float y, float x);
static float _jx_acosf(float x);
static float _jx_asinf(float x);
static float _jx_atanf(float x);
static float _jx_tanf(float x);

static double _jx_sind(double x);
static double _jx_cosd(double x);
static double _jx_floord(double x);
static double _jx_sqrtd(double x);
static double _jx_powd(double x, double e);
static double _jx_atan2d(double y, double x);

static void _jx_doubleToFloat(float* dst, const double* src, uint32_t n);
static void _jx_floatToDouble(double* dst, const float* src, uint32_t n);

static uint32_t _jx_bisectf(const float* arr, uint32_t n, uint32_t stride, float x);
static uint32_t _jx_bisectd(const double* arr, uint32_t n, uint32_t stride, double x);

static uint16_t _jx_crc16(const uint8_t* buffer, uint32_t len);

static jx_moving_averagef_t* _jx_movAvgfCreate(uint32_t n, jx_allocator_i* allocator);
static void _jx_movAvgfDestroy(jx_moving_averagef_t* ma, jx_allocator_i* allocator);
static float _jx_movAvgfPush(jx_moving_averagef_t* ma, float val);
static float _jx_movAvgfGetAverage(const jx_moving_averagef_t* ma);
static float _jx_movAvgfGetStdDev(const jx_moving_averagef_t* ma);
static void _jx_movAvgfGetBounds(const jx_moving_averagef_t* ma, float* minVal, float* maxVal);
static const float* _jx_movAvgfGetValues(const jx_moving_averagef_t* ma);
static uint32_t _jx_movAvgfGetNumValues(const jx_moving_averagef_t* ma);

jx_math_api* math_api = &(jx_math_api){
	.sinf = _jx_sinf,
	.cosf = _jx_cosf,
	.floorf = _jx_floorf,
	.expf = _jx_expf,
	.exp2f = _jx_exp2f,
	.logf = _jx_logf,
	.log2f = _jx_log2f,
	.sqrtf = _jx_sqrtf,
	.powf = _jx_powf,
	.atan2f = _jx_atan2f,
	.acosf = _jx_acosf,
	.asinf = _jx_asinf,
	.atanf = _jx_atanf,
	.tanf = _jx_tanf,

	.sind = _jx_sind,
	.cosd = _jx_cosd,
	.floord = _jx_floord,
	.sqrtd = _jx_sqrtd,
	.powd = _jx_powd,
	.atan2d = _jx_atan2d,

	.doubleToFloat = _jx_doubleToFloat,
	.floatToDouble = _jx_floatToDouble,

	.bisectf = _jx_bisectf,
	.bisectd = _jx_bisectd,

	.crc16 = _jx_crc16,

	.movAvgfCreate = _jx_movAvgfCreate,
	.movAvgfDestroy = _jx_movAvgfDestroy,
	.movAvgfPush = _jx_movAvgfPush,
	.movAvgfGetAverage = _jx_movAvgfGetAverage,
	.movAvgfGetStdDev = _jx_movAvgfGetStdDev,
	.movAvgfGetBounds = _jx_movAvgfGetBounds,
	.movAvgfGetValues = _jx_movAvgfGetValues,
	.movAvgfGetNumValues = _jx_movAvgfGetNumValues,
};

bool jx_math_initAPI(void)
{
	return true;
}

//////////////////////////////////////////////////////////////////////////
// Single-precision functions
//
static const float kPi2f       = 6.283185307179586476925286766559f;
static const float kInvPif     = 0.31830988618379067153776752674503f;
static const float kPiHalff    = 1.5707963267948966192313216916398f;
static const float kPiQuarterf = 0.78539816339744830961566084581988f;

#if 1
// Polynomial approximation of degree 5 for
// sin(x * 2 * pi) in the range [-1/4, 1/4]
// Absolute error: 1.893639565e-04 in [-10*PI, 10*PI]
static float _sin5q(float x)
{
	// A * x + B * x^3 + C * x^5
	// Exact at x = 0, 1/12, 1/6, 1/4, and their negatives,
	// which correspond to x * 2 * pi = 0, pi/6, pi/3, pi/2
	const float A = 0x1.921158p+2;  // 6.2823085463760208716505965852894
	const float B = -0x1.495adep+5; // -41.169367420163339464095453646309
	const float C = 0x1.29c16cp+6;  // 74.438890850352088282974532506846

	const float x2 = x * x;
	return x * (A + x2 * (B + x2 * C));
}

// Polynomial approximation of degree 7 for
// sin(x * 2 * pi) in the range [-1/4, 1/4]
// Absolute error: 3.933906555e-06 in [-10*PI, 10*PI]
static float _sin7q(float x)
{
	const float A = 0x1.921f96p+2;  // 6.2831776987541700224213353085257
	const float B = -0x1.4ab7cp+5;  // -41.339721286280573015160243400613
	const float C = 0x1.45ca7cp+6;  // 81.447735627746840098211596683982
	const float D = -0x1.205d28p+6; // -72.090974853203161528152660107345

	const float x2 = x * x;
	return x * (A + x2 * (B + x2 * (C + x2 * D)));
}

static float _jx_sinf(float x)
{
	const float pi2 = 1.0f / (2.0f * kPif);

	// Range reduction and mirroring
	const float x_2 = 0.25f - x * pi2;
	const float z = 0.25f - jx_absf(x_2 - _jx_floorf(x_2 + 0.5f));
	return _sin7q(z);
}

static float _jx_cosf(float x)
{
	const float pi2 = 1.0f / (2.0f * kPif);

	// Range reduction and mirroring
	float x_2 = x * pi2;
	float z = 0.25f - jx_absf(x_2 - _jx_floorf(x_2 + 0.5f));
	return _sin7q(z);
}
#else
// bx::sin()
static float _jx_sinf(float x)
{
	return _jx_cosf(x - kPiHalff);
}

// bx::cos()
static float _jx_cosf(float x)
{
	static const float kSinC2 = -0.16666667163372039794921875f;
	static const float kSinC4 = 8.333347737789154052734375e-3f;
	static const float kSinC6 = -1.9842604524455964565277099609375e-4f;
	static const float kSinC8 = 2.760012648650445044040679931640625e-6f;
	static const float kSinC10 = -2.50293279435709337121807038784027099609375e-8f;

	static const float kCosC2 = -0.5f;
	static const float kCosC4 = 4.166664183139801025390625e-2f;
	static const float kCosC6 = -1.388833043165504932403564453125e-3f;
	static const float kCosC8 = 2.47562347794882953166961669921875e-5f;
	static const float kCosC10 = -2.59630184018533327616751194000244140625e-7f;

	const float scaled = x * 2.0f * kInvPif;
	const float real = _jx_floorf(scaled);
	const float xx = x - real * kPiHalff;
	const int32_t bits = (int32_t)real & 3;

	float c0, c2, c4, c6, c8, c10;

	if (bits == 0 || bits == 2) {
		c0 = 1.0f;
		c2 = kCosC2;
		c4 = kCosC4;
		c6 = kCosC6;
		c8 = kCosC8;
		c10 = kCosC10;
	} else {
		c0 = xx;
		c2 = kSinC2;
		c4 = kSinC4;
		c6 = kSinC6;
		c8 = kSinC8;
		c10 = kSinC10;
	}

	const float xsq = xx * xx;
	const float tmp0 = c10 * xsq + c8;
	const float tmp1 = tmp0 * xsq + c6;
	const float tmp2 = tmp1 * xsq + c4;
	const float tmp3 = tmp2 * xsq + c2;
	const float tmp4 = tmp3 * xsq + 1.0;
	const float result = tmp4 * c0;

	return bits == 1 || bits == 2
		? -result
		: result
		;
}
#endif

#if 1
static float _jx_floorf(float x)
{
	const int32_t ix = (int32_t)x;
	const float fix = (float)ix;
#if 0
	return fix - ((fix > x) ? 1.0f : 0.0f);
#else
	const uint32_t mask = fix > x ? 0xFFFFFFFF : 0x00000000;
	const uint32_t delta = mask & 0x3F800000; // 1.0f
	return fix - *(const float*)&delta;
#endif
}
#else
// bx::floor()
static float _jx_floorf(float x)
{
	if (x < 0.0f) {
		const float fr = jx_fractf(-x);
		const float result = -x - fr;

		return -(0.0f != fr
			? result + 1.0f
			: result)
			;
	}

	return x - jx_fractf(x);
}
#endif

#if 0
float _jx_expf(float x)
{
	x = jx_minf(x, 88.3762626647949f);
	x = jx_maxf(x, -88.3762626647949f);

	/* express exp(x) as exp(g + n*log(2)) */
	float fx = (x * 1.44269504088896341f) + 0.5f;

	/* how to perform a floorf with SSE: just below */
	int32_t emm0 = (int32_t)fx;
	float tmp = (float)emm0;
	/* if greater, substract 1 */
#if 0
	fx = tmp - ((tmp > fx) ? 1.0f : 0.0f);
#else
	const uint32_t msk = tmp > fx ? 0xFFFFFFFF : 0x00000000;
	const uint32_t mask = msk & 0x3F800000;
	fx = tmp;
	fx -= *(const float*)&mask;
#endif

	tmp = fx * 0.693359375f;
	float z = fx * (-2.12194440e-4f);
	x -= tmp;
	x -= z;

	z = x * x;

	float y = 1.9875691500E-4f;
	y = y * x + 1.3981999507E-3f;
	y = y * x + 8.3334519073E-3f;
	y = y * x + 4.1665795894E-2f;
	y = y * x + 1.6666665459E-1f;
	y = y * x + 5.0000001201E-1f;
	y = y * z + x;
	y = y + 1.0f;

	/* build 2^n */
	emm0 = (int32_t)fx;
	emm0 = emm0 + 0x7f;
	emm0 = emm0 << 23;
	float pow2n = *(float*)&emm0;

	y = y * pow2n;
	return y;
}
#else
#define kExpf_InvLn2 0x1.715476p+0f // 1.4426950408889634073599246810019

static float _jx_expf(float x)
{
	return _jx_exp2f(kExpf_InvLn2 * x);
}
#endif

// Algorithm from https://github.com/google/swiftshader/blob/master/docs/Exp-Log-Optimization.pdf
// Maximum Relative Error: 3.067502519e-07 (x = 2.213944048e-01)
#define kExp2f_ArgMin -0x1.fbfffep+6 // -126.99999237060546875
#define kExp2f_ArgMax 0x1p+7         // 128.0

static float _jx_exp2f(float x)
{
	// Clamp x
	x = jx_minf(x, kExp2f_ArgMax);
	x = jx_maxf(x, kExp2f_ArgMin);

	// Calculate exp2f(x_fractional)
	const int32_t x_i = (int32_t)_jx_floorf(x);	
	const float x_f = x - (float)x_i;

	// Approximation of f(x) = (2^x-1)/x
	// with weight function g(x) = 1/x
	// on interval [ 0, 1 ]
	// with a polynomial of degree 4.
	const float a = 0x1.ee382ap-10; // 1.8852974e-3f;
	const float b = 0x1.260a28p-7;  // 8.9733787e-3f;
	const float c = 0x1.c9686ep-5;  // 5.5835927e-2f;
	const float d = 0x1.ebd53cp-3;  // 2.4015281e-1f;
	const float e = 0x1.62e4e2p-1;  // 6.9315247e-1f;
	const float exp2f = ((((a * x_f + b) * x_f + c) * x_f + d) * x_f + e) * x_f + 1.0f;

	// Calculate exp2f(x_integer)
	const float exp2i = jx_bitcast_i32_f((x_i + 127) << 23);

	// Calculate result
	return exp2i * exp2f;
}

#define kLogf_Ln2 0x1.62e43p-1f // 0.69314718055994530941723212145818
static float _jx_logf(float x)
{
	return kLogf_Ln2 * _jx_log2f(x);
}

// Algorithm from https://github.com/google/swiftshader/blob/master/docs/Exp-Log-Optimization.pdf
// Maximum Relative Error: 3.750227279e-06 (x = 1.000043035e+00)
static float _jx_log2f(float x)
{
	const int32_t im = jx_bitcast_f_i32(x);
	
	float y = (float)(im - (127 << 23)) * (1.0f / (1 << 23));
	if (im == 0x7F800000) {
		y = INFINITY;
	}

	float m = (float)(im & 0x007FFFFF) * (1.0f / (1 << 23));

	const float a = -9.3091638e-3f;
	const float b = 5.2059003e-2f;
	const float c = -1.3752135e-1f;
	const float d = 2.4186478e-1f;
	const float e = -3.4730109e-1f;
	const float f = 4.786837e-1f;
	const float g = -7.2116581e-1f;
	const float h = 4.4268988e-1f;
	return (((((((a * m + b) * m + c) * m + d) * m + e) * m + f) * m + g) * m + h) * m + y;
}

static float _jx_sqrtf(float x)
{
	return sqrtf(x);
}

static float _jx_powf(float x, float e)
{
	return powf(x, e);
}

static float _jx_atan2f(float y, float x)
{
	return atan2f(y, x);
}

static float _jx_acosf(float x)
{
	return acosf(x);
}

static float _jx_asinf(float x)
{
	return asinf(x);
}

static float _jx_atanf(float x)
{
	return atanf(x);
}

static float _jx_tanf(float x)
{
	return tanf(x);
}

//////////////////////////////////////////////////////////////////////////
// Double-precision functions
//
static double _jx_sind(double x)
{
	return sin(x);
}

static double _jx_cosd(double x)
{
	return cos(x);
}

static double _jx_floord(double x)
{
	const int64_t ix = (int64_t)x;
	const double dix = (double)ix;

	const uint64_t mask = dix > x ? 0xFFFFFFFFFFFFFFFF : 0x0000000000000000;
	const uint64_t delta = mask & 0x3ff0000000000000; // 1.0
	return dix - *(const double*)&delta;
}

static double _jx_sqrtd(double x)
{
	return sqrt(x);
}

static double _jx_powd(double x, double e)
{
	return pow(x, e);
}

static double _jx_atan2d(double y, double x)
{
	return atan2(y, x);
}

//////////////////////////////////////////////////////////////////////////
// Conversions
//
static void _jx_doubleToFloat(float* dst, const double* src, uint32_t n)
{
	for (uint32_t i = 0; i < n; ++i) {
		dst[i] = (float)src[i];
	}
}

static void _jx_floatToDouble(double* dst, const float* src, uint32_t n)
{
	for (uint32_t i = 0; i < n; ++i) {
		dst[i] = (double)src[i];
	}
}

//////////////////////////////////////////////////////////////////////////
// Misc
//
static uint32_t _jx_bisectf(const float* arr, uint32_t n, uint32_t stride, float x)
{
	stride = stride == 0
		? sizeof(float)
		: stride
		;

	const uint8_t* ptr = (const uint8_t*)arr;

	uint32_t start = 0;
	uint32_t end = n - 1;

	while (start <= end) {
		uint32_t mid = (start + end) / 2;
		const float val = *(float*)(ptr + mid * stride);
		if (val == x) {
			return mid;
		} else if (val < x) {
			start = mid + 1;
		} else {
			end = mid - 1;
		}
	}

	return end + 1;
}

static uint32_t _jx_bisectd(const double* arr, uint32_t n, uint32_t stride, double x)
{
	stride = stride == 0
		? sizeof(double)
		: stride
		;

	const uint8_t* ptr = (const uint8_t*)arr;

	int32_t start = 0;
	int32_t end = (int32_t)(n - 1);

	while (start <= end) {
		int32_t mid = (start + end) / 2;
		const double val = *(double*)(ptr + mid * stride);
		if (val == x) {
			return mid;
		} else if (val < x) {
			start = mid + 1;
		} else {
			end = mid - 1;
		}
	}

	return (uint32_t)(end + 1);
}

static uint16_t _jx_crc16(const uint8_t* buffer, uint32_t len)
{
	const uint8_t* ptr = buffer;
	uint32_t remaining = len;

	uint16_t crc16 = 0xFFFF;
	while (remaining-- > 0) {
		uint16_t x = crc16 >> 8 ^ *ptr++;
		x ^= x >> 4;
		crc16 = (crc16 << 8) ^ ((uint16_t)(x << 12)) ^ ((uint16_t)(x << 5)) ^ ((uint16_t)x);
	}

	return crc16;
}

//////////////////////////////////////////////////////////////////////////
// Moving average
//
typedef struct jx_moving_averagef_t
{
	jx_allocator_i* m_Allocator;
	float* m_Data;
	float m_Total;
	uint32_t m_Capacity;
	uint32_t m_Count;
	uint32_t m_InsertPos;
} jx_moving_averagef_t;

static jx_moving_averagef_t* _jx_movAvgfCreate(uint32_t n, jx_allocator_i* allocator)
{
	const uint32_t totalMem = 0
		+ jx_nextMultipleOf_u32(sizeof(jx_moving_averagef_t), 16)
		+ jx_nextMultipleOf_u32(sizeof(float) * n, 16);

	uint8_t* buffer = (uint8_t*)JX_ALIGNED_ALLOC(allocator, totalMem, 16);
	if (!buffer) {
		return NULL;
	}

	jx_memset(buffer, 0, totalMem);

	uint8_t* ptr = buffer;
	jx_moving_averagef_t* ma = (jx_moving_averagef_t*)ptr; ptr += jx_nextMultipleOf_u32(sizeof(jx_moving_averagef_t), 16);
	ma->m_Data = (float*)ptr;                              ptr += jx_nextMultipleOf_u32(sizeof(float) * n, 16);

	ma->m_Allocator = allocator;
	ma->m_InsertPos = 0;
	ma->m_Count = 0;
	ma->m_Total = 0.0f;
	ma->m_Capacity = n;

	return ma;
}

static void _jx_movAvgfDestroy(jx_moving_averagef_t* ma, jx_allocator_i* allocator)
{
	JX_ALIGNED_FREE(allocator, ma, 16);
}

static float _jx_movAvgfPush(jx_moving_averagef_t* ma, float val)
{
	ma->m_Total -= ma->m_Data[ma->m_InsertPos];
	ma->m_Total += val;
	ma->m_Data[ma->m_InsertPos] = val;
	ma->m_InsertPos = (ma->m_InsertPos + 1) % ma->m_Capacity;

	ma->m_Count = jx_min_u32(ma->m_Count + 1, ma->m_Capacity);

	return _jx_movAvgfGetAverage(ma);
}

static float _jx_movAvgfGetAverage(const jx_moving_averagef_t* ma)
{
	return ma->m_Total / (float)ma->m_Count;
}

static float _jx_movAvgfGetStdDev(const jx_moving_averagef_t* ma)
{
	if (ma->m_Count == 0) {
		return 0.0f;
	}

	const float avg = _jx_movAvgfGetAverage(ma);
	float stdDev = 0.0f;
	const uint32_t n = ma->m_Count;
	for (uint32_t i = 0; i < n; ++i) {
		const float v = ma->m_Data[i];
		const float d = v - avg;

		stdDev += d * d;
	}

	return jx_sqrtf(stdDev / ma->m_Count);
}

static void _jx_movAvgfGetBounds(const jx_moving_averagef_t* ma, float* minVal, float* maxVal)
{
	float min = 0.0f, max = 0.0f;
	if (ma->m_Count != 0) {
		min = ma->m_Data[0];
		max = ma->m_Data[0];
		const uint32_t n = ma->m_Count;
		for (uint32_t i = 1; i < n; ++i) {
			const float v = ma->m_Data[i];
			min = jx_minf(min, v);
			max = jx_maxf(max, v);
		}
	}

	if (minVal) {
		*minVal = min;
	}
	if (maxVal) {
		*maxVal = max;
	}
}

static const float* _jx_movAvgfGetValues(const jx_moving_averagef_t* ma)
{
	return ma->m_Data;
}

static uint32_t _jx_movAvgfGetNumValues(const jx_moving_averagef_t* ma)
{
	return ma->m_Count;
}

#if 0 // http://gruntthepeon.free.fr/ssemath/
#include <xmmintrin.h>
#include <emmintrin.h>


#define ALIGN16_BEG __declspec(align(16))
#define ALIGN16_END 

typedef __m128 v4sf;  // vector of 4 float (sse1)
typedef __m128i v4si; // vector of 4 int (sse2)

/* declare some SSE constants -- why can't I figure a better way to do that? */
#define _PS_CONST(Name, Val)            static const ALIGN16_BEG float _ps_##Name[4] ALIGN16_END = { Val, Val, Val, Val }
#define _PI32_CONST(Name, Val)          static const ALIGN16_BEG int _pi32_##Name[4] ALIGN16_END = { Val, Val, Val, Val }
#define _PS_CONST_TYPE(Name, Type, Val) static const ALIGN16_BEG Type _ps_##Name[4] ALIGN16_END = { Val, Val, Val, Val }

_PS_CONST(1, 1.0f);
_PS_CONST(0p5, 0.5f);

/* the smallest non denormalized float number */
_PS_CONST_TYPE(min_norm_pos, int, 0x00800000);
_PS_CONST_TYPE(mant_mask, int, 0x7f800000);
_PS_CONST_TYPE(inv_mant_mask, int, ~0x7f800000);

_PS_CONST_TYPE(sign_mask, int, (int)0x80000000);
_PS_CONST_TYPE(inv_sign_mask, int, ~0x80000000);

_PI32_CONST(1, 1);
_PI32_CONST(inv1, ~1);
_PI32_CONST(2, 2);
_PI32_CONST(4, 4);
_PI32_CONST(0x7f, 0x7f);

_PS_CONST(cephes_SQRTHF, 0.707106781186547524);
_PS_CONST(cephes_log_p0, 7.0376836292E-2);
_PS_CONST(cephes_log_p1, -1.1514610310E-1);
_PS_CONST(cephes_log_p2, 1.1676998740E-1);
_PS_CONST(cephes_log_p3, -1.2420140846E-1);
_PS_CONST(cephes_log_p4, +1.4249322787E-1);
_PS_CONST(cephes_log_p5, -1.6668057665E-1);
_PS_CONST(cephes_log_p6, +2.0000714765E-1);
_PS_CONST(cephes_log_p7, -2.4999993993E-1);
_PS_CONST(cephes_log_p8, +3.3333331174E-1);
_PS_CONST(cephes_log_q1, -2.12194440e-4);
_PS_CONST(cephes_log_q2, 0.693359375);

/* natural logarithm computed for 4 simultaneous float
   return NaN for x <= 0
*/
v4sf log_ps(v4sf x)
{
	v4si emm0;
	v4sf one = *(v4sf*)_ps_1;

	v4sf invalid_mask = _mm_cmple_ps(x, _mm_setzero_ps());

	x = _mm_max_ps(x, *(v4sf*)_ps_min_norm_pos);  /* cut off denormalized stuff */

	emm0 = _mm_srli_epi32(_mm_castps_si128(x), 23);
	/* keep only the fractional part */
	x = _mm_and_ps(x, *(v4sf*)_ps_inv_mant_mask);
	x = _mm_or_ps(x, *(v4sf*)_ps_0p5);

	emm0 = _mm_sub_epi32(emm0, *(v4si*)_pi32_0x7f);
	v4sf e = _mm_cvtepi32_ps(emm0);

	e = _mm_add_ps(e, one);

	/* part2:
	   if( x < SQRTHF ) {
		 e -= 1;
		 x = x + x - 1.0;
	   } else { x = x - 1.0; }
	*/
	v4sf mask = _mm_cmplt_ps(x, *(v4sf*)_ps_cephes_SQRTHF);
	v4sf tmp = _mm_and_ps(x, mask);
	x = _mm_sub_ps(x, one);
	e = _mm_sub_ps(e, _mm_and_ps(one, mask));
	x = _mm_add_ps(x, tmp);


	v4sf z = _mm_mul_ps(x, x);

	v4sf y = *(v4sf*)_ps_cephes_log_p0;
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p1);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p2);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p3);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p4);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p5);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p6);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p7);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_log_p8);
	y = _mm_mul_ps(y, x);

	y = _mm_mul_ps(y, z);


	tmp = _mm_mul_ps(e, *(v4sf*)_ps_cephes_log_q1);
	y = _mm_add_ps(y, tmp);


	tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
	y = _mm_sub_ps(y, tmp);

	tmp = _mm_mul_ps(e, *(v4sf*)_ps_cephes_log_q2);
	x = _mm_add_ps(x, y);
	x = _mm_add_ps(x, tmp);
	x = _mm_or_ps(x, invalid_mask); // negative arg will be NAN
	return x;
}

_PS_CONST(exp_hi, 88.3762626647949f);
_PS_CONST(exp_lo, -88.3762626647949f);

_PS_CONST(cephes_LOG2EF, 1.44269504088896341);
_PS_CONST(cephes_exp_C1, 0.693359375);
_PS_CONST(cephes_exp_C2, -2.12194440e-4);

_PS_CONST(cephes_exp_p0, 1.9875691500E-4);
_PS_CONST(cephes_exp_p1, 1.3981999507E-3);
_PS_CONST(cephes_exp_p2, 8.3334519073E-3);
_PS_CONST(cephes_exp_p3, 4.1665795894E-2);
_PS_CONST(cephes_exp_p4, 1.6666665459E-1);
_PS_CONST(cephes_exp_p5, 5.0000001201E-1);

v4sf exp_ps(v4sf x)
{
	v4sf tmp = _mm_setzero_ps(), fx;
	v4si emm0;
	v4sf one = *(v4sf*)_ps_1;

	x = _mm_min_ps(x, *(v4sf*)_ps_exp_hi);
	x = _mm_max_ps(x, *(v4sf*)_ps_exp_lo);

	/* express exp(x) as exp(g + n*log(2)) */
	fx = _mm_mul_ps(x, *(v4sf*)_ps_cephes_LOG2EF);
	fx = _mm_add_ps(fx, *(v4sf*)_ps_0p5);

	/* how to perform a floorf with SSE: just below */
	emm0 = _mm_cvttps_epi32(fx);
	tmp = _mm_cvtepi32_ps(emm0);
	/* if greater, substract 1 */
	v4sf mask = _mm_cmpgt_ps(tmp, fx);
	mask = _mm_and_ps(mask, one);
	fx = _mm_sub_ps(tmp, mask);

	tmp = _mm_mul_ps(fx, *(v4sf*)_ps_cephes_exp_C1);
	v4sf z = _mm_mul_ps(fx, *(v4sf*)_ps_cephes_exp_C2);
	x = _mm_sub_ps(x, tmp);
	x = _mm_sub_ps(x, z);

	z = _mm_mul_ps(x, x);

	v4sf y = *(v4sf*)_ps_cephes_exp_p0;
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p1);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p2);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p3);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p4);
	y = _mm_mul_ps(y, x);
	y = _mm_add_ps(y, *(v4sf*)_ps_cephes_exp_p5);
	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, x);
	y = _mm_add_ps(y, one);

	/* build 2^n */
	emm0 = _mm_cvttps_epi32(fx);
	emm0 = _mm_add_epi32(emm0, *(v4si*)_pi32_0x7f);
	emm0 = _mm_slli_epi32(emm0, 23);
	v4sf pow2n = _mm_castsi128_ps(emm0);

	y = _mm_mul_ps(y, pow2n);
	return y;
}

_PS_CONST(minus_cephes_DP1, -0.78515625);
_PS_CONST(minus_cephes_DP2, -2.4187564849853515625e-4);
_PS_CONST(minus_cephes_DP3, -3.77489497744594108e-8);
_PS_CONST(sincof_p0, -1.9515295891E-4);
_PS_CONST(sincof_p1, 8.3321608736E-3);
_PS_CONST(sincof_p2, -1.6666654611E-1);
_PS_CONST(coscof_p0, 2.443315711809948E-005);
_PS_CONST(coscof_p1, -1.388731625493765E-003);
_PS_CONST(coscof_p2, 4.166664568298827E-002);
_PS_CONST(cephes_FOPI, 1.27323954473516); // 4 / M_PI


/* evaluation of 4 sines at onces, using only SSE1+MMX intrinsics so
   it runs also on old athlons XPs and the pentium III of your grand
   mother.

   The code is the exact rewriting of the cephes sinf function.
   Precision is excellent as long as x < 8192 (I did not bother to
   take into account the special handling they have for greater values
   -- it does not return garbage for arguments over 8192, though, but
   the extra precision is missing).

   Note that it is such that sinf((float)M_PI) = 8.74e-8, which is the
   surprising but correct result.

   Performance is also surprisingly good, 1.33 times faster than the
   macos vsinf SSE2 function, and 1.5 times faster than the
   __vrs4_sinf of amd's ACML (which is only available in 64 bits). Not
   too bad for an SSE1 function (with no special tuning) !
   However the latter libraries probably have a much better handling of NaN,
   Inf, denormalized and other special arguments..

   On my core 1 duo, the execution of this function takes approximately 95 cycles.

   From what I have observed on the experiments with Intel AMath lib, switching to an
   SSE2 version would improve the perf by only 10%.

   Since it is based on SSE intrinsics, it has to be compiled at -O2 to
   deliver full speed.
*/
v4sf sin_ps(v4sf x)
{ // any x
	v4sf xmm1, xmm2 = _mm_setzero_ps(), xmm3, sign_bit, y;

	v4si emm0, emm2;

	sign_bit = x;
	/* take the absolute value */
	x = _mm_and_ps(x, *(v4sf*)_ps_inv_sign_mask);
	/* extract the sign bit (upper one) */
	sign_bit = _mm_and_ps(sign_bit, *(v4sf*)_ps_sign_mask);

	/* scale by 4/Pi */
	y = _mm_mul_ps(x, *(v4sf*)_ps_cephes_FOPI);

	/* store the integer part of y in mm0 */
	emm2 = _mm_cvttps_epi32(y);
	/* j=(j+1) & (~1) (see the cephes sources) */
	emm2 = _mm_add_epi32(emm2, *(v4si*)_pi32_1);
	emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_inv1);
	y = _mm_cvtepi32_ps(emm2);

	/* get the swap sign flag */
	emm0 = _mm_and_si128(emm2, *(v4si*)_pi32_4);
	emm0 = _mm_slli_epi32(emm0, 29);
	/* get the polynom selection mask
	   there is one polynom for 0 <= x <= Pi/4
	   and another one for Pi/4<x<=Pi/2

	   Both branches will be computed.
	*/
	emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_2);
	emm2 = _mm_cmpeq_epi32(emm2, _mm_setzero_si128());

	v4sf swap_sign_bit = _mm_castsi128_ps(emm0);
	v4sf poly_mask = _mm_castsi128_ps(emm2);
	sign_bit = _mm_xor_ps(sign_bit, swap_sign_bit);

	/* The magic pass: "Extended precision modular arithmetic"
	   x = ((x - y * DP1) - y * DP2) - y * DP3; */
	xmm1 = *(v4sf*)_ps_minus_cephes_DP1;
	xmm2 = *(v4sf*)_ps_minus_cephes_DP2;
	xmm3 = *(v4sf*)_ps_minus_cephes_DP3;
	xmm1 = _mm_mul_ps(y, xmm1);
	xmm2 = _mm_mul_ps(y, xmm2);
	xmm3 = _mm_mul_ps(y, xmm3);
	x = _mm_add_ps(x, xmm1);
	x = _mm_add_ps(x, xmm2);
	x = _mm_add_ps(x, xmm3);

	/* Evaluate the first polynom  (0 <= x <= Pi/4) */
	y = *(v4sf*)_ps_coscof_p0;
	v4sf z = _mm_mul_ps(x, x);

	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p1);
	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p2);
	y = _mm_mul_ps(y, z);
	y = _mm_mul_ps(y, z);
	v4sf tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
	y = _mm_sub_ps(y, tmp);
	y = _mm_add_ps(y, *(v4sf*)_ps_1);

	/* Evaluate the second polynom  (Pi/4 <= x <= 0) */

	v4sf y2 = *(v4sf*)_ps_sincof_p0;
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p1);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p2);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_mul_ps(y2, x);
	y2 = _mm_add_ps(y2, x);

	/* select the correct result from the two polynoms */
	xmm3 = poly_mask;
	y2 = _mm_and_ps(xmm3, y2); //, xmm3);
	y = _mm_andnot_ps(xmm3, y);
	y = _mm_add_ps(y, y2);
	/* update the sign */
	y = _mm_xor_ps(y, sign_bit);
	return y;
}

/* almost the same as sin_ps */
v4sf cos_ps(v4sf x)
{ // any x
	v4sf xmm1, xmm2 = _mm_setzero_ps(), xmm3, y;
	v4si emm0, emm2;

	/* take the absolute value */
	x = _mm_and_ps(x, *(v4sf*)_ps_inv_sign_mask);

	/* scale by 4/Pi */
	y = _mm_mul_ps(x, *(v4sf*)_ps_cephes_FOPI);

	/* store the integer part of y in mm0 */
	emm2 = _mm_cvttps_epi32(y);
	/* j=(j+1) & (~1) (see the cephes sources) */
	emm2 = _mm_add_epi32(emm2, *(v4si*)_pi32_1);
	emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_inv1);
	y = _mm_cvtepi32_ps(emm2);

	emm2 = _mm_sub_epi32(emm2, *(v4si*)_pi32_2);

	/* get the swap sign flag */
	emm0 = _mm_andnot_si128(emm2, *(v4si*)_pi32_4);
	emm0 = _mm_slli_epi32(emm0, 29);
	/* get the polynom selection mask */
	emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_2);
	emm2 = _mm_cmpeq_epi32(emm2, _mm_setzero_si128());

	v4sf sign_bit = _mm_castsi128_ps(emm0);
	v4sf poly_mask = _mm_castsi128_ps(emm2);

	/* The magic pass: "Extended precision modular arithmetic"
	   x = ((x - y * DP1) - y * DP2) - y * DP3; */
	xmm1 = *(v4sf*)_ps_minus_cephes_DP1;
	xmm2 = *(v4sf*)_ps_minus_cephes_DP2;
	xmm3 = *(v4sf*)_ps_minus_cephes_DP3;
	xmm1 = _mm_mul_ps(y, xmm1);
	xmm2 = _mm_mul_ps(y, xmm2);
	xmm3 = _mm_mul_ps(y, xmm3);
	x = _mm_add_ps(x, xmm1);
	x = _mm_add_ps(x, xmm2);
	x = _mm_add_ps(x, xmm3);

	/* Evaluate the first polynom  (0 <= x <= Pi/4) */
	y = *(v4sf*)_ps_coscof_p0;
	v4sf z = _mm_mul_ps(x, x);

	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p1);
	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p2);
	y = _mm_mul_ps(y, z);
	y = _mm_mul_ps(y, z);
	v4sf tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
	y = _mm_sub_ps(y, tmp);
	y = _mm_add_ps(y, *(v4sf*)_ps_1);

	/* Evaluate the second polynom  (Pi/4 <= x <= 0) */

	v4sf y2 = *(v4sf*)_ps_sincof_p0;
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p1);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p2);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_mul_ps(y2, x);
	y2 = _mm_add_ps(y2, x);

	/* select the correct result from the two polynoms */
	xmm3 = poly_mask;
	y2 = _mm_and_ps(xmm3, y2); //, xmm3);
	y = _mm_andnot_ps(xmm3, y);
	y = _mm_add_ps(y, y2);
	/* update the sign */
	y = _mm_xor_ps(y, sign_bit);

	return y;
}

/* since sin_ps and cos_ps are almost identical, sincos_ps could replace both of them..
   it is almost as fast, and gives you a free cosine with your sine */
void sincos_ps(v4sf x, v4sf* s, v4sf* c)
{
	v4sf xmm1, xmm2, xmm3 = _mm_setzero_ps(), sign_bit_sin, y;
	v4si emm0, emm2, emm4;

	sign_bit_sin = x;
	/* take the absolute value */
	x = _mm_and_ps(x, *(v4sf*)_ps_inv_sign_mask);
	/* extract the sign bit (upper one) */
	sign_bit_sin = _mm_and_ps(sign_bit_sin, *(v4sf*)_ps_sign_mask);

	/* scale by 4/Pi */
	y = _mm_mul_ps(x, *(v4sf*)_ps_cephes_FOPI);

	/* store the integer part of y in emm2 */
	emm2 = _mm_cvttps_epi32(y);

	/* j=(j+1) & (~1) (see the cephes sources) */
	emm2 = _mm_add_epi32(emm2, *(v4si*)_pi32_1);
	emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_inv1);
	y = _mm_cvtepi32_ps(emm2);

	emm4 = emm2;

	/* get the swap sign flag for the sine */
	emm0 = _mm_and_si128(emm2, *(v4si*)_pi32_4);
	emm0 = _mm_slli_epi32(emm0, 29);
	v4sf swap_sign_bit_sin = _mm_castsi128_ps(emm0);

	/* get the polynom selection mask for the sine*/
	emm2 = _mm_and_si128(emm2, *(v4si*)_pi32_2);
	emm2 = _mm_cmpeq_epi32(emm2, _mm_setzero_si128());
	v4sf poly_mask = _mm_castsi128_ps(emm2);

	/* The magic pass: "Extended precision modular arithmetic"
	   x = ((x - y * DP1) - y * DP2) - y * DP3; */
	xmm1 = *(v4sf*)_ps_minus_cephes_DP1;
	xmm2 = *(v4sf*)_ps_minus_cephes_DP2;
	xmm3 = *(v4sf*)_ps_minus_cephes_DP3;
	xmm1 = _mm_mul_ps(y, xmm1);
	xmm2 = _mm_mul_ps(y, xmm2);
	xmm3 = _mm_mul_ps(y, xmm3);
	x = _mm_add_ps(x, xmm1);
	x = _mm_add_ps(x, xmm2);
	x = _mm_add_ps(x, xmm3);

	emm4 = _mm_sub_epi32(emm4, *(v4si*)_pi32_2);
	emm4 = _mm_andnot_si128(emm4, *(v4si*)_pi32_4);
	emm4 = _mm_slli_epi32(emm4, 29);
	v4sf sign_bit_cos = _mm_castsi128_ps(emm4);

	sign_bit_sin = _mm_xor_ps(sign_bit_sin, swap_sign_bit_sin);


	/* Evaluate the first polynom  (0 <= x <= Pi/4) */
	v4sf z = _mm_mul_ps(x, x);
	y = *(v4sf*)_ps_coscof_p0;

	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p1);
	y = _mm_mul_ps(y, z);
	y = _mm_add_ps(y, *(v4sf*)_ps_coscof_p2);
	y = _mm_mul_ps(y, z);
	y = _mm_mul_ps(y, z);
	v4sf tmp = _mm_mul_ps(z, *(v4sf*)_ps_0p5);
	y = _mm_sub_ps(y, tmp);
	y = _mm_add_ps(y, *(v4sf*)_ps_1);

	/* Evaluate the second polynom  (Pi/4 <= x <= 0) */

	v4sf y2 = *(v4sf*)_ps_sincof_p0;
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p1);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_add_ps(y2, *(v4sf*)_ps_sincof_p2);
	y2 = _mm_mul_ps(y2, z);
	y2 = _mm_mul_ps(y2, x);
	y2 = _mm_add_ps(y2, x);

	/* select the correct result from the two polynoms */
	xmm3 = poly_mask;
	v4sf ysin2 = _mm_and_ps(xmm3, y2);
	v4sf ysin1 = _mm_andnot_ps(xmm3, y);
	y2 = _mm_sub_ps(y2, ysin2);
	y = _mm_sub_ps(y, ysin1);

	xmm1 = _mm_add_ps(ysin1, ysin2);
	xmm2 = _mm_add_ps(y, y2);

	/* update the sign */
	*s = _mm_xor_ps(xmm1, sign_bit_sin);
	*c = _mm_xor_ps(xmm2, sign_bit_cos);
}
#endif