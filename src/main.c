#include "jcc.h"
#include "jit.h"
#include "jit_gen.h"
#include "jir.h"
#include "jir_gen.h"
#include "jmir.h"
#include "jmir_gen.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/config.h>
#include <jlib/dbg.h>
#include <jlib/error.h>
#include <jlib/logger.h>
#include <jlib/hashmap.h>
#include <jlib/kernel.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/string.h>
#include <tracy/tracy/TracyC.h>

static void runCTestSuiteTests(jx_allocator_i* allocator);
static void runSingleFileCompile(jx_allocator_i* allocator);
static void* getExternalSymbolCallback(const char* symName, void* userData);
static bool redirectSystemLogger(void);

int main(int argc, char** argv)
{
	jx_kernel_initAPI();

	if (!redirectSystemLogger()) {
		return -1;
	}

	jx_allocator_i* allocator = allocator_api->createAllocator("jcc");

#if 0
	runCTestSuiteTests(allocator);
#elif 1
	runSingleFileCompile(allocator);
#endif

	allocator_api->destroyAllocator(allocator);

	jx_kernel_shutdownAPI();

	return 0;
}

static void runCTestSuiteTests(jx_allocator_i* allocator)
{
	uint32_t totalTests = 0;
	uint32_t numSkipped = 0;
	uint32_t numPass = 0;
	uint32_t numFailed = 0;
	for (uint32_t iTest = 1; iTest <= 220; ++iTest) {
		++totalTests;

		char sourceFile[256];
		jx_snprintf(sourceFile, JX_COUNTOF(sourceFile), "test/c-testsuite/%05d.c", iTest);

		JX_SYS_LOG_INFO(NULL, "%s: ", sourceFile);

		const bool skipTest = false
			|| iTest == 121 // Parsing error; complex variable/function declaration
			|| iTest == 152 // #line+#error
			|| iTest == 162 // const/static/volatile/restrict in array declaration
			|| iTest == 170 // forward enum
			|| iTest == 189 // fprintf/stdout
			|| iTest == 206 // #pragma
			|| iTest == 207 // VLA
			|| iTest == 210 // __attribute__
			|| iTest == 213 // Statement expressions
			|| iTest == 214 // __builtin_expect
			|| iTest == 216 // BUG? Parser: missing braces from inner union initializer
			|| iTest == 219 // BUG? Parser: _Generic
			|| iTest == 220 // Unicode
			;
		if (skipTest) {
			++numSkipped;
			JX_SYS_LOG_WARNING(NULL, "SKIPPED\n");
			continue;
		}

		jx_cc_context_t* ctx = jx_cc_createContext(allocator, logger_api->m_SystemLogger);
		jx_cc_addIncludePath(ctx, JX_FILE_BASE_DIR_INSTALL, "include");

		jx_cc_translation_unit_t* tu = jx_cc_compileFile(ctx, JX_FILE_BASE_DIR_INSTALL, sourceFile);
		if (tu && tu->m_NumErrors == 0) {
			jx_ir_context_t* irCtx = jx_ir_createContext(allocator);
			jx_irgen_context_t* genCtx = jx_irgen_createContext(irCtx, allocator);

			jx_irgen_moduleGen(genCtx, sourceFile, tu);

			jx_irgen_destroyContext(genCtx);

			jx_mir_context_t* mirCtx = jx_mir_createContext(allocator);
			jx_mirgen_context_t* mirGenCtx = jx_mirgen_createContext(irCtx, mirCtx, allocator);

			jx_ir_module_t* irMod = jx_ir_getModule(irCtx, 0);
			if (irMod) {
				jx_mirgen_moduleGen(mirGenCtx, irMod);
			}

			jx_mirgen_destroyContext(mirGenCtx);

			jx_x64_context_t* jitCtx = jx_x64_createContext(allocator);
			jx_x64gen_context_t* jitgenCtx = jx_x64gen_createContext(jitCtx, mirCtx, getExternalSymbolCallback, NULL, allocator);

			if (jx_x64gen_codeGen(jitgenCtx)) {
				uint32_t execBufSize = 0;
				const uint8_t* execBuf = jx64_getBuffer(jitCtx, &execBufSize);

				typedef int32_t(*pfnMain)(void);
				jx_x64_symbol_t* symMain = jx64_symbolGetByName(jitCtx, "main");
				if (symMain) {
					pfnMain mainFunc = (pfnMain)((uint8_t*)execBuf + jx64_labelGetOffset(jitCtx, symMain->m_Label));
					int32_t ret = mainFunc();
					if (ret == 0) {
						++numPass;
						JX_SYS_LOG_DEBUG(NULL, "PASS\n", sourceFile);
					} else {
						++numFailed;
						JX_SYS_LOG_ERROR(NULL, "FAIL\n", sourceFile);
					}
				} else {
					JX_SYS_LOG_ERROR(NULL, "main() not found!\n", sourceFile);
				}
			} else {
				++numFailed;
				JX_SYS_LOG_ERROR(NULL, "Codegen failed. Unresolved external symbol?\n");
			}

			jx_x64gen_destroyContext(jitgenCtx);
			jx_x64_destroyContext(jitCtx);
			jx_mir_destroyContext(mirCtx);
			jx_ir_destroyContext(irCtx);
		} else {
			++numFailed;
			JX_SYS_LOG_ERROR(NULL, "Compilation failed.\n", sourceFile);
		}
		jx_cc_destroyContext(ctx);
	}

	JX_SYS_LOG_INFO(NULL, "Total: %u\n", totalTests);
	JX_SYS_LOG_INFO(NULL, "Pass : %u\n", numPass);
	JX_SYS_LOG_INFO(NULL, "Fail : %u\n", numFailed);
	JX_SYS_LOG_INFO(NULL, "Skip : %u\n", numSkipped);
}

