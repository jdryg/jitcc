#include <jlib/image.h>

static void jimg_convert_L1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);
static void jimg_convert_L1_A1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);
static void jimg_convert_BGRA8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);
static void jimg_convert_A8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h, uint32_t rgbColor);

jx_image_api* image_api = &(jx_image_api)
{
	.convert_L1_to_RGBA8 = jimg_convert_L1_to_RGBA8,
	.convert_L1_A1_to_RGBA8 = jimg_convert_L1_A1_to_RGBA8,
	.convert_BGRA8_to_RGBA8 = jimg_convert_BGRA8_to_RGBA8,
	.convert_A8_to_RGBA8 = jimg_convert_A8_to_RGBA8,
};

static void jimg_convert_L1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
	const uint32_t numPixels = w * h;
	for (uint32_t i = 0; i < numPixels; ++i) {
		const uint32_t bitmapElementID = i >> 3;
		const uint32_t bitmapBitID = i & 7;

		const uint8_t pixelValue = ((src[bitmapElementID] >> (7 - bitmapBitID)) & 1) ? 0x00 : 0xFF;

		dst[0] = pixelValue;
		dst[1] = pixelValue;
		dst[2] = pixelValue;
		dst[3] = 255;
		dst += 4;
	}
}

static void jimg_convert_L1_A1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
	const uint32_t numPixels = w * h;
	const uint8_t* srcColor = src;
	const uint8_t* srcAlpha = src + (numPixels >> 3); // 8 pixels/byte

	for (uint32_t i = 0; i < numPixels; ++i) {
		const uint32_t bitmapElementID = i >> 3;
		const uint32_t bitmapBitID = i & 7;

		const uint8_t color = ((srcColor[bitmapElementID] >> (7 - bitmapBitID)) & 1) ? 0x00 : 0xFF;
		const uint8_t alpha = ((srcAlpha[bitmapElementID] >> (7 - bitmapBitID)) & 1) ? 0xFF : 0x01;

		dst[0] = color;
		dst[1] = color;
		dst[2] = color;
		dst[3] = alpha;
		dst += 4;
	}
}

static void jimg_convert_BGRA8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
	const uint32_t numPixels = w * h;
	for (uint32_t i = 0; i < numPixels; i++) {
		dst[0] = src[2];
		dst[1] = src[1];
		dst[2] = src[0];
		dst[3] = src[3];
		dst += 4;
		src += 4;
	}
}

static void jimg_convert_A8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h, uint32_t rgbColor)
{
	const uint32_t rgb0 = rgbColor & 0x00FFFFFF;

	uint32_t* rgba = (uint32_t*)dst;
	const uint8_t* a8 = src;
	uint32_t numPixels = w * h;
	for (uint32_t i = 0; i < numPixels; ++i) {
		*rgba++ = rgb0 | (((uint32_t)*a8) << 24);
		++a8;
	}
}
