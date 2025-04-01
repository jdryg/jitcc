#include <jlib/config.h>
#include <jlib/error.h>
#include <jlib/macros.h>
#include <jlib/allocator.h>
#include <jlib/memory.h>
#include <jlib/hashmap.h>
#include <jlib/os.h>
#include <jlib/string.h>
#include <jlib/dbg.h>
#include <jlib/array.h>
#include <jlib/math.h>

#define JSON_USE_ASSERT
#define JSON_STATIC
#define JSON_IMPLEMENTATION
#define JSON_ASSERT(expr) JX_CHECK(expr, "json error: %s ", #expr)
#include <vurtun/json.h>

static jx_config_t* _jx_config_createConfig(jx_allocator_i* allocator);
static void _jx_config_destroyConfig(jx_config_t* cfg);
static int32_t _jx_config_loadJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path);
static int32_t _jx_config_saveJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path);
static int32_t _jx_config_beginObject(jx_config_t* cfg, const char* key);
static int32_t _jx_config_endObject(jx_config_t* cfg);
static int32_t _jx_config_beginArray(jx_config_t* cfg, const char* key);
static int32_t _jx_config_endArray(jx_config_t* cfg);
static int32_t _jx_config_setBoolean(jx_config_t* cfg, const char* key, bool val);
static int32_t _jx_config_setInt32(jx_config_t* cfg, const char* key, int32_t val);
static int32_t _jx_config_setUint32(jx_config_t* cfg, const char* key, uint32_t val);
static int32_t _jx_config_setFloat(jx_config_t* cfg, const char* key, float val);
static int32_t _jx_config_setDouble(jx_config_t* cfg, const char* key, double val);
static int32_t _jx_config_setString(jx_config_t* cfg, const char* key, const char* val, uint32_t valLen);
static bool _jx_config_getBoolean(jx_config_t* cfg, const char* key, bool defaultVal);
static int32_t _jx_config_getInt32(jx_config_t* cfg, const char* key, int32_t defaultVal);
static uint32_t _jx_config_getUint32(jx_config_t* cfg, const char* key, uint32_t defaultVal);
static float _jx_config_getFloat(jx_config_t* cfg, const char* key, float defaultVal);
static double _jx_config_getDouble(jx_config_t* cfg, const char* key, double defaultVal);
static const char* _jx_config_getString(jx_config_t* cfg, const char* key, const char* defaultVal);
static uint32_t _jx_config_getArraySize(jx_config_t* cfg, const char* key);

jx_config_api* config_api = &(jx_config_api){
	.createConfig = _jx_config_createConfig,
	.destroyConfig = _jx_config_destroyConfig,
	.loadJSON = _jx_config_loadJSON,
	.saveJSON = _jx_config_saveJSON,
	.beginObject = _jx_config_beginObject,
	.endObject = _jx_config_endObject,
	.beginArray = _jx_config_beginArray,
	.endArray = _jx_config_endArray,
	.setBoolean = _jx_config_setBoolean,
	.setInt32 = _jx_config_setInt32,
	.setUint32 = _jx_config_setUint32,
	.setFloat = _jx_config_setFloat,
	.setDouble = _jx_config_setDouble,
	.setString = _jx_config_setString,
	.getBoolean = _jx_config_getBoolean,
	.getInt32 = _jx_config_getInt32,
	.getUint32 = _jx_config_getUint32,
	.getFloat = _jx_config_getFloat,
	.getDouble = _jx_config_getDouble,
	.getString = _jx_config_getString,
	.getArraySize = _jx_config_getArraySize,
};

typedef enum _jx_config_node_type
{
	_JNODE_TYPE_OBJECT,
	_JNODE_TYPE_ARRAY,
	_JNODE_TYPE_NUMBER,
	_JNODE_TYPE_BOOLEAN,
	_JNODE_TYPE_STRING
}_jx_config_node_type;

typedef struct _jx_config_node
{
	char* m_Key;
	union
	{
		struct _jx_config_node** m_Children;
		char* m_String;
		double m_Number;
		bool m_Boolean;
	};
	_jx_config_node_type m_Type;
	JX_PAD(4);
} _jx_config_node;

typedef struct _jx_config_hash_node
{
	char* m_FullPathKey;
	_jx_config_node* m_Node;
} _jx_config_hash_node;

typedef struct jx_config_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_NodePoolAllocator;
	jx_hashmap_t* m_Hashmap;
	_jx_config_node* m_Root;
	_jx_config_node* m_Stack[32];
	uint32_t m_StackTop;
	char m_CurPath[512];
} jx_config_t;

