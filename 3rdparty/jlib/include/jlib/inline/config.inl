#ifndef JX_CONFIG_H
#error "Must be included from jx/config.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline jx_config_t* jx_config_createConfig(jx_allocator_i* allocator)
{
	return config_api->createConfig(allocator);
}

static inline void jx_config_destroyConfig(jx_config_t* cfg)
{
	config_api->destroyConfig(cfg);
}

static inline int32_t jx_config_loadJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path)
{
	return config_api->loadJSON(cfg, baseDir, path);
}

static inline int32_t jx_config_saveJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path)
{
	return config_api->saveJSON(cfg, baseDir, path);
}

static inline int32_t jx_config_beginArray(jx_config_t* cfg, const char* key)
{
	return config_api->beginArray(cfg, key);
}

static inline int32_t jx_config_endArray(jx_config_t* cfg)
{
	return config_api->endArray(cfg);
}

static inline int32_t jx_config_beginObject(jx_config_t* cfg, const char* key)
{
	return config_api->beginObject(cfg, key);
}

static inline int32_t jx_config_endObject(jx_config_t* cfg)
{
	return config_api->endObject(cfg);
}

static inline int32_t jx_config_setBoolean(jx_config_t* cfg, const char* key, bool val)
{
	return config_api->setBoolean(cfg, key, val);
}

static inline int32_t jx_config_setInt32(jx_config_t* cfg, const char* key, int32_t val)
{
	return config_api->setInt32(cfg, key, val);
}

static inline int32_t jx_config_setUint32(jx_config_t* cfg, const char* key, uint32_t val)
{
	return config_api->setUint32(cfg, key, val);
}

static inline int32_t jx_config_setFloat(jx_config_t* cfg, const char* key, float val)
{
	return config_api->setFloat(cfg, key, val);
}

static inline int32_t jx_config_setDouble(jx_config_t* cfg, const char* key, double val)
{
	return config_api->setDouble(cfg, key, val);
}

static inline int32_t jx_config_setString(jx_config_t* cfg, const char* key, const char* val, uint32_t valLen)
{
	return config_api->setString(cfg, key, val, valLen);
}

static inline bool jx_config_getBoolean(jx_config_t* cfg, const char* key, bool defaultVal)
{
	return config_api->getBoolean(cfg, key, defaultVal);
}

static inline int32_t jx_config_getInt32(jx_config_t* cfg, const char* key, int32_t defaultVal)
{
	return config_api->getInt32(cfg, key, defaultVal);
}

static inline uint32_t jx_config_getUint32(jx_config_t* cfg, const char* key, uint32_t defaultVal)
{
	return config_api->getUint32(cfg, key, defaultVal);
}

static inline float jx_config_getFloat(jx_config_t* cfg, const char* key, float defaultVal)
{
	return config_api->getFloat(cfg, key, defaultVal);
}

static inline double jx_config_getDouble(jx_config_t* cfg, const char* key, double defaultVal)
{
	return config_api->getDouble(cfg, key, defaultVal);
}

static inline const char* jx_config_getString(jx_config_t* cfg, const char* key, const char* defaultVal)
{
	return config_api->getString(cfg, key, defaultVal);
}

static inline uint32_t jx_config_getArraySize(jx_config_t* cfg, const char* key)
{
	return config_api->getArraySize(cfg, key);
}

#ifdef __cplusplus
}
#endif
