#include <jlib/string.h>
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/error.h>
#include <jlib/hashmap.h>
#include <jlib/memory.h>
#define STB_SPRINTF_IMPLEMENTATION
#include <stb/stb_sprintf.h>

static jx_string_buffer_t* _jx_strbuf_create(jx_allocator_i* allocator);
static void _jx_strbuf_destroy(jx_string_buffer_t* sb);
static void _jx_strbuf_reset(jx_string_buffer_t* sb);
static int32_t _jx_strbuf_push(jx_string_buffer_t* sb, const char* str, uint32_t len);
static int32_t _jx_strbuf_pop(jx_string_buffer_t* sb, uint32_t n);
static const char* _jx_strbuf_peek(jx_string_buffer_t* sb, uint32_t n);
static int32_t _jx_strbuf_printf(jx_string_buffer_t* sb, const char* fmt, ...);
static int32_t _jx_strbuf_vprintf(jx_string_buffer_t* sb, const char* fmt, va_list argList);
static int32_t _jx_strbuf_nullTerminate(jx_string_buffer_t* sb);
static const char* _jx_strbuf_getString(const jx_string_buffer_t* sb, uint32_t* len);
static jx_string_table_t* _jx_strtable_create(jx_allocator_i* allocator);
static void _jx_strtable_destroy(jx_string_table_t* st);
static const char* _jx_strtable_insert(jx_string_table_t* st, const char* str, uint32_t len);
static uint32_t _jstr_strnlen(const char* str, uint32_t max);
static char* _jstr_strndup(const char* str, uint32_t n, jx_allocator_i* allocator);
static uint32_t _jstr_strcpy(char* dst, uint32_t dstSize, const char* src, uint32_t n);
static int32_t _jstr_strcmp(const char* lhs, uint32_t lhsMax, const char* rhs, uint32_t rhsMax);
static int32_t _jstr_stricmp(const char* lhs, uint32_t lhsMax, const char* rhs, uint32_t rhsMax);
static const char* _jstr_strnrchr(const char* str, uint32_t n, char ch);
static const char* _jstr_strnchr(const char* str, uint32_t n, char ch);
static uint32_t _jstr_strncat(char* dst, uint32_t dstSize, const char* src, uint32_t n);
static void _jstr_trimWhitespaces(char* str, uint32_t len);
static int64_t _jx_strto_int(const char* str, uint32_t len, char** str_end, int32_t base, int64_t defaultVal);
static double _jx_strto_double(const char* str, uint32_t len, char** str_end, double defaultVal);
static uint32_t _jstr_utf8ToUtf16(uint16_t* dst, uint32_t dstMax, const char* src, uint32_t srcLen, uint32_t* numCharsRead);
static uint32_t _jstr_utf8ToUtf32(uint32_t* dst, uint32_t dstMax, const char* src, uint32_t srcLen, uint32_t* numCharsRead);
static uint32_t _jstr_utf8FromUtf16(char* dst, uint32_t dstMax, const uint16_t* src, uint32_t srcLen);
static uint32_t _jstr_utf8FromUtf32(char* dst, uint32_t dstMax, const uint32_t* src, uint32_t srcLen);
static uint32_t _jstr_utf8nlen(const char* str, uint32_t max);
static const char* _jstr_utf8FindNextChar(const char* str);
static const char* _jstr_utf8FindPrevChar(const char* str);
static uint8_t* _jstr_base64Decode(const char* str, uint32_t len, uint32_t* sz, jx_allocator_i* allocator);
static char* _jstr_base64Encode(const uint8_t* data, uint32_t sz, uint32_t* len, jx_allocator_i* allocator);

jx_string_api* str_api = &(jx_string_api) {
	.strbufCreate = _jx_strbuf_create,
	.strbufDestroy = _jx_strbuf_destroy,
	.strbufReset = _jx_strbuf_reset,
	.strbufPush = _jx_strbuf_push,
	.strbufPop = _jx_strbuf_pop,
	.strbufPeek = _jx_strbuf_peek,
	.strbufPrintf = _jx_strbuf_printf,
	.strbufPrintfv = _jx_strbuf_vprintf,
	.strbufNullTerminate = _jx_strbuf_nullTerminate,
	.strbufGetString = _jx_strbuf_getString,
	.strtableCreate = _jx_strtable_create,
	.strtableDestroy = _jx_strtable_destroy,
	.strtableInsert = _jx_strtable_insert,
	.snprintf = stbsp_snprintf,
	.vsnprintf = stbsp_vsnprintf,
	.strnlen = _jstr_strnlen,
	.strndup = _jstr_strndup,
	.strcpy = _jstr_strcpy,
	.strcmp = _jstr_strcmp,
	.stricmp = _jstr_stricmp,
	.strnrchr = _jstr_strnrchr,
	.strnchr = _jstr_strnchr,
	.strncat = _jstr_strncat,
	.strTrimWhitespaces = _jstr_trimWhitespaces,
	.strto_int = _jx_strto_int,
	.strto_double = _jx_strto_double,
	.utf8to_utf16 = _jstr_utf8ToUtf16,
	.utf8to_utf32 = _jstr_utf8ToUtf32,
	.utf8from_utf16 = _jstr_utf8FromUtf16,
	.utf8from_utf32 = _jstr_utf8FromUtf32,
	.utf8nlen = _jstr_utf8nlen,
	.utf8FindNextChar = _jstr_utf8FindNextChar,
	.utf8FindPrevChar = _jstr_utf8FindPrevChar,
	.base64Decode = _jstr_base64Decode,
	.base64Encode = _jstr_base64Encode,
};

//////////////////////////////////////////////////////////////////////////
// String buffer
//
// TODO: Since jarray has been changed in order to hold the allocator used to grow the array
// there is no need for a separate struct for a string buffer. This can be replaced by a char*
// jarray.
typedef struct jx_string_buffer_t
{
	jx_allocator_i* m_Allocator;
	char* m_Buffer;
} jx_string_buffer_t;

static jx_string_buffer_t* _jx_strbuf_create(jx_allocator_i* allocator)
{
	jx_string_buffer_t* sb = (jx_string_buffer_t*)JX_ALLOC(allocator, sizeof(jx_string_buffer_t));
	if (!sb) {
		return NULL;
	}

	jx_memset(sb, 0, sizeof(jx_string_buffer_t));
	sb->m_Allocator = allocator;
	sb->m_Buffer = jx_array_create(allocator);
	_jx_strbuf_reset(sb);
//	_jx_strbuf_nullTerminate(sb);

	return sb;
}

static void _jx_strbuf_destroy(jx_string_buffer_t* sb)
{
	jx_array_free(sb->m_Buffer);
	JX_FREE(sb->m_Allocator, sb);
}

static void _jx_strbuf_reset(jx_string_buffer_t* sb)
{
	jx_array_resize(sb->m_Buffer, 1);
	sb->m_Buffer[0] = '\0';
}

static int32_t _jx_strbuf_push(jx_string_buffer_t* sb, const char* str, uint32_t len)
{
	const uint32_t sz = (uint32_t)jx_array_sizeu(sb->m_Buffer);
	if (sz > 4000) {
		int a = 0;
	}
	if (sb->m_Buffer[sz - 1] == '\0') {
		jx_array_pop_back(sb->m_Buffer);
	}

	len = jx_strnlen(str, len);
	char* ptr = jx_array_addnptr(sb->m_Buffer, len);
	if (!ptr) {
		return JX_ERROR_OUT_OF_MEMORY;
	}
	jx_memcpy(ptr, str, len);

	return JX_ERROR_NONE;
}

static int32_t _jx_strbuf_pop(jx_string_buffer_t* sb, uint32_t n)
{
	const uint32_t len = (uint32_t)jx_array_sizeu(sb->m_Buffer);
	if (n > len) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	jx_array_deln(sb->m_Buffer, len - n, n);

	return JX_ERROR_NONE;
}

static const char* _jx_strbuf_peek(jx_string_buffer_t* sb, uint32_t n)
{
	const uint32_t len = (uint32_t)jx_array_sizeu(sb->m_Buffer);
	if (n > len) {
		return NULL;
	}

	return &sb->m_Buffer[len - n];
}

static int32_t _jx_strbuf_printf(jx_string_buffer_t* sb, const char* fmt, ...)
{
	va_list argList;
	va_start(argList, fmt);
	int32_t res = _jx_strbuf_vprintf(sb, fmt, argList);
	va_end(argList);
	return res;
}

static char* strbufVPrintfCb(const char* buf, void* user, int len)
{
	int res = _jx_strbuf_push((jx_string_buffer_t*)user, buf, (uint32_t)len);
	return res == JX_ERROR_NONE
		? (char*)buf
		: NULL
		;
}

static int32_t _jx_strbuf_vprintf(jx_string_buffer_t* sb, const char* fmt, va_list argList)
{
	char buf[STB_SPRINTF_MIN];
	stbsp_vsprintfcb(strbufVPrintfCb, sb, &buf[0], fmt, argList);
	return JX_ERROR_NONE;
}

static int32_t _jx_strbuf_nullTerminate(jx_string_buffer_t* sb)
{
	const uint32_t len = (uint32_t)jx_array_sizeu(sb->m_Buffer);

	if (!len || sb->m_Buffer[len - 1] != '\0') {
		char* ptr = jx_array_addnptr(sb->m_Buffer, 1);
		if (!ptr) {
			return JX_ERROR_OUT_OF_MEMORY;
		}
		*ptr = '\0';
	}

	return JX_ERROR_NONE;
}

static const char* _jx_strbuf_getString(const jx_string_buffer_t* sb, uint32_t* len)
{
	if (len) {
		*len = (uint32_t)jx_array_sizeu(sb->m_Buffer);
	}
	return sb->m_Buffer;
}

//////////////////////////////////////////////////////////////////////////
// String table
//
typedef struct jx_string_table_entry_t
{
	const char* m_Str;
	uint32_t m_Len;
} jx_string_table_entry_t;

typedef struct jx_string_table_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_StringAllocator;
	jx_hashmap_t* m_Hashmap;
} jx_string_table_t;

static uint64_t _jx_strtable_hashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	jx_string_table_entry_t* entry = (jx_string_table_entry_t*)item;
	return jx_hashFNV1a_cstr(entry->m_Str, entry->m_Len, seed0, seed1);
}

static int32_t _jx_strtable_compareCallback(const void* a, const void* b, void* udata)
{
	jx_string_table_entry_t* entry0 = (jx_string_table_entry_t*)a;
	jx_string_table_entry_t* entry1 = (jx_string_table_entry_t*)b;
	return _jstr_strcmp(entry0->m_Str, entry0->m_Len, entry1->m_Str, entry1->m_Len);
}

