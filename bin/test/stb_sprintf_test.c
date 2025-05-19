#define STB_SPRINTF_STATIC
#define STB_SPRINTF_IMPLEMENTATION
#include "stb/stb_sprintf.h"

int printf(const char* fmt, ...);

int main(void)
{
	char buffer[256];
	stbsp_snprintf(buffer, 256, "Hello %s from %s!!!", "world", "jitcc");
	printf("stb: %s\n", buffer);
	printf("crt: Hello %s from %s!!!\n", "world", "jitcc");

	stbsp_snprintf(buffer, 256, "i32: %d, u32: %u, i64: %lld, u64: %llu", -123456, 456789, -1122334455667788, 1122334455667788);
	printf("stb: %s\n", buffer);
	printf("crt: i32: %d, u32: %u, i64: %lld, u64: %llu\n", -123456, 456789, -1122334455667788, 1122334455667788);

	stbsp_snprintf(buffer, 256, "f32: %f", 3.1415926f);
	printf("stb: %s\n", buffer);
	printf("crt: f32: %f\n", 3.1415926f);

	return 0;
}
