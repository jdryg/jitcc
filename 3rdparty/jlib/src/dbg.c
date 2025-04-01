#include <jlib/dbg.h>
#include <jlib/macros.h>
#include <jlib/string.h>
#include <intrin.h>

#if JX_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

static void _jx_dbg_brk(void);
static void _jx_dbg_puts(const char* str);
static void _jx_dbg_printf(const char* fmt, ...);
static void _jx_dbg_vprintf(const char* fmt, va_list argList);

jx_dbg_api* dbg_api = &(jx_dbg_api){
	.brk = _jx_dbg_brk,
	.puts = _jx_dbg_puts,
	.printf = _jx_dbg_printf,
	.vprintf = _jx_dbg_vprintf
};

static void _jx_dbg_brk(void)
{
#if JX_COMPILER_MSVC
	__debugbreak();
#else
	int32_t* int3 = (int32_t*)3;
	*int3 = 3;
#endif
}

static void _jx_dbg_puts(const char* str)
{
#if JX_PLATFORM_WINDOWS
	OutputDebugStringA(str);
#else
#endif
}

static void _jx_dbg_printf(const char* fmt, ...)
{
	va_list argList;
	va_start(argList, fmt);
	_jx_dbg_vprintf(fmt, argList);
	va_end(argList);
}

static void _jx_dbg_vprintf(const char* fmt, va_list argList)
{
	char buf[8192];
	str_api->vsnprintf(buf, JX_COUNTOF(buf), fmt, argList);
	_jx_dbg_puts(buf);
}
