#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/error.h>
#include <jlib/logger.h>
#include <jlib/macros.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/string.h>

static jx_logger_i* jlogger_createFileLogger(jx_allocator_i* allocator, jx_file_base_dir baseDir, const char* relPath, uint32_t flags);
static jx_logger_i* jlogger_createInMemoryLogger(jx_allocator_i* allocator, uint32_t flags);
static jx_logger_i* jlogger_createConsoleLogger(jx_allocator_i* allocator, uint32_t flags);
static jx_logger_i* jlogger_createCompositeLogger(jx_allocator_i* allocator, uint32_t flags);
static void jlogger_destroy(jx_logger_i* logger);
static int32_t jlogger_inmemory_dumpToBuffer(jx_logger_i* logger, jx_string_buffer_t* sb);
static int32_t jlogger_inmemory_dumpToLogger(jx_logger_i* logger, jx_logger_i* dst);
static void jlogger_compositeLoggerAddChild(jx_logger_i* compoundLogger, jx_logger_i* childLogger);

static void jlogger_base_logf(jx_logger_i* logger, jx_log_level level, const char* tag, const char* fmt, ...);
static void jlogger_base_vlogf(jx_logger_i* logger, jx_log_level level, const char* tag, const char* fmt, va_list argList);

typedef struct jx_base_logger_t
{
	JX_INHERITS(jx_logger_i);
	jx_allocator_i* m_Allocator;
	jx_os_mutex_t* m_Mutex;
	jx_string_buffer_t* m_StringBuf;
	uint32_t m_Flags;
	JX_PAD(4);
} jx_base_logger_t;

jx_logger_api* logger_api = &(jx_logger_api){
	.m_SystemLogger = NULL,
	.createFileLogger = jlogger_createFileLogger,
	.createInMemoryLogger = jlogger_createInMemoryLogger,
	.createConsoleLogger = jlogger_createConsoleLogger,
	.createCompositeLogger = jlogger_createCompositeLogger,
	.destroyLogger = jlogger_destroy,
	.inMemoryLoggerDumpToBuffer = jlogger_inmemory_dumpToBuffer,
	.inMemoryLoggerDumpToLogger = jlogger_inmemory_dumpToLogger,
	.compositeLoggerAddChild = jlogger_compositeLoggerAddChild,
};

static jx_base_logger_t* jlogger_baseLoggerAlloc(jx_allocator_i* allocator, uint32_t flags, uint32_t sz);
static void jlogger_baseLoggerFree(jx_base_logger_t* logger);

bool jx_logger_initAPI(jx_allocator_i* allocator)
{
	logger_api->m_SystemLogger = jlogger_createInMemoryLogger(allocator, JX_LOGGER_FLAGS_MULTITHREADED | JX_LOGGER_FLAGS_APPEND_TIMESTAMP);
	if (!logger_api->m_SystemLogger) {
		return false;
	}

	JX_LOG_INFO(logger_api->m_SystemLogger, "log", "System logger initialized.\n");

	return true;
}

void jx_logger_shutdownAPI(void)
{
	if (logger_api->m_SystemLogger) {
		JX_LOG_INFO(logger_api->m_SystemLogger, "log", "Closing system logger.\n");
		logger_api->m_SystemLogger->destroy(logger_api->m_SystemLogger);
		logger_api->m_SystemLogger = NULL;
	}
}

static void jlogger_destroy(jx_logger_i* logger)
{
	logger->destroy(logger);
}

//////////////////////////////////////////////////////////////////////////
// File logger
//
typedef struct jx_file_logger_t
{
	JX_INHERITS(jx_base_logger_t);
	jx_os_file_t* m_File;
} jx_file_logger_t;

static void jlogger_file_destroy(jx_logger_i* logger);
static void jlogger_file_writeLine(jx_logger_i* logger, const jx_log_line_t* line);
static void jlogger_file_writeStr(jx_file_logger_t* inst, const char* str, uint32_t len);

