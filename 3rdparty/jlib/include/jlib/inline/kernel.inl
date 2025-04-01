#ifndef JX_KERNEL_H
#error "Must be included from jx/kernel.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline void jx_kernel_regInterfaceImpl(const char* interfaceName, const void* impl)
{
	jx_kernel_getAPI()->regInterfaceImpl(interfaceName, impl);
}

static void jx_kernel_unregInterfaceImpl(const char* interfaceName, const void* impl)
{
	jx_kernel_getAPI()->unregInterfaceImpl(interfaceName, impl);
}

static void** jx_kernel_getInterfaceImpl(const char* interfaceName, uint32_t* numImplementations)
{
	return jx_kernel_getAPI()->getInterfaceImpl(interfaceName, numImplementations);
}

#ifdef __cplusplus
}
#endif