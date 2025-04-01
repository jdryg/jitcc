#include <jlib/kernel.h>
#include <jlib/allocator.h>
#include <jlib/cpu.h>
#include <jlib/dbg.h>
#include <jlib/string.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/error.h>
#include <jlib/application.h>
#include <jlib/array.h>
#include <jlib/image.h>
#include <jlib/logger.h>
#include <jlib/config.h>
#include <jlib/math.h>
#include <jlib/sort.h>

typedef struct jx_kernel_t
{
	const jx_application_i** m_AppImplementations;
} jx_kernel_t;

static jx_kernel_t s_Kernel = { 0 };

static uint32_t _jx_kernel_getVersion(void);
static const void* _jx_kernel_getAPI(const char* apiName);
static int32_t _jx_kernel_regInterfaceImpl(const char* interfaceName, const void* impl);
static int32_t _jx_kernel_unregInterfaceImpl(const char* interfaceName, const void* impl);
static void** _jx_kernel_getInterfaceImpl(const char* interfaceName, uint32_t* numImplementations);

static jx_kernel_api s_API = { 
	.getVersion = _jx_kernel_getVersion,
	.getAPI = _jx_kernel_getAPI,
	.regInterfaceImpl = _jx_kernel_regInterfaceImpl,
	.unregInterfaceImpl = _jx_kernel_unregInterfaceImpl,
	.getInterfaceImpl = _jx_kernel_getInterfaceImpl
};

extern bool jx_cpu_initAPI(void);
extern bool jx_mem_initAPI(void);
extern bool jx_os_initAPI(void);
extern void jx_os_shutdownAPI(void);
extern bool jx_allocator_initAPI(void);
extern void jx_allocator_shutdownAPI(void);
extern bool jx_memtracer_init(jx_allocator_i* allocator);
extern void jx_memtracer_shutdown(void);
extern bool jx_logger_initAPI(jx_allocator_i* allocator);
extern void jx_logger_shutdownAPI(void);
extern void jx_cpu_logInfo(jx_logger_i* logger);
extern void jx_os_logInfo(jx_logger_i* logger);
extern bool jx_math_initAPI(void);

extern jx_allocator_i* s_SystemAllocator;

bool jx_kernel_initAPI(void)
{
	if (!jx_cpu_initAPI()) {
		return false;
	}

	if (!jx_mem_initAPI()) {
		return false;
	}

	if (!jx_math_initAPI()) {
		return false;
	}

#if JX_CONFIG_TRACE_ALLOCATIONS
	if (!jx_memtracer_init(s_SystemAllocator)) {
		return false;
	}
#endif

	if (!jx_allocator_initAPI()) {
		return false;
	}

	if (!jx_os_initAPI()) {
		return false;
	}

	if (!jx_logger_initAPI(allocator_api->m_SystemAllocator)) {
		return false;
	}

	jx_cpu_logInfo(logger_api->m_SystemLogger);
	jx_os_logInfo(logger_api->m_SystemLogger);

	return true;
}

void jx_kernel_shutdownAPI(void)
{
	jx_array_free(s_Kernel.m_AppImplementations);
	
	jx_logger_shutdownAPI();

	jx_os_shutdownAPI();

	jx_allocator_shutdownAPI();

#if JX_CONFIG_TRACE_ALLOCATIONS
	jx_memtracer_shutdown();
#endif
}

const jx_kernel_api* jx_kernel_getAPI(void)
{
	return &s_API;
}

static uint32_t _jx_kernel_getVersion(void)
{
	return JX_KERNEL_VERSION;
}

static const void* _jx_kernel_getAPI(const char* apiName)
{
	// TODO: Something faster?
	if (!jx_strcmp(apiName, JX_API_ALLOCATOR)) {
		return allocator_api;
	} else if (!jx_strcmp(apiName, JX_API_STRING)) {
		return str_api;
	} else if (!jx_strcmp(apiName, JX_API_DEBUG)) {
		return dbg_api;
	} else if (!jx_strcmp(apiName, JX_API_CPU)) {
		return cpu_api;
	} else if (!jx_strcmp(apiName, JX_API_MEMORY)) {
		return mem_api;
	} else if (!jx_strcmp(apiName, JX_API_OS)) {
		return os_api;
	} else if (!jx_strcmp(apiName, JX_API_IMAGE)) {
		return image_api;
	} else if (!jx_strcmp(apiName, JX_API_LOGGER)) {
		return logger_api;
	} else if (!jx_strcmp(apiName, JX_API_CONFIG)) {
		return config_api;
	} else if (!jx_strcmp(apiName, JX_API_MATH)) {
		return math_api;
	} else if (!jx_strcmp(apiName, JX_API_SORT)) {
		return sort_api;
	}

	return NULL;
}

static int32_t _jx_kernel_regInterfaceImpl(const char* interfaceName, const void* impl)
{
	if (!jx_strcmp(interfaceName, JX_INTERFACE_APPLICATION)) {
		jx_array_push_back(s_Kernel.m_AppImplementations, impl);
		return JX_ERROR_NONE;
	}

	return JX_ERROR_UNKNOWN_INTERFACE;
}

static int32_t _jx_kernel_unregInterfaceImpl(const char* interfaceName, const void* impl)
{
	if (!jx_strcmp(interfaceName, JX_INTERFACE_APPLICATION)) {
		const uint32_t numImplementations = (uint32_t)jx_array_size(s_Kernel.m_AppImplementations);
		for (uint32_t i = 0; i < numImplementations; ++i) {
			if (s_Kernel.m_AppImplementations[i] == impl) {
				jx_array_delswap(s_Kernel.m_AppImplementations, i);
				break;
			}
		}

		return JX_ERROR_NONE;
	}

	return JX_ERROR_UNKNOWN_INTERFACE;
}

static void** _jx_kernel_getInterfaceImpl(const char* interfaceName, uint32_t* numImplementations)
{
	if (!jx_strcmp(interfaceName, JX_INTERFACE_APPLICATION)) {
		*numImplementations = (uint32_t)jx_array_size(s_Kernel.m_AppImplementations);
		return s_Kernel.m_AppImplementations;
	}

	*numImplementations = 0;
	return NULL;
}
