#ifndef JX_ERROR_H
#define JX_ERROR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum jx_error
{
	JX_ERROR_NONE = 0,
	JX_ERROR_ALREADY_INITIALIZED = -1,
	JX_ERROR_DRIVER_INIT = -2,
	JX_ERROR_OUT_OF_MEMORY = -3,
	JX_ERROR_INVALID_ARGUMENT = -4,
	JX_ERROR_UNSUPPORTED_OPERATION = -5,
	JX_ERROR_FILE_NOT_FOUND = -6,
	JX_ERROR_CONNECTION_FAILED = -7,
	JX_ERROR_INVALID_FORMAT = -8,
	JX_ERROR_OPERATION_FAILED = -9,
	JX_ERROR_UNKNOWN_INTERFACE = -10,
	JX_ERROR_CREATE_WINDOW = -11,
	JX_ERROR_INIT_RENDERER = -12,
	JX_ERROR_NO_RENDERER_BACKEND = -13,
	JX_ERROR_OPEN_CONSOLE = -14,
	JX_ERROR_ALREADY_EXISTS = -15,
	JX_ERROR_FILE_READ = -16,
	JX_ERROR_FILE_WRITE = -17,
	JX_ERROR_UNKNOWN = -1000
} jx_error;

typedef struct jx_error_t
{
	const char* m_Msg;
	uint32_t m_Code;
} jx_error_t;

#define JX_ERROR_SET(err, code, msg) (err)->m_Code = (code); (err)->m_Msg = (msg)

static void jerrorInit(jx_error_t* err);
static bool jerrorIsOk(const jx_error_t* err);
static void jerrorReset(jx_error_t* err);

#ifdef __cplusplus
}
#endif

#include "inline/error.inl"

#endif // JX_ERROR_H
