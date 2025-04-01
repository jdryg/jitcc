#ifndef JX_IMAGE_H
#error "Must be included from jx/image.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

static inline void jx_img_convert_L1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
	image_api->convert_L1_to_RGBA8(dst, src, w, h);
}

static inline void jx_img_convert_L1_A1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
	image_api->convert_L1_A1_to_RGBA8(dst, src, w, h);
}

static inline void jx_img_convert_BGRA8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h)
{
	image_api->convert_BGRA8_to_RGBA8(dst, src, w, h);
}

static void jx_img_convert_A8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h, uint32_t rgbColor)
{
	image_api->convert_A8_to_RGBA8(dst, src, w, h, rgbColor);
}

#ifdef __cplusplus
}
#endif
