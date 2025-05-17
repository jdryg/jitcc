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

int main(int argc, char** argv)
{
	jx_kernel_initAPI();

	// Redirect system logger to file and console
	{
		// Change application directories.
		if (os_api->fsSetBaseDir(JX_FILE_BASE_DIR_USERDATA, JX_FILE_BASE_DIR_USERDATA, "jitcc") != JX_ERROR_NONE || 
			os_api->fsSetBaseDir(JX_FILE_BASE_DIR_USERAPPDATA, JX_FILE_BASE_DIR_USERAPPDATA, "jitcc") != JX_ERROR_NONE || 
			os_api->fsSetBaseDir(JX_FILE_BASE_DIR_TEMP, JX_FILE_BASE_DIR_TEMP, "jitcc") != JX_ERROR_NONE) {
			return -1;
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
	}

	jx_allocator_i* allocator = allocator_api->createAllocator("jcc");

#if 1
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
			|| iTest == 179 // string.h functions
			|| iTest == 180 // string.h functions
			|| iTest == 187 // file functions
			|| iTest == 189 // fprintf/stdout
			|| iTest == 204 // va_start/va_end
			|| iTest == 206 // #pragma
			|| iTest == 207 // VLA
			|| iTest == 210 // __attribute__
//			|| iTest == 212 // Predefined macros
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
			jx_x64gen_context_t* jitgenCtx = jx_x64gen_createContext(jitCtx, mirCtx, allocator);

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
#elif 1
	jx_cc_context_t* ctx = jx_cc_createContext(allocator, logger_api->m_SystemLogger);
	jx_cc_addIncludePath(ctx, JX_FILE_BASE_DIR_INSTALL, "include");

	const char* sourceFile = "test/c-testsuite/00033.c";
//	const char* sourceFile = "test/test_arithmetic.c";

	JX_SYS_LOG_INFO(NULL, "%s\n", sourceFile);
	jx_cc_translation_unit_t* tu = jx_cc_compileFile(ctx, JX_FILE_BASE_DIR_INSTALL, sourceFile);
	if (!tu || tu->m_NumErrors != 0) {
		JX_SYS_LOG_INFO(NULL, "Failed to compile \"%s\"\n", sourceFile);
		goto end;
	}

	JX_SYS_LOG_INFO(NULL, "Building IR...\n");
	{
		jx_ir_context_t* irCtx = jx_ir_createContext(allocator);
		jx_irgen_context_t* genCtx = jx_irgen_createContext(irCtx, allocator);

		if (!jx_irgen_moduleGen(genCtx, sourceFile, tu)) {
			JX_SYS_LOG_ERROR(NULL, "Failed to generate module IR\n");
		} else {
			jx_irgen_destroyContext(genCtx);

			jx_string_buffer_t* sb = jx_strbuf_create(allocator);
			jx_ir_print(irCtx, sb);
			jx_strbuf_nullTerminate(sb);
			JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
			jx_strbuf_destroy(sb);

			{
				jx_mir_context_t* mirCtx = jx_mir_createContext(allocator);
				jx_mirgen_context_t* mirGenCtx = jx_mirgen_createContext(irCtx, mirCtx, allocator);

				jx_ir_module_t* irMod = jx_ir_getModule(irCtx, 0);
				if (irMod) {
					jx_mirgen_moduleGen(mirGenCtx, irMod);
				}

				jx_mirgen_destroyContext(mirGenCtx);

				sb = jx_strbuf_create(allocator);
				jx_mir_print(mirCtx, sb);
				jx_strbuf_nullTerminate(sb);
				JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
				jx_strbuf_destroy(sb);

				jx_x64_context_t* jitCtx = jx_x64_createContext(allocator);
				jx_x64gen_context_t* jitgenCtx = jx_x64gen_createContext(jitCtx, mirCtx, allocator);

				if (jx_x64gen_codeGen(jitgenCtx)) {
					sb = jx_strbuf_create(allocator);

					uint32_t bufferSize = 0;
					const uint8_t* buffer = jx64_getBuffer(jitCtx, &bufferSize);
					for (uint32_t i = 0; i < bufferSize; ++i) {
						jx_strbuf_printf(sb, "%02X", buffer[i]);
					}

					jx_strbuf_nullTerminate(sb);
					JX_SYS_LOG_INFO(NULL, "\n%s\n\n", jx_strbuf_getString(sb, NULL));
					jx_strbuf_destroy(sb);

					typedef int32_t(*pfnMain)(void);
					jx_x64_symbol_t* symMain = jx64_symbolGetByName(jitCtx, "main");
					if (symMain) {
						pfnMain mainFunc = (pfnMain)((uint8_t*)buffer + jx64_labelGetOffset(jitCtx, symMain->m_Label));
						int32_t ret = mainFunc();
						JX_SYS_LOG_DEBUG(NULL, "main() returned %d\n", ret);
					} else {
						JX_SYS_LOG_ERROR(NULL, "main() not found!\n");
					}
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
#endif

	allocator_api->destroyAllocator(allocator);

	jx_kernel_shutdownAPI();
 
	return 0;
}
