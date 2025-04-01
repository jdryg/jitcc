#ifndef JX_CPU_H
#error "Must be included from jx/cpu.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

static inline const char* jx_cpu_getVendorID(void)
{
	return cpu_api->getVendorID();
}

static inline const char* jx_cpu_getProcessorBrandString(void)
{
	return cpu_api->getProcessorBrandString();
}

static inline uint64_t jx_cpu_getFeatures(void)
{
	return cpu_api->getFeatures();
}

static inline const jx_cpu_version_t* jx_cpu_getVersionInfo(void)
{
	return cpu_api->getVersionInfo();
}

#ifdef __cplusplus
}
#endif