static jx_string_table_t* _jx_strtable_create(jx_allocator_i* allocator)
{
	jx_string_table_t* st = (jx_string_table_t*)JX_ALLOC(allocator, sizeof(jx_string_table_t));
	if (!st) {
		return NULL;
	}

	jx_memset(st, 0, sizeof(jx_string_table_t));
	st->m_Allocator = allocator;

	st->m_Hashmap = jx_hashmapCreate(allocator, sizeof(jx_string_table_entry_t), 64, 0, 0, _jx_strtable_hashCallback, _jx_strtable_compareCallback, NULL, NULL);
	if (!st->m_Hashmap) {
		_jx_strtable_destroy(st);
		return NULL;
	}

	st->m_StringAllocator = allocator_api->createLinearAllocator(4 << 10, allocator);
	if (!st->m_StringAllocator) {
		_jx_strtable_destroy(st);
		return NULL;
	}

	return st;
}

static void _jx_strtable_destroy(jx_string_table_t* st)
{
	if (st->m_Hashmap) {
		jx_hashmapDestroy(st->m_Hashmap);
		st->m_Hashmap = NULL;
	}

	if (st->m_StringAllocator) {
		allocator_api->destroyLinearAllocator(st->m_StringAllocator);
		st->m_StringAllocator = NULL;
	}

	JX_FREE(st->m_Allocator, st);
}

static const char* _jx_strtable_insert(jx_string_table_t* st, const char* str, uint32_t len)
{
	len = len != UINT32_MAX 
		? len 
		: _jstr_strnlen(str, len)
		;

	jx_string_table_entry_t dummy = (jx_string_table_entry_t){
		.m_Str = str,
		.m_Len = len
	};
	
	jx_string_table_entry_t* entry = (jx_string_table_entry_t*)jx_hashmapGet(st->m_Hashmap, &dummy);
	if (!entry) {
		dummy.m_Str = _jstr_strndup(str, len, st->m_StringAllocator);

		jx_hashmapSet(st->m_Hashmap, &dummy);
		entry = (jx_string_table_entry_t*)jx_hashmapGet(st->m_Hashmap, &dummy);
	}

	return entry->m_Str;
}

//////////////////////////////////////////////////////////////////////////
// String functions
//
static uint32_t _jstr_strnlen(const char* str, uint32_t max)
{
	const char* ptr = str;
	if (ptr != NULL) {
		for (; max > 0 && *ptr != '\0'; ++ptr, --max);
	}
	return (uint32_t)(ptr - str);
}

static char* _jstr_strndup(const char* str, uint32_t n, jx_allocator_i* allocator)
{
	const uint32_t len = jx_strnlen(str, n);
	char* dst = (char*)JX_ALLOC(allocator, len + 1);
	jx_memcpy(dst, str, len);
	dst[len] = '\0';

	return dst;
}

static uint32_t _jstr_strcpy(char* dst, uint32_t dstSize, const char* src, uint32_t n)
{
	const uint32_t len = jx_strnlen(src, n);
	const uint32_t max = dstSize - 1;
	const uint32_t num = len < max ? len : max;
	jx_memcpy(dst, src, num);
	dst[num] = '\0';

	return num;
}

static int32_t _jstr_strcmp(const char* lhs, uint32_t lhsMax, const char* rhs, uint32_t rhsMax)
{
	uint32_t max = lhsMax < rhsMax ? lhsMax : rhsMax;
	for (; max > 0 && *lhs == *rhs && *lhs != '\0'; ++lhs, ++rhs, --max);

	if (max == 0) {
		return lhsMax == rhsMax 
			? 0 
			: (lhsMax > rhsMax ? 1 : -1)
			;
	}

	return *lhs - *rhs;
}

int32_t _jstr_stricmp(const char* lhs, uint32_t lhsMax, const char* rhs, uint32_t rhsMax)
{
	uint32_t max = lhsMax < rhsMax 
		? lhsMax 
		: rhsMax
		;

	for (
		; max > 0 && jx_tolower(*lhs) == jx_tolower(*rhs) && *lhs != '\0' && *rhs != '\0'
		; ++lhs, ++rhs, --max
		);

	if (max == 0) {
		return lhsMax == rhsMax
			? 0
			: (lhsMax > rhsMax ? 1 : -1)
			;
	}

	return jx_tolower(*lhs) - jx_tolower(*rhs);
}

static const char* _jstr_strnrchr(const char* str, uint32_t n, char ch)
{	
	if (n == UINT32_MAX) {
		const char* lastOccurence = NULL;
		for (; *str != '\0'; ++str) {
			if (*str == ch) {
				lastOccurence = str;
			}
		}

		return lastOccurence;
	}

	while (n--) {
		if (str[n] == ch) {
			return &str[n];
		}
	}

	return NULL;
}

static const char* _jstr_strnchr(const char* str, uint32_t n, char ch)
{
	for (; n > 0 && *str != '\0'; ++str, --n) {
		if (*str == ch) {
			return str;
		}
	}

	return NULL;
}

static uint32_t _jstr_strncat(char* dst, uint32_t dstSize, const char* src, uint32_t n)
{
	const uint32_t max = dstSize;
	const uint32_t len = _jstr_strnlen(dst, dstSize);
	return _jstr_strcpy(&dst[len], max - len, src, n);
}

static void _jstr_trimWhitespaces(char* str, uint32_t len)
{
	len = len != UINT32_MAX
		? len
		: _jstr_strnlen(str, UINT32_MAX)
		;

	char* end = str + len;

	char* frontPtr = str;
	while (frontPtr != end && jx_isspace(*frontPtr)) {
		++frontPtr;
	}

	if (frontPtr == end) {
		// The whole string is filled with whitespaces.
		*str = '\0';
		return;
	}

	char* backPtr = end - 1;
	while (backPtr != str && jx_isspace(*backPtr)) {
		--backPtr;
	}

	const uint32_t newLen = (uint32_t)(backPtr - frontPtr);
	jx_memmove(str, frontPtr, newLen);
	str[newLen] = '\0';
}

//////////////////////////////////////////////////////////////////////////
// Conversion functions
//
static int64_t _jx_strto_int(const char* str, uint32_t len, char** str_end, int32_t base, int64_t defaultVal)
{
	// Skip whitespaces
	while (jx_isspace(*str) && len > 0) {
		++str;
		--len;
	}

	if (len == 0 || *str == '\0') {
		if (str_end) {
			*str_end = (char*)str;
		}
		return defaultVal;
	}

	bool negate = false;
	if (*str == '-' || *str == '+') {
		negate = *str == '-';
		++str;
		--len;
	}

	if (base == 0) {
		if (str[0] == '0') {
			if (str[1] == 'x') {
				str += 2;
				base = 16;
			} else if (str[1] == 'b') {
				str += 2;
				base = 2;
			} else {
				str++;
				base = 8;
			}
		} else {
			base = 10;
		}
	}

	const char* digitsStart = str;
	int64_t res = 0;
	if (base == 10) {
		while (jx_isdigit(*str) && len > 0) {
			res = res * 10 + (*str - '0');
			++str;
			--len;
		}
	} else if (base == 8) {
		while (jx_isoctal(*str) && len > 0) {
			res = res * 8 + (*str - '0');
			++str;
			--len;
		}
	} else if (base == 16) {
		while (jx_isxdigit(*str) && len > 0) {
			res = res * 16 + jx_hexCharToInt(*str);
			++str;
			--len;
		}
	} else if (base == 2) {
		while (*str == '0' || *str == '1') {
			res = res * 2 + (*str - '0');
			++str;
			--len;
		}
	}

	if (digitsStart == str) {
		return defaultVal;
	}

	if (str_end) {
		*str_end = (char*)str;
	}

	return negate
		? -res
		: res
		;
}

