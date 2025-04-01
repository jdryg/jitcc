#ifndef JX_KERNEL_H
#define JX_KERNEL_H

#include <stdint.h>
#include <stdbool.h>

// v1.0: bgfx + original vg-renderer
// v1.1: wgpu + vg
#define JX_KERNEL_VERSION 0x00010001 // 1.1

#define JX_API_ALLOCATOR  "allocator"
#define JX_API_STRING     "str"
#define JX_API_DEBUG      "dbg"
#define JX_API_CPU        "cpu"
#define JX_API_MEMORY     "mem"
#define JX_API_OS         "os"
#define JX_API_IMAGE      "image"
#define JX_API_LOGGER     "logger"
#define JX_API_CONFIG     "config"
#define JX_API_MATH       "math"
#define JX_API_GUI        "gui"
#define JX_API_SORT       "sort"
#define JX_API_TRACY      "tracy"
#define JX_API_GFX        "gfx"
#define JX_API_VG         "vg"

#define JX_INTERFACE_APPLICATION "application_i"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum jx_plugin_op
{
	JX_PLUGIN_OP_REGISTER,
	JX_PLUGIN_OP_UNREGISTER
} jx_plugin_op;

typedef struct jx_kernel_api
{
	// NOTE: This function must always come before anything else in order to be able
	// to check the API version.
	uint32_t (*getVersion)(void);
	const void* (*getAPI)(const char* apiName);

	int32_t (*regInterfaceImpl)(const char* interfaceName, const void* impl);
	int32_t (*unregInterfaceImpl)(const char* interfaceName, const void* impl);
	void** (*getInterfaceImpl)(const char* interfaceName, uint32_t* numImplementations);
} jx_kernel_api;

#if defined(JX_HOST)
bool jx_kernel_initAPI(void);
void jx_kernel_shutdownAPI(void);
const jx_kernel_api* jx_kernel_getAPI(void);
#else // JX_HOST
bool jx_kernel_initAPI(const void* hostAPI);
void jx_kernel_shutdownAPI(void);
const jx_kernel_api* jx_kernel_getAPI(void);
#endif

static void jx_kernel_regInterfaceImpl(const char* interfaceName, const void* impl);
static void jx_kernel_unregInterfaceImpl(const char* interfaceName, const void* impl);
static void** jx_kernel_getInterfaceImpl(const char* interfaceName, uint32_t* numImplementations);

#ifdef __cplusplus
}
#endif

#include "inline/kernel.inl"

#endif // JX_KERNEL_H
