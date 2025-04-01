#include <jlib/logger.h>
#include <jlib/allocator.h>
#include <jlib/string.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/macros.h>

static const char* kLogLevelPrefix[] = {
	"(d) ",
	"(i) ",
	"(!) ",
	"(x) ",
	"(F) ",
};
JX_STATIC_ASSERT(JX_COUNTOF(kLogLevelPrefix) == JX_NUM_LOG_LEVELS);

typedef struct _jx_logger_i
{
	JX_INHERITS(jx_logger_i);

	jx_allocator_i* m_Allocator;
	jx_os_mutex_t* m_Mutex;
	uint32_t m_Flags;

	void (*destroy)(jx_logger_i* logger);
} _jx_logger_i;

static jx_logger_i* _jlogger_createFileLogger(jx_allocator_i* allocator, jx_file_base_dir baseDir, const char* relPath, uint32_t flags);
static jx_logger_i* _jlogger_createInMemoryLogger(jx_allocator_i* allocator, uint32_t flags);
static void _jlogger_destroy(jx_logger_i* logger);
static const char* _jlogger_getInMemoryLoggerBuffer(jx_logger_i* logger);

jx_logger_api* logger_api = &(jx_logger_api){
	.m_SystemLogger = NULL,
	.createFileLogger = _jlogger_createFileLogger,
	.createInMemoryLogger = _jlogger_createInMemoryLogger,
	.destroyLogger = _jlogger_destroy,
	.getInMemoryLoggerBuffer = _jlogger_getInMemoryLoggerBuffer
};

static _jx_logger_i* _jlogger_createLoggerInterface(jx_allocator_i* allocator, uint32_t flags, uint32_t loggerDataSize);
static void _jlogger_destroyLoggerInterface(_jx_logger_i* logger);
static void _jlogger_text_logf(jx_logger_o* inst, jx_log_level level, const char* fmt, ...);
static void _jlogger_text_vlogf(jx_logger_o* inst, jx_log_level level, const char* fmt, va_list argList);

bool jx_logger_initAPI(jx_allocator_i* allocator)
{
	logger_api->m_SystemLogger = _jlogger_createInMemoryLogger(allocator, JX_LOGGER_FLAGS_MULTITHREADED | JX_LOGGER_FLAGS_APPEND_TIMESTAMP);
	if (!logger_api->m_SystemLogger) {
		return false;
	}

	JX_LOG_INFO(logger_api->m_SystemLogger, "log: System logger initialized.\n");

	return true;
}

void jx_logger_shutdownAPI(void)
{
	if (logger_api->m_SystemLogger) {
		JX_LOG_INFO(logger_api->m_SystemLogger, "log: Closing system logger.\n");
		_jlogger_destroy(logger_api->m_SystemLogger);
		logger_api->m_SystemLogger = NULL;
	}
}

static void _jlogger_destroy(jx_logger_i* logger)
{
	_jx_logger_i* loggerInterface = (_jx_logger_i*)logger;
	loggerInterface->destroy(logger);
	_jlogger_destroyLoggerInterface(loggerInterface);
}

//////////////////////////////////////////////////////////////////////////
// Logger interface
//
static _jx_logger_i* _jlogger_createLoggerInterface(jx_allocator_i* allocator, uint32_t flags, uint32_t loggerDataSize)
{
	const uint32_t requiredMemory = 0
		+ sizeof(_jx_logger_i)
		+ loggerDataSize
		;

	uint8_t* buffer = JX_ALLOC(allocator, requiredMemory);
	if (!buffer) {
		return NULL;
	}

	uint8_t* ptr = buffer;
	_jx_logger_i* loggerInterface = (_jx_logger_i*)ptr;
	ptr += sizeof(_jx_logger_i);
	loggerInterface->super.m_Inst = (jx_logger_o*)ptr;
	ptr += loggerDataSize;

	loggerInterface->m_Flags = flags;
	loggerInterface->m_Allocator = allocator;
	loggerInterface->m_Mutex = (flags & JX_LOGGER_FLAGS_MULTITHREADED) != 0
		? os_api->mutexCreate()
		: NULL
		;

	return loggerInterface;
}

