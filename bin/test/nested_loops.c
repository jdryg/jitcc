#include <stdint.h>

int printf(const char* fmt, ...);

static void checkerboard(uint8_t* board, uint32_t w, uint32_t h)
{
	for (uint32_t y = 0; y < h; ++y) {
		for (uint32_t x = 0; x < w; ++x) {
			if (((x & 1) ^ (y & 1))) {
				board[x + y * w] = 0;
			} else {
				board[x + y * w] = 1;
			}
		}
	}
}

int main(void)
{
	uint8_t board[64];
	checkerboard(board, 8, 8);

	for (uint32_t y = 0u; y < 8u; ++y) {
		uint8_t row = 0;
		for (uint32_t x = 0u; x < 8u; ++x) {
			row |= board[x + y * 8] << (7 - x);
		}
		
		if (y & 1) {
			if (row != 0x55) {
				return 1;
			}
		} else {
			if (row != 0xAA) {
				return 2;
			}
		}
	}
	return 0;
}