static void _jx_config_buildFullPath(jx_config_t* cfg, const char* key, char* fullPath, uint32_t max);
static void _jx_config_stackPush(jx_config_t* cfg, _jx_config_node* node);
static void _jx_config_stackPop(jx_config_t* cfg);
static _jx_config_node* _jx_config_stackGetTop(jx_config_t* cfg);
static void _jx_config_destroyNode_r(jx_config_t* cfg, _jx_config_node* node);

static uint64_t _jx_config_hashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t _jx_config_compareCallback(const void* a, const void* b, void* udata);
static void _jx_config_freeCallback(void* item, void* udata);

static void _jx_config_insertToken(jx_config_t* cfg, const char* key, uint32_t keyLen, struct json_token* val);

static jx_config_t* _jx_config_createConfig(jx_allocator_i* allocator)
{
	jx_config_t* cfg = (jx_config_t*)JX_ALLOC(allocator, sizeof(jx_config_t));
	if (!cfg) {
		return NULL;
	}

	jx_memset(cfg, 0, sizeof(jx_config_t));
	cfg->m_Allocator = allocator;

	cfg->m_NodePoolAllocator = allocator_api->createPoolAllocator(sizeof(_jx_config_node), 64, allocator);
	if (!cfg->m_NodePoolAllocator) {
		_jx_config_destroyConfig(cfg);
		return NULL;
	}

	cfg->m_Hashmap = jx_hashmapCreate(allocator, sizeof(_jx_config_hash_node), 0, 0, 0, _jx_config_hashCallback, _jx_config_compareCallback, _jx_config_freeCallback, cfg);
	if (!cfg->m_Hashmap) {
		_jx_config_destroyConfig(cfg);
		return NULL;
	}

	cfg->m_Root = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
	if (!cfg->m_Root) {
		_jx_config_destroyConfig(cfg);
		return NULL;
	}
	cfg->m_Root->m_Type = _JNODE_TYPE_OBJECT;
	cfg->m_Root->m_Children = jx_array_create(cfg->m_Allocator);
	cfg->m_Root->m_Key = NULL;

	cfg->m_Stack[0] = cfg->m_Root;
	cfg->m_StackTop = 0;

	return cfg;
}

static void _jx_config_destroyConfig(jx_config_t* cfg)
{
	jx_allocator_i* allocator = cfg->m_Allocator;

	if (cfg->m_Hashmap) {
		jx_hashmapDestroy(cfg->m_Hashmap);
		cfg->m_Hashmap = NULL;
	}

	if (cfg->m_Root) {
		_jx_config_destroyNode_r(cfg, cfg->m_Root);
		cfg->m_Root = NULL;
	}

	if (cfg->m_NodePoolAllocator) {
		allocator_api->destroyPoolAllocator(cfg->m_NodePoolAllocator);
		cfg->m_NodePoolAllocator = NULL;
	}

	JX_FREE(allocator, cfg);
}

static int32_t _jx_config_loadJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path)
{
	// Load the whole file.
	char* json = NULL;
	uint64_t jsonLen = 0ull;
	{
		jx_os_file_t* file = jx_os_fileOpenRead(baseDir, path);
		if (!file) {
			return JX_ERROR_FILE_NOT_FOUND;
		}

		jx_os_fileSeek(file, 0, JX_FILE_SEEK_ORIGIN_END);
		const uint64_t fileSize = jx_os_fileTell(file);
		jx_os_fileSeek(file, 0, JX_FILE_SEEK_ORIGIN_BEGIN);

		json = (char*)JX_ALLOC(cfg->m_Allocator, fileSize + 1);
		if (!json) {
			jx_os_fileClose(file);
			return JX_ERROR_OUT_OF_MEMORY;
		}

		if (jx_os_fileRead(file, json, (uint32_t)fileSize) != (uint32_t)fileSize) {
			jx_os_fileClose(file);
			JX_FREE(cfg->m_Allocator, json);
			return JX_ERROR_FILE_READ;
		}
		json[fileSize] = '\0';
		jsonLen = fileSize;

		jx_os_fileClose(file);
	}

	if (!json) {
		return JX_ERROR_OPERATION_FAILED;
	}

	// Parse JSON
	struct json_parser p = { 0 };
	while (json_load(&p, json, (int32_t)jsonLen)) {
		struct json_token* newToks = (struct json_token*)JX_ALLOC(cfg->m_Allocator, sizeof(struct json_token) * p.cap);
		if (!newToks) {
			JX_FREE(cfg->m_Allocator, json);
			JX_FREE(cfg->m_Allocator, p.toks);
			return JX_ERROR_OUT_OF_MEMORY;
		}

		jx_memcpy(newToks, p.toks, sizeof(struct json_token) * p.cnt);
		JX_FREE(cfg->m_Allocator, p.toks);
		p.toks = newToks;
	}

	if (p.err != JSON_OK) {
		JX_FREE(cfg->m_Allocator, json);
		JX_FREE(cfg->m_Allocator, p.toks);
		return JX_ERROR_INVALID_ARGUMENT;
	}

	// Build tree
	struct json_token* child = p.toks;
	while (child < p.toks + p.cnt) {
		struct json_token* keyToken = &child[0];
		struct json_token* valToken = &child[1];

		_jx_config_insertToken(cfg, keyToken->str, keyToken->len, valToken);

		child = json_obj_next(child);
	}

	JX_FREE(cfg->m_Allocator, json);
	JX_FREE(cfg->m_Allocator, p.toks);

	return JX_ERROR_NONE;
}

