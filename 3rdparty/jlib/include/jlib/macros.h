#ifndef JX_MACROS_H
#define JX_MACROS_H

#ifndef JX_CONFIG_DEBUG
#define JX_CONFIG_DEBUG _DEBUG
#endif

#ifndef JX_CONFIG_TRACE_ALLOCATIONS
#define JX_CONFIG_TRACE_ALLOCATIONS JX_CONFIG_DEBUG
#endif

#ifndef JX_CONFIG_MEMTRACER_MAX_STACK_FRAMES
#define JX_CONFIG_MEMTRACER_MAX_STACK_FRAMES 8
#endif

#ifndef JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT
#define JX_CONFIG_ALLOCATOR_NATURAL_ALIGNMENT 8
#endif

#ifndef JX_CONFIG_FRAME_ALLOCATOR_CHUNK_SIZE
#define JX_CONFIG_FRAME_ALLOCATOR_CHUNK_SIZE (4u << 20)
#endif

#define JX_COMPILER_MSVC              1 // TODO: Determine compiler
#define JX_PLATFORM_WINDOWS           1 // TODO: Determine platform
#define JX_CONFIG_SUPPORTS_THREADING  1
#define JX_ARCH_64BIT                 1

#define JX_MACRO_BLOCK_BEGIN do {
#define JX_MACRO_BLOCK_END   } while (0)
#define JX_NOOP(...)         JX_MACRO_BLOCK_BEGIN JX_MACRO_BLOCK_END

#define JX_CONCAT_IMPL(a, b) a ## b
#define JX_CONCAT(a, b)      JX_CONCAT_IMPL(a, b)

#define JX_MACRO_VAR(name)   JX_CONCAT(name, __LINE__)

#define JX_PAD(n) uint8_t JX_MACRO_VAR(_padding_)[n]
#define JX_PAD_BITFIELD(type, size) type JX_MACRO_VAR(_padding_) : size

#define JX_COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

#define JX_STATIC_ASSERT(condition, ...) static_assert(condition, "" __VA_ARGS__)

// https://stackoverflow.com/a/23238813
#define JX_UNUSED_1(a1)                              (void)(a1)
#define JX_UNUSED_2(a1,a2)                           JX_UNUSED_1(a1),JX_UNUSED_1(a2)
#define JX_UNUSED_3(a1,a2,a3)                        JX_UNUSED_2(a1,a2),JX_UNUSED_1(a3)
#define JX_UNUSED_4(a1,a2,a3,a4)                     JX_UNUSED_3(a1,a2,a3),JX_UNUSED_1(a4)
#define JX_UNUSED_5(a1,a2,a3,a4,a5)                  JX_UNUSED_4(a1,a2,a3,a4),JX_UNUSED_1(a5)
#define JX_UNUSED_6(a1,a2,a3,a4,a5,a6)               JX_UNUSED_5(a1,a2,a3,a4,a5),JX_UNUSED_1(a6)
#define JX_UNUSED_7(a1,a2,a3,a4,a5,a6,a7)            JX_UNUSED_6(a1,a2,a3,a4,a5,a6),JX_UNUSED_1(a7)
#define JX_UNUSED_8(a1,a2,a3,a4,a5,a6,a7,a8)         JX_UNUSED_7(a1,a2,a3,a4,a5,a6,a7),JX_UNUSED_1(a8)
#define JX_UNUSED_9(a1,a2,a3,a4,a5,a6,a7,a8,a9)      JX_UNUSED_8(a1,a2,a3,a4,a5,a6,a7,a8),JX_UNUSED_1(a9)
#define JX_UNUSED_10(a1,a2,a3,a4,a5,a6,a7,a8,a9,a10) JX_UNUSED_9(a1,a2,a3,a4,a5,a6,a7,a8,a9),JX_UNUSED_1(a10)

#define JX_VA_NUM_ARGS_IMPL(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,N,...) N
#define JX_VA_NUM_ARGS(...) JX_VA_NUM_ARGS_IMPL(__VA_ARGS__, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1)

#define JX_UNUSED_IMPL_(nargs) JX_UNUSED_ ## nargs
#define JX_UNUSED_IMPL(nargs)  JX_UNUSED_IMPL_(nargs)
#define JX_UNUSED(...)         JX_UNUSED_IMPL( JX_VA_NUM_ARGS(__VA_ARGS__) ) (__VA_ARGS__ )

#define JX_STRINGIZE(_x)  JX_STRINGIZE_(_x)
#define JX_STRINGIZE_(_x) #_x

#define JX_FILE __FILE__
#define JX_LINE __LINE__
#define JX_FILE_LINE_LITERAL "" __FILE__ "(" JX_STRINGIZE(__LINE__) "): "

#define JX_MAKEFOURCC(_a, _b, _c, _d) ( ( (uint32_t)(_a) | ( (uint32_t)(_b) << 8) | ( (uint32_t)(_c) << 16) | ( (uint32_t)(_d) << 24) ) )

