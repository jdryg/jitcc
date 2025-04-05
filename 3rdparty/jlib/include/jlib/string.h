#ifndef JX_STRING_H
#define JX_STRING_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h> // va_list

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#define JX_UTF8_BOM_LEN 3 // { 0xEF, 0xBB, 0xBF }

typedef struct jx_allocator_i jx_allocator_i;

typedef struct jx_string_buffer_t jx_string_buffer_t;
typedef struct jx_string_table_t jx_string_table_t;

typedef struct jx_string_api
{
	jx_string_buffer_t* (*strbufCreate)(jx_allocator_i* allocator);
	void                (*strbufDestroy)(jx_string_buffer_t* sb);
	void                (*strbufReset)(jx_string_buffer_t* sb);
	int32_t             (*strbufPush)(jx_string_buffer_t* sb, const char* str, uint32_t len);
	int32_t             (*strbufPop)(jx_string_buffer_t* sb, uint32_t n);
	const char*         (*strbufPeek)(jx_string_buffer_t* sb, uint32_t n);
	int32_t             (*strbufPrintf)(jx_string_buffer_t* sb, const char* fmt, ...);
	int32_t             (*strbufPrintfv)(jx_string_buffer_t* sb, const char* fmt, va_list argList);
	int32_t             (*strbufNullTerminate)(jx_string_buffer_t* sb);
	const char*         (*strbufGetString)(const jx_string_buffer_t* sb, uint32_t* len);

	jx_string_table_t*  (*strtableCreate)(jx_allocator_i* allocator);
	void                (*strtableDestroy)(jx_string_table_t* st);
	const char*         (*strtableInsert)(jx_string_table_t* st, const char* str, uint32_t len);

	int32_t     (*snprintf)(char* buf, int32_t max, const char* format, ...);
	int32_t     (*vsnprintf)(char* buf, int32_t max, const char* format, va_list argList);

	uint32_t    (*strnlen)(const char* str, uint32_t max);
	char*       (*strndup)(const char* str, uint32_t n, jx_allocator_i* allocator);
	uint32_t    (*strcpy)(char* dst, uint32_t dstMax, const char* src, uint32_t srcLen);

	// Compares the two strings up to min(lhsMax, rhsMax).
	// - If the two strings are identical up to that point and both sizes are equal, 0 is returned.
	// - If the two strings are identical up to that point and the sizes are not equal, 1 or -1 
	//   is returned depending on which size was larger (1 => lhsMax > rhsMax, -1 for the opposite).
	// - If a difference between the two strings is found before min(lhsMax, rhsMax) the function
	//   return the signed difference between the two characters.
	int32_t     (*strcmp)(const char* lhs, uint32_t lhsMax, const char* rhs, uint32_t rhsMax);
	int32_t     (*stricmp)(const char* lhs, uint32_t lhsMax, const char* rhs, uint32_t rhsMax);
	const char* (*strnrchr)(const char* str, uint32_t n, char ch);
	const char* (*strnchr)(const char* str, uint32_t n, char ch);
	uint32_t    (*strncat)(char* dst, uint32_t dstMax, const char* src, uint32_t srcLen);

	void        (*strTrimWhitespaces)(char* str, uint32_t len);

	int64_t     (*strto_int)(const char* str, uint32_t len, char** str_end, int32_t base, int64_t defaultVal);
	double      (*strto_double)(const char* str, uint32_t len, char** str_end, double defaultVal);

	uint32_t    (*utf8to_utf16)(uint16_t* dst, uint32_t dstMax, const char* src, uint32_t srcLen, uint32_t* numCharsRead);
	uint32_t    (*utf8to_utf32)(uint32_t* dst, uint32_t dstMax, const char* src, uint32_t srcLen, uint32_t* numCharsRead);
	uint32_t    (*utf8from_utf16)(char* dst, uint32_t dstMax, const uint16_t* src, uint32_t srcLen);
	uint32_t    (*utf8from_utf32)(char* dstUtf8, uint32_t dstMaxChars, const uint32_t* srcUtf32, uint32_t srcLen);
	uint32_t    (*utf8nlen)(const char* str, uint32_t max);
	const char* (*utf8FindPrevChar)(const char* str);
	const char* (*utf8FindNextChar)(const char* str);

	uint8_t*    (*base64Decode)(const char* str, uint32_t len, uint32_t* sz, jx_allocator_i* allocator);
	char*       (*base64Encode)(const uint8_t* data, uint32_t sz, uint32_t* len, jx_allocator_i* allocator);
} jx_string_api;

extern jx_string_api* str_api;