static void runSingleFileCompile(jx_allocator_i* allocator)
{
	jx_cc_context_t* ctx = jx_cc_createContext(allocator, logger_api->m_SystemLogger);
	jx_cc_addIncludePath(ctx, JX_FILE_BASE_DIR_INSTALL, "include");
	jx_cc_addIncludePath(ctx, JX_FILE_BASE_DIR_INSTALL, "include/winapi");

//	const char* sourceFile = "test/c-testsuite/00049.c";
//	const char* sourceFile = "test/stb_image_write_test.c";
//	const char* sourceFile = "test/stb_sprintf_test.c";
//	const char* sourceFile = "test/stb_truetype_test.c";
//	const char* sourceFile = "test/sieve.c";
//	const char* sourceFile = "test/compute.c";
//	const char* sourceFile = "test/win32_test.c";
	const char* sourceFile = "test/extern_global.c";

	JX_SYS_LOG_INFO(NULL, "%s\n", sourceFile);
	TracyCZoneN(frontend, "Frontend", 1);
	jx_cc_translation_unit_t* tu = jx_cc_compileFile(ctx, JX_FILE_BASE_DIR_INSTALL, sourceFile);
	if (!tu || tu->m_NumErrors != 0) {
		JX_SYS_LOG_INFO(NULL, "Failed to compile \"%s\"\n", sourceFile);
		goto end;
	}
	TracyCZoneEnd(frontend);

	JX_SYS_LOG_INFO(NULL, "Building IR...\n");
	{
		jx_ir_context_t* irCtx = jx_ir_createContext(allocator);
		jx_irgen_context_t* genCtx = jx_irgen_createContext(irCtx, allocator);

		TracyCZoneN(irgen, "IR Gen", 1);
		if (!jx_irgen_moduleGen(genCtx, sourceFile, tu)) {
			TracyCZoneEnd(irgen);
			JX_SYS_LOG_ERROR(NULL, "Failed to generate module IR\n");
		} else {
			jx_irgen_destroyContext(genCtx);
			TracyCZoneEnd(irgen);

			jx_string_buffer_t* sb = jx_strbuf_create(allocator);
			jx_ir_print(irCtx, sb);
			jx_strbuf_nullTerminate(sb);
			JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
			jx_strbuf_destroy(sb);

			{
				jx_mir_context_t* mirCtx = jx_mir_createContext(allocator);
				jx_mirgen_context_t* mirGenCtx = jx_mirgen_createContext(irCtx, mirCtx, allocator);

				TracyCZoneN(mirgen, "MIR Gen", 1);
				jx_ir_module_t* irMod = jx_ir_getModule(irCtx, 0);
				if (irMod) {
					jx_mirgen_moduleGen(mirGenCtx, irMod);
				}

				jx_mirgen_destroyContext(mirGenCtx);
				TracyCZoneEnd(mirgen);

				sb = jx_strbuf_create(allocator);
				jx_mir_print(mirCtx, sb);
				jx_strbuf_nullTerminate(sb);
				JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
				jx_strbuf_destroy(sb);

				TracyCZoneN(x64gen, "x64 Gen", 1);
				jx_x64_context_t* jitCtx = jx_x64_createContext(allocator);
				jx_x64gen_context_t* jitgenCtx = jx_x64gen_createContext(jitCtx, mirCtx, getExternalSymbolCallback, NULL, allocator);
				if (jx_x64gen_codeGen(jitgenCtx)) {
					TracyCZoneEnd(x64gen);
					uint32_t bufferSize = 0;
					const uint8_t* buffer = jx64_getBuffer(jitCtx, &bufferSize);

					jx_os_file_t* binFile = jx_os_fileOpenWrite(JX_FILE_BASE_DIR_USERDATA, "output.bin");
					jx_os_fileWrite(binFile, buffer, bufferSize);
					jx_os_fileClose(binFile);

					typedef int32_t(*pfnMain)(void);
					jx_x64_symbol_t* symMain = jx64_symbolGetByName(jitCtx, "main");
					if (symMain) {
						uint32_t mainOffset = jx64_labelGetOffset(jitCtx, symMain->m_Label);
						JX_SYS_LOG_INFO(NULL, "main offset %u\n", mainOffset);
						pfnMain mainFunc = (pfnMain)((uint8_t*)buffer + mainOffset);
						TracyCZoneN(execute, "main()", 1);
						int32_t ret = mainFunc();
						TracyCZoneEnd(execute);
						JX_SYS_LOG_DEBUG(NULL, "main() returned %d\n", ret);
					} else {
						JX_SYS_LOG_ERROR(NULL, "main() not found!\n");
					}
				} else {
					JX_SYS_LOG_ERROR(NULL, "Codegen failed. Unresolved external symbol?\n");
				}

				jx_x64gen_destroyContext(jitgenCtx);
				jx_x64_destroyContext(jitCtx);
				jx_mir_destroyContext(mirCtx);
			}
		}
		jx_ir_destroyContext(irCtx);
	}

end:
	jx_cc_destroyContext(ctx);
}