#if JX_COMPILER_CLANG
#	define JX_PRAGMA_DIAGNOSTIC_PUSH                   _Pragma("clang diagnostic push")
#	define JX_PRAGMA_DIAGNOSTIC_POP                    _Pragma("clang diagnostic pop")
#	define JX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC      _Pragma(JX_STRINGIZE(clang diagnostic ignored _x))
#	define JX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(_x)
#elif JX_COMPILER_GCC
#	define JX_PRAGMA_DIAGNOSTIC_PUSH                   _Pragma("GCC diagnostic push")
#	define JX_PRAGMA_DIAGNOSTIC_POP                    _Pragma("GCC diagnostic pop")
#	define JX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC      _Pragma(JX_STRINGIZE(GCC diagnostic ignored _x))
#	define JX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(_x)
#elif JX_COMPILER_MSVC
#	define JX_PRAGMA_DIAGNOSTIC_PUSH                   __pragma(warning(push))
#	define JX_PRAGMA_DIAGNOSTIC_POP                    __pragma(warning(pop))
#	define JX_PRAGMA_DIAGNOSTIC_IGNORED_MSVC(_x)       __pragma(warning(disable:_x) )
#	define JX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC(_x)
#endif

#if JX_COMPILER_GCC || JX_COMPILER_CLANG
#	define JX_ALIGN_DECL(_align, _decl) _decl __attribute__( (aligned(_align) ) )
#	define JX_ALLOW_UNUSED __attribute__( (unused) )
#	define JX_FORCE_INLINE inline __attribute__( (__always_inline__) )
#	define JX_FUNCTION __PRETTY_FUNCTION__
#	define JX_LIKELY(_x)   __builtin_expect(!!(_x), 1)
#	define JX_UNLIKELY(_x) __builtin_expect(!!(_x), 0)
#	define JX_NO_INLINE   __attribute__( (noinline) )
#	define JX_NO_RETURN   __attribute__( (noreturn) )
#	define JX_CONST_FUNC  __attribute__( (const) )

#	if JX_COMPILER_GCC >= 70000
#		define JX_FALLTHROUGH __attribute__( (fallthrough) )
#	else
#		define JX_FALLTHROUGH JX_NOOP()
#	endif // JX_COMPILER_GCC >= 70000

#	define JX_NO_VTABLE
#	define JX_PRINTF_ARGS(_format, _args) __attribute__( (format(__printf__, _format, _args) ) )

#	if JX_CLANG_HAS_FEATURE(cxx_thread_local) \
	|| (!JX_PLATFORM_OSX && (JX_COMPILER_GCC >= 40200) ) \
	|| (JX_COMPILER_GCC >= 40500)
#		define JX_THREAD_LOCAL __thread
#	endif // JX_COMPILER_GCC

#	define JX_ATTRIBUTE(_x) __attribute__( (_x) )

#	if JX_CRT_MSVC
#		define __stdcall
#	endif // JX_CRT_MSVC
#elif JX_COMPILER_MSVC
#	define JX_ALIGN_DECL(_align, _decl) __declspec(align(_align) ) _decl
#   define JX_ALIGNAS(_align) __declspec(align(_align))
#	define JX_ALLOW_UNUSED
#	define JX_FORCE_INLINE __forceinline
#	define JX_FUNCTION __FUNCTION__
#	define JX_LIKELY(_x)   (_x)
#	define JX_UNLIKELY(_x) (_x)
#	define JX_NO_INLINE __declspec(noinline)
#	define JX_NO_RETURN
#	define JX_CONST_FUNC  __declspec(noalias)
#	define JX_FALLTHROUGH JX_NOOP()
#	define JX_NO_VTABLE __declspec(novtable)
#	define JX_PRINTF_ARGS(_format, _args)
#	define JX_THREAD_LOCAL __declspec(thread)
#	define JX_ATTRIBUTE(_x)
#   define JX_NO_BUFFER_OVERRUN __declspec(safebuffers)
#   define JX_ALIGNOF(_type) _Alignof(_type)
#else
#	error "Unknown JX_COMPILER_?"
#endif

#if JX_CONFIG_DEBUG
#define JX_TRACE(_format, ...) \
	JX_MACRO_BLOCK_BEGIN \
		jx_dbg_printf(JX_FILE_LINE_LITERAL "jlib " _format "\n", ##__VA_ARGS__); \
	JX_MACRO_BLOCK_END

#define JX_WARN(_condition, _format, ...) \
	JX_MACRO_BLOCK_BEGIN \
		if (!(_condition) ) { \
			jx_dbg_printf(JX_FILE_LINE_LITERAL "jlib " _format "\n", ##__VA_ARGS__); \
		} \
	JX_MACRO_BLOCK_END

#define JX_CHECK(_condition, _format, ...) \
	JX_MACRO_BLOCK_BEGIN \
		if (!(_condition) ) { \
			jx_dbg_printf(JX_FILE_LINE_LITERAL "jlib " _format "\n", ##__VA_ARGS__); \
			jx_dbg_brk(); \
		} \
	JX_MACRO_BLOCK_END
#else
#define JX_TRACE(_format, ...)
#define JX_WARN(_condition, _format, ...)
#define JX_CHECK(_condition, _format, ...)
#endif

#define JX_NOT_IMPLEMENTED() JX_CHECK(false, "Not implemented yet", 0);
#define JX_ASSERT(x) JX_CHECK((x), "Assertion failed.", 0);

// A struct should never have more than one JX_INHERITS() and it should always be
// placed at the top of the struct.
#define JX_INHERITS(type) struct type super

#endif // JX_MACROS_H