static void _jx_config_writeNode(jx_string_buffer_t* sb, _jx_config_node* node, uint32_t indentationLevel)
{
	static const char indendationStr[] = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

	switch (node->m_Type) {
	case _JNODE_TYPE_OBJECT: {
		jx_strbuf_push(sb, "{\n", 2);

		const uint32_t numChildren = (uint32_t)jx_array_sizeu(node->m_Children);
		for (uint32_t i = 0; i < numChildren; ++i) {
			_jx_config_node* child = node->m_Children[i];

			jx_strbuf_push(sb, indendationStr, jx_min_u32(indentationLevel + 1, JX_COUNTOF(indendationStr)));
			jx_strbuf_push(sb, "\"", 1);
			jx_strbuf_push(sb, child->m_Key, UINT32_MAX);
			jx_strbuf_push(sb, "\": ", 3);
			_jx_config_writeNode(sb, child, indentationLevel + 1);
		}
		if (numChildren != 0) {
			jx_strbuf_pop(sb, 2); // Remove last comma
		}
		jx_strbuf_push(sb, "\n", 1);

		jx_strbuf_push(sb, indendationStr, jx_min_u32(indentationLevel, JX_COUNTOF(indendationStr)));
		jx_strbuf_push(sb, "},\n", 3);
	} break;
	case _JNODE_TYPE_ARRAY: {
		jx_strbuf_push(sb, "[\n", 2);

		const uint32_t numChildren = (uint32_t)jx_array_sizeu(node->m_Children);
		for (uint32_t i = 0; i < numChildren; ++i) {
			_jx_config_node* child = node->m_Children[i];
			jx_strbuf_push(sb, indendationStr, jx_min_u32(indentationLevel + 1, JX_COUNTOF(indendationStr)));
			_jx_config_writeNode(sb, child, indentationLevel + 1);
		}
		if (numChildren != 0) {
			jx_strbuf_pop(sb, 2); // Remove last comma
		}
		jx_strbuf_push(sb, "\n", 1);

		jx_strbuf_push(sb, indendationStr, jx_min_u32(indentationLevel, JX_COUNTOF(indendationStr)));
		jx_strbuf_push(sb, "],\n", 3);
	} break;
	case _JNODE_TYPE_NUMBER: {		
		char str[256];
		uint32_t strLen = jx_snprintf(str, JX_COUNTOF(str), "%.10g,\n", node->m_Number);
		jx_strbuf_push(sb, str, strLen);
	} break;
	case _JNODE_TYPE_STRING: {
		jx_strbuf_push(sb, "\"", 1);
		jx_strbuf_push(sb, node->m_String, UINT32_MAX);
		jx_strbuf_push(sb, "\",\n", 3);
	} break;
	case _JNODE_TYPE_BOOLEAN: {
		jx_strbuf_push(sb, node->m_Boolean ? "true,\n" : "false,\n", UINT32_MAX);
	} break;
	default:
		break;
	}
}