static jx_logger_i* jlogger_createFileLogger(jx_allocator_i* allocator, jx_file_base_dir baseDir, const char* relPath, uint32_t flags)
{
	jx_file_logger_t* logger = (jx_file_logger_t*)jlogger_baseLoggerAlloc(allocator, flags, sizeof(jx_file_logger_t));
	if (!logger) {
		return NULL;
	}

	logger->super.super.destroy = jlogger_file_destroy;
	logger->super.super.logf = jlogger_base_logf;
	logger->super.super.vlogf = jlogger_base_vlogf;
	logger->super.super.writeLine = jlogger_file_writeLine;

	logger->m_File = jx_os_fileOpenWrite(baseDir, relPath);
	if (!logger->m_File) {
		jlogger_file_destroy(&logger->super.super);
		return NULL;
	}

	return &logger->super.super;
}

static void jlogger_file_destroy(jx_logger_i* logger)
{
	jx_file_logger_t* inst = (jx_file_logger_t*)logger;

	if (inst->m_File) {
		jx_os_fileClose(inst->m_File);
		inst->m_File = NULL;
	}

	jlogger_baseLoggerFree(&inst->super);
}

// <level indicator> <thread id> <timestamp> <tag>: <log message>
static void jlogger_file_writeLine(jx_logger_i* logger, const jx_log_line_t* line)
{
	static const char* kLogLevelPrefix[] = {
		[JX_LOG_LEVEL_DEBUG]   = "(d) ",
		[JX_LOG_LEVEL_INFO]    = "(i) ",
		[JX_LOG_LEVEL_WARNING] = "(!) ",
		[JX_LOG_LEVEL_ERROR]   = "(x) ",
		[JX_LOG_LEVEL_FATAL]   = "(F) ",
	};
	JX_STATIC_ASSERT(JX_COUNTOF(kLogLevelPrefix) == JX_LOG_LEVEL_COUNT);

	jx_file_logger_t* inst = (jx_file_logger_t*)logger;

	if (inst->super.m_Mutex) {
		jx_os_mutexLock(inst->super.m_Mutex);
	}

	if (line->m_Level < JX_LOG_LEVEL_COUNT && (inst->super.m_Flags & JX_LOGGER_FLAGS_HIDE_LOG_LEVEL_INDICATOR) == 0) {
		jlogger_file_writeStr(inst, kLogLevelPrefix[line->m_Level], UINT32_MAX);
	}

	if ((inst->super.m_Flags & JX_LOGGER_FLAGS_APPEND_THREAD_ID) != 0) {
		char threadIDStr[64];
		jx_snprintf(threadIDStr, JX_COUNTOF(threadIDStr), "0x%08X ", line->m_ThreadID);
		jlogger_file_writeStr(inst, threadIDStr, 11);
	}

	if ((inst->super.m_Flags & JX_LOGGER_FLAGS_APPEND_TIMESTAMP) != 0) {
		char timestamp[64];
		const uint32_t timestampLen = jx_os_timestampToString(line->m_Timestamp, timestamp, JX_COUNTOF(timestamp));
		jlogger_file_writeStr(inst, timestamp, timestampLen);
	}

	if (line->m_Tag) {
		jlogger_file_writeStr(inst, line->m_Tag, UINT32_MAX);
		jlogger_file_writeStr(inst, ": ", 2);
	}

	jlogger_file_writeStr(inst, line->m_Text, UINT32_MAX);

	if ((inst->super.m_Flags & JX_LOGGER_FLAGS_FLUSH_ON_EVERY_LOG) != 0) {
		jx_os_fileFlush(inst->m_File);
	}

	if (inst->super.m_Mutex) {
		jx_os_mutexUnlock(inst->super.m_Mutex);
	}
}

static void jlogger_file_writeStr(jx_file_logger_t* inst, const char* str, uint32_t len)
{
	len = len == UINT32_MAX
		? jx_strlen(str)
		: len
		;

	jx_os_fileWrite(inst->m_File, str, len);
}

//////////////////////////////////////////////////////////////////////////
// In-Memory logger
//
typedef struct jx_inmemory_logger_t
{
	JX_INHERITS(jx_base_logger_t);
	jx_allocator_i* m_TextAllocator;
	jx_log_line_t* m_LineArr;
} jx_inmemory_logger_t;