#include <stdlib.h> // calloc
#include <stdio.h>  // printf
#include <math.h>   // cosf/sinf
#include <memory.h> // memset/memcpy
#include <string.h> // strcpy
#include <Windows.h>

static void* getExternalSymbolCallback(const char* symName, void* userData)
{
	if (!jx_strcmp(symName, "abs")) { return (void*)abs; }
	if (!jx_strcmp(symName, "abort")) { return (void*)abort; }
	if (!jx_strcmp(symName, "acos")) { return (void*)acos; }
	if (!jx_strcmp(symName, "atoi")) { return (void*)atoi; }
	if (!jx_strcmp(symName, "calloc")) { return (void*)calloc; }
	if (!jx_strcmp(symName, "ceil")) { return (void*)ceil; }
	if (!jx_strcmp(symName, "cos")) { return (void*)cos; }
	if (!jx_strcmp(symName, "cosf")) { return (void*)cosf; }
	if (!jx_strcmp(symName, "fabs")) { return (void*)fabs; }
	if (!jx_strcmp(symName, "fclose")) { return (void*)fclose; }
	if (!jx_strcmp(symName, "fgetc")) { return (void*)fgetc; }
	if (!jx_strcmp(symName, "fgets")) { return (void*)fgets; }
	if (!jx_strcmp(symName, "floor")) { return (void*)floor; }
	if (!jx_strcmp(symName, "fmod")) { return (void*)fmod; }
	if (!jx_strcmp(symName, "fopen")) { return (void*)fopen; }
	if (!jx_strcmp(symName, "fprintf")) { return (void*)fprintf; }
	if (!jx_strcmp(symName, "fread")) { return (void*)fread; }
	if (!jx_strcmp(symName, "free")) { return (void*)free; }
	if (!jx_strcmp(symName, "frexp")) { return (void*)frexp; }
	if (!jx_strcmp(symName, "fseek")) { return (void*)fseek; }
	if (!jx_strcmp(symName, "ftell")) { return (void*)ftell; }
	if (!jx_strcmp(symName, "fwrite")) { return (void*)fwrite; }
	if (!jx_strcmp(symName, "getc")) { return (void*)getc; }
	if (!jx_strcmp(symName, "malloc")) { return (void*)malloc; }
	if (!jx_strcmp(symName, "memcmp")) { return (void*)memcmp; }
	if (!jx_strcmp(symName, "memcpy")) { return (void*)memcpy; }
	if (!jx_strcmp(symName, "memmove")) { return (void*)memmove; }
	if (!jx_strcmp(symName, "memset")) { return (void*)memset; }
	if (!jx_strcmp(symName, "pow")) { return (void*)pow; }
	if (!jx_strcmp(symName, "printf")) { return (void*)printf; }
	if (!jx_strcmp(symName, "putchar")) { return (void*)putchar; }
	if (!jx_strcmp(symName, "realloc")) { return (void*)realloc; }
	if (!jx_strcmp(symName, "roundf")) { return (void*)roundf; }
	if (!jx_strcmp(symName, "sin")) { return (void*)sin; }
	if (!jx_strcmp(symName, "sinf")) { return (void*)sinf; }
	if (!jx_strcmp(symName, "sprintf")) { return (void*)sprintf; }
	if (!jx_strcmp(symName, "sqrt")) { return (void*)sqrt; }
	if (!jx_strcmp(symName, "strcat")) { return (void*)strcat; }
	if (!jx_strcmp(symName, "strchr")) { return (void*)strchr; }
	if (!jx_strcmp(symName, "strcmp")) { return (void*)strcmp; }
	if (!jx_strcmp(symName, "strcpy")) { return (void*)strcpy; }
	if (!jx_strcmp(symName, "strlen")) { return (void*)strlen; }
	if (!jx_strcmp(symName, "strncmp")) { return (void*)strncmp; }
	if (!jx_strcmp(symName, "strncpy")) { return (void*)strncpy; }
	if (!jx_strcmp(symName, "strrchr")) { return (void*)strrchr; }
	if (!jx_strcmp(symName, "GetStockObject")) { return (void*)GetStockObject; }
	if (!jx_strcmp(symName, "LoadIconA")) { return (void*)LoadIconA; }
	if (!jx_strcmp(symName, "LoadCursorA")) { return (void*)LoadCursorA; }
	if (!jx_strcmp(symName, "RegisterClassA")) { return (void*)RegisterClassA; }
	if (!jx_strcmp(symName, "CreateWindowExA")) { return (void*)CreateWindowExA; }
	if (!jx_strcmp(symName, "GetMessageA")) { return (void*)GetMessageA; }
	if (!jx_strcmp(symName, "TranslateMessage")) { return (void*)TranslateMessage; }
	if (!jx_strcmp(symName, "DispatchMessageA")) { return (void*)DispatchMessageA; }
	if (!jx_strcmp(symName, "DefWindowProcA")) { return (void*)DefWindowProcA; }
	if (!jx_strcmp(symName, "PostQuitMessage")) { return (void*)PostQuitMessage; }
	if (!jx_strcmp(symName, "DestroyWindow")) { return (void*)DestroyWindow; }
	if (!jx_strcmp(symName, "EndPaint")) { return (void*)EndPaint; }
	if (!jx_strcmp(symName, "DrawTextA")) { return (void*)DrawTextA; }
	if (!jx_strcmp(symName, "SetBkMode")) { return (void*)SetBkMode; }
	if (!jx_strcmp(symName, "SetTextColor")) { return (void*)SetTextColor; }
	if (!jx_strcmp(symName, "GetClientRect")) { return (void*)GetClientRect; }
	if (!jx_strcmp(symName, "BeginPaint")) { return (void*)BeginPaint; }
	if (!jx_strcmp(symName, "SetWindowPos")) { return (void*)SetWindowPos; }
	if (!jx_strcmp(symName, "GetWindowRect")) { return (void*)GetWindowRect; }
	if (!jx_strcmp(symName, "GetClientRect")) { return (void*)GetClientRect; }
	if (!jx_strcmp(symName, "GetDesktopWindow")) { return (void*)GetDesktopWindow; }
	if (!jx_strcmp(symName, "GetParent")) { return (void*)GetParent; }

	static int32_t g_ExternalVar = 1000;
	if (!jx_strcmp(symName, "g_ExternalVar")) { return (void*)&g_ExternalVar; }

	static int32_t g_ExternalArr[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	if (!jx_strcmp(symName, "g_ExternalArr")) { return (void*)g_ExternalArr; }

	return NULL;
}

static bool redirectSystemLogger(void)
{
	// Change application directories.
	const bool redir = true
		&& os_api->fsSetBaseDir(JX_FILE_BASE_DIR_USERDATA, JX_FILE_BASE_DIR_USERDATA, "jitcc") == JX_ERROR_NONE
		&& os_api->fsSetBaseDir(JX_FILE_BASE_DIR_USERAPPDATA, JX_FILE_BASE_DIR_USERAPPDATA, "jitcc") == JX_ERROR_NONE
		&& os_api->fsSetBaseDir(JX_FILE_BASE_DIR_TEMP, JX_FILE_BASE_DIR_TEMP, "jitcc") == JX_ERROR_NONE
		;
	if (!redir) {
		return false;
	}

	jx_logger_i* sysLogger = logger_api->createCompositeLogger(allocator_api->m_SystemAllocator, 0);
	if (sysLogger) {
		jx_logger_i* fileLogger = logger_api->createFileLogger(allocator_api->m_SystemAllocator, JX_FILE_BASE_DIR_USERDATA, "jitcc.log", JX_LOGGER_FLAGS_MULTITHREADED | JX_LOGGER_FLAGS_APPEND_TIMESTAMP | JX_LOGGER_FLAGS_FLUSH_ON_EVERY_LOG);
		if (fileLogger) {
			logger_api->compositeLoggerAddChild(sysLogger, fileLogger);
		}

		jx_logger_i* consoleLogger = logger_api->createConsoleLogger(allocator_api->m_SystemAllocator, JX_LOGGER_FLAGS_MULTITHREADED);
		if (consoleLogger) {
			logger_api->compositeLoggerAddChild(sysLogger, consoleLogger);
		}

		// Replace system logger
		{
			logger_api->inMemoryLoggerDumpToLogger(logger_api->m_SystemLogger, fileLogger);
			logger_api->destroyLogger(logger_api->m_SystemLogger);
			logger_api->m_SystemLogger = sysLogger;
		}
	}

	return true;
}