static int32_t _jx_config_saveJSON(jx_config_t* cfg, jx_file_base_dir baseDir, const char* path)
{
	jx_string_buffer_t* sb = jx_strbuf_create(cfg->m_Allocator);
	if (!sb) {
		return JX_ERROR_OUT_OF_MEMORY;
	}

	_jx_config_node* node = cfg->m_Root;
	_jx_config_writeNode(sb, node, 0);
	jx_strbuf_pop(sb, 2);

	uint32_t jsonLen = 0;
	const char* json = jx_strbuf_getString(sb, &jsonLen);

	jx_os_file_t* file = jx_os_fileOpenWrite(baseDir, path);
	if (!file) {
		return JX_ERROR_FILE_WRITE;
	}

	jx_os_fileWrite(file, json, jsonLen);
	jx_os_fileClose(file);

	jx_strbuf_destroy(sb);

	return JX_ERROR_NONE;
}

static int32_t _jx_config_beginObject(jx_config_t* cfg, const char* key)
{
	JX_CHECK(jx_strchr(key, '.') == NULL, "Invalid key", 0);

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));

	_jx_config_node* node = NULL;
	_jx_config_hash_node* nodePtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!nodePtr) {
		_jx_config_node* top = _jx_config_stackGetTop(cfg);
		if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
			return JX_ERROR_OPERATION_FAILED;
		}

		// Add new object child to current node.
		node = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!node) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*node = (_jx_config_node){
			.m_Type = _JNODE_TYPE_OBJECT,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_Children = jx_array_create(cfg->m_Allocator)
		};

		jx_array_push_back(top->m_Children, node);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node) {
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = node
		});
	} else {
		node = nodePtr->m_Node;
	}

	if (!node || node->m_Type != _JNODE_TYPE_OBJECT) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	_jx_config_stackPush(cfg, node);

	return JX_ERROR_NONE;
}

static int32_t _jx_config_endObject(jx_config_t* cfg)
{
	_jx_config_node* node = _jx_config_stackGetTop(cfg);
	if (node->m_Type != _JNODE_TYPE_OBJECT) {
		return JX_ERROR_OPERATION_FAILED;
	}

	_jx_config_stackPop(cfg);

	return JX_ERROR_NONE;
}

static int32_t _jx_config_beginArray(jx_config_t* cfg, const char* key)
{
	JX_CHECK(jx_strchr(key, '.') == NULL, "Invalid key", 0);

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));

	_jx_config_node* node = NULL;
	_jx_config_hash_node* nodePtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!nodePtr) {
		_jx_config_node* top = _jx_config_stackGetTop(cfg);
		if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
			return JX_ERROR_OPERATION_FAILED;
		}

		// Add new array child to current node.
		node = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!node) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*node = (_jx_config_node){
			.m_Type = _JNODE_TYPE_ARRAY,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_Children = jx_array_create(cfg->m_Allocator)
		};

		jx_array_push_back(top->m_Children, node);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node){
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = node
		});
	} else {
		node = nodePtr->m_Node;
	}

	if (!node || node->m_Type != _JNODE_TYPE_ARRAY) {
		return JX_ERROR_INVALID_ARGUMENT;
	}

	_jx_config_stackPush(cfg, node);

	return JX_ERROR_NONE;
}

static int32_t _jx_config_endArray(jx_config_t* cfg)
{
	_jx_config_node* node = _jx_config_stackGetTop(cfg);
	if (node->m_Type != _JNODE_TYPE_ARRAY) {
		return JX_ERROR_OPERATION_FAILED;
	}

	_jx_config_stackPop(cfg);

	return JX_ERROR_NONE;
}

static int32_t _jx_config_setBoolean(jx_config_t* cfg, const char* key, bool val)
{
	_jx_config_node* top = _jx_config_stackGetTop(cfg);
	if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
		return JX_ERROR_OPERATION_FAILED;
	}

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* childPtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!childPtr) {
		_jx_config_node* child = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!child) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*child = (_jx_config_node){
			.m_Type = _JNODE_TYPE_BOOLEAN,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_Boolean = val
		};

		jx_array_push_back(top->m_Children, child);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node){
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = child
		});
	} else {
		_jx_config_node* child = childPtr->m_Node;
		switch (child->m_Type) {
		case _JNODE_TYPE_OBJECT:
		case _JNODE_TYPE_ARRAY:
			JX_CHECK(false, "Cannot change inner node to leaf node.", 0);
			return JX_ERROR_OPERATION_FAILED;
		case _JNODE_TYPE_STRING:
			JX_FREE(cfg->m_Allocator, child->m_String);
			break;
		default:
			break;
		}

		child->m_Type = _JNODE_TYPE_BOOLEAN;
		child->m_Boolean = val;
	}

	return JX_ERROR_NONE;
}

