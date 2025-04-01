#ifndef JX_IMAGE_H
#define JX_IMAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct jx_image_api
{
	// NOTE: Assumes 'w' is a multiply of 8
	void (*convert_L1_to_RGBA8)(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);

	// NOTE: Assumes 'w' is a multiply of 8
	void (*convert_L1_A1_to_RGBA8)(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);

	void (*convert_BGRA8_to_RGBA8)(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);
	void (*convert_A8_to_RGBA8)(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h, uint32_t rgbColor);
} jx_image_api;

extern jx_image_api* image_api;

static void jx_img_convert_L1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);
static void jx_img_convert_L1_A1_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);
static void jx_img_convert_BGRA8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h);
static void jx_img_convert_A8_to_RGBA8(uint8_t* dst, const uint8_t* src, uint32_t w, uint32_t h, uint32_t rgbColor);

#ifdef __cplusplus
}
#endif

#include "inline/image.inl"

#endif // JX_IMAGE_H
