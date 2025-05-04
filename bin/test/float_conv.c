typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int int64_t;

float s8_f32(int8_t x)   { return (float)x;   }
float s16_f32(int16_t x) { return (float)x;   }
float s32_f32(int32_t x) { return (float)x;   }
float s64_f32(int64_t x) { return (float)x;   }
int8_t f32_s8(float f)   { return (int8_t)f;  }
int16_t f32_s16(float f) { return (int16_t)f; }
int32_t f32_s32(float f) { return (int32_t)f; }
int64_t f32_s64(float f) { return (int64_t)f; }
float f64_f32(double f)  { return (float)f;   }
double f32_f64(float f)  { return (double)f;  }

int main(void)
{
    if (s8_f32(16) != 16.0f) {
        return 1;
    }
    if (s16_f32(8192) != 8192.0f) {
        return 2;
    }
    if (s32_f32(0x123456) != 1193046.0f) {
        return 3;
    }
    if (s64_f32(0x123456) != 1193046.0f) {
        return 4;
    }

    if (f32_s8(16.0f) != (int8_t)16) {
        return 1;
    }
    if (f32_s16(8192.0f) != (int16_t)8192) {
        return 2;
    }
    if (f32_s32(1193046.0f) != 1193046) {
        return 3;
    }
    if (f32_s64(1193046.0f) != 1193046ll) {
        return 4;
    }

    if (f32_f64(1193046.0f) != 1193046.0) {
        return 5;
    }
    if (f64_f32(1193046.0) != 1193046.0f) {
        return 6;
    }

    return 0;
}