static int32_t _jx_config_setInt32(jx_config_t* cfg, const char* key, int32_t val)
{
	_jx_config_node* top = _jx_config_stackGetTop(cfg);
	if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
		return JX_ERROR_OPERATION_FAILED;
	}

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* childPtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!childPtr) {
		_jx_config_node* child = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!child) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*child = (_jx_config_node){
			.m_Type = _JNODE_TYPE_NUMBER,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_Number = (double)val
		};

		jx_array_push_back(top->m_Children, child);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node){
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = child
		});
	} else {
		_jx_config_node* child = childPtr->m_Node;
		switch (child->m_Type) {
		case _JNODE_TYPE_OBJECT:
		case _JNODE_TYPE_ARRAY:
			JX_CHECK(false, "Cannot change inner node to leaf node.", 0);
			return JX_ERROR_OPERATION_FAILED;
		case _JNODE_TYPE_STRING:
			JX_FREE(cfg->m_Allocator, child->m_String);
			break;
		default:
			break;
		}

		child->m_Type = _JNODE_TYPE_NUMBER;
		child->m_Number = (double)val;
	}

	return JX_ERROR_NONE;
}

static int32_t _jx_config_setUint32(jx_config_t* cfg, const char* key, uint32_t val)
{
	_jx_config_node* top = _jx_config_stackGetTop(cfg);
	if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
		return JX_ERROR_OPERATION_FAILED;
	}

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* childPtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!childPtr) {
		_jx_config_node* child = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!child) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*child = (_jx_config_node){
			.m_Type = _JNODE_TYPE_NUMBER,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_Number = (double)val
		};

		jx_array_push_back(top->m_Children, child);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node){
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = child
		});
	} else {
		_jx_config_node* child = childPtr->m_Node;
		switch (child->m_Type) {
		case _JNODE_TYPE_OBJECT:
		case _JNODE_TYPE_ARRAY:
			JX_CHECK(false, "Cannot change inner node to leaf node.", 0);
			return JX_ERROR_OPERATION_FAILED;
		case _JNODE_TYPE_STRING:
			JX_FREE(cfg->m_Allocator, child->m_String);
			break;
		default:
			break;
		}

		child->m_Type = _JNODE_TYPE_NUMBER;
		child->m_Number = (double)val;
	}

	return JX_ERROR_NONE;
}

static int32_t _jx_config_setFloat(jx_config_t* cfg, const char* key, float val)
{
	_jx_config_node* top = _jx_config_stackGetTop(cfg);
	if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
		return JX_ERROR_OPERATION_FAILED;
	}

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* childPtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!childPtr) {
		_jx_config_node* child = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!child) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*child = (_jx_config_node){
			.m_Type = _JNODE_TYPE_NUMBER,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_Number = (double)val
		};

		jx_array_push_back(top->m_Children, child);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node){
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = child
		});
	} else {
		_jx_config_node* child = childPtr->m_Node;
		switch (child->m_Type) {
		case _JNODE_TYPE_OBJECT:
		case _JNODE_TYPE_ARRAY:
			JX_CHECK(false, "Cannot change inner node to leaf node.", 0);
			return JX_ERROR_OPERATION_FAILED;
		case _JNODE_TYPE_STRING:
			JX_FREE(cfg->m_Allocator, child->m_String);
			break;
		default:
			break;
		}

		child->m_Type = _JNODE_TYPE_NUMBER;
		child->m_Number = (double)val;
	}

	return JX_ERROR_NONE;
}

static int32_t _jx_config_setDouble(jx_config_t* cfg, const char* key, double val)
{
	_jx_config_node* top = _jx_config_stackGetTop(cfg);
	if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
		return JX_ERROR_OPERATION_FAILED;
	}

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* childPtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!childPtr) {
		_jx_config_node* child = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!child) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*child = (_jx_config_node){
			.m_Type = _JNODE_TYPE_NUMBER,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_Number = val
		};

		jx_array_push_back(top->m_Children, child);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node){
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = child
		});
	} else {
		_jx_config_node* child = childPtr->m_Node;
		switch (child->m_Type) {
		case _JNODE_TYPE_OBJECT:
		case _JNODE_TYPE_ARRAY:
			JX_CHECK(false, "Cannot change inner node to leaf node.", 0);
			return JX_ERROR_OPERATION_FAILED;
		case _JNODE_TYPE_STRING:
			JX_FREE(cfg->m_Allocator, child->m_String);
			break;
		default:
			break;
		}

		child->m_Type = _JNODE_TYPE_NUMBER;
		child->m_Number = val;
	}

	return JX_ERROR_NONE;
}

