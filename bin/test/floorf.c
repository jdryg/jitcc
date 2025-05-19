#include <stdint.h>

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

int main(void)
{
	if ((int32_t)_jx_floorf(5.5f) != 5) {
		return 1;
	}

	return 0;
}
