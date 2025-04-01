#ifndef JX_DBG_H
#define JX_DBG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h> // va_list

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct jx_dbg_api
{
	void (*brk)(void);
	void (*puts)(const char* str);
	void (*printf)(const char* fmt, ...);
	void (*vprintf)(const char* fmt, va_list argList);
} jx_dbg_api;

extern jx_dbg_api* dbg_api;

static void jx_dbg_brk();
static void jx_dbg_puts(const char* str);
static void jx_dbg_printf(const char* fmt, ...);
static void jx_dbg_vprintf(const char* fmt, va_list argList);

#ifdef __cplusplus
}
#endif

#include "inline/dbg.inl"

#endif // JX_DBG_H
