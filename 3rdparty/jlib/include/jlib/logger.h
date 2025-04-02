#ifndef JX_LOGGER_H
#define JX_LOGGER_H

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum jx_file_base_dir jx_file_base_dir;
typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_string_buffer_t jx_string_buffer_t;

typedef enum jx_log_level
{
	JX_LOG_LEVEL_DEBUG = 0,
	JX_LOG_LEVEL_INFO,
	JX_LOG_LEVEL_WARNING,
	JX_LOG_LEVEL_ERROR,
	JX_LOG_LEVEL_FATAL,

	JX_LOG_LEVEL_COUNT
} jx_log_level;

typedef struct jx_log_line_t
{
	const char* m_Text;
	const char* m_Tag;
	uint64_t m_Timestamp;
	uint32_t m_ThreadID;
	jx_log_level m_Level;
} jx_log_line_t;

#define JX_LOGGER_FLAGS_MULTITHREADED             (1u << 0)
#define JX_LOGGER_FLAGS_APPEND_TIMESTAMP          (1u << 1)
#define JX_LOGGER_FLAGS_APPEND_THREAD_ID          (1u << 2)
#define JX_LOGGER_FLAGS_HIDE_LOG_LEVEL_INDICATOR  (1u << 3)
#define JX_LOGGER_FLAGS_FLUSH_ON_EVERY_LOG        (1u << 4)

typedef struct jx_logger_i jx_logger_i;
typedef struct jx_logger_i
{
	void (*destroy)(jx_logger_i* logger);
	void (*logf)(jx_logger_i* logger, jx_log_level level, const char* tag, const char* fmt, ...);
	void (*vlogf)(jx_logger_i* logger, jx_log_level level, const char* tag, const char* fmt, va_list argList);
	void (*writeLine)(jx_logger_i* logger, const jx_log_line_t* line);
} jx_logger_i;

#define JX_LOG_DEBUG(logger, tag, fmt, ...)   (logger)->logf(logger, JX_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define JX_LOG_INFO(logger, tag, fmt, ...)    (logger)->logf(logger, JX_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define JX_LOG_WARNING(logger, tag, fmt, ...) (logger)->logf(logger, JX_LOG_LEVEL_WARNING, tag, fmt, ##__VA_ARGS__)
#define JX_LOG_ERROR(logger, tag, fmt, ...)   (logger)->logf(logger, JX_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define JX_LOG_FATAL(logger, tag, fmt, ...)   (logger)->logf(logger, JX_LOG_LEVEL_FATAL, tag, fmt, ##__VA_ARGS__)

#define JX_SYS_LOG_DEBUG(tag, fmt, ...)       JX_LOG_DEBUG(logger_api->m_SystemLogger, tag, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_INFO(tag, fmt, ...)        JX_LOG_INFO(logger_api->m_SystemLogger, tag, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_WARNING(tag, fmt, ...)     JX_LOG_WARNING(logger_api->m_SystemLogger, tag, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_ERROR(tag, fmt, ...)       JX_LOG_ERROR(logger_api->m_SystemLogger, tag, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_FATAL(tag, fmt, ...)       JX_LOG_FATAL(logger_api->m_SystemLogger, tag, fmt, ##__VA_ARGS__)

typedef struct jx_logger_api
{
	jx_logger_i* m_SystemLogger;

	jx_logger_i*  (*createFileLogger)(jx_allocator_i* allocator, jx_file_base_dir baseDir, const char* relPath, uint32_t flags);
	jx_logger_i*  (*createInMemoryLogger)(jx_allocator_i* allocator, uint32_t flags);
	jx_logger_i*  (*createConsoleLogger)(jx_allocator_i* allocator, uint32_t flags);
	jx_logger_i*  (*createCompositeLogger)(jx_allocator_i* allocator, uint32_t flags);
	void          (*destroyLogger)(jx_logger_i* logger);

	int32_t       (*inMemoryLoggerDumpToBuffer)(jx_logger_i* logger, jx_string_buffer_t* sb);
	int32_t       (*inMemoryLoggerDumpToLogger)(jx_logger_i* logger, jx_logger_i* dst);

	void          (*compositeLoggerAddChild)(jx_logger_i* compoundLogger, jx_logger_i* childLogger);
} jx_logger_api;

extern jx_logger_api* logger_api;

#ifdef __cplusplus
}
#endif

#endif // JX_LOGGER_H