// Adapted from https://www.gurucoding.com/dconvstr/
/*
 *  Bijective, heapless and bignumless conversion of IEEE 754 double to string and vice versa
 *  http://www.gurucoding.com/dconvstr/
 *
 *  Copyright (c) 2014 Mikhail Kupchik <Mikhail.Kupchik@prime-expert.com>
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without modification, are permitted
 *  provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *     and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions
 *     and the following disclaimer in the documentation and/or other materials provided with the
 *     distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 *  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 *  FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 *  IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 *  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
typedef enum _jdouble_parser_state
{
	_JDOUBLE_PARSER_STATE_INITIAL = 0,
	_JDOUBLE_PARSER_STATE_AFTER_SIGN,
	_JDOUBLE_PARSER_STATE_BEFORE_DECIMAL_POINT,
	_JDOUBLE_PARSER_STATE_FIRST_FRACTIONAL_DIGIT,
	_JDOUBLE_PARSER_STATE_FRACTIONAL,
	_JDOUBLE_PARSER_STATE_BEFORE_EXPONENT_SIGN,
	_JDOUBLE_PARSER_STATE_FIRST_EXPONENT_DIGIT,
	_JDOUBLE_PARSER_STATE_EXPONENT
} _jdouble_parser_state;

static uint64_t bcd_compress(const uint8_t* decompressed_bcd);
static int32_t pack_ieee754_double(int32_t input_is_nan, int32_t input_sign, uint64_t input_binary_mantissa, int32_t input_binary_exponent, int32_t input_is_infinity, double* output);
static int32_t convert_extended_decimal_to_binary_and_round(uint64_t a, int32_t b, uint64_t* c, int32_t* d);

static double _jx_strto_double(const char* str, uint32_t len, char** str_end, double defaultVal)
{
	// TODO: Handle special cases here (NAN, INF, -INF)
	int32_t error = 0;
	int32_t negativeMantissa = 0;
	uint8_t parsedDigits[20];
	int32_t numParsedDigits = 0;
	int32_t exponent = 0;
	int32_t exponentOffset = 0;
	int32_t negativeExponent = 0;

	_jdouble_parser_state state = _JDOUBLE_PARSER_STATE_INITIAL;
	const uint32_t initialLen = len;

	const char* ptr = str;

	while (!error && *ptr && len > 0) {
		const char ch = *ptr;
		switch (state) {
		case _JDOUBLE_PARSER_STATE_INITIAL: {
			if (jx_isspace(ch)) {
				++ptr;
				--len;
			} else if (ch == '-') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_AFTER_SIGN;
				negativeMantissa = 1;
			} else if (ch == '+') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_AFTER_SIGN;
			} else if (jx_isdigit(ch)) {
				state = _JDOUBLE_PARSER_STATE_BEFORE_DECIMAL_POINT;
			} else if (ch == '.') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_FIRST_FRACTIONAL_DIGIT;
			} else {
				error = 1;
			}
		} break;
		case _JDOUBLE_PARSER_STATE_AFTER_SIGN: {
			if (jx_isdigit(ch)) {
				state = _JDOUBLE_PARSER_STATE_BEFORE_DECIMAL_POINT;
			} else if (ch == '.') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_FIRST_FRACTIONAL_DIGIT;
			} else {
				error = 1;
			}
		} break;
		case _JDOUBLE_PARSER_STATE_BEFORE_DECIMAL_POINT: {
			if (jx_isdigit(ch)) {
				++ptr;
				--len;
				if (numParsedDigits < JX_COUNTOF(parsedDigits)) {
					parsedDigits[numParsedDigits] = ch - '0';
					if ((ch != '0') || (numParsedDigits != 0)) {
						++numParsedDigits;
					}
				} else {
					++exponentOffset;
				}
			} else if (ch == '.') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_FIRST_FRACTIONAL_DIGIT;
			} else if (jx_tolower(ch) == 'e') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_BEFORE_EXPONENT_SIGN;
			} else {
				error = 1;
			}
		} break;
		case _JDOUBLE_PARSER_STATE_FIRST_FRACTIONAL_DIGIT: {
			if (jx_isdigit(ch)) {
				state = _JDOUBLE_PARSER_STATE_FRACTIONAL;
			} else {
				error = 1;
			}
		} break;
		case _JDOUBLE_PARSER_STATE_FRACTIONAL: {
			if (jx_isdigit(ch)) {
				++ptr;
				--len;

				if (numParsedDigits < JX_COUNTOF(parsedDigits)) {
					parsedDigits[numParsedDigits] = ch - '0';
					if ((ch != '0') || (numParsedDigits != 0)) {
						++numParsedDigits;
					}
					--exponentOffset;
				}
			} else if (jx_tolower(ch) == 'e') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_BEFORE_EXPONENT_SIGN;
			} else {
				error = 1;
			}
		} break;
		case _JDOUBLE_PARSER_STATE_BEFORE_EXPONENT_SIGN: {
			if (jx_isdigit(ch)) {
				state = _JDOUBLE_PARSER_STATE_EXPONENT;
			} else if (ch == '+') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_FIRST_EXPONENT_DIGIT;
			} else if (ch == '-') {
				++ptr;
				--len;
				state = _JDOUBLE_PARSER_STATE_FIRST_EXPONENT_DIGIT;
				negativeExponent = 1;
			} else {
				error = 1;
			}
		} break;
		case _JDOUBLE_PARSER_STATE_FIRST_EXPONENT_DIGIT: {
			if (jx_isdigit(ch)) {
				state = _JDOUBLE_PARSER_STATE_EXPONENT;
			} else {
				error = 1;
			}
		} break;
		case _JDOUBLE_PARSER_STATE_EXPONENT: {
			if (jx_isdigit(ch)) {
				++ptr;
				--len;

				if (exponent < 350) {
					exponent = exponent * 10 + (ch - '0');
				}
			} else {
				error = 1;
			}
		} break;
		}
	}

	// JD: Allow ending parsing at invalid characters without error if initial len is equal to UINT32_MAX
	if (error && initialLen == UINT32_MAX && (state == _JDOUBLE_PARSER_STATE_BEFORE_DECIMAL_POINT || state == _JDOUBLE_PARSER_STATE_FRACTIONAL || state == _JDOUBLE_PARSER_STATE_EXPONENT)) {
		error = 0;
	}

	if (!error && (false
		|| (state == _JDOUBLE_PARSER_STATE_INITIAL)
		|| (state == _JDOUBLE_PARSER_STATE_AFTER_SIGN)
		|| (state == _JDOUBLE_PARSER_STATE_FIRST_FRACTIONAL_DIGIT)
		|| (state == _JDOUBLE_PARSER_STATE_BEFORE_EXPONENT_SIGN)
		|| (state == _JDOUBLE_PARSER_STATE_FIRST_EXPONENT_DIGIT))) {
		error = 1;
	}

	if (str_end) {
		*str_end = error
			? (char*)str
			: (char*)ptr
			;
	}

	if (error) {
		return defaultVal;
	}

	if (numParsedDigits < JX_COUNTOF(parsedDigits)) {
		const int32_t delta = JX_COUNTOF(parsedDigits) - numParsedDigits;
		jx_memset(&parsedDigits[numParsedDigits], 0, delta);
		numParsedDigits += delta;
		exponentOffset -= delta;
	}
	jx_memmove(&parsedDigits[1], &parsedDigits[0], JX_COUNTOF(parsedDigits) - 1);
	parsedDigits[0] = 0;
	++exponentOffset;

	uint64_t mantissa = bcd_compress(parsedDigits);

	if (mantissa == 0) {
		exponent = 0;
	} else {
		exponent = ((exponent < 350) ? exponentOffset : 0) + (negativeExponent ? -exponent : exponent);
	}

	if (exponent <= -350) {
		// +/- 0.0
		double output;
		pack_ieee754_double(
			0,                // input_is_nan
			negativeMantissa, // input_sign
			0,                // input_binary_mantissa
			0,                // input_binary_exponent
			0,                // input_is_infinity
			&output
		);
		return output;
	} else if (exponent >= 350) {
		// +/- Inf
		double output;
		pack_ieee754_double(
			0,                // input_is_nan
			negativeMantissa, // input_sign
			0,                // input_binary_mantissa
			0,                // input_binary_exponent
			1,                // input_is_infinity
			&output
		);
		return output;
	}

	if (mantissa != 0) {
		if (!convert_extended_decimal_to_binary_and_round(mantissa, exponent, &mantissa, &exponent)) {
			// internal error
			return defaultVal;
		}
	}

	double output;
	pack_ieee754_double(
		0,                // input_is_nan
		negativeMantissa, // input_sign
		mantissa,         // input_binary_mantissa
		exponent,         // input_binary_exponent
		0,                // input_is_infinity
		&output
	);
	return output;
}

static inline uint32_t bcd_compress_small(const uint8_t* decompressed_bcd)
{
	uint32_t high_pair = 10 * decompressed_bcd[0] + decompressed_bcd[1];
	uint32_t low_pair = 10 * decompressed_bcd[2] + decompressed_bcd[3];
	return 100 * high_pair + low_pair;
}

static uint64_t bcd_compress(const uint8_t* decompressed_bcd)
{
	uint64_t  d2 = bcd_compress_small(decompressed_bcd);
	uint64_t  d1 = 10000 * bcd_compress_small(decompressed_bcd + 4) + bcd_compress_small(decompressed_bcd + 8);
	uint64_t  d0 = 10000 * bcd_compress_small(decompressed_bcd + 12) + bcd_compress_small(decompressed_bcd + 16);
	return  ((d2 * (10000ULL * 10000ULL) + d1) * (10000ULL * 10000ULL)) + d0;
}

static inline uint32_t count_leading_zeros(uint64_t a)
{
	uint32_t n = 0;
	if (a <= 0x00000000FFFFFFFFull) { n += 32; a <<= 32; }
	if (a <= 0x0000FFFFFFFFFFFFull) { n += 16; a <<= 16; }
	if (a <= 0x00FFFFFFFFFFFFFFull) { n += 8;  a <<= 8; }
	if (a <= 0x0FFFFFFFFFFFFFFFull) { n += 4;  a <<= 4; }
	if (a <= 0x3FFFFFFFFFFFFFFFull) { n += 2;  a <<= 2; }
	if (a <= 0x7FFFFFFFFFFFFFFFull) { n += 1; }
	return n;
}

static int32_t pack_ieee754_double(int32_t input_is_nan, int32_t input_sign, uint64_t input_binary_mantissa, int32_t input_binary_exponent, int32_t input_is_infinity, double* output)
{
	// 1. Initialize values to pack
	uint64_t output_sign = 0;
	uint64_t output_exponent = 0;
	uint64_t output_mantissa = 0;

	// 2. Handle special case: NaN
	int32_t had_overflow_or_underflow_in_exponent = 0;
	if (input_is_nan) {
		output_sign = 1;              // Quiet NaN
		output_exponent = 0x7FF;
		output_mantissa = (1ULL << 51);   // mantissa MSB set
	} else {
		output_sign = (input_sign ? 1 : 0);
		if (input_is_infinity) {
			// 3. Handle special case: +INF/-INF
			output_exponent = 0x7FF;
			output_mantissa = 0;
		} else if (input_binary_mantissa == 0) {
			// 4. Handle special case: +0/-0
			output_exponent = 0;
			output_mantissa = 0;
		} else {
			uint32_t lz = count_leading_zeros(input_binary_mantissa);
			input_binary_mantissa <<= lz;
			input_binary_exponent -= lz;

			if (input_binary_exponent > 1023) {
				// 5. Handle unintentional infinity (due to exponent overflow)
				output_exponent = 0x7FF;
				output_mantissa = 0;
				had_overflow_or_underflow_in_exponent = 1;
			} else if (input_binary_exponent >= -1022) {
				// 6. Handle normalized numbers
				output_exponent = ((uint64_t)(input_binary_exponent + 1023));
				output_mantissa = (input_binary_mantissa >> 11) & ((1ULL << 52) - 1ULL);
			} else {
				// 7. Handle denormalized numbers
				//    and unintentional zero (due to exponent underflow)
				input_binary_mantissa >>= 11;
				while ((input_binary_mantissa != 0) && (input_binary_exponent < -1022)) {
					input_binary_mantissa >>= 1;
					++input_binary_exponent;
				}
				output_exponent = 0;
				output_mantissa = input_binary_mantissa;
				had_overflow_or_underflow_in_exponent = (input_binary_mantissa == 0);
			}
		}
	}

	// 8. Pack bits up
	uint64_t output_bits = 0
		| (output_sign << 63) 
		| ((output_exponent & 0x7FFULL) << 52) 
		| (output_mantissa & ((1ULL << 52) - 1ULL))
		;
	*((uint64_t*)output) = output_bits;

	return (!had_overflow_or_underflow_in_exponent);
}

// Table: powers of ten in binary representation.
// 10^decimal_exponent ~= binary_mantissa * 2^binary_exponent.
// 2^63 < binary_mantissa < 2^64, mantissa has been rounded to nearest integer.
// Covers entire range of IEEE 754 double, including denormals, plus small spare.
typedef struct power_of_ten
{
	int32_t   decimal_exponent;
	int32_t   binary_exponent;
	uint64_t  binary_mantissa;
} power_of_ten;

static const power_of_ten powers_of_ten_[] = {
	{ -344, -1206,  11019826852086880396ULL },    { -343, -1203,  13774783565108600494ULL },
	{ -342, -1200,  17218479456385750618ULL },    { -341, -1196,  10761549660241094136ULL },
	{ -340, -1193,  13451937075301367670ULL },    { -339, -1190,  16814921344126709588ULL },
	{ -338, -1186,  10509325840079193492ULL },    { -337, -1183,  13136657300098991866ULL },
	{ -336, -1180,  16420821625123739832ULL },    { -335, -1176,  10263013515702337395ULL },
	{ -334, -1173,  12828766894627921744ULL },    { -333, -1170,  16035958618284902180ULL },
	{ -332, -1166,  10022474136428063862ULL },    { -331, -1163,  12528092670535079828ULL },
	{ -330, -1160,  15660115838168849785ULL },    { -329, -1156,   9787572398855531116ULL },
	{ -328, -1153,  12234465498569413894ULL },    { -327, -1150,  15293081873211767368ULL },
	{ -326, -1146,   9558176170757354605ULL },    { -325, -1143,  11947720213446693256ULL },
	{ -324, -1140,  14934650266808366570ULL },    { -323, -1136,   9334156416755229106ULL },
	{ -322, -1133,  11667695520944036383ULL },    { -321, -1130,  14584619401180045479ULL },
	{ -320, -1127,  18230774251475056849ULL },    { -319, -1123,  11394233907171910530ULL },
	{ -318, -1120,  14242792383964888163ULL },    { -317, -1117,  17803490479956110204ULL },
	{ -316, -1113,  11127181549972568877ULL },    { -315, -1110,  13908976937465711097ULL },
	{ -314, -1107,  17386221171832138871ULL },    { -313, -1103,  10866388232395086794ULL },
	{ -312, -1100,  13582985290493858493ULL },    { -311, -1097,  16978731613117323116ULL },
	{ -310, -1093,  10611707258198326947ULL },    { -309, -1090,  13264634072747908684ULL },
	{ -308, -1087,  16580792590934885855ULL },    { -307, -1083,  10362995369334303660ULL },
	{ -306, -1080,  12953744211667879575ULL },    { -305, -1077,  16192180264584849468ULL },
	{ -304, -1073,  10120112665365530918ULL },    { -303, -1070,  12650140831706913647ULL },
	{ -302, -1067,  15812676039633642059ULL },    { -301, -1063,   9882922524771026287ULL },
	{ -300, -1060,  12353653155963782858ULL },    { -299, -1057,  15442066444954728573ULL },
	{ -298, -1053,   9651291528096705358ULL },    { -297, -1050,  12064114410120881698ULL },
	{ -296, -1047,  15080143012651102122ULL },    { -295, -1043,   9425089382906938826ULL },
	{ -294, -1040,  11781361728633673533ULL },    { -293, -1037,  14726702160792091916ULL },
	{ -292, -1034,  18408377700990114895ULL },    { -291, -1030,  11505236063118821809ULL },
	{ -290, -1027,  14381545078898527262ULL },    { -289, -1024,  17976931348623159077ULL },
	{ -288, -1020,  11235582092889474423ULL },    { -287, -1017,  14044477616111843029ULL },
	{ -286, -1014,  17555597020139803786ULL },    { -285, -1010,  10972248137587377367ULL },
	{ -284, -1007,  13715310171984221708ULL },    { -283, -1004,  17144137714980277135ULL },
	{ -282, -1000,  10715086071862673209ULL },    { -281,  -997,  13393857589828341512ULL },
	{ -280,  -994,  16742321987285426890ULL },    { -279,  -990,  10463951242053391806ULL },
	{ -278,  -987,  13079939052566739758ULL },    { -277,  -984,  16349923815708424697ULL },
	{ -276,  -980,  10218702384817765436ULL },    { -275,  -977,  12773377981022206795ULL },
	{ -274,  -974,  15966722476277758493ULL },    { -273,  -970,   9979201547673599058ULL },
	{ -272,  -967,  12474001934591998823ULL },    { -271,  -964,  15592502418239998529ULL },
	{ -270,  -960,   9745314011399999080ULL },    { -269,  -957,  12181642514249998850ULL },
	{ -268,  -954,  15227053142812498563ULL },    { -267,  -950,   9516908214257811602ULL },
	{ -266,  -947,  11896135267822264502ULL },    { -265,  -944,  14870169084777830628ULL },
	{ -264,  -940,   9293855677986144142ULL },    { -263,  -937,  11617319597482680178ULL },
	{ -262,  -934,  14521649496853350223ULL },    { -261,  -931,  18152061871066687778ULL },
	{ -260,  -927,  11345038669416679861ULL },    { -259,  -924,  14181298336770849827ULL },
	{ -258,  -921,  17726622920963562283ULL },    { -257,  -917,  11079139325602226427ULL },
	{ -256,  -914,  13848924157002783034ULL },    { -255,  -911,  17311155196253478792ULL },
	{ -254,  -907,  10819471997658424245ULL },    { -253,  -904,  13524339997073030307ULL },
	{ -252,  -901,  16905424996341287883ULL },    { -251,  -897,  10565890622713304927ULL },
	{ -250,  -894,  13207363278391631159ULL },    { -249,  -891,  16509204097989538949ULL },
	{ -248,  -887,  10318252561243461843ULL },    { -247,  -884,  12897815701554327304ULL },
	{ -246,  -881,  16122269626942909129ULL },    { -245,  -877,  10076418516839318206ULL },
	{ -244,  -874,  12595523146049147757ULL },    { -243,  -871,  15744403932561434697ULL },
	{ -242,  -867,   9840252457850896685ULL },    { -241,  -864,  12300315572313620857ULL },
	{ -240,  -861,  15375394465392026071ULL },    { -239,  -857,   9609621540870016294ULL },
	{ -238,  -854,  12012026926087520368ULL },    { -237,  -851,  15015033657609400460ULL },
	{ -236,  -847,   9384396036005875287ULL },    { -235,  -844,  11730495045007344109ULL },
	{ -234,  -841,  14663118806259180137ULL },    { -233,  -838,  18328898507823975171ULL },
	{ -232,  -834,  11455561567389984482ULL },    { -231,  -831,  14319451959237480602ULL },
	{ -230,  -828,  17899314949046850753ULL },    { -229,  -824,  11187071843154281720ULL },
	{ -228,  -821,  13983839803942852151ULL },    { -227,  -818,  17479799754928565188ULL },
	{ -226,  -814,  10924874846830353243ULL },    { -225,  -811,  13656093558537941553ULL },
	{ -224,  -808,  17070116948172426942ULL },    { -223,  -804,  10668823092607766839ULL },
	{ -222,  -801,  13336028865759708548ULL },    { -221,  -798,  16670036082199635685ULL },
	{ -220,  -794,  10418772551374772303ULL },    { -219,  -791,  13023465689218465379ULL },
	{ -218,  -788,  16279332111523081724ULL },    { -217,  -784,  10174582569701926077ULL },
	{ -216,  -781,  12718228212127407597ULL },    { -215,  -778,  15897785265159259496ULL },
	{ -214,  -774,   9936115790724537185ULL },    { -213,  -771,  12420144738405671481ULL },
	{ -212,  -768,  15525180923007089351ULL },    { -211,  -764,   9703238076879430845ULL },
	{ -210,  -761,  12129047596099288556ULL },    { -209,  -758,  15161309495124110695ULL },
	{ -208,  -754,   9475818434452569184ULL },    { -207,  -751,  11844773043065711480ULL },
	{ -206,  -748,  14805966303832139350ULL },    { -205,  -744,   9253728939895087094ULL },
	{ -204,  -741,  11567161174868858868ULL },    { -203,  -738,  14458951468586073584ULL },
	{ -202,  -735,  18073689335732591980ULL },    { -201,  -731,  11296055834832869988ULL },
	{ -200,  -728,  14120069793541087485ULL },    { -199,  -725,  17650087241926359356ULL },
	{ -198,  -721,  11031304526203974597ULL },    { -197,  -718,  13789130657754968247ULL },
	{ -196,  -715,  17236413322193710309ULL },    { -195,  -711,  10772758326371068943ULL },
	{ -194,  -708,  13465947907963836179ULL },    { -193,  -705,  16832434884954795223ULL },
	{ -192,  -701,  10520271803096747014ULL },    { -191,  -698,  13150339753870933768ULL },
	{ -190,  -695,  16437924692338667210ULL },    { -189,  -691,  10273702932711667006ULL },
	{ -188,  -688,  12842128665889583758ULL },    { -187,  -685,  16052660832361979697ULL },
	{ -186,  -681,  10032913020226237311ULL },    { -185,  -678,  12541141275282796639ULL },
	{ -184,  -675,  15676426594103495798ULL },    { -183,  -671,   9797766621314684874ULL },
	{ -182,  -668,  12247208276643356092ULL },    { -181,  -665,  15309010345804195115ULL },
	{ -180,  -661,   9568131466127621947ULL },    { -179,  -658,  11960164332659527434ULL },
	{ -178,  -655,  14950205415824409292ULL },    { -177,  -651,   9343878384890255808ULL },
	{ -176,  -648,  11679847981112819760ULL },    { -175,  -645,  14599809976391024700ULL },
	{ -174,  -642,  18249762470488780875ULL },    { -173,  -638,  11406101544055488047ULL },
	{ -172,  -635,  14257626930069360058ULL },    { -171,  -632,  17822033662586700073ULL },
	{ -170,  -628,  11138771039116687546ULL },    { -169,  -625,  13923463798895859432ULL },
	{ -168,  -622,  17404329748619824290ULL },    { -167,  -618,  10877706092887390181ULL },
	{ -166,  -615,  13597132616109237726ULL },    { -165,  -612,  16996415770136547158ULL },
	{ -164,  -608,  10622759856335341974ULL },    { -163,  -605,  13278449820419177467ULL },
	{ -162,  -602,  16598062275523971834ULL },    { -161,  -598,  10373788922202482396ULL },
	{ -160,  -595,  12967236152753102995ULL },    { -159,  -592,  16209045190941378744ULL },
	{ -158,  -588,  10130653244338361715ULL },    { -157,  -585,  12663316555422952144ULL },
	{ -156,  -582,  15829145694278690180ULL },    { -155,  -578,   9893216058924181362ULL },
	{ -154,  -575,  12366520073655226703ULL },    { -153,  -572,  15458150092069033379ULL },
	{ -152,  -568,   9661343807543145862ULL },    { -151,  -565,  12076679759428932327ULL },
	{ -150,  -562,  15095849699286165409ULL },    { -149,  -558,   9434906062053853381ULL },
	{ -148,  -555,  11793632577567316726ULL },    { -147,  -552,  14742040721959145907ULL },
	{ -146,  -549,  18427550902448932384ULL },    { -145,  -545,  11517219314030582740ULL },
	{ -144,  -542,  14396524142538228425ULL },    { -143,  -539,  17995655178172785531ULL },
	{ -142,  -535,  11247284486357990957ULL },    { -141,  -532,  14059105607947488696ULL },
	{ -140,  -529,  17573882009934360870ULL },    { -139,  -525,  10983676256208975544ULL },
	{ -138,  -522,  13729595320261219430ULL },    { -137,  -519,  17161994150326524287ULL },
	{ -136,  -515,  10726246343954077680ULL },    { -135,  -512,  13407807929942597100ULL },
	{ -134,  -509,  16759759912428246374ULL },    { -133,  -505,  10474849945267653984ULL },
	{ -132,  -502,  13093562431584567480ULL },    { -131,  -499,  16366953039480709350ULL },
	{ -130,  -495,  10229345649675443344ULL },    { -129,  -492,  12786682062094304180ULL },
	{ -128,  -489,  15983352577617880225ULL },    { -127,  -485,   9989595361011175140ULL },
	{ -126,  -482,  12486994201263968926ULL },    { -125,  -479,  15608742751579961157ULL },
	{ -124,  -475,   9755464219737475723ULL },    { -123,  -472,  12194330274671844654ULL },
	{ -122,  -469,  15242912843339805817ULL },    { -121,  -465,   9526820527087378636ULL },
	{ -120,  -462,  11908525658859223295ULL },    { -119,  -459,  14885657073574029118ULL },
	{ -118,  -455,   9303535670983768199ULL },    { -117,  -452,  11629419588729710249ULL },
	{ -116,  -449,  14536774485912137811ULL },    { -115,  -446,  18170968107390172264ULL },
	{ -114,  -442,  11356855067118857665ULL },    { -113,  -439,  14196068833898572081ULL },
	{ -112,  -436,  17745086042373215101ULL },    { -111,  -432,  11090678776483259438ULL },
	{ -110,  -429,  13863348470604074298ULL },    { -109,  -426,  17329185588255092872ULL },
	{ -108,  -422,  10830740992659433045ULL },    { -107,  -419,  13538426240824291307ULL },
	{ -106,  -416,  16923032801030364133ULL },    { -105,  -412,  10576895500643977583ULL },
	{ -104,  -409,  13221119375804971979ULL },    { -103,  -406,  16526399219756214974ULL },
	{ -102,  -402,  10328999512347634359ULL },    { -101,  -399,  12911249390434542948ULL },
	{ -100,  -396,  16139061738043178685ULL },    {  -99,  -392,  10086913586276986678ULL },
	{  -98,  -389,  12608641982846233348ULL },    {  -97,  -386,  15760802478557791685ULL },
	{  -96,  -382,   9850501549098619803ULL },    {  -95,  -379,  12313126936373274754ULL },
	{  -94,  -376,  15391408670466593442ULL },    {  -93,  -372,   9619630419041620901ULL },
	{  -92,  -369,  12024538023802026127ULL },    {  -91,  -366,  15030672529752532658ULL },
	{  -90,  -362,   9394170331095332912ULL },    {  -89,  -359,  11742712913869166139ULL },
	{  -88,  -356,  14678391142336457674ULL },    {  -87,  -353,  18347988927920572093ULL },
	{  -86,  -349,  11467493079950357558ULL },    {  -85,  -346,  14334366349937946948ULL },
	{  -84,  -343,  17917957937422433684ULL },    {  -83,  -339,  11198723710889021053ULL },
	{  -82,  -336,  13998404638611276316ULL },    {  -81,  -333,  17498005798264095395ULL },
	{  -80,  -329,  10936253623915059622ULL },    {  -79,  -326,  13670317029893824527ULL },
	{  -78,  -323,  17087896287367280659ULL },    {  -77,  -319,  10679935179604550412ULL },
	{  -76,  -316,  13349918974505688015ULL },    {  -75,  -313,  16687398718132110019ULL },
	{  -74,  -309,  10429624198832568762ULL },    {  -73,  -306,  13037030248540710952ULL },
	{  -72,  -303,  16296287810675888690ULL },    {  -71,  -299,  10185179881672430431ULL },
	{  -70,  -296,  12731474852090538039ULL },    {  -69,  -293,  15914343565113172549ULL },
	{  -68,  -289,   9946464728195732843ULL },    {  -67,  -286,  12433080910244666054ULL },
	{  -66,  -283,  15541351137805832567ULL },    {  -65,  -279,   9713344461128645355ULL },
	{  -64,  -276,  12141680576410806693ULL },    {  -63,  -273,  15177100720513508367ULL },
	{  -62,  -269,   9485687950320942729ULL },    {  -61,  -266,  11857109937901178411ULL },
	{  -60,  -263,  14821387422376473014ULL },    {  -59,  -259,   9263367138985295634ULL },
	{  -58,  -256,  11579208923731619542ULL },    {  -57,  -253,  14474011154664524428ULL },
	{  -56,  -250,  18092513943330655535ULL },    {  -55,  -246,  11307821214581659709ULL },
	{  -54,  -243,  14134776518227074637ULL },    {  -53,  -240,  17668470647783843296ULL },
	{  -52,  -236,  11042794154864902060ULL },    {  -51,  -233,  13803492693581127575ULL },
	{  -50,  -230,  17254365866976409469ULL },    {  -49,  -226,  10783978666860255918ULL },
	{  -48,  -223,  13479973333575319897ULL },    {  -47,  -220,  16849966666969149872ULL },
	{  -46,  -216,  10531229166855718670ULL },    {  -45,  -213,  13164036458569648337ULL },
	{  -44,  -210,  16455045573212060422ULL },    {  -43,  -206,  10284403483257537763ULL },
	{  -42,  -203,  12855504354071922204ULL },    {  -41,  -200,  16069380442589902755ULL },
	{  -40,  -196,  10043362776618689222ULL },    {  -39,  -193,  12554203470773361528ULL },
	{  -38,  -190,  15692754338466701910ULL },    {  -37,  -186,   9807971461541688693ULL },
	{  -36,  -183,  12259964326927110867ULL },    {  -35,  -180,  15324955408658888584ULL },
	{  -34,  -176,   9578097130411805365ULL },    {  -33,  -173,  11972621413014756706ULL },
	{  -32,  -170,  14965776766268445882ULL },    {  -31,  -166,   9353610478917778677ULL },
	{  -30,  -163,  11692013098647223346ULL },    {  -29,  -160,  14615016373309029182ULL },
	{  -28,  -157,  18268770466636286478ULL },    {  -27,  -153,  11417981541647679048ULL },
	{  -26,  -150,  14272476927059598811ULL },    {  -25,  -147,  17840596158824498513ULL },
	{  -24,  -143,  11150372599265311571ULL },    {  -23,  -140,  13937965749081639463ULL },
	{  -22,  -137,  17422457186352049329ULL },    {  -21,  -133,  10889035741470030831ULL },
	{  -20,  -130,  13611294676837538539ULL },    {  -19,  -127,  17014118346046923173ULL },
	{  -18,  -123,  10633823966279326983ULL },    {  -17,  -120,  13292279957849158729ULL },
	{  -16,  -117,  16615349947311448411ULL },    {  -15,  -113,  10384593717069655257ULL },
	{  -14,  -110,  12980742146337069071ULL },    {  -13,  -107,  16225927682921336339ULL },
	{  -12,  -103,  10141204801825835212ULL },    {  -11,  -100,  12676506002282294015ULL },
	{  -10,   -97,  15845632502852867519ULL },    {   -9,   -93,   9903520314283042199ULL },
	{   -8,   -90,  12379400392853802749ULL },    {   -7,   -87,  15474250491067253436ULL },
	{   -6,   -83,   9671406556917033398ULL },    {   -5,   -80,  12089258196146291747ULL },
	{   -4,   -77,  15111572745182864684ULL },    {   -3,   -73,   9444732965739290427ULL },
	{   -2,   -70,  11805916207174113034ULL },    {   -1,   -67,  14757395258967641293ULL },
	{    0,   -63,   9223372036854775808ULL },    {    1,   -60,  11529215046068469760ULL },
	{    2,   -57,  14411518807585587200ULL },    {    3,   -54,  18014398509481984000ULL },
	{    4,   -50,  11258999068426240000ULL },    {    5,   -47,  14073748835532800000ULL },
	{    6,   -44,  17592186044416000000ULL },    {    7,   -40,  10995116277760000000ULL },
	{    8,   -37,  13743895347200000000ULL },    {    9,   -34,  17179869184000000000ULL },
	{   10,   -30,  10737418240000000000ULL },    {   11,   -27,  13421772800000000000ULL },
	{   12,   -24,  16777216000000000000ULL },    {   13,   -20,  10485760000000000000ULL },
	{   14,   -17,  13107200000000000000ULL },    {   15,   -14,  16384000000000000000ULL },
	{   16,   -10,  10240000000000000000ULL },    {   17,    -7,  12800000000000000000ULL },
	{   18,    -4,  16000000000000000000ULL },    {   19,     0,  10000000000000000000ULL },
	{   20,     3,  12500000000000000000ULL },    {   21,     6,  15625000000000000000ULL },
	{   22,    10,   9765625000000000000ULL },    {   23,    13,  12207031250000000000ULL },
	{   24,    16,  15258789062500000000ULL },    {   25,    20,   9536743164062500000ULL },
	{   26,    23,  11920928955078125000ULL },    {   27,    26,  14901161193847656250ULL },
	{   28,    30,   9313225746154785156ULL },    {   29,    33,  11641532182693481445ULL },
	{   30,    36,  14551915228366851807ULL },    {   31,    39,  18189894035458564758ULL },
	{   32,    43,  11368683772161602974ULL },    {   33,    46,  14210854715202003717ULL },
	{   34,    49,  17763568394002504647ULL },    {   35,    53,  11102230246251565404ULL },
	{   36,    56,  13877787807814456755ULL },    {   37,    59,  17347234759768070944ULL },
	{   38,    63,  10842021724855044340ULL },    {   39,    66,  13552527156068805425ULL },
	{   40,    69,  16940658945086006781ULL },    {   41,    73,  10587911840678754238ULL },
	{   42,    76,  13234889800848442798ULL },    {   43,    79,  16543612251060553497ULL },
	{   44,    83,  10339757656912845936ULL },    {   45,    86,  12924697071141057420ULL },
	{   46,    89,  16155871338926321775ULL },    {   47,    93,  10097419586828951109ULL },
	{   48,    96,  12621774483536188887ULL },    {   49,    99,  15777218104420236108ULL },
	{   50,   103,   9860761315262647568ULL },    {   51,   106,  12325951644078309460ULL },
	{   52,   109,  15407439555097886824ULL },    {   53,   113,   9629649721936179265ULL },
	{   54,   116,  12037062152420224082ULL },    {   55,   119,  15046327690525280102ULL },
	{   56,   123,   9403954806578300064ULL },    {   57,   126,  11754943508222875080ULL },
	{   58,   129,  14693679385278593850ULL },    {   59,   132,  18367099231598242312ULL },
	{   60,   136,  11479437019748901445ULL },    {   61,   139,  14349296274686126806ULL },
	{   62,   142,  17936620343357658508ULL },    {   63,   146,  11210387714598536567ULL },
	{   64,   149,  14012984643248170709ULL },    {   65,   152,  17516230804060213387ULL },
	{   66,   156,  10947644252537633367ULL },    {   67,   159,  13684555315672041708ULL },
	{   68,   162,  17105694144590052135ULL },    {   69,   166,  10691058840368782585ULL },
	{   70,   169,  13363823550460978231ULL },    {   71,   172,  16704779438076222788ULL },
	{   72,   176,  10440487148797639243ULL },    {   73,   179,  13050608935997049053ULL },
	{   74,   182,  16313261169996311317ULL },    {   75,   186,  10195788231247694573ULL },
	{   76,   189,  12744735289059618216ULL },    {   77,   192,  15930919111324522770ULL },
	{   78,   196,   9956824444577826731ULL },    {   79,   199,  12446030555722283414ULL },
	{   80,   202,  15557538194652854268ULL },    {   81,   206,   9723461371658033917ULL },
	{   82,   209,  12154326714572542397ULL },    {   83,   212,  15192908393215677996ULL },
	{   84,   216,   9495567745759798747ULL },    {   85,   219,  11869459682199748434ULL },
	{   86,   222,  14836824602749685543ULL },    {   87,   226,   9273015376718553464ULL },
	{   88,   229,  11591269220898191830ULL },    {   89,   232,  14489086526122739788ULL },
	{   90,   235,  18111358157653424735ULL },    {   91,   239,  11319598848533390459ULL },
	{   92,   242,  14149498560666738074ULL },    {   93,   245,  17686873200833422593ULL },
	{   94,   249,  11054295750520889120ULL },    {   95,   252,  13817869688151111401ULL },
	{   96,   255,  17272337110188889251ULL },    {   97,   259,  10795210693868055782ULL },
	{   98,   262,  13494013367335069727ULL },    {   99,   265,  16867516709168837159ULL },
	{  100,   269,  10542197943230523224ULL },    {  101,   272,  13177747429038154030ULL },
	{  102,   275,  16472184286297692538ULL },    {  103,   279,  10295115178936057836ULL },
	{  104,   282,  12868893973670072295ULL },    {  105,   285,  16086117467087590369ULL },
	{  106,   289,  10053823416929743981ULL },    {  107,   292,  12567279271162179976ULL },
	{  108,   295,  15709099088952724970ULL },    {  109,   299,   9818186930595453106ULL },
	{  110,   302,  12272733663244316383ULL },    {  111,   305,  15340917079055395478ULL },
	{  112,   309,   9588073174409622174ULL },    {  113,   312,  11985091468012027718ULL },
	{  114,   315,  14981364335015034647ULL },    {  115,   319,   9363352709384396654ULL },
	{  116,   322,  11704190886730495818ULL },    {  117,   325,  14630238608413119772ULL },
	{  118,   328,  18287798260516399715ULL },    {  119,   332,  11429873912822749822ULL },
	{  120,   335,  14287342391028437278ULL },    {  121,   338,  17859177988785546597ULL },
	{  122,   342,  11161986242990966623ULL },    {  123,   345,  13952482803738708279ULL },
	{  124,   348,  17440603504673385349ULL },    {  125,   352,  10900377190420865843ULL },
	{  126,   355,  13625471488026082304ULL },    {  127,   358,  17031839360032602880ULL },
	{  128,   362,  10644899600020376800ULL },    {  129,   365,  13306124500025471000ULL },
	{  130,   368,  16632655625031838750ULL },    {  131,   372,  10395409765644899219ULL },
	{  132,   375,  12994262207056124023ULL },    {  133,   378,  16242827758820155029ULL },
	{  134,   382,  10151767349262596893ULL },    {  135,   385,  12689709186578246116ULL },
	{  136,   388,  15862136483222807645ULL },    {  137,   392,   9913835302014254778ULL },
	{  138,   395,  12392294127517818473ULL },    {  139,   398,  15490367659397273091ULL },
	{  140,   402,   9681479787123295682ULL },    {  141,   405,  12101849733904119603ULL },
	{  142,   408,  15127312167380149503ULL },    {  143,   412,   9454570104612593439ULL },
	{  144,   415,  11818212630765741799ULL },    {  145,   418,  14772765788457177249ULL },
	{  146,   422,   9232978617785735781ULL },    {  147,   425,  11541223272232169726ULL },
	{  148,   428,  14426529090290212157ULL },    {  149,   431,  18033161362862765197ULL },
	{  150,   435,  11270725851789228248ULL },    {  151,   438,  14088407314736535310ULL },
	{  152,   441,  17610509143420669137ULL },    {  153,   445,  11006568214637918211ULL },
	{  154,   448,  13758210268297397764ULL },    {  155,   451,  17197762835371747205ULL },
	{  156,   455,  10748601772107342003ULL },    {  157,   458,  13435752215134177504ULL },
	{  158,   461,  16794690268917721879ULL },    {  159,   465,  10496681418073576175ULL },
	{  160,   468,  13120851772591970218ULL },    {  161,   471,  16401064715739962773ULL },
	{  162,   475,  10250665447337476733ULL },    {  163,   478,  12813331809171845916ULL },
	{  164,   481,  16016664761464807395ULL },    {  165,   485,  10010415475915504622ULL },
	{  166,   488,  12513019344894380778ULL },    {  167,   491,  15641274181117975972ULL },
	{  168,   495,   9775796363198734983ULL },    {  169,   498,  12219745453998418728ULL },
	{  170,   501,  15274681817498023410ULL },    {  171,   505,   9546676135936264631ULL },
	{  172,   508,  11933345169920330789ULL },    {  173,   511,  14916681462400413487ULL },
	{  174,   515,   9322925914000258429ULL },    {  175,   518,  11653657392500323036ULL },
	{  176,   521,  14567071740625403795ULL },    {  177,   524,  18208839675781754744ULL },
	{  178,   528,  11380524797363596715ULL },    {  179,   531,  14225655996704495894ULL },
	{  180,   534,  17782069995880619868ULL },    {  181,   538,  11113793747425387417ULL },
	{  182,   541,  13892242184281734272ULL },    {  183,   544,  17365302730352167839ULL },
	{  184,   548,  10853314206470104900ULL },    {  185,   551,  13566642758087631125ULL },
	{  186,   554,  16958303447609538906ULL },    {  187,   558,  10598939654755961816ULL },
	{  188,   561,  13248674568444952270ULL },    {  189,   564,  16560843210556190338ULL },
	{  190,   568,  10350527006597618961ULL },    {  191,   571,  12938158758247023701ULL },
	{  192,   574,  16172698447808779627ULL },    {  193,   578,  10107936529880487267ULL },
	{  194,   581,  12634920662350609083ULL },    {  195,   584,  15793650827938261354ULL },
	{  196,   588,   9871031767461413346ULL },    {  197,   591,  12338789709326766683ULL },
	{  198,   594,  15423487136658458354ULL },    {  199,   598,   9639679460411536471ULL },
	{  200,   601,  12049599325514420589ULL },    {  201,   604,  15061999156893025736ULL },
	{  202,   608,   9413749473058141085ULL },    {  203,   611,  11767186841322676356ULL },
	{  204,   614,  14708983551653345445ULL },    {  205,   617,  18386229439566681806ULL },
	{  206,   621,  11491393399729176129ULL },    {  207,   624,  14364241749661470161ULL },
	{  208,   627,  17955302187076837702ULL },    {  209,   631,  11222063866923023564ULL },
	{  210,   634,  14027579833653779454ULL },    {  211,   637,  17534474792067224318ULL },
	{  212,   641,  10959046745042015199ULL },    {  213,   644,  13698808431302518998ULL },
	{  214,   647,  17123510539128148748ULL },    {  215,   651,  10702194086955092968ULL },
	{  216,   654,  13377742608693866209ULL },    {  217,   657,  16722178260867332762ULL },
	{  218,   661,  10451361413042082976ULL },    {  219,   664,  13064201766302603720ULL },
	{  220,   667,  16330252207878254650ULL },    {  221,   671,  10206407629923909156ULL },
	{  222,   674,  12758009537404886445ULL },    {  223,   677,  15947511921756108057ULL },
	{  224,   681,   9967194951097567536ULL },    {  225,   684,  12458993688871959419ULL },
	{  226,   687,  15573742111089949274ULL },    {  227,   691,   9733588819431218296ULL },
	{  228,   694,  12166986024289022870ULL },    {  229,   697,  15208732530361278588ULL },
	{  230,   701,   9505457831475799118ULL },    {  231,   704,  11881822289344748897ULL },
	{  232,   707,  14852277861680936121ULL },    {  233,   711,   9282673663550585076ULL },
	{  234,   714,  11603342079438231345ULL },    {  235,   717,  14504177599297789181ULL },
	{  236,   720,  18130221999122236476ULL },    {  237,   724,  11331388749451397798ULL },
	{  238,   727,  14164235936814247247ULL },    {  239,   730,  17705294921017809059ULL },
	{  240,   734,  11065809325636130662ULL },    {  241,   737,  13832261657045163327ULL },
	{  242,   740,  17290327071306454159ULL },    {  243,   744,  10806454419566533849ULL },
	{  244,   747,  13508068024458167312ULL },    {  245,   750,  16885085030572709140ULL },
	{  246,   754,  10553178144107943212ULL },    {  247,   757,  13191472680134929015ULL },
	{  248,   760,  16489340850168661269ULL },    {  249,   764,  10305838031355413293ULL },
	{  250,   767,  12882297539194266616ULL },    {  251,   770,  16102871923992833271ULL },
	{  252,   774,  10064294952495520794ULL },    {  253,   777,  12580368690619400993ULL },
	{  254,   780,  15725460863274251241ULL },    {  255,   784,   9828413039546407025ULL },
	{  256,   787,  12285516299433008782ULL },    {  257,   790,  15356895374291260977ULL },
	{  258,   794,   9598059608932038111ULL },    {  259,   797,  11997574511165047639ULL },
	{  260,   800,  14996968138956309548ULL },    {  261,   804,   9373105086847693468ULL },
	{  262,   807,  11716381358559616835ULL },    {  263,   810,  14645476698199521043ULL },
	{  264,   813,  18306845872749401304ULL },    {  265,   817,  11441778670468375815ULL },
	{  266,   820,  14302223338085469769ULL },    {  267,   823,  17877779172606837211ULL },
	{  268,   827,  11173611982879273257ULL },    {  269,   830,  13967014978599091571ULL },
	{  270,   833,  17458768723248864464ULL },    {  271,   837,  10911730452030540290ULL },
	{  272,   840,  13639663065038175362ULL },    {  273,   843,  17049578831297719203ULL },
	{  274,   847,  10655986769561074502ULL },    {  275,   850,  13319983461951343127ULL },
	{  276,   853,  16649979327439178909ULL },    {  277,   857,  10406237079649486818ULL },
	{  278,   860,  13007796349561858523ULL },    {  279,   863,  16259745436952323153ULL },
	{  280,   867,  10162340898095201971ULL },    {  281,   870,  12702926122619002464ULL },
	{  282,   873,  15878657653273753079ULL },    {  283,   877,   9924161033296095675ULL },
	{  284,   880,  12405201291620119593ULL },    {  285,   883,  15506501614525149492ULL },
	{  286,   887,   9691563509078218432ULL },    {  287,   890,  12114454386347773040ULL },
	{  288,   893,  15143067982934716300ULL },    {  289,   897,   9464417489334197688ULL },
	{  290,   900,  11830521861667747110ULL },    {  291,   903,  14788152327084683887ULL },
	{  292,   907,   9242595204427927429ULL },    {  293,   910,  11553244005534909287ULL }
};

static inline void multiply_128(uint64_t u, uint64_t v, uint64_t* w)
{
	// Make use of 64x64->128 unsigned multiplication instruction
	// (if processor has this instruction and compiler allows for using it).
#if (defined(__GNUC__) || defined(__clang__)) && defined(__SIZEOF_INT128__) && (__SIZEOF_INT128__ == 16)
	unsigned __int128  value_w = ((unsigned __int128)u) * v;
	w[0] = ((uint64_t)(value_w));
	w[1] = ((uint64_t)(value_w >> 64));
#elif defined(_MSC_VER) && defined(_M_X64)
	w[0] = _umul128(u, v, w + 1);
#else
	// On other processors and compilers, do column multiplication.
	uint64_t  u1 = u >> 32;   uint64_t  u0 = u & 0xFFFFFFFFULL;
	uint64_t  v1 = v >> 32;   uint64_t  v0 = v & 0xFFFFFFFFULL;

	uint64_t  t = u0 * v0;
	uint64_t  w0 = t & 0xFFFFFFFFULL;
	uint64_t  k = t >> 32;
	t = u1 * v0 + k;

	uint64_t  w1 = t & 0xFFFFFFFFULL;
	uint64_t  w2 = t >> 32;
	t = u0 * v1 + w1;
	k = t >> 32;

	w[0] = (t << 32) + w0;
	w[1] = u1 * v1 + w2 + k;
#endif
}

static int32_t convert_extended_decimal_to_binary_and_round(uint64_t a, int32_t b, uint64_t* c, int32_t* d)
{
	// 1. Check input arguments
	if ((a == 0) || (b < powers_of_ten_[0].decimal_exponent) || (b > powers_of_ten_[JX_COUNTOF(powers_of_ten_) - 1].decimal_exponent)) {
		return  0;
	}

	// 2. Convert (a * 10^b) -> (mantissa * 2^exponent)
	uint64_t mantissa = powers_of_ten_[b - powers_of_ten_[0].decimal_exponent].binary_mantissa;
	int32_t exponent = powers_of_ten_[b - powers_of_ten_[0].decimal_exponent].binary_exponent;
	uint64_t long_mantissa[2];
	multiply_128(a, mantissa, long_mantissa);
	if (long_mantissa[1] != 0) {
		// high half
		uint32_t lz = count_leading_zeros(long_mantissa[1]);
		if (lz == 0) {
			mantissa = long_mantissa[1];
		} else {
			mantissa = (long_mantissa[1] << lz) | (long_mantissa[0] >> (64 - lz));
		}
		exponent += (64 - lz);
	} else if (long_mantissa[0] != 0) {
		// low half
		uint32_t lz = count_leading_zeros(long_mantissa[0]);
		mantissa = (long_mantissa[0] << lz);
		exponent -= lz;
	} else {
		return  0;   // product is unexpectedly zero
	}

	// 3. Round mantissa
	uint64_t remainder = mantissa & 0x07FFULL;  // 0x07FF: 11 least significant bits set, where 11 = 64-53
	if (remainder < 0x0400ULL) {
		mantissa -= remainder;
	} else {
		mantissa += (0x0800ULL - remainder);
		if (mantissa == 0) {
			// handle overflow of the mantissa
			mantissa = (1ULL << 63);
			++exponent;
		}
	}

	// 4. Move binary point 63 bits to the left: adjust exponent
	exponent += 63;

	// 5. Save computation results and exit
	(*c) = mantissa;
	(*d) = exponent;
	return  1;
}

//////////////////////////////////////////////////////////////////////////
// UTF-8 functions
//
// https://www.rfc-editor.org/rfc/rfc3629.html
// 
#define UTF8_INVALID_CODEPOINT 0xFFFFFFFF
#define UTF8_INVALID_CHAR      '?'

static bool utf8IsStartByte(uint8_t ch);
static const char* utf8ToCodepoint(const char* str, uint32_t* cp);
static char* utf8FromCodepoint(uint32_t cp, char* dst, uint32_t* dstSize);
static const uint16_t* utf16ToCodepoint(const uint16_t* str, uint32_t* cp);
static uint16_t* utf16FromCodepoint(uint32_t cp, uint16_t* dst, uint32_t* dstSize);

static uint32_t _jstr_utf8ToUtf16(uint16_t* dstUtf16, uint32_t dstMaxChars, const char* srcUtf8, uint32_t srcLen, uint32_t* numCharsRead)
{
	if (dstMaxChars == 0) {
		return 0;
	}

	uint16_t* dst = dstUtf16;
	const char* src = srcUtf8;

	// Don't care if 'srcLen' is UINT32_MAX because the loop 
	// ends when the null character is encountered.
	const char* srcEnd = src + srcLen;

	// Make sure there's enough room for the null character
	uint32_t remaining = dstMaxChars - 1;
	while (remaining != 0 && src < srcEnd) {
		uint32_t cp = UTF8_INVALID_CODEPOINT;
		src = utf8ToCodepoint(src, &cp);

		if (!cp) {
			break;
		}

		dst = utf16FromCodepoint(cp, dst, &remaining);
		remaining--;
	}

	*dst = 0;

	if (numCharsRead) {
		*numCharsRead = (uint32_t)(src - srcUtf8);
	}

	return (uint32_t)(dst - dstUtf16);
}

static uint32_t _jstr_utf8ToUtf32(uint32_t* dstUtf32, uint32_t dstMaxChars, const char* srcUtf8, uint32_t srcLen, uint32_t* numCharsRead)
{
	if (dstMaxChars == 0) {
		return 0;
	}

	uint32_t* dst = dstUtf32;
	const char* src = srcUtf8;

	// Don't care if 'srcLen' is UINT32_MAX because the loop 
	// ends when the null character is encountered.
	const char* srcEnd = src + srcLen;

	// Make sure there's enough room for the null character
	uint32_t remaining = dstMaxChars - 1;
	while (remaining != 0 && src < srcEnd) {
		uint32_t cp = UTF8_INVALID_CODEPOINT;
		src = utf8ToCodepoint(src, &cp);

		if (!cp) {
			break;
		}

		*dst++ = cp;
		remaining--;
	}

	*dst = 0;

	if (numCharsRead) {
		*numCharsRead = (uint32_t)(src - srcUtf8);
	}

	return (uint32_t)(dst - dstUtf32);
}

static uint32_t _jstr_utf8FromUtf16(char* dstUtf8, uint32_t dstMaxChars, const uint16_t* srcUtf16, uint32_t srcLen)
{
	if (dstMaxChars == 0) {
		return 0;
	}

	char* dst = dstUtf8;

	// Don't care if 'srcLen' is UINT32_MAX because the loop 
	// ends when the null character is encountered.
	const uint16_t* srcEnd = srcUtf16 + srcLen;

	// Make sure there's enough room for the null character
	uint32_t remaining = dstMaxChars - 1;
	while (remaining != 0 && srcUtf16 < srcEnd) {
		uint32_t cp = UTF8_INVALID_CODEPOINT;
		srcUtf16 = utf16ToCodepoint(srcUtf16, &cp);

		if (!cp) {
			break;
		}

		dst = utf8FromCodepoint(cp, dst, &remaining);
	}

	*dst = 0;
	return (uint32_t)(dst - dstUtf8);
}

static uint32_t _jstr_utf8FromUtf32(char* dstUtf8, uint32_t dstMaxChars, const uint32_t* srcUtf32, uint32_t srcLen)
{
	if (dstMaxChars == 0) {
		return 0;
	}

	char* dst = dstUtf8;

	// Don't care if 'srcLen' is UINT32_MAX because the loop 
	// ends when the null character is encountered.
	const uint32_t* srcEnd = srcUtf32 + srcLen;

	// Make sure there's enough room for the null character
	uint32_t remaining = dstMaxChars - 1;
	while (remaining != 0 && srcUtf32 < srcEnd) {
		uint32_t cp = *srcUtf32;
		++srcUtf32;
		if (!cp) {
			break;
		}

		dst = utf8FromCodepoint(cp, dst, &remaining);
	}

	*dst = 0;
	return (uint32_t)(dst - dstUtf8);
}

static uint32_t _jstr_utf8nlen(const char* str, uint32_t max)
{
	uint32_t n = 0;

	const char* strEnd = str + max;
	while (*str && str < strEnd) {
		uint32_t cp = UTF8_INVALID_CODEPOINT;
		str = utf8ToCodepoint(str, &cp);
		if (!cp) {
			break;
		}
		
		++n;
	}

	return n;
}

static uint32_t _jstr_utf8to_codepoint(const char* str, uint32_t len, uint32_t* numBytes)
{
	JX_UNUSED(len);
	uint32_t codepoint = 0;
	const char* nextChar = utf8ToCodepoint(str, &codepoint);
	if (numBytes) {
		*numBytes = (uint32_t)(nextChar - str);
	}
	return codepoint;
}

static const char* _jstr_utf8FindNextChar(const char* str)
{
	const bool isStartByte = utf8IsStartByte((uint8_t)*str);
	if (isStartByte) {
		++str;
	}

	while (*str != '\0' && !utf8IsStartByte(*str)) {
		++str;
	}

	return str;
}

static const char* _jstr_utf8FindPrevChar(const char* str)
{
	const bool isStartByte = utf8IsStartByte((uint8_t)*str);
	if (isStartByte) {
		--str;
	}

	while (!utf8IsStartByte(*str)) {
		--str;
	}

	return str;
}

static inline bool utf8IsStartByte(uint8_t ch)
{
	return (ch & 0x80) == 0x00  // ASCII
		|| (ch & 0xC0) == 0xC0; // UTF-8 first byte
}

static const char* utf8ToCodepoint(const char* str, uint32_t* cp)
{
	*cp = UTF8_INVALID_CODEPOINT;

	const uint32_t octet0 = (uint32_t)str[0];
	if (octet0 == 0) {
		*cp = 0;
		return str;
	} else if ((octet0 & 0x80) == 0) {
		// 1-octet "sequence"
		// 0xxxxxxx
		*cp = (uint32_t)(octet0 & 0x7F);
	} else if ((octet0 & 0xC0) == 0x80) {
		// Invalid start
		// Let it fall through
	} else if ((octet0 & 0xE0) == 0xC0) {
		// 2-octet sequence
		// 110xxxxx 10xxxxxx
		const uint32_t octet1 = (uint32_t)str[1];
		if ((octet1 & 0xC0) == 0x80) {
			const uint32_t val = 0
				| ((octet0 & 0x0F) << 6)
				| ((octet1 & 0x3F) << 0)
				;
			if (val >= 0x00000080 && val <= 0x000007FF) {
				*cp = val;
				return str + 2;
			}
		}
	} else if ((octet0 & 0xF0) == 0xE0) {
		// 3-octet sequence
		// 1110xxxx 10xxxxxx 10xxxxxx
		const uint32_t octet12 = (uint32_t)(*(uint16_t*)&str[1]);
		if ((octet12 & 0xC0C0) == 0x8080) {
			const uint32_t val = 0
				| ((octet0 & 0x0F) << 12)
				| ((octet12 & 0x003F) << 6)
				| ((octet12 & 0x3F00) >> 8)
				;
			if ((val >= 0x00000800 && val < 0x0000D800) || (val > 0x0000DFFF && val <= 0x0000FFFD)) {
				*cp = val;
				return str + 3;
			}
		}
	} else if ((octet0 & 0xF8) == 0xF0) {
		// 4-octet sequence
		// 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
		const uint32_t octet0123 = *(uint32_t*)&str[0];
		if ((octet0123 & 0xC0C0C000) == 0x80808000) {
			const uint32_t val = 0
				| ((octet0123 & 0x00000007) << 18)
				| ((octet0123 & 0x00003F00) << 4)
				| ((octet0123 & 0x003F0000) >> 10)
				| ((octet0123 & 0x3F000000) >> 24)
				;
			if (val >= 0x00010000 && val <= 0x0010FFFF) {
				*cp = val;
				return str + 4;
			}
		}
	}

	return str + 1;
}

static char* utf8FromCodepoint(uint32_t cp, char* dst, uint32_t* dstSize)
{
	if (*dstSize == 0) {
		return dst;
	}

	if (cp < 0x80) {
		// 1-octet sequence
		// 0xxxxxxx
		dst[0] = (char)(cp & 0x7F);
		return dst + 1;
	} else if (cp < 0x800) {
		// 2-octet sequence
		// 110xxxxx 10xxxxxx
		if (*dstSize >= 2) {
			dst[0] = (char)((uint8_t)(0xC0 | ((cp >> 6) & 0x1F)));
			dst[1] = (char)((uint8_t)(0x80 | ((cp >> 0) & 0x3F)));
			*dstSize -= 2;
			return dst + 2;
		}

		// Is there's not enough room to decode this character, inform the caller
		// that the whole buffer has been filled in order to avoid calling back
		// with the same buffer.
		*dstSize = 0;
		return dst;
	} else if (cp < 0x00010000) {
		// 3-octet sequence
		// 1110xxxx 10xxxxxx 10xxxxxx
		if (*dstSize >= 3) {
			dst[0] = (char)((uint8_t)(0xE0 | ((cp >> 12) & 0x0F)));
			dst[1] = (char)((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
			dst[2] = (char)((uint8_t)(0x80 | ((cp >> 0) & 0x3F)));
			*dstSize -= 3;
			return dst + 3;
		}

		*dstSize = 0;
		return dst;
	} else if (cp < 0x00110000) {
		// 4-octet sequence
		// 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
		if (*dstSize >= 4) {
			dst[0] = (char)((uint8_t)(0xF0 | ((cp >> 18) & 0x07)));
			dst[1] = (char)((uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
			dst[2] = (char)((uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
			dst[3] = (char)((uint8_t)(0x80 | ((cp >> 0) & 0x3F)));
			*dstSize -= 4;
			return dst + 4;
		}

		*dstSize = 0;
		return dst;
	}

	dst[0] = UTF8_INVALID_CHAR;
	*dstSize -= 1;
	return dst + 1;
}

static const uint16_t* utf16ToCodepoint(const uint16_t* str, uint32_t* cp)
{
	*cp = UTF8_INVALID_CODEPOINT;

	uint32_t word = (uint32_t)str[0];

	if (word == 0) {
		// null terminator, end of string.
		*cp = 0;
		return str;
	} else if (word >= 0xD800 && word <= 0xDBFF) {
		// Start surrogate pair
		const uint32_t pair = (uint32_t)str[1];
		if (pair >= 0xDC00 && pair <= 0xDFFF) {
			*cp = 0x10000
				| ((word & 0x03FF) << 10)
				| ((pair & 0x03FF) << 0)
				;

			return str + 2;
		}
	} else if (word < 0xDC00 || word > 0xDFFF) {
		*cp = word;
	} else {
		// 'word' is the second half of a surrogate pair? 
		// Let it fall through.
	}

	return str + 1;
}

static uint16_t* utf16FromCodepoint(uint32_t cp, uint16_t* dst, uint32_t* dstSize)
{
	if (*dstSize == 0) {
		return dst;
	}

	if (cp < 0x00010000) {
		dst[0] = (uint16_t)cp;
		*dstSize -= 1;
		return dst + 1;
	} else if (cp <= 0x0010FFFF) {
		if (*dstSize >= 2) {
			cp -= 0x10000;
			dst[0] = (uint16_t)(0xD800 | ((cp >> 10) & 0x3FF));
			dst[1] = (uint16_t)(0xDC00 | ((cp >> 0) & 0x3FF));
			*dstSize -= 2;
			return dst + 2;
		}

		*dstSize = 0;
		return dst;
	}

	dst[0] = (uint16_t)UTF8_INVALID_CHAR;
	*dstSize -= 1;
	return dst + 1;
}

//////////////////////////////////////////////////////////////////////////
// Base64
//
// https://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c
/*
 * Base64 encoding/decoding (RFC1341)
 * Copyright (c) 2005-2011, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */
