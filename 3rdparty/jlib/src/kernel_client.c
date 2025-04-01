#include <jlib/kernel.h>
#include <jlib/allocator.h>
#include <jlib/dbg.h>
#include <jlib/cpu.h>
#include <jlib/string.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/image.h>
#include <jlib/logger.h>
#include <jlib/config.h>
#include <jlib/math.h>
#include <jlib/gui.h>
#include <jlib/sort.h>
#include <jlib/tracy.h>
#include <jlib/gfx.h>
#include <jlib/vg.h>

static jx_kernel_api* s_API = NULL;

jx_allocator_api* allocator_api = NULL;
jx_dbg_api* dbg_api = NULL;
jx_cpu_api* cpu_api = NULL;
jx_string_api* str_api = NULL;
jx_mem_api* mem_api = NULL;
jx_os_api* os_api = NULL;
jx_image_api* image_api = NULL;
jx_gfx_api* gfx_api = NULL;
jx_vg_api* vg_api = NULL;
jx_logger_api* logger_api = NULL;
jx_config_api* config_api = NULL;
jx_math_api* math_api = NULL;
#if !defined(JX_CONFIG_NO_GUI)
jx_gui_api* gui_api = NULL;
#endif
jx_sort_api* sort_api = NULL;
jx_tracy_api* tracy_api = NULL;

bool jx_kernel_initAPI(const void* hostAPI)
{
	if (!hostAPI) {
		return false;
	}

	const jx_kernel_api* kernelAPI = (const jx_kernel_api*)hostAPI;
	if (kernelAPI->getVersion() != JX_KERNEL_VERSION) {
		return false;
	}

	s_API = (jx_kernel_api*)kernelAPI;

	allocator_api = (jx_allocator_api*)s_API->getAPI(JX_API_ALLOCATOR);
	dbg_api = (jx_dbg_api*)s_API->getAPI(JX_API_DEBUG);
	cpu_api = (jx_cpu_api*)s_API->getAPI(JX_API_CPU);
	str_api = (jx_string_api*)s_API->getAPI(JX_API_STRING);
	mem_api = (jx_mem_api*)s_API->getAPI(JX_API_MEMORY);
	os_api = (jx_os_api*)s_API->getAPI(JX_API_OS);
	image_api = (jx_image_api*)s_API->getAPI(JX_API_IMAGE);
	gfx_api = (jx_gfx_api*)s_API->getAPI(JX_API_GFX);
	logger_api = (jx_logger_api*)s_API->getAPI(JX_API_LOGGER);
	config_api = (jx_config_api*)s_API->getAPI(JX_API_CONFIG);
	math_api = (jx_math_api*)s_API->getAPI(JX_API_MATH);
#if !defined(JX_CONFIG_NO_GUI)
	gui_api = (jx_gui_api*)s_API->getAPI(JX_API_GUI);
#endif
	sort_api = (jx_sort_api*)s_API->getAPI(JX_API_SORT);
	tracy_api = (jx_tracy_api*)s_API->getAPI(JX_API_TRACY);
	vg_api = (jx_vg_api*)s_API->getAPI(JX_API_VG);

	return true;
}

void jx_kernel_shutdownAPI(void)
{
	allocator_api = NULL;
	dbg_api = NULL;
	cpu_api = NULL;
	str_api = NULL;
	os_api = NULL;
	image_api = NULL;
	gfx_api = NULL;
	logger_api = NULL;
	config_api = NULL;
	math_api = NULL;
#if !defined(JX_CONFIG_NO_GUI)
	gui_api = NULL;
#endif
	sort_api = NULL;
	tracy_api = NULL;
	vg_api = NULL;

	s_API = NULL;
}

const jx_kernel_api* jx_kernel_getAPI(void)
{
	return s_API;
}
