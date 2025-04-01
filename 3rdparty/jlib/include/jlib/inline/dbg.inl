#ifndef JX_DBG_H
#error "Must be included from jx/dbg.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

static inline void jx_dbg_brk()
{
    dbg_api->brk();
}

static inline void jx_dbg_puts(const char* str)
{
    dbg_api->puts(str);
}

static inline void jx_dbg_printf(const char* fmt, ...)
{
    va_list argList;
    va_start(argList, fmt);
    dbg_api->vprintf(fmt, argList);
    va_end(argList);
}

static inline void jx_dbg_vprintf(const char* fmt, va_list argList)
{
    dbg_api->vprintf(fmt, argList);
}

#ifdef __cplusplus
}
#endif
