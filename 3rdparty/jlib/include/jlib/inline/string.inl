#ifndef JX_STRING_H
#error "Must be included from jx/string.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

static inline jx_string_buffer_t* jx_strbuf_create(jx_allocator_i* allocator)
{
	return str_api->strbufCreate(allocator);
}

static inline void jx_strbuf_destroy(jx_string_buffer_t* sb)
{
	str_api->strbufDestroy(sb);
}

static inline int32_t jx_strbuf_push(jx_string_buffer_t* sb, const char* str, uint32_t len)
{
	return str_api->strbufPush(sb, str, len);
}

static int32_t jx_strbuf_pushCStr(jx_string_buffer_t* sb, const char* str)
{
	return str_api->strbufPush(sb, str, UINT32_MAX);
}

static inline int32_t jx_strbuf_pop(jx_string_buffer_t* sb, uint32_t n)
{
	return str_api->strbufPop(sb, n);
}

static inline const char* jx_strbuf_peek(jx_string_buffer_t* sb, uint32_t n)
{
	return str_api->strbufPeek(sb, n);
}

static inline int32_t jx_strbuf_printf(jx_string_buffer_t* sb, const char* fmt, ...)
{
	va_list argList;
	va_start(argList, fmt);
	int32_t res = str_api->strbufPrintfv(sb, fmt, argList);
	va_end(argList);

	return res;
}

static inline int32_t jx_strbuf_vprintf(jx_string_buffer_t* sb, const char* fmt, va_list argList)
{
	return str_api->strbufPrintfv(sb, fmt, argList);
}

static int32_t jx_strbuf_nullTerminate(jx_string_buffer_t* sb)
{
	return str_api->strbufNullTerminate(sb);
}

static inline const char* jx_strbuf_getString(const jx_string_buffer_t* sb, uint32_t* len)
{
	return str_api->strbufGetString(sb, len);
}

static inline jx_string_table_t* jx_strtable_create(jx_allocator_i* allocator)
{
	return str_api->strtableCreate(allocator);
}

static inline void jx_strtable_destroy(jx_string_table_t* st)
{
	str_api->strtableDestroy(st);
}

static inline const char* jx_strtable_insert(jx_string_table_t* st, const char* str, uint32_t len)
{
	return str_api->strtableInsert(st, str, len);
}

static inline int32_t jx_snprintf(char* buf, int32_t max, const char* fmt, ...)
{
	va_list argList;
	va_start(argList, fmt);
	const int32_t retVal = str_api->vsnprintf(buf, max, fmt, argList);
	va_end(argList);
	return retVal;
}

static inline int32_t jx_vsnprintf(char* buf, int32_t max, const char* fmt, va_list argList) 
{
	return str_api->vsnprintf(buf, max, fmt, argList);
}

static inline uint32_t jx_strlen(const char* str)
{
	return str_api->strnlen(str, UINT32_MAX);
}

static inline uint32_t jx_strnlen(const char* str, uint32_t max)
{
	return str_api->strnlen(str, max);
}

static inline char* jx_strdup(const char* str, jx_allocator_i* allocator)
{
	return str_api->strndup(str, UINT32_MAX, allocator);
}

static inline char* jx_strndup(const char* str, uint32_t n, jx_allocator_i* allocator)
{
	return str_api->strndup(str, n, allocator);
}

static inline uint32_t jx_strcpy(char* dst, uint32_t dstSize, const char* src, uint32_t n)
{
	return str_api->strcpy(dst, dstSize, src, n);
}

static inline int32_t jx_strcmp(const char* lhs, const char* rhs)
{
	return str_api->strcmp(lhs, UINT32_MAX, rhs, UINT32_MAX);
}

static inline int32_t jx_strncmp(const char* lhs, const char* rhs, uint32_t n)
{
	return str_api->strcmp(lhs, n, rhs, n);
}

static inline int32_t jx_stricmp(const char* lhs, const char* rhs)
{
	return str_api->stricmp(lhs, UINT32_MAX, rhs, UINT32_MAX);
}

static inline int32_t jx_strnicmp(const char* lhs, const char* rhs, uint32_t n)
{
	return str_api->stricmp(lhs, n, rhs, n);
}

static inline const char* jx_strnrchr(const char* str, uint32_t n, char ch)
{
	return str_api->strnrchr(str, n, ch);
}

static inline const char* jx_strrchr(const char* str, char ch)
{
	return str_api->strnrchr(str, UINT32_MAX, ch);
}

static inline const char* jx_strnchr(const char* str, uint32_t n, char ch)
{
	return str_api->strnchr(str, n, ch);
}

static inline const char* jx_strchr(const char* str, char ch)
{
	return str_api->strnchr(str, UINT32_MAX, ch);
}

static inline uint32_t jx_strcat(char* dst, const char* src)
{
	return str_api->strncat(dst, UINT32_MAX, src, UINT32_MAX);
}

static inline uint32_t jx_strncat(char* dst, uint32_t dstMax, const char* src, uint32_t srcLen)
{
	return str_api->strncat(dst, dstMax, src, srcLen);
}

