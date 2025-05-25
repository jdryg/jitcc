#include <stdint.h>
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

static void generateCheckerboardImg(uint8_t* pixels, uint32_t w, uint32_t h)
{
	uint8_t* ptr = pixels;
	for (uint32_t y = 0; y < h; ++y) {
		uint32_t ry = y / 8;
		for (uint32_t x = 0; x < w; ++x) {
			uint32_t rx = x / 8;
			if (((rx & 1) ^ (ry & 1))) {
				*pixels++ = 255;
				*pixels++ = 255;
				*pixels++ = 255;
				*pixels++ = 255;
			} else {
				*pixels++ = 0;
				*pixels++ = 0;
				*pixels++ = 0;
				*pixels++ = 255;
			}
		}
	}
}

int main(void)
{
	uint8_t checkerboard[64 * 64 * 4];
	generateCheckerboardImg(checkerboard, 64, 64);
	stbi_write_png("./checkerboard.png", 64, 64, 4, checkerboard, 64 * 4);

	return 0;
}

