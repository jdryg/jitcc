typedef char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int int64_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long int uint64_t;

float   s8_f32(int8_t x)   { return (float)x;   }
float   s16_f32(int16_t x) { return (float)x;   }
float   s32_f32(int32_t x) { return (float)x;   }
float   s64_f32(int64_t x) { return (float)x;   }
double  s8_f64(int8_t x)   { return (double)x;  }
double  s16_f64(int16_t x) { return (double)x;  }
double  s32_f64(int32_t x) { return (double)x;  }
double  s64_f64(int64_t x) { return (double)x;  }
int8_t  f32_s8(float f)    { return (int8_t)f;  }
int16_t f32_s16(float f)   { return (int16_t)f; }
int32_t f32_s32(float f)   { return (int32_t)f; }
int64_t f32_s64(float f)   { return (int64_t)f; }
int8_t  f64_s8(double f)   { return (int8_t)f;  }
int16_t f64_s16(double f)  { return (int16_t)f; }
int32_t f64_s32(double f)  { return (int32_t)f; }
int64_t f64_s64(double f)  { return (int64_t)f; }

float   f64_f32(double f)  { return (float)f;   }
double  f32_f64(float f)   { return (double)f;  }

float   u8_f32(uint8_t x)   { return (float)x;    }
float   u16_f32(uint16_t x) { return (float)x;    }
float   u32_f32(uint32_t x) { return (float)x;    }
float   u64_f32(uint64_t x) { return (float)x;    }
double  u8_f64(uint8_t x)   { return (double)x;   }
double  u16_f64(uint16_t x) { return (double)x;   }
double  u32_f64(uint32_t x) { return (double)x;   }
double  u64_f64(uint64_t x) { return (double)x;   }
uint8_t f32_u8(float f)     { return (uint8_t)f;  }
uint16_t f32_u16(float f)   { return (uint16_t)f; }
uint32_t f32_u32(float f)   { return (uint32_t)f; }
uint64_t f32_u64(float f)   { return (uint64_t)f; }
uint8_t f64_u8(double f)    { return (uint8_t)f;  }
uint16_t f64_u16(double f)  { return (uint16_t)f; }
uint32_t f64_u32(double f)  { return (uint32_t)f; }
uint64_t f64_u64(double f)  { return (uint64_t)f; }

int main(void)
{
    if (s8_f32(-16) != -16.0f)               { return 1; }
    if (s16_f32(-8192) != -8192.0f)          { return 2; }
    if (s32_f32(0x123456) != 1193046.0f)     { return 3; }
    if (s64_f32(0x123456) != 1193046.0f)     { return 4; }

    if (s8_f64(-16) != -16.0)                { return 5; }
    if (s16_f64(-8192) != -8192.0)           { return 6; }
    if (s32_f64(0x123456) != 1193046.0)      { return 7; }
    if (s64_f64(0x123456) != 1193046.0)      { return 8; }

    if (f32_s8(-16.0f) != (int8_t)-16)       { return 9; }
    if (f32_s16(-8192.0f) != (int16_t)-8192) { return 10; }
    if (f32_s32(1193046.0f) != 1193046)      { return 11; }
    if (f32_s64(1193046.0f) != 1193046ll)    { return 12; }

    if (f64_s8(-16.0) != (int8_t)(-16))      { return 13; }
    if (f64_s16(-8192.0) != (int16_t)-8192)  { return 14; }
    if (f64_s32(-11223344.0) != -11223344)   { return 15; }
    if (f64_s64(-11223344556677.0) != -11223344556677) { return 16; }

    if (f32_f64(1193046.0f) != 1193046.0)    { return 17; }
    if (f64_f32(1193046.0) != 1193046.0f)    { return 18; }

    if (u8_f32(64) != 64.0f)                 { return 19; }
    if (u16_f32(5234) != 5234.0f)            { return 20; }
    if (u32_f32(11223344) != 11223344.0f)    { return 21; }
    if (u64_f32(11223344) != 11223344.0f)    { return 22; }

    if (u8_f64(64) != 64.0)                  { return 23; }
    if (u16_f64(5234) != 5234.0)             { return 24; }
    if (u32_f64(11223344) != 11223344.0)     { return 25; }
    if (u64_f64(11223344) != 11223344.0)     { return 26; }

    if (f32_u8(64.0f) != 64)                 { return 27; }
    if (f32_u16(5234.0f) != 5234)            { return 28; }
    if (f32_u32(11223344.0f) != 11223344)    { return 29; }
    if (f32_u64(11223344087040.0f) != 11223344087040ull)    { return 30; }

    if (f64_u8(64.0) != 64)                  { return 31; }
    if (f64_u16(5234.0) != 5234)             { return 32; }
    if (f64_u32(11223344.0) != 11223344)     { return 33; }
    if (f64_u64(11223344556677.0) != 11223344556677ull)     { return 34; }

    return 0;
}