static int32_t _jx_config_setString(jx_config_t* cfg, const char* key, const char* val, uint32_t valLen)
{
	_jx_config_node* top = _jx_config_stackGetTop(cfg);
	if (!top || (top->m_Type != _JNODE_TYPE_OBJECT && top->m_Type != _JNODE_TYPE_ARRAY)) {
		return JX_ERROR_OPERATION_FAILED;
	}

	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* childPtr = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!childPtr) {
		_jx_config_node* child = (_jx_config_node*)JX_ALLOC(cfg->m_NodePoolAllocator, sizeof(_jx_config_node));
		if (!child) {
			return JX_ERROR_OUT_OF_MEMORY;
		}

		*child = (_jx_config_node){
			.m_Type = _JNODE_TYPE_STRING,
			.m_Key = jx_strdup(key, cfg->m_Allocator),
			.m_String = jx_strndup(val, valLen, cfg->m_Allocator)
		};

		jx_array_push_back(top->m_Children, child);

		jx_hashmapSet(cfg->m_Hashmap, &(_jx_config_hash_node){
			.m_FullPathKey = jx_strdup(fullPath, cfg->m_Allocator),
			.m_Node = child
		});
	} else {
		_jx_config_node* child = childPtr->m_Node;
		switch (child->m_Type) {
		case _JNODE_TYPE_OBJECT:
		case _JNODE_TYPE_ARRAY:
			JX_CHECK(false, "Cannot change inner node to leaf node.", 0);
			return JX_ERROR_OPERATION_FAILED;
		case _JNODE_TYPE_STRING:
			JX_FREE(cfg->m_Allocator, child->m_String);
			break;
		default:
			break;
		}

		child->m_Type = _JNODE_TYPE_STRING;
		child->m_String = jx_strndup(val, valLen, cfg->m_Allocator);
	}

	return JX_ERROR_NONE;
}

static bool _jx_config_getBoolean(jx_config_t* cfg, const char* key, bool defaultVal)
{
	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* hashNode = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){ .m_FullPathKey = fullPath });
	if (!hashNode) {
		return defaultVal;
	}

	_jx_config_node* node = hashNode->m_Node;
	switch (node->m_Type) {
	case _JNODE_TYPE_BOOLEAN:
		return node->m_Boolean;
	case _JNODE_TYPE_NUMBER:
		return node->m_Number != 0.0;
	case _JNODE_TYPE_STRING:
		return node->m_String[0] == 't' || node->m_String[0] == '1';
	default:
		break;
	}

	return defaultVal;
}

static int32_t _jx_config_getInt32(jx_config_t* cfg, const char* key, int32_t defaultVal)
{
	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* hashNode = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){.m_FullPathKey = fullPath });
	if (!hashNode) {
		return defaultVal;
	}

	_jx_config_node* node = hashNode->m_Node;
	switch (node->m_Type) {
	case _JNODE_TYPE_BOOLEAN:
		return node->m_Boolean ? 1 : 0;
	case _JNODE_TYPE_NUMBER:
		return (int32_t)node->m_Number;
	case _JNODE_TYPE_STRING:
		return (int32_t)jx_strto_int(node->m_String, UINT32_MAX, NULL, 0, defaultVal);
	default:
		break;
	}

	return defaultVal;
}

static uint32_t _jx_config_getUint32(jx_config_t* cfg, const char* key, uint32_t defaultVal)
{
	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* hashNode = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){.m_FullPathKey = fullPath });
	if (!hashNode) {
		return defaultVal;
	}

	_jx_config_node* node = hashNode->m_Node;
	switch (node->m_Type) {
	case _JNODE_TYPE_BOOLEAN:
		return node->m_Boolean ? 1u : 0u;
	case _JNODE_TYPE_NUMBER:
		return (uint32_t)node->m_Number;
	case _JNODE_TYPE_STRING:
		return (uint32_t)jx_strto_int(node->m_String, UINT32_MAX, NULL, 0, (int32_t)defaultVal);
	default:
		break;
	}

	return defaultVal;
}