static void _jlogger_destroyLoggerInterface(_jx_logger_i* logger)
{
	jx_allocator_i* allocator = logger->m_Allocator;

	if (logger->m_Mutex) {
		os_api->mutexDestroy(logger->m_Mutex);
		logger->m_Mutex = NULL;
	}

	JX_FREE(allocator, logger);
}

//////////////////////////////////////////////////////////////////////////
// File logger
//
typedef struct _jx_file_logger_o
{
	jx_os_file_t* m_File;
} _jx_file_logger_o;

static void _jlogger_file_puts(jx_logger_o* inst, const char* str, uint32_t len);
static void _jlogger_file_destroy(jx_logger_i* inst);

static jx_logger_i* _jlogger_createFileLogger(jx_allocator_i* allocator, jx_file_base_dir baseDir, const char* relPath, uint32_t flags)
{
	_jx_logger_i* loggerInterface = _jlogger_createLoggerInterface(allocator, flags, sizeof(_jx_file_logger_o));
	if (!loggerInterface) {
		return NULL;
	}

	_jx_file_logger_o* loggerData = (_jx_file_logger_o*)loggerInterface->super.m_Inst;
	loggerData->m_File = os_api->fileOpenWrite(baseDir, relPath);
	if (!loggerData->m_File) {
		_jlogger_destroyLoggerInterface(loggerInterface);
		return NULL;
	}

	loggerInterface->super.logf = _jlogger_text_logf;
	loggerInterface->super.vlogf = _jlogger_text_vlogf;
	loggerInterface->super.puts = _jlogger_file_puts;

	loggerInterface->destroy = _jlogger_file_destroy;

	return &loggerInterface->super;
}

static void _jlogger_file_puts(jx_logger_o* inst, const char* str, uint32_t len)
{
	_jx_logger_i* loggerInterface = (_jx_logger_i*)((uint8_t*)inst - sizeof(_jx_logger_i));
	_jx_file_logger_o* loggerData = (_jx_file_logger_o*)inst;

	len = len == UINT32_MAX
		? jx_strlen(str)
		: len
		;

	if (loggerInterface->m_Mutex) {
		os_api->mutexLock(loggerInterface->m_Mutex);
	}

	os_api->fileWrite(loggerData->m_File, str, len);

	if ((loggerInterface->m_Flags & JX_LOGGER_FLAGS_FLUSH_ON_EVERY_LOG) != 0) {
		os_api->fileFlush(loggerData->m_File);
	}

	if (loggerInterface->m_Mutex) {
		os_api->mutexUnlock(loggerInterface->m_Mutex);
	}
}

static void _jlogger_file_destroy(jx_logger_i* logger)
{
	_jx_file_logger_o* inst = (_jx_file_logger_o*)logger->m_Inst;

	if (inst->m_File) {
		os_api->fileClose(inst->m_File);
		inst->m_File = NULL;
	}
}

//////////////////////////////////////////////////////////////////////////
// In-Memory logger
//
typedef struct _jx_inmemory_logger_o
{
	char* m_Buffer;
	uint32_t m_Capacity;
	uint32_t m_Pos;
} _jx_inmemory_logger_o;

static void _jlogger_inmemory_puts(jx_logger_o* inst, const char* str, uint32_t len);
static void _jlogger_inmemory_destroy(jx_logger_i* logger);

static jx_logger_i* _jlogger_createInMemoryLogger(jx_allocator_i* allocator, uint32_t flags)
{
	_jx_logger_i* loggerInterface = _jlogger_createLoggerInterface(allocator, flags, sizeof(_jx_inmemory_logger_o));
	if (!loggerInterface) {
		return NULL;
	}

	_jx_inmemory_logger_o* loggerData = (_jx_inmemory_logger_o*)loggerInterface->super.m_Inst;
	loggerData->m_Buffer = NULL;
	loggerData->m_Capacity = 0;
	loggerData->m_Pos = 0;

	loggerInterface->super.logf = _jlogger_text_logf;
	loggerInterface->super.vlogf = _jlogger_text_vlogf;
	loggerInterface->super.puts = _jlogger_inmemory_puts;

	loggerInterface->destroy = _jlogger_inmemory_destroy;

	return &loggerInterface->super;
}

static void _jlogger_inmemory_destroy(jx_logger_i* logger)
{
	_jx_logger_i* loggerInterface = (_jx_logger_i*)logger;
	_jx_inmemory_logger_o* inst = (_jx_inmemory_logger_o*)logger->m_Inst;

	JX_FREE(loggerInterface->m_Allocator, inst->m_Buffer);
}