static void jlogger_inmemory_destroy(jx_logger_i* logger);
static void jlogger_inmemory_writeLine(jx_logger_i* logger, const jx_log_line_t* line);

static jx_logger_i* jlogger_createInMemoryLogger(jx_allocator_i* allocator, uint32_t flags)
{
	jx_inmemory_logger_t* logger = (jx_inmemory_logger_t*)jlogger_baseLoggerAlloc(allocator, flags, sizeof(jx_inmemory_logger_t));
	if (!logger) {
		return NULL;
	}

	logger->super.super.destroy = jlogger_inmemory_destroy;
	logger->super.super.logf = jlogger_base_logf;
	logger->super.super.vlogf = jlogger_base_vlogf;
	logger->super.super.writeLine = jlogger_inmemory_writeLine;
	
	logger->m_TextAllocator = allocator_api->createLinearAllocator(1u << 20, allocator);
	if (!logger->m_TextAllocator) {
		jlogger_inmemory_destroy(&logger->super.super);
		return NULL;
	}
	logger->m_LineArr = (jx_log_line_t*)jx_array_create(allocator);
	if (!logger->m_LineArr) {
		jlogger_inmemory_destroy(&logger->super.super);
		return NULL;
	}

	return &logger->super.super;
}

static void jlogger_inmemory_destroy(jx_logger_i* logger)
{
	jx_inmemory_logger_t* inst = (jx_inmemory_logger_t*)logger;

	jx_array_free(inst->m_LineArr);

	if (inst->m_TextAllocator) {
		allocator_api->destroyLinearAllocator(inst->m_TextAllocator);
		inst->m_TextAllocator = NULL;
	}

	jlogger_baseLoggerFree(&inst->super);
}

static void jlogger_inmemory_writeLine(jx_logger_i* logger, const jx_log_line_t* line)
{
	jx_inmemory_logger_t* inst = (jx_inmemory_logger_t*)logger;

	if (inst->super.m_Mutex) {
		jx_os_mutexLock(inst->super.m_Mutex);
	}

	jx_log_line_t tmp = {
		.m_Level = line->m_Level,
		.m_Tag = line->m_Tag,
		.m_Timestamp = line->m_Timestamp,
		.m_ThreadID = line->m_ThreadID,
	};
	tmp.m_Text = jx_strdup(line->m_Text, inst->m_TextAllocator);
	jx_array_push_back(inst->m_LineArr, tmp);

	if (inst->super.m_Mutex) {
		jx_os_mutexUnlock(inst->super.m_Mutex);
	}
}

static int32_t jlogger_inmemory_dumpToBuffer(jx_logger_i* logger, jx_string_buffer_t* sb)
{
	JX_NOT_IMPLEMENTED();
	return JX_ERROR_OPERATION_FAILED;
}