static float _jx_config_getFloat(jx_config_t* cfg, const char* key, float defaultVal)
{
	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* hashNode = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){.m_FullPathKey = fullPath });
	if (!hashNode) {
		return defaultVal;
	}

	_jx_config_node* node = hashNode->m_Node;
	switch (node->m_Type) {
	case _JNODE_TYPE_BOOLEAN:
		return node->m_Boolean ? 1.0f : 0.0f;
	case _JNODE_TYPE_NUMBER:
		return (float)node->m_Number;
	case _JNODE_TYPE_STRING:
		return (float)jx_strto_double(node->m_String, UINT32_MAX, NULL, (double)defaultVal);
	default:
		break;
	}

	return defaultVal;
}

static double _jx_config_getDouble(jx_config_t* cfg, const char* key, double defaultVal)
{
	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* hashNode = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){.m_FullPathKey = fullPath });
	if (!hashNode) {
		return defaultVal;
	}

	_jx_config_node* node = hashNode->m_Node;
	switch (node->m_Type) {
	case _JNODE_TYPE_BOOLEAN:
		return node->m_Boolean ? 1.0 : 0.0;
	case _JNODE_TYPE_NUMBER:
		return node->m_Number;
	case _JNODE_TYPE_STRING:
		return jx_strto_double(node->m_String, UINT32_MAX, NULL, defaultVal);
	default:
		break;
	}

	return defaultVal;
}

static const char* _jx_config_getString(jx_config_t* cfg, const char* key, const char* defaultVal)
{
	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* hashNode = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){.m_FullPathKey = fullPath });
	if (!hashNode) {
		return defaultVal;
	}

	_jx_config_node* node = hashNode->m_Node;
	switch (node->m_Type) {
	case _JNODE_TYPE_STRING:
		return node->m_String;
	default:
		break;
	}

	return defaultVal;
}

static uint32_t _jx_config_getArraySize(jx_config_t* cfg, const char* key)
{
	char fullPath[512];
	_jx_config_buildFullPath(cfg, key, fullPath, JX_COUNTOF(fullPath));
	_jx_config_hash_node* hashNode = jx_hashmapGet(cfg->m_Hashmap, &(_jx_config_hash_node){.m_FullPathKey = fullPath });
	if (!hashNode) {
		// Key not found. If it was an array it would have no children.
		return 0;
	}

	_jx_config_node* node = hashNode->m_Node;
	return node->m_Type == _JNODE_TYPE_ARRAY
		? jx_array_sizeu(node->m_Children)
		: UINT32_MAX // Not an array. Error value
		;
}

static void _jx_config_buildFullPath(jx_config_t* cfg, const char* key, char* fullPath, uint32_t max)
{
	if (cfg->m_CurPath[0] == '\0') {
		jx_snprintf(fullPath, max, "%s", key);
	} else {
		_jx_config_node* top = _jx_config_stackGetTop(cfg);
		if (top->m_Type == _JNODE_TYPE_OBJECT) {
			jx_snprintf(fullPath, max, "%s.%s", cfg->m_CurPath, key);
		} else if (top->m_Type == _JNODE_TYPE_ARRAY) {
			jx_snprintf(fullPath, max, "%s%s", cfg->m_CurPath, key); // Keys are expected to be in the form "[%d]"
		} else {
			JX_CHECK(false, "Cannot build full path!", 0);
		}
	}
}

static void _jx_config_stackPush(jx_config_t* cfg, _jx_config_node* node)
{
	JX_CHECK(node->m_Type == _JNODE_TYPE_OBJECT || node->m_Type == _JNODE_TYPE_ARRAY, "Invalid node type", 0);

	if (cfg->m_StackTop == JX_COUNTOF(cfg->m_Stack) - 1) {
		JX_CHECK(false, "Stack overflow", 0);
		return;
	}

	if (cfg->m_CurPath[0] != '\0') {
		_jx_config_node* top = cfg->m_Stack[cfg->m_StackTop];
		if (top->m_Type == _JNODE_TYPE_OBJECT) {
			jx_strcat(cfg->m_CurPath, ".");
		} else if (top->m_Type == _JNODE_TYPE_ARRAY) {
			// Do nothing.
		} else {
			JX_CHECK(false, "Leaves cannot have children. Stack top is not an object or array.", 0);
		}
	}
	jx_strcat(cfg->m_CurPath, node->m_Key);

	cfg->m_StackTop++;
	cfg->m_Stack[cfg->m_StackTop] = node;
}

