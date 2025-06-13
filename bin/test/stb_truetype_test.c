#include <stdio.h>
#include <stdint.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

int main(void)
{
    stbtt_fontinfo font;
    unsigned char* bitmap;
    int w, h, i, j, c = 'a', s = 20;

    FILE* f = fopen("c:/windows/fonts/arialbd.ttf", "rb");
    fseek(f, 0, SEEK_END);
    int64_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* ttf_buffer = (uint8_t*)malloc(sz);
    fread(ttf_buffer, 1, sz, f);
    fclose(f);

    stbtt_InitFont(&font, ttf_buffer, stbtt_GetFontOffsetForIndex(ttf_buffer, 0));
    bitmap = stbtt_GetCodepointBitmap(&font, 0, stbtt_ScaleForPixelHeight(&font, s), c, &w, &h, 0, 0);

    printf("bitmap size: %dx%d\n", w, h);
    for (j = 0; j < h; ++j) {
#if 1
        for (i = 0; i < w; ++i) {
            printf("%02X", bitmap[j * w + i]);
        }
        putchar(' ');
#endif
        for (i = 0; i < w; ++i) {
            putchar(" .:ioVM@"[bitmap[j * w + i] >> 5]);
        }

        putchar('\n');
    }

    free(ttf_buffer);

    return 0;
}

