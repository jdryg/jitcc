#ifndef JX_CPU_H
#define JX_CPU_H

#include <stdint.h>
#include <stdbool.h>
#include "macros.h"

#define JX_CPU_FEATURE_MMX      (1ull << 0)
#define JX_CPU_FEATURE_SSE      (1ull << 1)
#define JX_CPU_FEATURE_SSE2     (1ull << 2)
#define JX_CPU_FEATURE_SSE3     (1ull << 3)
#define JX_CPU_FEATURE_SSSE3    (1ull << 4)
#define JX_CPU_FEATURE_SSE4_1   (1ull << 5)
#define JX_CPU_FEATURE_SSE4_2   (1ull << 6)
#define JX_CPU_FEATURE_AVX      (1ull << 7)
#define JX_CPU_FEATURE_AVX2     (1ull << 8)
#define JX_CPU_FEATURE_AVX512F  (1ull << 9)
#define JX_CPU_FEATURE_BMI1     (1ull << 10)
#define JX_CPU_FEATURE_BMI2     (1ull << 11)
#define JX_CPU_FEATURE_POPCNT   (1ull << 12)
#define JX_CPU_FEATURE_F16C     (1ull << 13) // CPU supports 16-bit floating point conversion instructions
#define JX_CPU_FEATURE_FMA      (1ull << 14)
#define JX_CPU_FEATURE_ERMSB    (1ull << 15) // Enhanced REP MOVSB

#define JX_CPU_NUM_FEATURES     16

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

static const char* jx_cpu_kFeatureName[] = {
	"MMX",
	"SSE",
	"SSE2",
	"SSE3",
	"SSSE3",
	"SSE4.1",
	"SSE4.2",
	"AVX",
	"AVX2",
	"AVX512F",
	"BMI1",
	"BMI2",
	"POPCNT",
	"F16C",
	"FMA",
	"ERMSB"
};
JX_STATIC_ASSERT(JX_COUNTOF(jx_cpu_kFeatureName) == JX_CPU_NUM_FEATURES, "Missing CPU feature name");

typedef struct jx_cpu_version_t
{
	uint8_t m_FamilyID;
	uint8_t m_ModelID;
	uint8_t m_SteppingID;
	JX_PAD(1);
} jx_cpu_version_t;

typedef struct jx_cpu_api
{
	const char*             (*getVendorID)(void);
	const char*             (*getProcessorBrandString)(void);
	uint64_t                (*getFeatures)(void);
	const jx_cpu_version_t* (*getVersionInfo)(void);
} jx_cpu_api;

extern jx_cpu_api* cpu_api;

static const char* jx_cpu_getVendorID(void);
static const char* jx_cpu_getProcessorBrandString(void);
static uint64_t jx_cpu_getFeatures(void);
static const jx_cpu_version_t* jx_cpu_getVersionInfo(void);

#ifdef __cplusplus
}
#endif

#include "inline/cpu.inl"

#endif // JX_CPU_H