static void _jlogger_inmemory_puts(jx_logger_o* inst, const char* str, uint32_t len)
{
	_jx_logger_i* loggerInterface = (_jx_logger_i*)((uint8_t*)inst - sizeof(_jx_logger_i));
	_jx_inmemory_logger_o* loggerData = (_jx_inmemory_logger_o*)inst;

	len = len == UINT32_MAX
		? jx_strlen(str)
		: len
		;

	if (loggerInterface->m_Mutex) {
		os_api->mutexLock(loggerInterface->m_Mutex);
	}

	if (loggerData->m_Pos + len >= loggerData->m_Capacity) {
		const uint32_t oldCapacity = loggerData->m_Capacity;
		
		uint32_t extraCapacity = 2048;
		while (extraCapacity < len) {
			extraCapacity += 2048;
		}

		const uint32_t newCapacity = oldCapacity + extraCapacity;

		char* buffer = (char*)JX_ALLOC(loggerInterface->m_Allocator, newCapacity);
		if (!buffer) {
			if (loggerInterface->m_Mutex) {
				os_api->mutexUnlock(loggerInterface->m_Mutex);
			}
			return;
		}

		jx_memcpy(buffer, loggerData->m_Buffer, oldCapacity);

		JX_FREE(loggerInterface->m_Allocator, loggerData->m_Buffer);
		loggerData->m_Buffer = buffer;
		loggerData->m_Capacity = newCapacity;
	}

	jx_memcpy(&loggerData->m_Buffer[loggerData->m_Pos], str, len);
	loggerData->m_Pos += len;
	loggerData->m_Buffer[loggerData->m_Pos] = '\0';

	if (loggerInterface->m_Mutex) {
		os_api->mutexUnlock(loggerInterface->m_Mutex);
	}
}

static const char* _jlogger_getInMemoryLoggerBuffer(jx_logger_i* logger)
{
	return ((_jx_inmemory_logger_o*)logger->m_Inst)->m_Buffer;
}

//////////////////////////////////////////////////////////////////////////
// Common logger functions
//
static void _jlogger_text_logf(jx_logger_o* inst, jx_log_level level, const char* fmt, ...)
{
	va_list argList;
	va_start(argList, fmt);
	_jlogger_text_vlogf(inst, level, fmt, argList);
	va_end(argList);
}

// <level indicator> <thread id> <timestamp> <log message>
static void _jlogger_text_vlogf(jx_logger_o* inst, jx_log_level level, const char* fmt, va_list argList)
{
	_jx_logger_i* loggerInterface = (_jx_logger_i*)((uint8_t*)inst - sizeof(_jx_logger_i));

	char logLine[4096];
	const uint32_t logLineLen = jx_vsnprintf(logLine, JX_COUNTOF(logLine), fmt, argList);

	if (loggerInterface->m_Mutex) {
		os_api->mutexLock(loggerInterface->m_Mutex);
	}

	if ((loggerInterface->m_Flags & JX_LOGGER_FLAGS_HIDE_LOG_LEVEL_INDICATOR) == 0) {
		loggerInterface->super.puts(inst, kLogLevelPrefix[level], UINT32_MAX);
	}

	if ((loggerInterface->m_Flags & JX_LOGGER_FLAGS_APPEND_THREAD_ID) != 0) {
		const uint32_t threadID = os_api->threadGetID();

		char threadIDStr[64];
		jx_snprintf(threadIDStr, JX_COUNTOF(threadIDStr), "0x%08X ", threadID);
		loggerInterface->super.puts(inst, threadIDStr, 11);
	}

	if ((loggerInterface->m_Flags & JX_LOGGER_FLAGS_APPEND_TIMESTAMP) != 0) {
		char timestamp[64];
		const uint32_t timestampLen = os_api->timestampToString(os_api->timestampNow(), timestamp, JX_COUNTOF(timestamp));
		loggerInterface->super.puts(inst, timestamp, timestampLen);
	}

	loggerInterface->super.puts(inst, logLine, logLineLen);

	if (loggerInterface->m_Mutex) {
		os_api->mutexUnlock(loggerInterface->m_Mutex);
	}
}
