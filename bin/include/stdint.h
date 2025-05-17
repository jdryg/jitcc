#ifndef _STDINT
#define _STDINT

typedef signed char        int8_t;
typedef short              int16_t;
typedef int                int32_t;
typedef long long          int64_t;
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

#define INT8_MIN         ((int8_t)-127 - 1)
#define INT16_MIN        ((int16_t)-32767 - 1)
#define INT32_MIN        ((int32_t)-2147483647 - 1)
#define INT64_MIN        ((int64_t)-9223372036854775807 - 1)
#define INT8_MAX         (int8_t)127
#define INT16_MAX        (int16_t)32767
#define INT32_MAX        (int32_t)2147483647
#define INT64_MAX        (int64_t)9223372036854775807
#define UINT8_MAX        (uint8_t)0xff
#define UINT16_MAX       (uint16_t)0xffff
#define UINT32_MAX       0xffffffffu
#define UINT64_MAX       0xffffffffffffffffull

#endif // _STDINT
