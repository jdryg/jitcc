#ifndef JX_CONFIG_H
#define JX_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jx_allocator_i jx_allocator_i;
typedef enum jx_file_base_dir jx_file_base_dir;

typedef struct jx_config_t jx_config_t;

typedef struct jx_config_api
{
	jx_config_t* (*createConfig)(jx_allocator_i* allocator);
	void         (*destroyConfig)(jx_config_t* cfg);

	int32_t      (*loadJSON)(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path);
	int32_t      (*saveJSON)(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path);

	int32_t      (*beginObject)(jx_config_t* cfg, const char* key);
	int32_t      (*endObject)(jx_config_t* cfg);
	int32_t      (*beginArray)(jx_config_t* cfg, const char* key);
	int32_t      (*endArray)(jx_config_t* cfg);

	int32_t      (*setBoolean)(jx_config_t* cfg, const char* key, bool val);
	int32_t      (*setInt32)(jx_config_t* cfg, const char* key, int32_t val);
	int32_t      (*setUint32)(jx_config_t* cfg, const char* key, uint32_t val);
	int32_t      (*setFloat)(jx_config_t* cfg, const char* key, float val);
	int32_t      (*setDouble)(jx_config_t* cfg, const char* key, double val);
	int32_t      (*setString)(jx_config_t* cfg, const char* key, const char* val, uint32_t valLen);

	bool         (*getBoolean)(jx_config_t* cfg, const char* key, bool defaultVal);
	int32_t      (*getInt32)(jx_config_t* cfg, const char* key, int32_t defaultVal);
	uint32_t     (*getUint32)(jx_config_t* cfg, const char* key, uint32_t defaultVal);
	float        (*getFloat)(jx_config_t* cfg, const char* key, float defaultVal);
	double       (*getDouble)(jx_config_t* cfg, const char* key, double defaultVal);
	const char*  (*getString)(jx_config_t* cfg, const char* key, const char* defaultVal);

	uint32_t     (*getArraySize)(jx_config_t* cfg, const char* key);
} jx_config_api;

extern jx_config_api* config_api;

static jx_config_t* jx_config_createConfig(jx_allocator_i* allocator);
static void jx_config_destroyConfig(jx_config_t* cfg);
static int32_t jx_config_loadJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path);
static int32_t jx_config_saveJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path);
static int32_t jx_config_beginArray(jx_config_t* cfg, const char* key);
static int32_t jx_config_endArray(jx_config_t* cfg);
static int32_t jx_config_beginObject(jx_config_t* cfg, const char* key);
static int32_t jx_config_endObject(jx_config_t* cfg);
static int32_t jx_config_setBoolean(jx_config_t* cfg, const char* key, bool val);
static int32_t jx_config_setInt32(jx_config_t* cfg, const char* key, int32_t val);
static int32_t jx_config_setUint32(jx_config_t* cfg, const char* key, uint32_t val);
static int32_t jx_config_setFloat(jx_config_t* cfg, const char* key, float val);
static int32_t jx_config_setDouble(jx_config_t* cfg, const char* key, double val);
static int32_t jx_config_setString(jx_config_t* cfg, const char* key, const char* val, uint32_t valLen);
static bool jx_config_getBoolean(jx_config_t* cfg, const char* key, bool defaultVal);
static int32_t jx_config_getInt32(jx_config_t* cfg, const char* key, int32_t defaultVal);
static uint32_t jx_config_getUint32(jx_config_t* cfg, const char* key, uint32_t defaultVal);
static float jx_config_getFloat(jx_config_t* cfg, const char* key, float defaultVal);
static double jx_config_getDouble(jx_config_t* cfg, const char* key, double defaultVal);
static const char* jx_config_getString(jx_config_t* cfg, const char* key, const char* defaultVal);
static uint32_t jx_config_getArraySize(jx_config_t* cfg, const char* key);

#ifdef __cplusplus
}
#endif

#include "inline/config.inl"

#endif // JX_CONFIG_H