static int32_t jlogger_inmemory_dumpToLogger(jx_logger_i* logger, jx_logger_i* dst)
{
	jx_inmemory_logger_t* inst = (jx_inmemory_logger_t*)logger;

	if (inst->super.m_Mutex) {
		jx_os_mutexLock(inst->super.m_Mutex);
	}

	const uint32_t numLines = (uint32_t)jx_array_sizeu(inst->m_LineArr);
	for (uint32_t iLine = 0; iLine < numLines; ++iLine) {
		dst->writeLine(dst, &inst->m_LineArr[iLine]);
	}

	if (inst->super.m_Mutex) {
		jx_os_mutexUnlock(inst->super.m_Mutex);
	}

	return JX_ERROR_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Console logger
//
typedef struct jx_console_logger_t
{
	JX_INHERITS(jx_base_logger_t);
} jx_console_logger_t;

static void jlogger_console_destroy(jx_logger_i* logger);
static void jlogger_console_writeLine(jx_logger_i* logger, const jx_log_line_t* line);

static jx_logger_i* jlogger_createConsoleLogger(jx_allocator_i* allocator, uint32_t flags)
{
	jx_console_logger_t* logger = (jx_console_logger_t*)jlogger_baseLoggerAlloc(allocator, flags, sizeof(jx_console_logger_t));
	if (!logger) {
		return NULL;
	}

	logger->super.super.destroy = jlogger_console_destroy;
	logger->super.super.logf = jlogger_base_logf;
	logger->super.super.vlogf = jlogger_base_vlogf;
	logger->super.super.writeLine = jlogger_console_writeLine;
	
	return &logger->super.super;
}

static void jlogger_console_destroy(jx_logger_i* logger)
{
	jx_console_logger_t* inst = (jx_console_logger_t*)logger;
	jlogger_baseLoggerFree(&inst->super);
}

static void jlogger_console_writeLine(jx_logger_i* logger, const jx_log_line_t* line)
{
	jx_console_logger_t* inst = (jx_console_logger_t*)logger;

	if (inst->super.m_Mutex) {
		jx_os_mutexLock(inst->super.m_Mutex);
	}

	if ((inst->super.m_Flags & JX_LOGGER_FLAGS_HIDE_LOG_LEVEL_INDICATOR) == 0) {
		switch (line->m_Level) {
		case JX_LOG_LEVEL_DEBUG: {
			jx_os_consolePuts("\x1b[32m", 5);
		} break;
		case JX_LOG_LEVEL_INFO: {
//			jx_os_consolePuts("\x1b[37m", 5);
		} break;
		case JX_LOG_LEVEL_WARNING: {
			jx_os_consolePuts("\x1b[33m", 5);
		} break;
		case JX_LOG_LEVEL_ERROR: {
			jx_os_consolePuts("\x1b[31m", 5);
		} break;
		case JX_LOG_LEVEL_FATAL: {
			jx_os_consolePuts("\x1b[41m", 5);
		} break;
		}
	}

	if ((inst->super.m_Flags & JX_LOGGER_FLAGS_APPEND_THREAD_ID) != 0) {
		char threadIDStr[64];
		jx_snprintf(threadIDStr, JX_COUNTOF(threadIDStr), "0x%08X ", line->m_ThreadID);
		jx_os_consolePuts(threadIDStr, 11);
	}

	if ((inst->super.m_Flags & JX_LOGGER_FLAGS_APPEND_TIMESTAMP) != 0) {
		char timestamp[64];
		const uint32_t timestampLen = jx_os_timestampToString(line->m_Timestamp, timestamp, JX_COUNTOF(timestamp));
		jx_os_consolePuts(timestamp, timestampLen);
	}

	if (line->m_Tag) {
		jx_os_consolePuts(line->m_Tag, UINT32_MAX);
		jx_os_consolePuts(": ", 2);
	}

	jx_os_consolePuts(line->m_Text, UINT32_MAX);

	if ((inst->super.m_Flags & JX_LOGGER_FLAGS_HIDE_LOG_LEVEL_INDICATOR) == 0) {
		jx_os_consolePuts("\x1b[0m", UINT32_MAX);
	}

	if (inst->super.m_Mutex) {
		jx_os_mutexUnlock(inst->super.m_Mutex);
	}
}

//////////////////////////////////////////////////////////////////////////
// Compound logger
//
typedef struct jx_composite_logger_t
{
	JX_INHERITS(jx_base_logger_t);
	jx_logger_i** m_ChildArr;
} jx_composite_logger_t;

static void jlogger_compositeLogger_destroy(jx_logger_i* inst);
static void jlogger_compositeLogger_writeLine(jx_logger_i* inst, const jx_log_line_t* line);

static jx_logger_i* jlogger_createCompositeLogger(jx_allocator_i* allocator, uint32_t flags)
{
	jx_composite_logger_t* logger = (jx_composite_logger_t*)jlogger_baseLoggerAlloc(allocator, flags, sizeof(jx_composite_logger_t));
	if (!logger) {
		return NULL;
	}

	logger->super.super.destroy = jlogger_compositeLogger_destroy;
	logger->super.super.logf = jlogger_base_logf;
	logger->super.super.vlogf = jlogger_base_vlogf;
	logger->super.super.writeLine = jlogger_compositeLogger_writeLine;
	
	logger->m_ChildArr = (jx_logger_i**)jx_array_create(allocator);
	if (!logger->m_ChildArr) {
		jlogger_compositeLogger_destroy(&logger->super.super);
		return NULL;
	}

	return &logger->super.super;
}

static void jlogger_compositeLogger_destroy(jx_logger_i* logger)
{
	jx_composite_logger_t* inst = (jx_composite_logger_t*)logger;

	const uint32_t numChildren = (uint32_t)jx_array_sizeu(inst->m_ChildArr);
	for (uint32_t iChild = 0; iChild < numChildren; ++iChild) {
		jx_logger_i* child = inst->m_ChildArr[iChild];
		child->destroy(child);
	}
	jx_array_free(inst->m_ChildArr);
	inst->m_ChildArr = NULL;

	jlogger_baseLoggerFree(&inst->super);
}

static void jlogger_compositeLogger_writeLine(jx_logger_i* logger, const jx_log_line_t* line)
{
	jx_composite_logger_t* inst = (jx_composite_logger_t*)logger;
	const uint32_t numChildren = (uint32_t)jx_array_sizeu(inst->m_ChildArr);
	for (uint32_t iChild = 0; iChild < numChildren; ++iChild) {
		jx_logger_i* child = inst->m_ChildArr[iChild];
		child->writeLine(child, line);
	}
}

static void jlogger_compositeLoggerAddChild(jx_logger_i* logger, jx_logger_i* childLogger)
{
	jx_composite_logger_t* inst = (jx_composite_logger_t*)logger;
	jx_array_push_back(inst->m_ChildArr, childLogger);
}

static void jlogger_base_logf(jx_logger_i* logger, jx_log_level level, const char* tag, const char* fmt, ...)
{
	va_list argList;
	va_start(argList, fmt);
	logger->vlogf(logger, level, tag, fmt, argList);
	va_end(argList);
}

static void jlogger_base_vlogf(jx_logger_i* logger, jx_log_level level, const char* tag, const char* fmt, va_list argList)
{
	jx_base_logger_t* inst = (jx_base_logger_t*)logger;

	if (inst->m_Mutex) {
		jx_os_mutexLock(inst->m_Mutex);
	}

	jx_strbuf_reset(inst->m_StringBuf);
	jx_strbuf_vprintf(inst->m_StringBuf, fmt, argList);
	jx_strbuf_nullTerminate(inst->m_StringBuf);

	const char* logLine = jx_strbuf_getString(inst->m_StringBuf, NULL);

	logger->writeLine(logger, &(jx_log_line_t){
		.m_Level = level,
		.m_Tag = tag,
		.m_Text = logLine,
		.m_ThreadID = jx_os_threadGetID(),
		.m_Timestamp = jx_os_timestampNow()
	});

	if (inst->m_Mutex) {
		jx_os_mutexUnlock(inst->m_Mutex);
	}
}

static jx_base_logger_t* jlogger_baseLoggerAlloc(jx_allocator_i* allocator, uint32_t flags, uint32_t sz)
{
	JX_CHECK(sz >= sizeof(jx_base_logger_t), "Logger size should be greater or equal to sizeof(jx_base_logger_t).");
	jx_base_logger_t* logger = (jx_base_logger_t*)JX_ALLOC(allocator, sz);
	if (!logger) {
		return NULL;
	}

	jx_memset(logger, 0, sz);
	logger->m_Allocator = allocator;
	logger->m_Flags = flags;
	logger->m_Mutex = (flags & JX_LOGGER_FLAGS_MULTITHREADED) != 0
		? jx_os_mutexCreate()
		: NULL
		;
	logger->m_StringBuf = jx_strbuf_create(allocator);
	if (!logger->m_StringBuf) {
		jlogger_baseLoggerFree(logger);
		return NULL;
	}

	return logger;
}

static void jlogger_baseLoggerFree(jx_base_logger_t* logger)
{
	if (logger->m_StringBuf) {
		jx_strbuf_destroy(logger->m_StringBuf);
		logger->m_StringBuf = NULL;
	}

	if (logger->m_Mutex) {
		jx_os_mutexDestroy(logger->m_Mutex);
		logger->m_Mutex = NULL;
	}

	JX_FREE(logger->m_Allocator, logger);
}
