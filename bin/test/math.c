typedef int int32_t;
typedef unsigned int uint32_t;

static const float kPif = 3.14159274101257324219f; // Pi value from Windows calculator rounded by https://float.exposed/ to float
static const float kPi2f = 6.283185307179586476925286766559f;
static const float kInvPif = 0.31830988618379067153776752674503f;
static const float kPiHalff = 1.5707963267948966192313216916398f;
static const float kPiQuarterf = 0.78539816339744830961566084581988f;

static inline float jx_absf(float x)
{
	return x < 0.0f ? -x : x;
}

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

// Polynomial approximation of degree 7 for
// sin(x * 2 * pi) in the range [-1/4, 1/4]
// Absolute error: 3.933906555e-06 in [-10*PI, 10*PI]
static float _sin7q(float x)
{
	const float A = 6.2831776987541700224213353085257f;
	const float B = -41.339721286280573015160243400613f;
	const float C = 81.447735627746840098211596683982f;
	const float D = -72.090974853203161528152660107345f;

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

int printf(const char* str, ...);

int main(void)
{
	printf("%f: (%f, %f) \n", 0.0f, _jx_cosf(0.0f), _jx_sinf(0.0f));
	printf("%f: (%f, %f) \n", kPiHalff, _jx_cosf(kPiHalff), _jx_sinf(kPiHalff));
	printf("%f: (%f, %f) \n", kPif, _jx_cosf(kPif), _jx_sinf(kPif));
	printf("%f: (%f, %f) \n", kPif + kPiHalff, _jx_cosf(kPif + kPiHalff), _jx_sinf(kPif + kPiHalff));

	return 0;
}