static const uint8_t kBase64Table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define _ 0x80
static const uint8_t kBase64CharOffset[256] = {
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _, 62,  _,  _,  _, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  _,  _,  _,  0,  _,  _,
	 _,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  _,  _,  _,  _,  _,
	 _, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
	 _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
};
#undef _

static uint8_t* _jstr_base64Decode(const char* str, uint32_t len, uint32_t* sz, jx_allocator_i* allocator)
{
	size_t count = 0;
	for (size_t i = 0; i < len; i++) {
		if (kBase64CharOffset[str[i]] != 0x80) {
			count++;
		}
	}

	if (count == 0 || count % 4) {
		return NULL;
	}

	size_t olen = count / 4 * 3;
	uint8_t* out = (uint8_t*)JX_ALLOC(allocator, olen);
	if (!out) {
		return NULL;
	}
	uint8_t* pos = out;

	uint8_t block[4];
	int pad = 0;
	count = 0;
	for (size_t i = 0; i < len; i++) {
		uint8_t tmp = kBase64CharOffset[str[i]];
		if (tmp == 0x80) {
			continue;
		}

		if (str[i] == '=') {
			pad++;
		}

		block[count] = tmp;
		count++;
		if (count == 4) {
			*pos++ = (block[0] << 2) | (block[1] >> 4);
			*pos++ = (block[1] << 4) | (block[2] >> 2);
			*pos++ = (block[2] << 6) | block[3];

			count = 0;

			if (pad) {
				if (pad == 1) {
					pos--;
				} else if (pad == 2) {
					pos -= 2;
				} else {
					/* Invalid padding */
					JX_FREE(allocator, out);
					return NULL;
				}

				break;
			}
		}
	}

	*sz = (uint32_t)(pos - out);

	return out;
}