static void _jx_config_stackPop(jx_config_t* cfg)
{
	if (cfg->m_StackTop == 0) {
		JX_CHECK(false, "Stack underflow", 0);
		return;
	}

	cfg->m_StackTop--;

	_jx_config_node* top = cfg->m_Stack[cfg->m_StackTop];
	if (top->m_Type == _JNODE_TYPE_OBJECT) {
		const char* lastDot = jx_strrchr(cfg->m_CurPath, '.');
		if (lastDot) {
			*(char*)lastDot = '\0';
		} else {
			cfg->m_CurPath[0] = '\0';
		}
	} else if (top->m_Type == _JNODE_TYPE_ARRAY) {
		const char* openBracket = jx_strrchr(cfg->m_CurPath, '[');
		if (!openBracket) {
			JX_CHECK(false, "Expected open bracket.", 0);
			return;
		}
		*(char*)openBracket = '\0';
	}
}

static _jx_config_node* _jx_config_stackGetTop(jx_config_t* cfg)
{
	return cfg->m_Stack[cfg->m_StackTop];
}

static void _jx_config_destroyNode_r(jx_config_t* cfg, _jx_config_node* node)
{
	switch (node->m_Type) {
	case _JNODE_TYPE_ARRAY:
	case _JNODE_TYPE_OBJECT: {
		const uint32_t numChildren = (uint32_t)jx_array_sizeu(node->m_Children);
		for (uint32_t i = 0; i < numChildren; ++i) {
			_jx_config_destroyNode_r(cfg, node->m_Children[i]);
		}
		jx_array_free(node->m_Children);
	} break;
	case _JNODE_TYPE_STRING: {
		JX_FREE(cfg->m_Allocator, node->m_String);
	} break;
	default:
		break;
	}

	JX_FREE(cfg->m_Allocator, node->m_Key);
	JX_FREE(cfg->m_NodePoolAllocator, node);
}

static uint64_t _jx_config_hashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	JX_UNUSED(udata);
	const _jx_config_hash_node* node = (const _jx_config_hash_node*)item;
	return jx_hashFNV1a_cstr(node->m_FullPathKey, UINT32_MAX, seed0, seed1);
}

static int32_t _jx_config_compareCallback(const void* a, const void* b, void* udata)
{
	JX_UNUSED(udata);
	const _jx_config_hash_node* nodeA = (const _jx_config_hash_node*)a;
	const _jx_config_hash_node* nodeB = (const _jx_config_hash_node*)b;
	return jx_strcmp(nodeA->m_FullPathKey, nodeB->m_FullPathKey);
}

static void _jx_config_freeCallback(void* item, void* udata)
{
	jx_config_t* cfg = (jx_config_t*)udata;
	_jx_config_hash_node* node = (_jx_config_hash_node*)item;
	JX_FREE(cfg->m_Allocator, node->m_FullPathKey);
}

static void _jx_config_insertToken(jx_config_t* cfg, const char* keyPtr, uint32_t keyLen, struct json_token* val)
{
	char key[256];
	jx_memcpy(key, keyPtr, keyLen);
	key[keyLen] = '\0';

	switch (val->type) {
	case JSON_OBJECT: {
		_jx_config_beginObject(cfg, key);
		{
			struct json_token* child = json_obj_begin(val);
			for (int32_t i = 0; i < val->children && child; ++i) {
				struct json_token* keyToken = &child[0];
				struct json_token* valToken = &child[1];

				_jx_config_insertToken(cfg, keyToken->str, keyToken->len, valToken);

				child = json_obj_next(child);
			}
		}
		_jx_config_endObject(cfg);
	} break;
	case JSON_ARRAY: {
		_jx_config_beginArray(cfg, key);
		{
			struct json_token* child = json_array_begin(val);
			for (int32_t i = 0; i < val->children && child; ++i) {
				struct json_token* valToken = &child[0];

				char childKey[256];
				const uint32_t childKeyLen = jx_snprintf(childKey, JX_COUNTOF(childKey), "[%d]", i);
				_jx_config_insertToken(cfg, childKey, childKeyLen, valToken);

				child = json_array_next(child);
			}
		}
		_jx_config_endArray(cfg);
	} break;
	case JSON_NUMBER:
		_jx_config_setDouble(cfg, key, jx_strto_double(val->str, val->len, NULL, 0.0));
		break;
	case JSON_STRING:
		_jx_config_setString(cfg, key, val->str, val->len);
		break;
	case JSON_TRUE:
	case JSON_FALSE:
		_jx_config_setBoolean(cfg, key, val->type == JSON_TRUE);
		break;
	default:
		break;
	}
}