static jx_string_buffer_t* jx_strbuf_create(jx_allocator_i* allocator);
static void jx_strbuf_destroy(jx_string_buffer_t* sb);
static void jx_strbuf_reset(jx_string_buffer_t* sb);
static int32_t jx_strbuf_push(jx_string_buffer_t* sb, const char* str, uint32_t len);
static int32_t jx_strbuf_pushCStr(jx_string_buffer_t* sb, const char* str);
static int32_t jx_strbuf_pop(jx_string_buffer_t* sb, uint32_t n);
static const char* jx_strbuf_peek(jx_string_buffer_t* sb, uint32_t n);
static int32_t jx_strbuf_printf(jx_string_buffer_t* sb, const char* fmt, ...);
static int32_t jx_strbuf_vprintf(jx_string_buffer_t* sb, const char* fmt, va_list argList);
static int32_t jx_strbuf_nullTerminate(jx_string_buffer_t* sb);
static const char* jx_strbuf_getString(const jx_string_buffer_t* sb, uint32_t* len);

static jx_string_table_t* jx_strtable_create(jx_allocator_i* allocator);
static void jx_strtable_destroy(jx_string_table_t* st);
static const char* jx_strtable_insert(jx_string_table_t* st, const char* str, uint32_t len);

static int32_t jx_snprintf(char* buf, int32_t max, const char* fmt, ...);
static int32_t jx_vsnprintf(char* buf, int32_t max, const char* fmt, va_list argList);
static uint32_t jx_strlen(const char* str);
static uint32_t jx_strnlen(const char* str, uint32_t max);
static char* jx_strdup(const char* str, jx_allocator_i* allocator);
static char* jx_strndup(const char* str, uint32_t n, jx_allocator_i* allocator);
static uint32_t jx_strcpy(char* dst, uint32_t dstMax, const char* src, uint32_t srcLen);
static int32_t jx_strcmp(const char* lhs, const char* rhs);
static int32_t jx_strncmp(const char* lhs, const char* rhs, uint32_t n);
static int32_t jx_stricmp(const char* lhs, const char* rhs);
static int32_t jx_strnicmp(const char* lhs, const char* rhs, uint32_t n);
static const char* jx_strnrchr(const char* str, uint32_t n, char ch);
static const char* jx_strrchr(const char* str, char ch);
static const char* jx_strnchr(const char* str, uint32_t n, char ch);
static const char* jx_strchr(const char* str, char ch);
static uint32_t jx_strcat(char* dst, const char* src);
static uint32_t jx_strncat(char* dst, uint32_t dstMax, const char* src, uint32_t srcLen);

static void jx_strTrimWhitespaces(char* str, uint32_t len);

static int64_t jx_strto_int(const char* str, uint32_t len, char** str_end, int32_t base, int64_t defaultVal);
static double jx_strto_double(const char* str, uint32_t len, char** str_end, double defaultVal);

static bool jx_isupper(char ch);
static bool jx_islower(char ch);
static bool jx_isalpha(char ch);
static bool jx_isdigit(char ch);
static bool jx_isspace(char ch);
static bool jx_isxdigit(char ch);
static bool jx_ispunct(char ch);
static bool jx_isprint(char ch);
static char jx_tolower(char ch);
static char jx_toupper(char ch);

static int32_t jx_hexCharToInt(char ch);

static uint32_t jx_utf8to_utf16(uint16_t* dstUTF16, uint32_t dstMax, const char* srcUTF8, uint32_t srcSize, uint32_t* numCharsRead);
static uint32_t jx_utf8to_utf32(uint32_t* dstUTF32, uint32_t dstMax, const char* srcUTF8, uint32_t srcSize, uint32_t* numCharsRead);
static uint32_t jx_utf8from_utf16(char* dstUTF8, uint32_t dstMax, const uint16_t* srcUTF16, uint32_t srcLen);
static uint32_t jx_utf8from_utf32(char* dstUTF8, uint32_t dstMax, const uint32_t* srcUTF32, uint32_t srcLen);
static uint32_t jx_utf8nlen(const char* str, uint32_t max);
static uint32_t jx_utf8len(const char* str);
static bool jx_utf8to_codepoint(uint32_t* cp, const char* srcUTF8, uint32_t srcLen, uint32_t* numCharsRead);
static const char* jx_utf8FindNextChar(const char* str);
static const char* jx_utf8FindPrevChar(const char* str);
static bool jx_utf8IsBOM(const char* str);

static uint8_t* jx_base64Decode(const char* str, uint32_t len, uint32_t* sz, jx_allocator_i* allocator);
static char* jx_base64Encode(const uint8_t* data, uint32_t sz, uint32_t* len, jx_allocator_i* allocator);

#ifdef __cplusplus
}
#endif

#include "inline/string.inl"

#endif // JX_STRING_H