static char* _jstr_base64Encode(const uint8_t* data, uint32_t sz, uint32_t* len, jx_allocator_i* allocator)
{
	size_t olen = sz * 4 / 3 + 4; /* 3-byte blocks to 4-byte */
	olen += olen / 72; /* line feeds */
	olen++; /* nul termination */
	if (olen < sz) {
		return NULL; /* integer overflow */
	}

	uint8_t* out = (uint8_t*)JX_ALLOC(allocator, olen);
	if (!out) {
		return NULL;
	}

	const uint8_t* end = data + sz;
	const uint8_t* in = data;
	uint8_t* pos = out;
	int line_len = 0;
	while (end - in >= 3) {
		*pos++ = kBase64Table[in[0] >> 2];
		*pos++ = kBase64Table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
		*pos++ = kBase64Table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
		*pos++ = kBase64Table[in[2] & 0x3f];

		in += 3;

		line_len += 4;
		if (line_len >= 72) {
			*pos++ = '\n';
			line_len = 0;
		}
	}

	if (end - in) {
		*pos++ = kBase64Table[in[0] >> 2];
		if (end - in == 1) {
			*pos++ = kBase64Table[(in[0] & 0x03) << 4];
			*pos++ = '=';
		} else {
			*pos++ = kBase64Table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
			*pos++ = kBase64Table[(in[1] & 0x0f) << 2];
		}
		*pos++ = '=';
		line_len += 4;
	}

	if (line_len) {
		*pos++ = '\n';
	}

	*pos = '\0';
	if (len) {
		*len = (uint32_t)(pos - out);
	}

	return (char*)out;
}
