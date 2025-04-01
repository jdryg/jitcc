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

typedef enum jx_log_level
{
	JX_LOG_LEVEL_DEBUG = 0,
	JX_LOG_LEVEL_INFO,
	JX_LOG_LEVEL_WARNING,
	JX_LOG_LEVEL_ERROR,
	JX_LOG_LEVEL_FATAL,

	JX_NUM_LOG_LEVELS
} jx_log_level;

#define JX_LOGGER_FLAGS_MULTITHREADED             (1u << 0)
#define JX_LOGGER_FLAGS_APPEND_TIMESTAMP          (1u << 1)
#define JX_LOGGER_FLAGS_APPEND_THREAD_ID          (1u << 2)
#define JX_LOGGER_FLAGS_HIDE_LOG_LEVEL_INDICATOR  (1u << 3)
#define JX_LOGGER_FLAGS_FLUSH_ON_EVERY_LOG        (1u << 4)

typedef struct jx_logger_o jx_logger_o;

typedef struct jx_logger_i
{
	jx_logger_o* m_Inst;

	void (*logf)(jx_logger_o* inst, jx_log_level level, const char* fmt, ...);
	void (*vlogf)(jx_logger_o* inst, jx_log_level level, const char* fmt, va_list argList);
	void (*puts)(jx_logger_o* inst, const char* str, uint32_t len);
} jx_logger_i;

#define JX_LOG_DEBUG(logger, fmt, ...)   (logger)->logf((logger)->m_Inst, JX_LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define JX_LOG_INFO(logger, fmt, ...)    (logger)->logf((logger)->m_Inst, JX_LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define JX_LOG_WARNING(logger, fmt, ...) (logger)->logf((logger)->m_Inst, JX_LOG_LEVEL_WARNING, fmt, ##__VA_ARGS__)
#define JX_LOG_ERROR(logger, fmt, ...)   (logger)->logf((logger)->m_Inst, JX_LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define JX_LOG_FATAL(logger, fmt, ...)   (logger)->logf((logger)->m_Inst, JX_LOG_LEVEL_FATAL, fmt, ##__VA_ARGS__)

#define JX_SYS_LOG_DEBUG(fmt, ...)       JX_LOG_DEBUG(logger_api->m_SystemLogger, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_INFO(fmt, ...)        JX_LOG_INFO(logger_api->m_SystemLogger, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_WARNING(fmt, ...)     JX_LOG_WARNING(logger_api->m_SystemLogger, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_ERROR(fmt, ...)       JX_LOG_ERROR(logger_api->m_SystemLogger, fmt, ##__VA_ARGS__)
#define JX_SYS_LOG_FATAL(fmt, ...)       JX_LOG_FATAL(logger_api->m_SystemLogger, fmt, ##__VA_ARGS__)

typedef struct jx_logger_api
{
	jx_logger_i* m_SystemLogger;

	jx_logger_i*  (*createFileLogger)(jx_allocator_i* allocator, jx_file_base_dir baseDir, const char* relPath, uint32_t flags);
	jx_logger_i*  (*createInMemoryLogger)(jx_allocator_i* allocator, uint32_t flags);
	void        (*destroyLogger)(jx_logger_i* logger);
	const char* (*getInMemoryLoggerBuffer)(jx_logger_i* logger);
} jx_logger_api;

extern jx_logger_api* logger_api;

#ifdef __cplusplus
}
#endif

#endif // JX_LOGGER_H