static inline void jx_strTrimWhitespaces(char* str, uint32_t len)
{
	str_api->strTrimWhitespaces(str, len);
}

static int64_t jx_strto_int(const char* str, uint32_t len, char** str_end, int32_t base, int64_t defaultVal)
{
	return str_api->strto_int(str, len, str_end, base, defaultVal);
}

static double jx_strto_double(const char* str, uint32_t len, char** str_end, double defaultVal)
{
	return str_api->strto_double(str, len, str_end, defaultVal);
}

static inline bool jx_isupper(char ch)
{
	return ((uint32_t)ch - 'A') < 26;
}

static inline bool jx_islower(char ch)
{
	return ((uint32_t)ch - 'a') < 26;
}

static inline bool jx_isalpha(char ch)
{
	return (((uint32_t)ch | 0x20) - 'a') < 26;
}

static inline bool jx_isdigit(char ch)
{
	return ((uint32_t)ch - '0') < 10;
}

static inline bool jx_isoctal(char ch)
{
	return ((uint32_t)ch - '0') < 8;
}

static inline bool jx_isspace(char ch)
{
	return ch == ' ' || ((uint32_t)ch - '\t') < 5;
}

static inline bool jx_isxdigit(char ch)
{
	return false 
		|| (ch >= '0' && ch <= '9') 
		|| (ch >= 'a' && ch <= 'f') 
		|| (ch >= 'A' && ch <= 'F')
		;
}

static inline bool jx_ispunct(char ch)
{
	return false
		|| (ch >= '!' && ch <= '/') // !"#$%&'()*+,-./
		|| (ch >= ':' && ch <= '@') // :;<=>?@
		|| (ch >= '[' && ch <= '`') // [\]^_`
		|| (ch >= '{' && ch <= '~') // {|}~
		;
}

static bool jx_isprint(char ch)
{
	return false
		|| (ch >= ' ' && ch <= '~')
		;
}

static inline int32_t jx_hexCharToInt(char ch)
{
	if (ch >= '0' && ch <= '9') {
		return ch - '0';
	} else if (ch >= 'a' && ch <= 'f') {
		return (ch - 'a') + 10;
	}

	return (ch - 'A') + 10;
}

static inline char jx_tolower(char ch)
{
	return ch + (jx_isupper(ch) ? 0x20 : 0);
}

static inline char jx_toupper(char ch)
{
	return ch - (jx_islower(ch) ? 0x20 : 0);
}

static inline uint32_t jx_utf8to_utf16(uint16_t* dstUTF16, uint32_t dstMax, const char* srcUTF8, uint32_t srcSize, uint32_t* numCharsRead)
{
	return str_api->utf8to_utf16(dstUTF16, dstMax, srcUTF8, srcSize, numCharsRead);
}

static inline uint32_t jx_utf8to_utf32(uint32_t* dstUTF32, uint32_t dstMax, const char* srcUTF8, uint32_t srcSize, uint32_t* numCharsRead)
{
	return str_api->utf8to_utf32(dstUTF32, dstMax, srcUTF8, srcSize, numCharsRead);
}

static inline uint32_t jx_utf8from_utf16(char* dstUTF8, uint32_t dstMax, const uint16_t* srcUTF16, uint32_t srcLen)
{
	return str_api->utf8from_utf16(dstUTF8, dstMax, srcUTF16, srcLen);
}

static inline uint32_t jx_utf8from_utf32(char* dstUTF8, uint32_t dstMax, const uint32_t* srcUTF32, uint32_t srcLen)
{
	return str_api->utf8from_utf32(dstUTF8, dstMax, srcUTF32, srcLen);
}

static inline uint32_t jx_utf8nlen(const char* str, uint32_t max)
{
	return str_api->utf8nlen(str, max);
}

static inline uint32_t jx_utf8len(const char* str)
{
	return str_api->utf8nlen(str, UINT32_MAX);
}

static inline const char* jx_utf8FindNextChar(const char* str)
{
	return str_api->utf8FindNextChar(str);
}

static inline const char* jx_utf8FindPrevChar(const char* str)
{
	return str_api->utf8FindPrevChar(str);
}

static inline bool jx_utf8IsBOM(const char* str)
{
	return ((*(uint32_t*)str) & 0x00FFFFFF) == 0x00BFBBEF;
}

static inline bool jx_utf8to_codepoint(uint32_t* cp, const char* srcUTF8, uint32_t srcLen, uint32_t* numCharsRead)
{
	uint32_t tmp[2] = { 0 };
	if (!str_api->utf8to_utf32(&tmp[0], 2, srcUTF8, srcLen, numCharsRead)) {
		return false;
	}

	*cp = tmp[0];

	return true;
}

static inline uint8_t* jx_base64Decode(const char* str, uint32_t len, uint32_t* sz, jx_allocator_i* allocator)
{
	return str_api->base64Decode(str, len, sz, allocator);
}

static inline char* jx_base64Encode(const uint8_t* data, uint32_t sz, uint32_t* len, jx_allocator_i* allocator)
{
	return str_api->base64Encode(data, sz, len, allocator);
}
#ifdef __cplusplus
}
#endif
