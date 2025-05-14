#include "jir.h"
#include "jir_pass.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/hashmap.h>
#include <jlib/logger.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/os.h>
#include <jlib/string.h>

#define JX_IR_CONFIG_PRINT_ABSTRACT_POINTERS 0
#define JX_IR_CONFIG_APPLY_PASSES            1
#define JX_IR_CONFIG_FORCE_VALUE_NAMES       1

typedef struct jir_vm_stack_frame_t
{
	jx_ir_function_t* m_Func;
	jx_ir_basic_block_t* m_BB;
	jx_ir_instruction_t* m_Instr;
	jx_ir_instruction_t* m_CallerInstr;
	jx_hashmap_t* m_ValueMap;
	void** m_AllocaArr;
} jir_vm_stack_frame_t;

typedef struct jx_ir_context_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_LinearAllocator;
	jx_string_table_t* m_StringTable;
	jx_hashmap_t* m_TypeMap;
	jx_hashmap_t* m_ConstMap;
	jx_ir_type_t* m_BuildinTypes[JIR_TYPE_NUM_PRIMITIVE_TYPES];
	jx_ir_constant_t* m_ConstBool[2]; // { false, true }
	jx_ir_module_t* m_ModuleListHead;
	jx_ir_function_pass_t* m_OnFuncEndPassListHead;
	jx_ir_module_pass_t* m_OnModuleEndModulePassListHead;
	jx_ir_function_pass_t* m_OnModuleEndFuncPassListHead;
} jx_ir_context_t;

static jx_ir_instruction_t* jir_instrAlloc(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t opcode, uint32_t numOperands);

static bool jir_moduleCtor(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name);
static void jir_moduleDtor(jx_ir_context_t* ctx, jx_ir_module_t* mod);

static bool jir_valueCtor(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* type, jx_ir_value_kind kind, const char* name);
static void jir_valueDtor(jx_ir_context_t* ctx, jx_ir_value_t* val);

static void jir_useCtor(jx_ir_context_t* ctx, jx_ir_use_t* use, jx_ir_value_t* val, jx_ir_user_t* user);
static void jir_useDtor(jx_ir_context_t* ctx, jx_ir_use_t* use);
static void jir_useSetValue(jx_ir_context_t* ctx, jx_ir_use_t* use, jx_ir_value_t* val);

static bool jir_userCtor(jx_ir_context_t* ctx, jx_ir_user_t* user, jx_ir_type_t* type, jx_ir_value_kind kind, const char* name, uint32_t numOperands);
static void jir_userDtor(jx_ir_context_t* ctx, jx_ir_user_t* user);
static void jir_userAddOperand(jx_ir_context_t* ctx, jx_ir_user_t* user, jx_ir_value_t* operand);
static void jir_userRemoveOperand(jx_ir_context_t* ctx, jx_ir_user_t* user, uint32_t operandID);
static jx_ir_value_t* jir_userReplaceOperand(jx_ir_context_t* ctx, jx_ir_user_t* user, uint32_t operandID, jx_ir_value_t* newVal);

static bool jir_typeCtor(jx_ir_context_t* ctx, jx_ir_type_t* type, const char* name, jx_ir_type_kind kind, uint32_t flags);
static void jir_typeDtor(jx_ir_context_t* ctx, jx_ir_type_t* type);

static bool jir_constCtor(jx_ir_context_t* ctx, jx_ir_constant_t* c, jx_ir_type_t* type);
static void jir_constDtor(jx_ir_context_t* ctx, jx_ir_constant_t* c);

static bool jir_globalValCtor(jx_ir_context_t* ctx, jx_ir_global_value_t* gv, jx_ir_type_t* type, jx_ir_value_kind valKind, jx_ir_linkage_kind linkageKind, const char* name);
static void jir_globalValDtor(jx_ir_context_t* ctx, jx_ir_global_value_t* gv);

static bool jir_globalVarCtor(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv, jx_ir_type_t* type, bool isConstant, jx_ir_linkage_kind linkageKind, jx_ir_constant_t* initializer, const char* name);
static void jir_globalVarDtor(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv);

static bool jir_argCtor(jx_ir_context_t* ctx, jx_ir_argument_t* arg, jx_ir_type_t* type, const char* name, uint32_t id);
static void jir_argDtor(jx_ir_context_t* ctx, jx_ir_argument_t* arg);

static bool jir_bbCtor(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, const char* name);
static void jir_bbDtor(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb);
static bool jir_bbIncludesInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
static void jir_bbAddPred(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* pred);
static void jir_bbAddSucc(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* succ);
static void jir_bbRemovePred(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* pred);
static void jir_bbRemoveSucc(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* succ);
static bool jir_bbHasPred(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* pred);
static bool jir_bbHasSucc(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* succ);

static bool jir_funcCtor(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_type_t* type, jx_ir_linkage_kind linkageKind, const char* name);
static void jir_funcDtor(jx_ir_context_t* ctx, jx_ir_function_t* func);
static const char* jir_funcGenTempName(jx_ir_context_t* ctx, jx_ir_function_t* func);
static bool jir_funcIsExternal(jx_ir_context_t* ctx, jx_ir_function_t* func);
static void jir_funcApplyPasses(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_function_pass_t* passListHead);

static bool jir_instrCtor(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, jx_ir_type_t* type, uint32_t opcode, const char* name, uint32_t numOperands);
static void jir_instrDtor(jx_ir_context_t* ctx, jx_ir_instruction_t* instr);
static void jir_instrAddOperand(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, jx_ir_value_t* operand);
static void jir_instrRemoveOperand(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, uint32_t operandID);

static uint64_t jir_typeHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_typeCompareCallback(const void* a, const void* b, void* udata);
static uint64_t jir_constHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_constCompareCallback(const void* a, const void* b, void* udata);
static jx_ir_constant_t* jir_constGetIntSigned(jx_ir_context_t* ctx, jx_ir_type_t* type, int64_t val);
static jx_ir_constant_t* jir_constGetIntUnsigned(jx_ir_context_t* ctx, jx_ir_type_t* type, uint64_t val);
static jx_ir_constant_t* jir_constGetPointer(jx_ir_context_t* ctx, jx_ir_type_t* type, uintptr_t val);
static jx_ir_instruction_t* jir_instrBinaryOp(jx_ir_context_t* ctx, uint32_t op, jx_ir_value_t* operand1, jx_ir_value_t* operand2);
static jx_ir_instruction_t* jir_instrBinaryOpTyped(jx_ir_context_t* ctx, uint32_t op, jx_ir_value_t* operand1, jx_ir_value_t* operand2, jx_ir_type_t* type);

typedef struct jir_buildin_type_desc_t
{
	const char* m_Name;
	uint32_t m_Flags;
} jir_buildin_type_desc_t;

static const jir_buildin_type_desc_t kBuildinTypeDesc[] = {
	[JIR_TYPE_VOID]  = { "void", 0 },
	[JIR_TYPE_BOOL]  = { "bool", 0 },
	[JIR_TYPE_U8]    = { "u8", JIR_TYPE_FLAGS_IS_UNSIGNED_Msk },
	[JIR_TYPE_I8]    = { "i8", JIR_TYPE_FLAGS_IS_SIGNED_Msk },
	[JIR_TYPE_U16]   = { "u16", JIR_TYPE_FLAGS_IS_UNSIGNED_Msk },
	[JIR_TYPE_I16]   = { "i16", JIR_TYPE_FLAGS_IS_SIGNED_Msk },
	[JIR_TYPE_U32]   = { "u32", JIR_TYPE_FLAGS_IS_UNSIGNED_Msk },
	[JIR_TYPE_I32]   = { "i32", JIR_TYPE_FLAGS_IS_SIGNED_Msk },
	[JIR_TYPE_U64]   = { "u64", JIR_TYPE_FLAGS_IS_UNSIGNED_Msk },
	[JIR_TYPE_I64]   = { "i64", JIR_TYPE_FLAGS_IS_SIGNED_Msk },
	[JIR_TYPE_F32]   = { "f32", 0 },
	[JIR_TYPE_F64]   = { "f64", 0 },
	[JIR_TYPE_TYPE]  = { "type", 0 },
	[JIR_TYPE_LABEL] = { "label", 0 },
};
JX_STATIC_ASSERT(JX_COUNTOF(kBuildinTypeDesc) == JIR_TYPE_NUM_PRIMITIVE_TYPES, "Missing primitive type descriptor?");

jx_ir_context_t* jx_ir_createContext(jx_allocator_i* allocator)
{
	jx_ir_context_t* ctx = (jx_ir_context_t*)JX_ALLOC(allocator, sizeof(jx_ir_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_ir_context_t));
	ctx->m_Allocator = allocator;

	ctx->m_LinearAllocator = allocator_api->createLinearAllocator(256 << 10, allocator);
	if (!ctx->m_LinearAllocator) {
		jx_ir_destroyContext(ctx);
		return NULL;
	}

	ctx->m_StringTable = jx_strtable_create(allocator);
	if (!ctx->m_StringTable) {
		jx_ir_destroyContext(ctx);
		return NULL;
	}

	ctx->m_TypeMap = jx_hashmapCreate(ctx->m_Allocator, sizeof(jx_ir_type_t*), 64, 0, 0, jir_typeHashCallback, jir_typeCompareCallback, NULL, ctx);
	if (!ctx->m_TypeMap) {
		jx_ir_destroyContext(ctx);
		return NULL;
	}

	ctx->m_ConstMap = jx_hashmapCreate(ctx->m_Allocator, sizeof(jx_ir_constant_t*), 64, 0, 0, jir_constHashCallback, jir_constCompareCallback, NULL, ctx);
	if (!ctx->m_ConstMap) {
		jx_ir_destroyContext(ctx);
		return NULL;
	}

	// Initialize build-in types
	// NOTE: First allocate all types and then initialize them because the type ctor 
	// uses the JIR_TYPE_TYPE type even if it's not initialized yet.
	for (uint32_t iType = 0; iType < JIR_TYPE_NUM_PRIMITIVE_TYPES; ++iType) {
		ctx->m_BuildinTypes[iType] = (jx_ir_type_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_type_t));
		if (!ctx->m_BuildinTypes[iType]) {
			jx_ir_destroyContext(ctx);
			return NULL;
		}
	}

	for (uint32_t iType = 0;iType < JIR_TYPE_NUM_PRIMITIVE_TYPES;++iType) {
		jir_typeCtor(ctx, ctx->m_BuildinTypes[iType], kBuildinTypeDesc[iType].m_Name, (jx_ir_type_kind)iType, kBuildinTypeDesc[iType].m_Flags);
	}

	// Initialize bool constants
	for (uint32_t iConst = 0; iConst < 2; ++iConst) {
		jx_ir_constant_t* cb = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
		if (!cb) {
			jx_ir_destroyContext(ctx);
			return NULL;
		}

		jir_constCtor(ctx, cb, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_BOOL));
		cb->u.m_Bool = iConst == 1;
		ctx->m_ConstBool[iConst] = cb;
	}

	// Initialize function passes to be executed when funcEnd is called
	{
		jx_ir_function_pass_t head = { 0 };
		jx_ir_function_pass_t* cur = &head;

		// Canonicalize operands
		{
			jx_ir_function_pass_t* pass = (jx_ir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_function_pass_t));
			if (!pass) {
				jx_ir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_ir_function_pass_t));
			if (!jx_ir_funcPassCreate_canonicalizeOperands(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		// Single return block
		{
			jx_ir_function_pass_t* pass = (jx_ir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_function_pass_t));
			if (!pass) {
				jx_ir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_ir_function_pass_t));
			if (!jx_ir_funcPassCreate_singleRetBlock(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		// Simplify CFG
		{
			jx_ir_function_pass_t* pass = (jx_ir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_function_pass_t));
			if (!pass) {
				jx_ir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_ir_function_pass_t));
			if (!jx_ir_funcPassCreate_simplifyCFG(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		// Simple SSA
		{
			jx_ir_function_pass_t* pass = (jx_ir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_function_pass_t));
			if (!pass) {
				jx_ir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_ir_function_pass_t));
			if (!jx_ir_funcPassCreate_simpleSSA(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		ctx->m_OnFuncEndPassListHead = head.m_Next;
	}

	// Initialize function passes to be executed when moduleEnd is called
	{
		jx_ir_function_pass_t head = { 0 };
		jx_ir_function_pass_t* cur = &head;

		// Constant folding
		{
			jx_ir_function_pass_t* pass = (jx_ir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_function_pass_t));
			if (!pass) {
				jx_ir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_ir_function_pass_t));
			if (!jx_ir_funcPassCreate_constantFolding(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		// Peephole optimizations
		{
			jx_ir_function_pass_t* pass = (jx_ir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_function_pass_t));
			if (!pass) {
				jx_ir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_ir_function_pass_t));
			if (!jx_ir_funcPassCreate_peephole(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		ctx->m_OnModuleEndFuncPassListHead = head.m_Next;
	}

	// Initialize module passes to be executed when moduleEnd is called
	{
		jx_ir_module_pass_t head = { 0 };
		jx_ir_module_pass_t* cur = &head;

		// Inline functions
		{
			jx_ir_module_pass_t* pass = (jx_ir_module_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_module_pass_t));
			if (!pass) {
				jx_ir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_ir_module_pass_t));
			if (!jx_ir_modulePassCreate_inlineFuncs(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize module pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		ctx->m_OnModuleEndModulePassListHead = head.m_Next;
	}

	return ctx;
}

void jx_ir_destroyContext(jx_ir_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	// Free modules
	jx_ir_module_t* mod = ctx->m_ModuleListHead;
	while (mod) {
		jx_ir_module_t* next = mod->m_Next;

		// Free intrinsics
		for (uint32_t iFunc = 0; iFunc < JIR_INTRINSIC_COUNT; ++iFunc) {
			if (mod->m_IntrinsicFuncs[iFunc]) {
				jir_funcDtor(ctx, jx_ir_valueToFunc(mod->m_IntrinsicFuncs[iFunc]));
				mod->m_IntrinsicFuncs[iFunc] = NULL;
			}
		}

		jir_moduleDtor(ctx, mod);
		mod = next;
	}
	ctx->m_ModuleListHead = NULL;

	// Free bool constants
	for (uint32_t iConst = 0; iConst < 2; ++iConst) {
		if (ctx->m_ConstBool[iConst]) {
			jir_constDtor(ctx, ctx->m_ConstBool[iConst]);
			ctx->m_ConstBool[iConst] = NULL;
		}
	}

	// Free constant hashmap
	if (ctx->m_ConstMap) {
		uint32_t constID = 0;
		jx_ir_constant_t** constPtr = NULL;
		while (jx_hashmapIter(ctx->m_ConstMap, &constID, (void**)&constPtr)) {
			jir_constDtor(ctx, *constPtr);
		}

		jx_hashmapDestroy(ctx->m_ConstMap);
		ctx->m_ConstMap = NULL;
	}

	// Free type hashmap
	if (ctx->m_TypeMap) {
		uint32_t typeID = 0;
		jx_ir_type_t** typePtr = NULL;
		while (jx_hashmapIter(ctx->m_TypeMap, &typeID, (void**)&typePtr)) {
			jir_typeDtor(ctx, *typePtr);
		}

		jx_hashmapDestroy(ctx->m_TypeMap);
		ctx->m_TypeMap = NULL;
	}

	// Free build-in types
	for (uint32_t iType = 0; iType < JIR_TYPE_NUM_PRIMITIVE_TYPES; ++iType) {
		if (ctx->m_BuildinTypes[iType]) {
			jir_typeDtor(ctx, ctx->m_BuildinTypes[iType]);
			ctx->m_BuildinTypes[iType] = NULL;
		}
	}

	// Free function passes
	{
		jx_ir_function_pass_t* pass = ctx->m_OnFuncEndPassListHead;
		while (pass) {
			jx_ir_function_pass_t* next = pass->m_Next;
			pass->destroy(pass->m_Inst, allocator);
			JX_FREE(allocator, pass);

			pass = next;
		}
	}
	{
		jx_ir_function_pass_t* pass = ctx->m_OnModuleEndFuncPassListHead;
		while (pass) {
			jx_ir_function_pass_t* next = pass->m_Next;
			pass->destroy(pass->m_Inst, allocator);
			JX_FREE(allocator, pass);

			pass = next;
		}
	}

	// Free module passes
	{
		jx_ir_module_pass_t* pass = ctx->m_OnModuleEndModulePassListHead;
		while (pass) {
			jx_ir_module_pass_t* next = pass->m_Next;
			pass->destroy(pass->m_Inst, allocator);
			JX_FREE(allocator, pass);

			pass = next;
		}
	}

	// Free string table
	if (ctx->m_StringTable) {
		jx_strtable_destroy(ctx->m_StringTable);
		ctx->m_StringTable = NULL;
	}

	// Free linear allocator
	if (ctx->m_LinearAllocator) {
		allocator_api->destroyLinearAllocator(ctx->m_LinearAllocator);
		ctx->m_LinearAllocator = NULL;
	}

	// Free the context itself
	JX_FREE(allocator, ctx);
}

void jx_ir_print(jx_ir_context_t* ctx, jx_string_buffer_t* sb)
{
	jx_ir_module_t* mod = ctx->m_ModuleListHead;
	while (mod) {
		jx_strbuf_printf(sb, "module \"%s\"\n", mod->m_Name);
		jx_ir_modulePrint(ctx, mod, sb);
		jx_strbuf_pushCStr(sb, "\n");

		mod = mod->m_Next;
	}
}

jx_ir_module_t* jx_ir_getModule(jx_ir_context_t* ctx, uint32_t id)
{
	jx_ir_module_t* mod = ctx->m_ModuleListHead;
	while (mod) {
		if (id == 0) {
			return mod;
		}

		--id;
		mod = mod->m_Next;
	}

	return NULL;
}

jx_ir_module_t* jx_ir_moduleBegin(jx_ir_context_t* ctx, const char* name)
{
	jx_ir_module_t* mod = (jx_ir_module_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_module_t));
	if (!mod) {
		return NULL;
	}

	if (!jir_moduleCtor(ctx, mod, name)) {
		return NULL;
	}

	// Initialize intrinsic function
	{
		// memcpy.p0.p0.i32
		{
			jx_ir_type_t* args[] = {
				jx_ir_typeGetPointer(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID)),
				jx_ir_typeGetPointer(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID)),
				jx_ir_typeGetPrimitive(ctx, JIR_TYPE_I32)
			};
			jx_ir_type_t* memcpyType = jx_ir_typeGetFunction(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID), JX_COUNTOF(args), args, false);
			jx_ir_global_value_t* gv = jx_ir_moduleDeclareGlobalVal(ctx, mod, "jir.memcpy.p0.p0.i32", memcpyType, JIR_LINKAGE_EXTERNAL);
			if (!gv) {
				return NULL;
			}
			mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMCPY_P0_P0_I32] = jx_ir_globalValToValue(gv);
		}

		// memcpy.p0.p0.i64
		{
			jx_ir_type_t* args[] = {
				jx_ir_typeGetPointer(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID)),
				jx_ir_typeGetPointer(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID)),
				jx_ir_typeGetPrimitive(ctx, JIR_TYPE_I64)
			};
			jx_ir_type_t* memcpyType = jx_ir_typeGetFunction(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID), JX_COUNTOF(args), args, false);
			jx_ir_global_value_t* gv = jx_ir_moduleDeclareGlobalVal(ctx, mod, "jir.memcpy.p0.p0.i64", memcpyType, JIR_LINKAGE_EXTERNAL);
			if (!gv) {
				return NULL;
			}
			mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMCPY_P0_P0_I64] = jx_ir_globalValToValue(gv);
		}

		// memset.p0.i32
		{
			jx_ir_type_t* args[] = {
				jx_ir_typeGetPointer(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID)),
				jx_ir_typeGetPrimitive(ctx, JIR_TYPE_I8),
				jx_ir_typeGetPrimitive(ctx, JIR_TYPE_I32)
			};
			jx_ir_type_t* memcpyType = jx_ir_typeGetFunction(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID), JX_COUNTOF(args), args, false);
			jx_ir_global_value_t* gv = jx_ir_moduleDeclareGlobalVal(ctx, mod, "jir.memset.p0.i32", memcpyType, JIR_LINKAGE_EXTERNAL);
			if (!gv) {
				return NULL;
			}
			mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMSET_P0_I32] = jx_ir_globalValToValue(gv);
		}

		// memset.p0.i64
		{
			jx_ir_type_t* args[] = {
				jx_ir_typeGetPointer(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID)),
				jx_ir_typeGetPrimitive(ctx, JIR_TYPE_I8),
				jx_ir_typeGetPrimitive(ctx, JIR_TYPE_I64)
			};
			jx_ir_type_t* memcpyType = jx_ir_typeGetFunction(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_VOID), JX_COUNTOF(args), args, false);
			jx_ir_global_value_t* gv = jx_ir_moduleDeclareGlobalVal(ctx, mod, "jir.memset.p0.i64", memcpyType, JIR_LINKAGE_EXTERNAL);
			if (!gv) {
				return NULL;
			}
			mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMSET_P0_I64] = jx_ir_globalValToValue(gv);
		}
	}

	// Insert into context's module list
	// NOTE: Insert new modules at the head of the list
	if (ctx->m_ModuleListHead) {
		ctx->m_ModuleListHead->m_Prev = mod;
		mod->m_Next = ctx->m_ModuleListHead;
	}
	ctx->m_ModuleListHead = mod;

	return mod;
}

void jx_ir_moduleEnd(jx_ir_context_t* ctx, jx_ir_module_t* mod)
{
	jx_ir_module_pass_t* pass = ctx->m_OnModuleEndModulePassListHead;
	while (pass) {
		bool moduleModified = pass->run(pass->m_Inst, ctx, mod);
		JX_UNUSED(moduleModified);
		pass = pass->m_Next;
	}

	jx_ir_function_t* func = mod->m_FunctionListHead;
	while (func) {
		if (!jir_funcIsExternal(ctx, func)) {
			jir_funcApplyPasses(ctx, func, ctx->m_OnModuleEndFuncPassListHead);
		}

		func = func->m_Next;
	}
}

jx_ir_global_value_t* jx_ir_moduleDeclareGlobalVal(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name, jx_ir_type_t* type, jx_ir_linkage_kind linkageKind)
{
	jx_ir_global_value_t* existingDecl = jx_ir_moduleGetGlobalVal(ctx, mod, name);
	if (existingDecl) {
		return type == jx_ir_globalValToValue(existingDecl)->m_Type
			? existingDecl
			: NULL
			;
	}

	jx_ir_global_value_t* decl = NULL;

	jx_ir_type_function_t* funcType = jx_ir_typeToFunction(type);
	if (funcType) {
		jx_ir_function_t* func = (jx_ir_function_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_function_t));
		if (!func) {
			return NULL;
		}

		if (!jir_funcCtor(ctx, func, type, linkageKind, name)) {
			return NULL;
		}

		if (!mod->m_FunctionListHead) {
			mod->m_FunctionListHead = func;
		} else {
			jx_ir_function_t* tail = mod->m_FunctionListHead;
			while (tail->m_Next) {
				tail = tail->m_Next;
			}
			tail->m_Next = func;
			func->m_Prev = tail;
		}

		func->super.m_ParentModule = mod;

		decl = &func->super;
	} else {
		jx_ir_global_variable_t* gv = (jx_ir_global_variable_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_global_variable_t));
		if (!gv) {
			return NULL;
		}

		if (!jir_globalVarCtor(ctx, gv, type, false, linkageKind, NULL, name)) {
			return NULL;
		}

		if (!mod->m_GlobalVarListHead) {
			mod->m_GlobalVarListHead = gv;
		} else {
			jx_ir_global_variable_t* tail = mod->m_GlobalVarListHead;
			while (tail->m_Next) {
				tail = tail->m_Next;
			}
			tail->m_Next = gv;
			gv->m_Prev = tail;
		}

		gv->super.m_ParentModule = mod;

		decl = &gv->super;
	}

	return decl;
}

jx_ir_function_t* jx_ir_moduleGetFunc(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name)
{
	jx_ir_function_t* func = mod->m_FunctionListHead;
	while (func) {
		jx_ir_value_t* funcVal = jx_ir_funcToValue(func);
		if (funcVal->m_Name && !jx_strcmp(funcVal->m_Name, name)) {
			break;
		}

		func = func->m_Next;
	}

	return func;
}

jx_ir_global_variable_t* jx_ir_moduleGetGlobalVar(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name)
{
	jx_ir_global_variable_t* gv = mod->m_GlobalVarListHead;
	while (gv) {
		jx_ir_value_t* gvVal = jx_ir_globalVarToValue(gv);
		if (gvVal->m_Name && !jx_strcmp(gvVal->m_Name, name)) {
			break;
		}

		gv = gv->m_Next;
	}

	return gv;
}

jx_ir_global_value_t* jx_ir_moduleGetGlobalVal(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name)
{
	jx_ir_global_value_t* val = NULL;
	jx_ir_global_variable_t* gv = jx_ir_moduleGetGlobalVar(ctx, mod, name);
	if (!gv) {
		jx_ir_function_t* func = jx_ir_moduleGetFunc(ctx, mod, name);
		if (!func) {
			return NULL;
		}

		val = &func->super;
	} else {
		val = &gv->super;
	}

	return val;
}

void jx_ir_modulePrint(jx_ir_context_t* ctx, jx_ir_module_t* mod, jx_string_buffer_t* sb)
{
	jx_ir_global_variable_t* gv = mod->m_GlobalVarListHead;
	while (gv) {
		jx_ir_globalVarPrint(ctx, gv, sb);
		jx_strbuf_pushCStr(sb, "\n");
		gv = gv->m_Next;
	}

	jx_ir_function_t* func = mod->m_FunctionListHead;
	while (func) {
		jx_ir_funcPrint(ctx, func, sb);
		jx_strbuf_pushCStr(sb, "\n");
		func = func->m_Next;
	}
}

bool jx_ir_funcBegin(jx_ir_context_t* ctx, jx_ir_function_t* func, uint32_t flags)
{
	JX_CHECK(jx_ir_funcGetType(ctx, func), "Expected function type!");
	func->m_Flags = flags;
	return true;
}

void jx_ir_funcEnd(jx_ir_context_t* ctx, jx_ir_function_t* func)
{
#if JX_IR_CONFIG_APPLY_PASSES
	if (!jir_funcIsExternal(ctx, func)) {
		jir_funcApplyPasses(ctx, func, ctx->m_OnFuncEndPassListHead);
	}
#endif
}

jx_ir_argument_t* jx_ir_funcGetArgument(jx_ir_context_t* ctx, jx_ir_function_t* func, uint32_t argID)
{
	jx_ir_argument_t* arg = func->m_ArgListHead;
	while (arg && argID > 0) {
		arg = arg->m_Next;
		--argID;
	}

	return arg;
}

void jx_ir_funcAppendBasicBlock(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_basic_block_t* bb)
{
	JX_CHECK(!bb->m_ParentFunc, "Basic block already part of a function's body.");

	jx_ir_basic_block_t* tail = func->m_BasicBlockListHead;
	if (!tail) {
		func->m_BasicBlockListHead = bb;
	} else {
		while (tail->m_Next) {
			tail = tail->m_Next;
		}

		// Check if the last basic block ends with a terminator instruction.
		// If it doesn't, add an unconditional branch to the new basic block.
		jx_ir_instruction_t* lastInstr = jx_ir_bbGetLastInstr(ctx, tail);
		if (!lastInstr || !jx_ir_opcodeIsTerminator(lastInstr->m_OpCode)) {
			jx_ir_bbAppendInstr(ctx, tail, jx_ir_instrBranch(ctx, bb));
		}

		tail->m_Next = bb;
		bb->m_Prev = tail;
	}

	bb->m_ParentFunc = func;

#if JX_IR_CONFIG_FORCE_VALUE_NAMES
	jx_ir_value_t* bbVal = jx_ir_bbToValue(bb);
	if (!bbVal->m_Name) {
		jx_ir_valueSetName(ctx, bbVal, jir_funcGenTempName(ctx, func));
	}

	// Make sure all instructions and operands have a name at this point.
	jx_ir_instruction_t* instr = bb->m_InstrListHead;
	while (instr) {
		jx_ir_value_t* instrVal = jx_ir_instrToValue(instr);
		if (!instrVal->m_Name) {
			jx_ir_valueSetName(ctx, instrVal, jir_funcGenTempName(ctx, func));
		}

		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			jx_ir_value_t* operandVal = instr->super.m_OperandArr[iOperand]->m_Value;
			if (!operandVal->m_Name && (operandVal->m_Kind == JIR_VALUE_BASIC_BLOCK || operandVal->m_Kind == JIR_VALUE_INSTRUCTION)) {
				jx_ir_valueSetName(ctx, operandVal, jir_funcGenTempName(ctx, func));
			}
		}

		instr = instr->m_Next;
	}
#endif
}

// NOTE: Does not (currently) perform any meaningful checks on whether the basic block
// can be safely removed or not. It's up to the caller to make sure it's safe to remove the 
// block from the function.
bool jx_ir_funcRemoveBasicBlock(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_basic_block_t* bb)
{
	if (bb->m_ParentFunc != func) {
		JX_CHECK(false, "Basic block not part of this function!");
		return false;
	}

	if (func->m_BasicBlockListHead == bb) {
		JX_CHECK(false, "Cannot remove entry basic block! Or should we can?");
		return false;
	}
	
	// TODO: Should I search the basic block list to make sure this is actually
	// part of the list?
	if (bb->m_Next) {
		bb->m_Next->m_Prev = bb->m_Prev;
	}
	if (bb->m_Prev) {
		bb->m_Prev->m_Next = bb->m_Next;
	}

	bb->m_Prev = NULL;
	bb->m_Next = NULL;
	bb->m_ParentFunc = NULL;

	return true;
}

jx_ir_type_function_t* jx_ir_funcGetType(jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	jx_ir_value_t* funcVal = jx_ir_funcToValue(func);
	JX_CHECK(funcVal, "Should not happen! Invalid cast from value to function maybe?");

	jx_ir_type_pointer_t* funcPtrType = jx_ir_typeToPointer(funcVal->m_Type);
	JX_CHECK(funcPtrType, "Function's type expected to be a pointer to a function type.");

	jx_ir_type_function_t* funcType = jx_ir_typeToFunction(funcPtrType->m_BaseType);
	JX_CHECK(funcType, "Function's type expected to be a pointer to a function type.");

	return funcType;
}

void jx_ir_funcPrint(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_string_buffer_t* sb)
{
	jx_ir_type_function_t* funcType = jx_ir_funcGetType(ctx, func);

	const bool hasBody = !jir_funcIsExternal(ctx, func);

	if (!hasBody) {
		jx_strbuf_pushCStr(sb, "declare");
	} else {
		jx_strbuf_pushCStr(sb, "define");
	}

	jx_strbuf_pushCStr(sb, " ");
	jx_strbuf_pushCStr(sb, kLinkageName[func->super.m_LinkageKind]);
	jx_strbuf_pushCStr(sb, " ");

	jx_ir_typePrint(ctx, funcType->m_RetType, sb);
	jx_strbuf_pushCStr(sb, " ");

	jx_strbuf_printf(sb, "@%s(", jx_ir_funcToValue(func)->m_Name);

	jx_ir_argument_t* arg = func->m_ArgListHead;
	bool first = true;
	while (arg) {
		if (!first) {
			jx_strbuf_pushCStr(sb, ", ");
		}
		first = false;

		jx_ir_value_t* argValue = jx_ir_argToValue(arg);
		jx_ir_typePrint(ctx, argValue->m_Type, sb);

		if (!argValue->m_Name) {
			argValue->m_Name = jir_funcGenTempName(ctx, func);
		}
		jx_strbuf_printf(sb, " %%%s", argValue->m_Name);

		arg = arg->m_Next;
	}

	if (funcType->m_IsVarArg) {
		if (!first) {
			jx_strbuf_pushCStr(sb, ", ");
		}
		jx_strbuf_pushCStr(sb, "...");
	}

	if (!hasBody) {
		jx_strbuf_pushCStr(sb, ");\n");
		return;
	}

	jx_strbuf_pushCStr(sb, ") {\n");

	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_ir_bbPrint(ctx, bb, sb);

		bb = bb->m_Next;
	}

	jx_strbuf_pushCStr(sb, "}\n");
}

// Checks if the specified function's IR and CFG are well-formed.
bool jx_ir_funcCheck(jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_ir_instruction_t* lastInstr = bb->m_InstrListHead;
		if (!lastInstr) {
			JX_CHECK(false, "Empty basic block found!");
			return false;
		}

		// Make sure: 
		// - there are no terminator instructions inside the block
		// - all phi instructions appear at the start of the block
		// - all phi instructions have the correct number of arguments (num predecessors * 2)
		// - all phi instructions have one value for each predecessor
		bool prevIsPhi = lastInstr->m_OpCode == JIR_OP_PHI;
		while (lastInstr->m_Next) {
			if (lastInstr->m_ParentBB != bb) {
				JX_CHECK(false, "Instruction not part of current basic block!");
				return false;
			}

			if (jx_ir_opcodeIsTerminator(lastInstr->m_OpCode)) {
				JX_CHECK(false, "Terminator instruction found in the middle of a basic block!");
				return false;
			}

			if (lastInstr->m_OpCode == JIR_OP_PHI) {
				if (!prevIsPhi) {
					JX_CHECK(false, "Phi instruction found in the middle of a basic block!");
					return false;
				}

				const uint32_t numOperands = (uint32_t)jx_array_sizeu(lastInstr->super.m_OperandArr);
				const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
				if (numOperands != numPred * 2) {
					JX_CHECK(false, "Invalid number of phi operands.");
					return false;
				}

				for (uint32_t iPred = 0; iPred < numPred; ++iPred) {
					jx_ir_value_t* predVal = jx_ir_instrPhiHasValue(ctx, lastInstr, bb->m_PredArr[iPred]);
					if (!predVal) {
						JX_CHECK(false, "Phi instruction missing operand for predecessor.");
						return false;
					}

					// Make sure value from predecessor is valid.
					if (predVal->m_Kind == JIR_VALUE_INSTRUCTION) {
						jx_ir_instruction_t* predValInstr = jx_ir_valueToInstr(predVal);
						if (!jx_ir_instrCheck(ctx, predValInstr)) {
							JX_CHECK(false, "Phi instruction value is invalid!");
							return false;
						}

						// NOTE: Removed because it seems to be valid as long as the phis do not
						// have cyclic dependencies. Because phis are assumed to be evaluated 
						// simultaneously, having a phi reference another phi means that the second
						// phi will get the value from the previous evaluation of the first phi.
#if 0
						// If it's a phi instruction, make sure it comes from another basic block.
						if (predValInstr->m_OpCode == JIR_OP_PHI) {
							if (predValInstr->m_ParentBB == lastInstr->m_ParentBB) {
								JX_CHECK(false, "Phi instruction has a phi operand from the same basic block!");
								return false;
							}
						}
#endif
					}
				}
			} else {
				prevIsPhi = false;
			}

			if (!jx_ir_instrCheck(ctx, lastInstr)) {
				// Should have already asserted.
				return false;
			}

			lastInstr = lastInstr->m_Next;
		}

		if (lastInstr->m_ParentBB != bb) {
			JX_CHECK(false, "Instruction not part of current basic block!");
			return false;
		}

		// Make sure the last instruction is a terminator.
		if (!jx_ir_opcodeIsTerminator(lastInstr->m_OpCode)) {
			JX_CHECK(false, "Last basic block instruction expected to be a terminator instruction!");
			return false;
		}

		if (!jx_ir_instrCheck(ctx, lastInstr)) {
			// Should have already asserted.
			return false;
		}

		// Make sure successor/predecessor links are correct.
		if (lastInstr->m_OpCode == JIR_OP_BRANCH) {
			const uint32_t numOperands = (uint32_t)jx_array_sizeu(lastInstr->super.m_OperandArr);
			if (numOperands == 1) {
				// Unconditional branch.
				jx_ir_basic_block_t* targetBB = jx_ir_valueToBasicBlock(lastInstr->super.m_OperandArr[0]->m_Value);
				if (!targetBB) {
					JX_CHECK(false, "Unconditional branch target expected to be a basic block value!");
					return false;
				}

				const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
				if (numSucc != 1) {
					JX_CHECK(false, "Basic block with unconditional branch expected to have 1 successor!");
					return false;
				}

				if (bb->m_SuccArr[0] != targetBB) {
					JX_CHECK(false, "Invalid basic block successor.");
					return false;
				}

				if (!jir_bbHasPred(ctx, targetBB, bb)) {
					JX_CHECK(false, "Basic block not found in successor's predecessor list!");
					return false;
				}
			} else if (numOperands == 3) {
				// Conditional branch
				jx_ir_basic_block_t* trueBB = jx_ir_valueToBasicBlock(lastInstr->super.m_OperandArr[1]->m_Value);
				if (!trueBB) {
					JX_CHECK(false, "Conditional branch true target expected to be a basic block value!");
					return false;
				}

				jx_ir_basic_block_t* falseBB = jx_ir_valueToBasicBlock(lastInstr->super.m_OperandArr[2]->m_Value);
				if (!falseBB) {
					JX_CHECK(false, "Conditional branch false target expected to be a basic block value!");
					return false;
				}

				const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
				if (numSucc != 2) {
					JX_CHECK(false, "Basic block with conditional branch expected to have 2 successors!");
					return false;
				}

				if (!jir_bbHasSucc(ctx, bb, trueBB) || !jir_bbHasSucc(ctx, bb, falseBB)) {
					JX_CHECK(false, "Invalid basic block successors.");
					return false;
				}

				if (!jir_bbHasPred(ctx, trueBB, bb) || !jir_bbHasPred(ctx, falseBB, bb)) {
					JX_CHECK(false, "Basic block not found in successor's predecessor list!");
					return false;
				}
			} else {
				JX_CHECK(false, "Unknown branch instruction");
				return false;
			}
		} else if (lastInstr->m_OpCode == JIR_OP_RET) {
			const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
			if (numSucc) {
				JX_CHECK(false, "No successors expected from a basic block with ret terminator!");
				return false;
			}
		} else {
			JX_CHECK(false, "Unknown terminator instruction.");
			return false;
		}

		bb = bb->m_Next;
	}
	
	return true;
}

bool jx_ir_globalVarDefine(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv, bool isConst, jx_ir_constant_t* initializer)
{
	gv->m_IsConstantGlobal = isConst;
	
	if (initializer) {
		jir_userAddOperand(ctx, jx_ir_globalVarToUser(gv), jx_ir_constToValue(initializer));
	}

	return true;
}

void jx_ir_globalVarPrint(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv, jx_string_buffer_t* sb)
{
	jx_ir_value_t* gvValue = jx_ir_globalVarToValue(gv);
	jx_ir_type_pointer_t* gvTypePtr = jx_ir_typeToPointer(gvValue->m_Type);

	jx_strbuf_printf(sb, "@%s = %sglobal", gvValue->m_Name, gv->super.m_LinkageKind == JIR_LINKAGE_INTERNAL ? "internal " : "");
	jx_strbuf_pushCStr(sb, " ");
	jx_ir_typePrint(ctx, gvTypePtr->m_BaseType, sb);
	jx_strbuf_pushCStr(sb, " ");
	jx_ir_constPrint(ctx, jx_ir_valueToConst(gv->super.super.m_OperandArr[0]->m_Value), sb);
	jx_strbuf_pushCStr(sb, "\n");
}

jx_ir_basic_block_t* jx_ir_bbAlloc(jx_ir_context_t* ctx, const char* name)
{
	jx_ir_basic_block_t* bb = (jx_ir_basic_block_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_basic_block_t));
	if (!bb) {
		return NULL;
	}

	if (!jir_bbCtor(ctx, bb, name)) {
		return NULL;
	}

	return bb;
}

void jx_ir_bbFree(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb)
{
	JX_CHECK(!bb->m_ParentFunc, "Basic block is part of a function!");
	jir_bbDtor(ctx, bb);
}

jx_ir_instruction_t* jx_ir_bbGetLastInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb)
{
	jx_ir_instruction_t* tail = bb->m_InstrListHead;
	if (!tail) {
		return NULL;
	}

	while (tail->m_Next) {
		tail = tail->m_Next;
	}

	return tail;
}

bool jx_ir_bbAppendInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr)
{
	if (!instr) {
		JX_CHECK(false, "NULL instruction passed to basic block!");
		return false;
	}

	JX_CHECK(!instr->m_ParentBB, "Instruction already in a basic block!");

	jx_ir_instruction_t* tail = bb->m_InstrListHead;
	if (!tail) {
		bb->m_InstrListHead = instr;
	} else {
		while (tail->m_Next) {
			tail = tail->m_Next;
		}
		
		JX_CHECK(!jx_ir_opcodeIsTerminator(instr->m_OpCode) || !jx_ir_opcodeIsTerminator(tail->m_OpCode), "Basic block already has a terminator instruction!");

		tail->m_Next = instr;
		instr->m_Prev = tail;
	}

	instr->m_ParentBB = bb;

	// If this is a branch instruction, update successor/predecessor links
	if (instr->m_OpCode == JIR_OP_BRANCH) {
		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		if (numOperands == 1) {
			// Unconditional branch
			jx_ir_basic_block_t* targetBB = jx_ir_valueToBasicBlock(instr->super.m_OperandArr[0]->m_Value);
			JX_CHECK(targetBB, "Unconditional branch 1st operand expected to be a basic block.");
			jir_bbAddPred(ctx, targetBB, bb);
			jir_bbAddSucc(ctx, bb, targetBB);
		} else if (numOperands == 3) {
			// Conditional branch
			jx_ir_basic_block_t* trueBB = jx_ir_valueToBasicBlock(instr->super.m_OperandArr[1]->m_Value);
			jx_ir_basic_block_t* falseBB = jx_ir_valueToBasicBlock(instr->super.m_OperandArr[2]->m_Value);
			JX_CHECK(trueBB && falseBB, "Conditional branch 2nd and 3rd operands expected to be basic blocks.");
			jir_bbAddPred(ctx, trueBB, bb);
			jir_bbAddPred(ctx, falseBB, bb);
			jir_bbAddSucc(ctx, bb, trueBB);
			jir_bbAddSucc(ctx, bb, falseBB);
		} else {
			JX_CHECK(false, "Unknown branch instruction!");
		}
	}

#if JX_IR_CONFIG_FORCE_VALUE_NAMES
	if (bb->m_ParentFunc) {
		jx_ir_function_t* func = bb->m_ParentFunc;

		jx_ir_value_t* instrVal = jx_ir_instrToValue(instr);
		if (!instrVal->m_Name) {
			jx_ir_valueSetName(ctx, instrVal, jir_funcGenTempName(ctx, func));
		}

		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			jx_ir_value_t* operandVal = instr->super.m_OperandArr[iOperand]->m_Value;
			if (!operandVal->m_Name && (operandVal->m_Kind == JIR_VALUE_BASIC_BLOCK || operandVal->m_Kind == JIR_VALUE_INSTRUCTION)) {
				jx_ir_valueSetName(ctx, operandVal, jir_funcGenTempName(ctx, func));
			}
		}
	}
#endif

	return true;
}

bool jx_ir_bbPrependInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr)
{
	if (!instr) {
		JX_CHECK(false, "NULL instruction passed to basic block!");
		return false;
	}

	JX_CHECK(!instr->m_ParentBB, "Instruction already in a basic block!");

	instr->m_Next = bb->m_InstrListHead;
	bb->m_InstrListHead->m_Prev = instr;
	bb->m_InstrListHead = instr;

	instr->m_ParentBB = bb;

#if JX_IR_CONFIG_FORCE_VALUE_NAMES
	if (bb->m_ParentFunc) {
		jx_ir_function_t* func = bb->m_ParentFunc;

		jx_ir_value_t* instrVal = jx_ir_instrToValue(instr);
		if (!instrVal->m_Name) {
			jx_ir_valueSetName(ctx, instrVal, jir_funcGenTempName(ctx, func));
		}

		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			jx_ir_value_t* operandVal = instr->super.m_OperandArr[iOperand]->m_Value;
			if (!operandVal->m_Name && (operandVal->m_Kind == JIR_VALUE_BASIC_BLOCK || operandVal->m_Kind == JIR_VALUE_INSTRUCTION)) {
				jx_ir_valueSetName(ctx, operandVal, jir_funcGenTempName(ctx, func));
			}
		}
	}
#endif

	return true;
}

bool jx_ir_bbInsertInstrBefore(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* anchor, jx_ir_instruction_t* instr)
{
	if (!instr) {
		JX_CHECK(false, "NULL instruction passed to basic block!");
		return false;
	}

	JX_UNUSED(ctx);
	JX_CHECK(!instr->m_ParentBB && !instr->m_Prev && !instr->m_Next, "Instruction already part of a basic block?");
	JX_CHECK(anchor->m_ParentBB == bb, "Anchor instruction not part of this basic block");

	instr->m_ParentBB = bb;

	if (anchor->m_Prev) {
		instr->m_Prev = anchor->m_Prev;
		anchor->m_Prev->m_Next = instr;
	} else {
		instr->m_Prev = NULL;
	}

	anchor->m_Prev = instr;
	instr->m_Next = anchor;

	if (bb->m_InstrListHead == anchor) {
		bb->m_InstrListHead = instr;
	}

#if JX_IR_CONFIG_FORCE_VALUE_NAMES
	if (bb->m_ParentFunc) {
		jx_ir_function_t* func = bb->m_ParentFunc;

		jx_ir_value_t* instrVal = jx_ir_instrToValue(instr);
		if (!instrVal->m_Name) {
			jx_ir_valueSetName(ctx, instrVal, jir_funcGenTempName(ctx, func));
		}

		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			jx_ir_value_t* operandVal = instr->super.m_OperandArr[iOperand]->m_Value;
			if (!operandVal->m_Name && (operandVal->m_Kind == JIR_VALUE_BASIC_BLOCK || operandVal->m_Kind == JIR_VALUE_INSTRUCTION)) {
				jx_ir_valueSetName(ctx, operandVal, jir_funcGenTempName(ctx, func));
			}
		}
	}
#endif

	return true;
}

// NOTE: Does not destroy the instruction. The instruction is still valid and can be inserted 
// into a basic block just like if it was new.
void jx_ir_bbRemoveInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr)
{
	if (!instr) {
		JX_CHECK(false, "Trying too remove NULL instruction from basic block.");
		return;
	}

	JX_CHECK(instr->m_ParentBB == bb, "Trying to remove instruction from wrong basic block.");

	if (instr == bb->m_InstrListHead) {
		JX_CHECK(!instr->m_Prev, "Instruction list head has prev link.");
		bb->m_InstrListHead = instr->m_Next;
		if (bb->m_InstrListHead) {
			bb->m_InstrListHead->m_Prev = NULL;
		}
	} else {
		JX_CHECK(jir_bbIncludesInstr(ctx, bb, instr), "Instruction not in basic block's list.");
		if (instr->m_Prev) {
			instr->m_Prev->m_Next = instr->m_Next;
		}
		if (instr->m_Next) {
			instr->m_Next->m_Prev = instr->m_Prev;
		}
	}

	instr->m_Next = NULL;
	instr->m_Prev = NULL;
	instr->m_ParentBB = NULL;

	// If this is a branch instruction, update successor/predecessor links
	if (instr->m_OpCode == JIR_OP_BRANCH) {
		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		if (numOperands == 1) {
			// Unconditional branch
			jx_ir_basic_block_t* targetBB = jx_ir_valueToBasicBlock(instr->super.m_OperandArr[0]->m_Value);
			JX_CHECK(targetBB, "Unconditional branch 1st operand expected to be a basic block.");
			jir_bbRemovePred(ctx, targetBB, bb);
			jir_bbRemoveSucc(ctx, bb, targetBB);
		} else if (numOperands == 3) {
			// Conditional branch
			jx_ir_basic_block_t* trueBB = jx_ir_valueToBasicBlock(instr->super.m_OperandArr[1]->m_Value);
			jx_ir_basic_block_t* falseBB = jx_ir_valueToBasicBlock(instr->super.m_OperandArr[2]->m_Value);
			JX_CHECK(trueBB && falseBB, "Conditional branch 2nd and 3rd operands expected to be basic blocks.");
			jir_bbRemovePred(ctx, trueBB, bb);
			jir_bbRemovePred(ctx, falseBB, bb);
			jir_bbRemoveSucc(ctx, bb, trueBB);
			jir_bbRemoveSucc(ctx, bb, falseBB);
		} else {
			JX_CHECK(false, "Unknown branch instruction!");
		}
	}
}

// Given a constant conditional value, convert a conditional branch into 
// an unconditional branch.
// 
// NOTE: Simply removing the old conditional branch and appending a new unconditional 
// branch to the basic block does not work because all phi operands referencing this 
// basic block are removed once the branch is removed from the basic block.
bool jx_ir_bbConvertCondBranch(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, bool condVal)
{
	jx_ir_instruction_t* branchInstr = jx_ir_bbGetLastInstr(ctx, bb);
	if (branchInstr->m_OpCode != JIR_OP_BRANCH || jx_array_sizeu(branchInstr->super.m_OperandArr) != 3) {
		JX_CHECK(false, "Expected conditional branch as last basic block instruction!");
		return false;
	}

	JX_CHECK(jx_array_sizeu(bb->m_SuccArr) == 2, "Basic block with conditional branch as terminator expected to have 2 successors!");

	// Remove targetRemoveID successor
	jx_ir_basic_block_t* succRemove = bb->m_SuccArr[condVal ? 1 : 0];
	jir_bbRemovePred(ctx, succRemove, bb);
	jir_bbRemoveSucc(ctx, bb, succRemove);

	// Rewrite branch instruction into an unconditional branch.
	jir_userRemoveOperand(ctx, jx_ir_instrToUser(branchInstr), condVal ? 2 : 1);
	jir_userRemoveOperand(ctx, jx_ir_instrToUser(branchInstr), 0);

	return true;
}

jx_ir_basic_block_t* jx_ir_bbSplitAt(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr)
{
	if (instr->m_ParentBB != bb) {
		JX_CHECK(false, "Instruction not part of the specified basic block!");
		return NULL;
	}

	jx_ir_basic_block_t* contBB = jx_ir_bbAlloc(ctx, NULL);

	// Move all instructions after instr to the new block
	jx_ir_instruction_t* movedInstr = instr->m_Next;
	while (movedInstr) {
		jx_ir_instruction_t* nextInstr = movedInstr->m_Next;

		jx_ir_bbRemoveInstr(ctx, bb, movedInstr);
		jx_ir_bbAppendInstr(ctx, contBB, movedInstr);

		movedInstr = nextInstr;
	}

	// Append an unconditional jump to the new block at the end of the original block.
	jx_ir_bbAppendInstr(ctx, bb, jx_ir_instrBranch(ctx, contBB));

	// Insert new block after the existing block.
	{
		contBB->m_ParentFunc = bb->m_ParentFunc;
		contBB->m_Prev = bb;
		contBB->m_Next = bb->m_Next;
		bb->m_Next = contBB;
	}

	// Patch all phi instructions referencing the original block to have the same values
	// but with the new block as the source.
	jx_ir_use_t* bbUse = bb->super.m_UsesListHead;
	while (bbUse) {
		jx_ir_value_t* userValue = jx_ir_userToValue(bbUse->m_User);
		if (userValue->m_Kind == JIR_VALUE_INSTRUCTION) {
			jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(userValue);
			if (userInstr->m_OpCode == JIR_OP_PHI) {
				if (jx_ir_instrPhiHasValue(ctx, userInstr, bb)) {
					jx_ir_value_t* val = jx_ir_instrPhiRemoveValue(ctx, userInstr, bb);
					jx_ir_instrPhiAddValue(ctx, userInstr, contBB, val);
				}
			}
		}

		bbUse = bbUse->m_Next;
	}

	return contBB;
}

void jx_ir_bbPrint(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_string_buffer_t* sb)
{
	jx_ir_value_t* bbValue = jx_ir_bbToValue(bb);
	
	if (!bbValue->m_Name) {
		JX_CHECK(!JX_IR_CONFIG_FORCE_VALUE_NAMES, "Basic block should have a name at this point!");
		bbValue->m_Name = jir_funcGenTempName(ctx, bb->m_ParentFunc);
	}

	jx_strbuf_printf(sb, "%s:\n", bbValue->m_Name);

	jx_ir_instruction_t* instr = bb->m_InstrListHead;
	while (instr) {
		jx_ir_instrPrint(ctx, instr, sb);

		instr = instr->m_Next;
	}
}

static jx_ir_instruction_t* jir_instrAlloc(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t opcode, uint32_t numOperands)
{
	jx_ir_instruction_t* instr = (jx_ir_instruction_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_instruction_t));
	if (!instr) {
		return NULL;
	}

	if (!jir_instrCtor(ctx, instr, type, opcode, NULL, numOperands)) {
		return NULL;
	}

	return instr;
}

jx_ir_instruction_t* jx_ir_instrClone(jx_ir_context_t* ctx, jx_ir_instruction_t* instr)
{
	const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
	jx_ir_instruction_t* clone = jir_instrAlloc(ctx, instr->super.super.m_Type, instr->m_OpCode, numOperands);
	if (!clone) {
		return NULL;
	}

	for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
		jir_instrAddOperand(ctx, clone, instr->super.m_OperandArr[iOperand]->m_Value);
	}

	return clone;
}

jx_ir_value_t* jx_ir_instrReplaceOperand(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, uint32_t id, jx_ir_value_t* val)
{
	return jir_userReplaceOperand(ctx, jx_ir_instrToUser(instr), id, val);
}

void jx_ir_instrFree(jx_ir_context_t* ctx, jx_ir_instruction_t* instr)
{
	// Remove instruction from parent basic block
	if (instr->m_ParentBB) {
		jx_ir_bbRemoveInstr(ctx, instr->m_ParentBB, instr);
		instr->m_ParentBB = NULL;
	}

	jir_instrDtor(ctx, instr);
}

jx_ir_instruction_t* jx_ir_instrRet(jx_ir_context_t* ctx, jx_ir_value_t* val)
{
	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, val ? val->m_Type : ctx->m_BuildinTypes[JIR_TYPE_VOID], JIR_OP_RET, val ? 1 : 0);
	if (!instr) {
		return NULL;
	}

	if (val) {
		jir_instrAddOperand(ctx, instr, val);
	}

	return instr;
}

jx_ir_instruction_t* jx_ir_instrBranch(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb)
{
	if (!bb) {
		JX_CHECK(false, "Unconditional branch instruction must target a valid basic block");
		return false;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, ctx->m_BuildinTypes[JIR_TYPE_VOID], JIR_OP_BRANCH, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, jx_ir_bbToValue(bb));

	return instr;
}

jx_ir_instruction_t* jx_ir_instrBranchIf(jx_ir_context_t* ctx, jx_ir_value_t* cond, jx_ir_basic_block_t* trueBB, jx_ir_basic_block_t* falseBB)
{
	if (!cond || !trueBB || !falseBB) {
		JX_CHECK(false, "Both targets and the condition of a conditional branch must be valid.");
		return false;
	}

	JX_CHECK(cond->m_Type->m_Kind == JIR_TYPE_BOOL, "Expected boolean conditional value for branch");

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, ctx->m_BuildinTypes[JIR_TYPE_VOID], JIR_OP_BRANCH, 3);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, cond);
	jir_instrAddOperand(ctx, instr, jx_ir_bbToValue(trueBB));
	jir_instrAddOperand(ctx, instr, jx_ir_bbToValue(falseBB));

	return instr;
}

jx_ir_instruction_t* jx_ir_instrAdd(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_ADD, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrSub(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_SUB, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrMul(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_MUL, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrDiv(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_DIV, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrRem(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_REM, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrAnd(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_AND, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrOr(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_OR, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrXor(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2)
{
	return jir_instrBinaryOp(ctx, JIR_OP_XOR, op1, op2);
}

jx_ir_instruction_t* jx_ir_instrShl(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_value_t* shiftAmount)
{
	// NOTE: This isn't implemented as a binary op because shiftAmount might be a different type than val.
	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, val->m_Type, JIR_OP_SHL, 2);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);
	jir_instrAddOperand(ctx, instr, shiftAmount);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrShr(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_value_t* shiftAmount)
{
	// NOTE: This isn't implemented as a binary op because shiftAmount might be a different type than val.
	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, val->m_Type, JIR_OP_SHR, 2);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);
	jir_instrAddOperand(ctx, instr, shiftAmount);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrNeg(jx_ir_context_t* ctx, jx_ir_value_t* val)
{
	jx_ir_constant_t* zero = jx_ir_constGetZero(ctx, val->m_Type);
	return jir_instrBinaryOp(ctx, JIR_OP_SUB, jx_ir_constToValue(zero), val);
}

jx_ir_instruction_t* jx_ir_instrNot(jx_ir_context_t* ctx, jx_ir_value_t* val)
{
	jx_ir_constant_t* ones = jx_ir_constGetOnes(ctx, val->m_Type);
	return jir_instrBinaryOp(ctx, JIR_OP_XOR, val, jx_ir_constToValue(ones));
}

jx_ir_instruction_t* jx_ir_instrSetCC(jx_ir_context_t* ctx, jx_ir_condition_code cc, jx_ir_value_t* val1, jx_ir_value_t* val2)
{
	return jir_instrBinaryOpTyped(ctx, JIR_OP_SET_CC_BASE + cc, val1, val2, ctx->m_BuildinTypes[JIR_TYPE_BOOL]);
}

jx_ir_instruction_t* jx_ir_instrSetEQ(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2)
{
	return jir_instrBinaryOpTyped(ctx, JIR_OP_SET_EQ, val1, val2, ctx->m_BuildinTypes[JIR_TYPE_BOOL]);
}

jx_ir_instruction_t* jx_ir_instrSetNE(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2)
{
	return jir_instrBinaryOpTyped(ctx, JIR_OP_SET_NE, val1, val2, ctx->m_BuildinTypes[JIR_TYPE_BOOL]);
}

jx_ir_instruction_t* jx_ir_instrSetLT(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2)
{
	return jir_instrBinaryOpTyped(ctx, JIR_OP_SET_LT, val1, val2, ctx->m_BuildinTypes[JIR_TYPE_BOOL]);
}

jx_ir_instruction_t* jx_ir_instrSetLE(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2)
{
	return jir_instrBinaryOpTyped(ctx, JIR_OP_SET_LE, val1, val2, ctx->m_BuildinTypes[JIR_TYPE_BOOL]);
}

jx_ir_instruction_t* jx_ir_instrSetGT(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2)
{
	return jir_instrBinaryOpTyped(ctx, JIR_OP_SET_GT, val1, val2, ctx->m_BuildinTypes[JIR_TYPE_BOOL]);
}

jx_ir_instruction_t* jx_ir_instrSetGE(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2)
{
	return jir_instrBinaryOpTyped(ctx, JIR_OP_SET_GE, val1, val2, ctx->m_BuildinTypes[JIR_TYPE_BOOL]);
}

jx_ir_instruction_t* jx_ir_instrTrunc(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsInteger(valType) || !jx_ir_typeIsInteger(targetType) || jx_ir_typeGetSize(valType) <= jx_ir_typeGetSize(targetType)) {
		JX_CHECK(false, "trunc can only be applied from one integer type to another smaller integer type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_TRUNC, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrZeroExt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsIntegral(valType) || !jx_ir_typeIsInteger(targetType) || jx_ir_typeGetSize(valType) >= jx_ir_typeGetSize(targetType)) {
		JX_CHECK(false, "zext can only be applied from one integer type to another larger integer type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_ZEXT, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrSignExt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsIntegral(valType) || !jx_ir_typeIsIntegral(targetType) || jx_ir_typeGetSize(valType) >= jx_ir_typeGetSize(targetType)) {
		JX_CHECK(false, "sext can only be applied from one integral type to another larger integral type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_SEXT, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}


jx_ir_instruction_t* jx_ir_instrPtrToInt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeToPointer(valType) || !jx_ir_typeIsInteger(targetType)) {
		JX_CHECK(false, "ptrtoint expects a pointer value and an integer type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_PTR_TO_INT, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrIntToPtr(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsInteger(valType) || !jx_ir_typeToPointer(targetType)) {
		JX_CHECK(false, "inttoptr expects an integer value and a pointer type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_INT_TO_PTR, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrBitcast(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	const bool isValPtr = jx_ir_typeToPointer(valType) != NULL;
	const bool isTargetPtr = jx_ir_typeToPointer(targetType) != NULL;
	if (isValPtr != isTargetPtr || jx_ir_typeGetSize(valType) != jx_ir_typeGetSize(targetType)) {
		JX_CHECK(false, "bitcast expects either two pointers or two types with same size.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_BITCAST, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrFPExt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsFloatingPoint(valType) || !jx_ir_typeIsFloatingPoint(targetType) || jx_ir_typeGetSize(valType) >= jx_ir_typeGetSize(targetType)) {
		JX_CHECK(false, "fpext can only be applied from one floating point type to another larger floating point type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_FPEXT, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrFPTrunc(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsFloatingPoint(valType) || !jx_ir_typeIsFloatingPoint(targetType) || jx_ir_typeGetSize(valType) <= jx_ir_typeGetSize(targetType)) {
		JX_CHECK(false, "fptrunc can only be applied from one floating point type to another smaller floating point type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_FPTRUNC, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrFP2UI(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsFloatingPoint(valType) || !jx_ir_typeIsInteger(targetType)) {
		JX_CHECK(false, "fp2ui can only be applied from one floating point type to an integer type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_FP2UI, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrFP2SI(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsFloatingPoint(valType) || !jx_ir_typeIsInteger(targetType)) {
		JX_CHECK(false, "fp2si can only be applied from one floating point type to an integer type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_FP2SI, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrUI2FP(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsInteger(valType) || !jx_ir_typeIsFloatingPoint(targetType)) {
		JX_CHECK(false, "ui2fp can only be applied from one integer type to a floating point type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_UI2FP, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrSI2FP(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType)
{
	jx_ir_type_t* valType = val->m_Type;
	if (!jx_ir_typeIsInteger(valType) || !jx_ir_typeIsFloatingPoint(targetType)) {
		JX_CHECK(false, "si2fp can only be applied from one integer type to a floating point type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, targetType, JIR_OP_SI2FP, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, val);

	return instr;
}


jx_ir_instruction_t* jx_ir_instrCall(jx_ir_context_t* ctx, jx_ir_value_t* funcVal, uint32_t numArgs, jx_ir_value_t** argValues)
{
	// NOTE: Don't assume funcVal is a function value. It might be an instruction pointing to a function
	// (i.e. call via pointer to function).
	jx_ir_type_pointer_t* funcPtrType = jx_ir_typeToPointer(funcVal->m_Type);
	if (!funcPtrType) {
		JX_CHECK(false, "Call function value type should be a pointer to a function.");
		return NULL;
	}

	jx_ir_type_function_t* funcType = jx_ir_typeToFunction(funcPtrType->m_BaseType);
	if (!funcType) {
		JX_CHECK(false, "Call function value's type should be a pointer to a function.");
		return NULL;
	}

	if (numArgs != funcType->m_NumArgs && (funcType->m_IsVarArg && numArgs < funcType->m_NumArgs)) {
		JX_CHECK(false, "Calling a function with bad signature.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, funcType->m_RetType, JIR_OP_CALL, numArgs + 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, funcVal);
	for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
		jir_instrAddOperand(ctx, instr, argValues[iArg]);
	}

	return instr;
}

jx_ir_instruction_t* jx_ir_instrAlloca(jx_ir_context_t* ctx, jx_ir_type_t* type, jx_ir_value_t* arraySize)
{
	if (!jx_ir_typeIsSized(type)) {
		JX_CHECK(false, "alloca type should be sized.");
		return NULL;
	}

	if (!arraySize) {
		jx_ir_constant_t* const_one = jx_ir_constGetU32(ctx, 1);
		if (!const_one) {
			return NULL;
		}

		arraySize = jx_ir_constToValue(const_one);
	}

	if (!jx_ir_typeIsInteger(arraySize->m_Type)) {
		JX_CHECK(false, "Alloca array size must be an integer.");
		return false;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, jx_ir_typeGetPointer(ctx, type), JIR_OP_ALLOCA, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, arraySize);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrLoad(jx_ir_context_t* ctx, jx_ir_type_t* type, jx_ir_value_t* ptr)
{
	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(ptr->m_Type);
	if (!ptrType) {
		JX_CHECK(false, "Load instruction source operand must be a pointer type");
		return NULL;
	}

	if (!jx_ir_typeIsFirstClass(type)) {
		JX_CHECK(false, "Load instruction type must first-class.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, type, JIR_OP_LOAD, 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, ptr);

	return instr;
}

jx_ir_instruction_t* jx_ir_instrStore(jx_ir_context_t* ctx, jx_ir_value_t* ptr, jx_ir_value_t* val)
{
	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(ptr->m_Type);
	if (!ptrType) {
		JX_CHECK(false, "Store instruction destination operand must be a pointer type.");
		return NULL;
	}

	if (ptrType->m_BaseType != val->m_Type) {
		JX_CHECK(false, "Store instruction source operand must have the same type as the pointer's base type.");
		return NULL;
	}

	if (!jx_ir_typeIsFirstClass(val->m_Type) && !jx_ir_typeIsSmallPow2Struct(val->m_Type)) {
		JX_CHECK(false, "Store instruction source operand must have a first class type.");
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, ctx->m_BuildinTypes[JIR_TYPE_VOID], JIR_OP_STORE, 2);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, ptr);
	jir_instrAddOperand(ctx, instr, val);

	return instr;
}

static jx_ir_type_t* jir_getIndexedType(jx_ir_type_t* ptr, uint32_t numIndices, jx_ir_value_t** indices, bool allowCompositeLeaf)
{
	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(ptr);
	if (!ptrType) {
		return NULL;
	}

	if (numIndices == 0) {
		return allowCompositeLeaf || jx_ir_typeIsFirstClass(ptrType->m_BaseType)
			? ptrType->m_BaseType
			: NULL
			;
	}

	uint32_t curIndex = 0;
	while (jx_ir_typeIsComposite(ptr)) {
		if (curIndex == numIndices) {
			return allowCompositeLeaf || jx_ir_typeIsFirstClass(ptr)
				? ptr
				: NULL
				;
		}

		jx_ir_value_t* index = indices[curIndex++];

		// Check if index is valid for the current composite type
		// and move on to the pointed type.
		if (ptr->m_Kind == JIR_TYPE_ARRAY) {
			const bool isValidIndexType = false
				|| index->m_Type->m_Kind == JIR_TYPE_I32
				|| index->m_Type->m_Kind == JIR_TYPE_I64
				|| index->m_Type->m_Kind == JIR_TYPE_U32
				|| index->m_Type->m_Kind == JIR_TYPE_U64
				;
			if (!isValidIndexType) {
				return NULL;
			}

			ptr = jx_ir_typeToArray(ptr)->m_BaseType;
		} else if (ptr->m_Kind == JIR_TYPE_STRUCT) {
			jx_ir_constant_t* constIndex = jx_ir_valueToConst(index);
			if (!constIndex || index->m_Type->m_Kind != JIR_TYPE_I32) {
				return NULL;
			}

			jx_ir_type_struct_t* structType = jx_ir_typeToStruct(ptr);
			if (constIndex->u.m_I64 >= (int64_t)structType->m_NumMembers) {
				return NULL;
			}

			ptr = structType->m_Members[constIndex->u.m_I64];
		} else if (ptr->m_Kind == JIR_TYPE_POINTER) {
			// Can only index into pointer types at the first index!
			if (curIndex != 1) {
				return NULL;
			}

			const bool isValidIndexType = false
				|| index->m_Type->m_Kind == JIR_TYPE_I32
				|| index->m_Type->m_Kind == JIR_TYPE_I64
				|| index->m_Type->m_Kind == JIR_TYPE_U32
				|| index->m_Type->m_Kind == JIR_TYPE_U64
				;
			if (!isValidIndexType) {
				return NULL;
			}

			ptr = jx_ir_typeToPointer(ptr)->m_BaseType;
		} else {
			break;
		}
	}

	return curIndex == numIndices ? ptr : NULL;
}

jx_ir_instruction_t* jx_ir_instrGetElementPtr(jx_ir_context_t* ctx, jx_ir_value_t* ptr, uint32_t numIndices, jx_ir_value_t** indices)
{
	jx_ir_type_t* type = jir_getIndexedType(ptr->m_Type, numIndices, indices, true);
	if (!type) {
		return NULL;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, jx_ir_typeGetPointer(ctx, type), JIR_OP_GET_ELEMENT_PTR, numIndices + 1);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, ptr);
	for (uint32_t iIndex = 0; iIndex < numIndices; ++iIndex) {
		jir_instrAddOperand(ctx, instr, indices[iIndex]);
	}

	return instr;
}

jx_ir_instruction_t* jx_ir_instrPhi(jx_ir_context_t* ctx, jx_ir_type_t* type)
{
	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, type, JIR_OP_PHI, 0);
	if (!instr) {
		return NULL;
	}

	return instr;
}

jx_ir_instruction_t* jx_ir_instrMemCopy(jx_ir_context_t* ctx, jx_ir_value_t* dstPtr, jx_ir_value_t* srcPtr, jx_ir_value_t* size)
{
	jx_ir_module_t* mod = ctx->m_ModuleListHead;
	JX_CHECK(mod, "moduleBegin() not called yet!");

	if (!jx_ir_typeToPointer(dstPtr->m_Type)) {
		JX_CHECK(false, "memcpy destination operand expected to be a pointer.");
		return NULL;
	}

	if (!jx_ir_typeToPointer(srcPtr->m_Type)) {
		JX_CHECK(false, "memcpy source operand expected to be a pointer.");
		return NULL;
	}

	jx_ir_value_t* args[] = {
		dstPtr,
		srcPtr,
		size
	};
	if (size->m_Type->m_Kind == JIR_TYPE_I32) {
		return jx_ir_instrCall(ctx, mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMCPY_P0_P0_I32], JX_COUNTOF(args), args);
	} else if (size->m_Type->m_Kind == JIR_TYPE_I64) {
		return jx_ir_instrCall(ctx, mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMCPY_P0_P0_I64], JX_COUNTOF(args), args);
	}

	JX_CHECK(false, "memcpy size operand expected to be either i32 or i64.");

	return NULL;
}

jx_ir_instruction_t* jx_ir_instrMemSet(jx_ir_context_t* ctx, jx_ir_value_t* dstPtr, jx_ir_value_t* i8Val, jx_ir_value_t* size)
{
	jx_ir_module_t* mod = ctx->m_ModuleListHead;
	JX_CHECK(mod, "moduleBegin() not called yet!");

	if (!jx_ir_typeToPointer(dstPtr->m_Type)) {
		JX_CHECK(false, "memset destination operand expected to be a pointer.");
		return NULL;
	}

	if (i8Val->m_Type->m_Kind != JIR_TYPE_I8) {
		JX_CHECK(false, "memset value operand expected to be an i8");
		return NULL;
	}

	jx_ir_value_t* args[] = {
		dstPtr,
		i8Val,
		size
	};
	if (size->m_Type->m_Kind == JIR_TYPE_I32) {
		return jx_ir_instrCall(ctx, mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMSET_P0_I32], JX_COUNTOF(args), args);
	} else if (size->m_Type->m_Kind == JIR_TYPE_I64) {
		return jx_ir_instrCall(ctx, mod->m_IntrinsicFuncs[JIR_INTRINSIC_MEMSET_P0_I64], JX_COUNTOF(args), args);
	}

	JX_CHECK(false, "memset size operand expected to be either i32 or i64.");

	return NULL;
}

bool jx_ir_instrPhiAddValue(jx_ir_context_t* ctx, jx_ir_instruction_t* phiInstr, jx_ir_basic_block_t* bb, jx_ir_value_t* val)
{
	if (phiInstr->m_OpCode != JIR_OP_PHI) {
		JX_CHECK(false, "Expected phi instruction!");
		return false;
	}

	jx_ir_value_t* phiVal = jx_ir_instrToValue(phiInstr);
	if (phiVal->m_Type != val->m_Type) {
		JX_CHECK(false, "All phi node operands must have the same type.");
		return false;
	}

	jir_instrAddOperand(ctx, phiInstr, val);
	jir_instrAddOperand(ctx, phiInstr, jx_ir_bbToValue(bb));

	return true;
}

jx_ir_value_t* jx_ir_instrPhiRemoveValue(jx_ir_context_t* ctx, jx_ir_instruction_t* phiInstr, jx_ir_basic_block_t* bb)
{
	if (phiInstr->m_OpCode != JIR_OP_PHI) {
		JX_CHECK(false, "Expected phi instruction!");
		return NULL;
	}
	
	jx_ir_value_t* bbVal = NULL;

	const uint32_t numOperands = (uint32_t)jx_array_sizeu(phiInstr->super.m_OperandArr);
	for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
		jx_ir_basic_block_t* phiBB = jx_ir_valueToBasicBlock(phiInstr->super.m_OperandArr[iOperand + 1]->m_Value);
		if (phiBB == bb) {
			bbVal = phiInstr->super.m_OperandArr[iOperand + 0]->m_Value;
			jir_instrRemoveOperand(ctx, phiInstr, iOperand + 1);
			jir_instrRemoveOperand(ctx, phiInstr, iOperand + 0);
			break;
		}
	}

	JX_CHECK(bbVal, "Basic block not found in phi instruction!");

	return bbVal;
}

jx_ir_value_t* jx_ir_instrPhiHasValue(jx_ir_context_t* ctx, jx_ir_instruction_t* phiInstr, jx_ir_basic_block_t* bb)
{
	if (phiInstr->m_OpCode != JIR_OP_PHI) {
		JX_CHECK(false, "Expected phi instruction!");
		return NULL;
	}

	const uint32_t numOperands = (uint32_t)jx_array_sizeu(phiInstr->super.m_OperandArr);
	for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
		jx_ir_basic_block_t* phiBB = jx_ir_valueToBasicBlock(phiInstr->super.m_OperandArr[iOperand + 1]->m_Value);
		if (phiBB == bb) {
			return phiInstr->super.m_OperandArr[iOperand + 0]->m_Value;
		}
	}

	return NULL;
}

jx_ir_constant_t* jx_ir_valueToConst(jx_ir_value_t* val)
{
	return val->m_Kind == JIR_VALUE_CONSTANT
		? (jx_ir_constant_t*)val
		: NULL
		;
}

jx_ir_function_t* jx_ir_valueToFunc(jx_ir_value_t* val)
{
	return val->m_Kind == JIR_VALUE_FUNCTION
		? (jx_ir_function_t*)val
		: NULL
		;
}

jx_ir_instruction_t* jx_ir_valueToInstr(jx_ir_value_t* val)
{
	return val->m_Kind == JIR_VALUE_INSTRUCTION
		? (jx_ir_instruction_t*)val
		: NULL
		;
}

jx_ir_basic_block_t* jx_ir_valueToBasicBlock(jx_ir_value_t* val)
{
	return val->m_Kind == JIR_VALUE_BASIC_BLOCK
		? (jx_ir_basic_block_t*)val
		: NULL
		;
}

jx_ir_argument_t* jx_ir_valueToArgument(jx_ir_value_t* val)
{
	return val->m_Kind == JIR_VALUE_ARGUMENT
		? (jx_ir_argument_t*)val
		: NULL
		;
}

void jx_ir_instrPrint(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, jx_string_buffer_t* sb)
{
	jx_ir_value_t* instrVal = jx_ir_instrToValue(instr);
	jx_ir_type_t* instrType = instrVal->m_Type;

	jx_strbuf_pushCStr(sb, "  ");

	if (instrType->m_Kind != JIR_TYPE_VOID && !jx_ir_opcodeIsTerminator(instr->m_OpCode)) {
		if (!instrVal->m_Name) {
			JX_CHECK(!JX_IR_CONFIG_FORCE_VALUE_NAMES, "Instruction must have a name at this point!");
			instrVal->m_Name = jir_funcGenTempName(ctx, instr->m_ParentBB->m_ParentFunc);
		}

		jx_strbuf_printf(sb, "%%%s = ", instrVal->m_Name);
	}

	jx_strbuf_printf(sb, "%s ", kOpcodeMnemonic[instr->m_OpCode]);

	jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
	jx_ir_use_t** operandArr = instrUser->m_OperandArr;
	const uint32_t numOperands = (uint32_t)jx_array_sizeu(operandArr);

	// Generate names for all operands if they don't have any
	for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
		jx_ir_value_t* operandVal = operandArr[iOperand]->m_Value;
		if (!operandVal->m_Name && (operandVal->m_Kind == JIR_VALUE_BASIC_BLOCK || operandVal->m_Kind == JIR_VALUE_INSTRUCTION)) {
			JX_CHECK(!JX_IR_CONFIG_FORCE_VALUE_NAMES, "Operand should have a name at this point!");
			operandVal->m_Name = jir_funcGenTempName(ctx, instr->m_ParentBB->m_ParentFunc);
		}
	}

	if (instr->m_OpCode == JIR_OP_ALLOCA) {
		jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(instrType);
		JX_CHECK(ptrType, "alloca expected to have a pointer type!");

		jx_ir_value_t* arraySizeOperand = operandArr[0]->m_Value;

		if (arraySizeOperand != jx_ir_constToValue(jx_ir_constGetU32(ctx, 1))) {
			jx_strbuf_pushCStr(sb, "[");
			jx_ir_valuePrint(ctx, arraySizeOperand, sb);
			jx_strbuf_pushCStr(sb, " x ");
			jx_ir_typePrint(ctx, ptrType->m_BaseType, sb);
			jx_strbuf_pushCStr(sb, "]");
		} else {
			jx_ir_typePrint(ctx, ptrType->m_BaseType, sb);
		}
	} else if (instr->m_OpCode == JIR_OP_GET_ELEMENT_PTR) {
		jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(operandArr[0]->m_Value->m_Type);
		JX_CHECK(ptrType, "getelementptr first operand expected to have a pointer type!");

		jx_ir_typePrint(ctx, ptrType->m_BaseType, sb);
		jx_strbuf_pushCStr(sb, ", ");

		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			if (iOperand != 0) {
				jx_strbuf_pushCStr(sb, ", ");
			}

			jx_ir_value_t* operand = operandArr[iOperand]->m_Value;
			jx_ir_typePrint(ctx, operand->m_Type, sb);
			jx_strbuf_pushCStr(sb, " ");
			jx_ir_valuePrint(ctx, operand, sb);
		}
	} else if (instr->m_OpCode == JIR_OP_PHI) {
		JX_CHECK((numOperands & 1) == 0, "phi instruction expected to have even number of operands");
		jx_ir_typePrint(ctx, instrType, sb);
		jx_strbuf_pushCStr(sb, " ");

		for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
			if (iOperand != 0) {
				jx_strbuf_pushCStr(sb, ", ");
			}

			jx_strbuf_pushCStr(sb, "[");
			jx_ir_valuePrint(ctx, operandArr[iOperand + 0]->m_Value, sb);
			jx_strbuf_pushCStr(sb, ", ");
			jx_ir_valuePrint(ctx, operandArr[iOperand + 1]->m_Value, sb);
			jx_strbuf_pushCStr(sb, "]");
		}
	} else {
		if (instrType->m_Kind != JIR_TYPE_VOID) {
			jx_ir_typePrint(ctx, instrType, sb);
			jx_strbuf_pushCStr(sb, ", ");
		}

		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			if (iOperand != 0) {
				jx_strbuf_pushCStr(sb, ", ");
			}

			jx_ir_value_t* operand = operandArr[iOperand]->m_Value;
			jx_ir_typePrint(ctx, operand->m_Type, sb);
			jx_strbuf_pushCStr(sb, " ");
			jx_ir_valuePrint(ctx, operand, sb);
		}
	}

	jx_strbuf_pushCStr(sb, "\n");
}

bool jx_ir_instrCheck(jx_ir_context_t* ctx, jx_ir_instruction_t* instr)
{
	jx_ir_value_t* instrVal = jx_ir_instrToValue(instr);

	// All operands should include this instruction in their use lists.
	const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
	for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
		jx_ir_use_t* operand = instr->super.m_OperandArr[iOperand];
		jx_ir_value_t* operandVal = operand->m_Value;
		jx_ir_use_t* operandUse = operandVal->m_UsesListHead;
		bool found = false;
		while (operandUse) {
			if (jx_ir_userToValue(operandUse->m_User) == instrVal) {
				found = true;
				break;
			}

			operandUse = operandUse->m_Next;
		}

		if (!found) {
			JX_CHECK(false, "Instruction not found in operand's use list");
			return false;
		}

		// Make sure the operand is (part of) a basic block of this function
		if (operandVal->m_Kind == JIR_VALUE_BASIC_BLOCK) {
			jx_ir_basic_block_t* bb = jx_ir_valueToBasicBlock(operandVal);
			if (!bb->m_ParentFunc || bb->m_ParentFunc != instr->m_ParentBB->m_ParentFunc) {
				JX_CHECK(false, "Instruction operand not part of the current function.");
				return false;
			}
		} else if (operandVal->m_Kind == JIR_VALUE_INSTRUCTION) {
			jx_ir_instruction_t* opInstr = jx_ir_valueToInstr(operandVal);
			if (!opInstr->m_ParentBB || opInstr->m_ParentBB->m_ParentFunc != instr->m_ParentBB->m_ParentFunc) {
				JX_CHECK(false, "Instruction operand not part of the current function.");
				return false;
			}
		}
	}

	// All users of this instruction should include it as an operand.
	jx_ir_use_t* instrUse = instrVal->m_UsesListHead;
	while (instrUse) {
		jx_ir_user_t* instrUser = instrUse->m_User;
		const uint32_t numUserOperands = (uint32_t)jx_array_sizeu(instrUser->m_OperandArr);
		bool found = false;
		for (uint32_t iUserOperand = 0; iUserOperand < numUserOperands; ++iUserOperand) {
			if (instrUser->m_OperandArr[iUserOperand]->m_Value == instrVal) {
				found = true;
				break;
			}
		}

		if (!found) {
			JX_CHECK(false, "Instruction not found as a user operand.");
			return false;
		}

		instrUse = instrUse->m_Next;
	}

	return true;
}

jx_ir_value_t* jx_ir_userToValue(jx_ir_user_t* user)
{
	return &user->super;
}

jx_ir_value_t* jx_ir_typeToValue(jx_ir_type_t* type)
{
	return &type->super;
}

jx_ir_value_t* jx_ir_constToValue(jx_ir_constant_t* c)
{
	return jx_ir_userToValue(&c->super);
}

jx_ir_value_t* jx_ir_globalValToValue(jx_ir_global_value_t* gv)
{
	return jx_ir_userToValue(&gv->super);
}

jx_ir_value_t* jx_ir_globalVarToValue(jx_ir_global_variable_t* gv)
{
	return jx_ir_globalValToValue(&gv->super);
}

jx_ir_value_t* jx_ir_funcToValue(jx_ir_function_t* func)
{
	return jx_ir_globalValToValue(&func->super);
}

jx_ir_value_t* jx_ir_argToValue(jx_ir_argument_t* arg)
{
	return &arg->super;
}

jx_ir_value_t* jx_ir_bbToValue(jx_ir_basic_block_t* bb)
{
	return &bb->super;
}

jx_ir_value_t* jx_ir_instrToValue(jx_ir_instruction_t* instr)
{
	return jx_ir_userToValue(&instr->super);
}

jx_ir_user_t* jx_ir_constToUser(jx_ir_constant_t* c)
{
	return &c->super;
}

jx_ir_user_t* jx_ir_globalValToUser(jx_ir_global_value_t* gv)
{
	return &gv->super;
}

jx_ir_user_t* jx_ir_globalVarToUser(jx_ir_global_variable_t* gv)
{
	return jx_ir_globalValToUser(&gv->super);
}

jx_ir_user_t* jx_ir_funcToUser(jx_ir_function_t* func)
{
	return jx_ir_globalValToUser(&func->super);
}

jx_ir_user_t* jx_ir_instrToUser(jx_ir_instruction_t* instr)
{
	return &instr->super;
}

bool jx_ir_opcodeIsAssociative(jx_ir_opcode opcode, jx_ir_type_t* type)
{
	return !jx_ir_typeIsFloatingPoint(type)
		&& (false 
			|| opcode == JIR_OP_ADD 
			|| opcode == JIR_OP_MUL 
			|| opcode == JIR_OP_AND 
			|| opcode == JIR_OP_OR 
			|| opcode == JIR_OP_XOR
			)
		;
}

bool jx_ir_opcodeIsCommutative(jx_ir_opcode opcode)
{
	return (false
		|| opcode == JIR_OP_ADD
		|| opcode == JIR_OP_MUL
		|| opcode == JIR_OP_AND
		|| opcode == JIR_OP_OR
		|| opcode == JIR_OP_XOR
		|| opcode == JIR_OP_SET_EQ
		|| opcode == JIR_OP_SET_NE
		);
}

bool jx_ir_opcodeIsTerminator(jx_ir_opcode opcode)
{
	return false
		|| opcode == JIR_OP_RET
		|| opcode == JIR_OP_BRANCH
		;
}

static bool jir_moduleCtor(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name)
{
	jx_memset(mod, 0, sizeof(jx_ir_module_t));

	if (name) {
		mod->m_Name = jx_strtable_insert(ctx->m_StringTable, name, UINT32_MAX);
		if (!mod->m_Name) {
			return false;
		}
	}

	return true;
}

static void jir_moduleDtor(jx_ir_context_t* ctx, jx_ir_module_t* mod)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	// Free all global variables
	jx_ir_global_variable_t* gv = mod->m_GlobalVarListHead;
	while (gv) {
		jx_ir_global_variable_t* next = gv->m_Next;
		jir_globalVarDtor(ctx, gv);
		gv = next;
	}
	mod->m_GlobalVarListHead = NULL;

	// Free all functions
	jx_ir_function_t* func = mod->m_FunctionListHead;
	while (func) {
		jx_ir_function_t* next = func->m_Next;
		jir_funcDtor(ctx, func);
		func = next;
	}
	mod->m_FunctionListHead = NULL;
}

static bool jir_valueCtor(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* type, jx_ir_value_kind kind, const char* name)
{
	jx_memset(val, 0, sizeof(jx_ir_value_t));
	val->m_Type = type;
	val->m_Kind = kind;
	if (name) {
		val->m_Name = jx_strtable_insert(ctx->m_StringTable, name, UINT32_MAX);
	}

	return true;
}

static void jir_valueDtor(jx_ir_context_t* ctx, jx_ir_value_t* val)
{
	// Nothing to free
	// TODO: What about uses? Who owns them?
}

bool jx_ir_valueSetName(jx_ir_context_t* ctx, jx_ir_value_t* val, const char* name)
{
	if (name) {
		val->m_Name = jx_strtable_insert(ctx->m_StringTable, name, UINT32_MAX);
	} else {
		val->m_Name = NULL;
	}

	return true;
}

void jx_ir_valueAddUse(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_use_t* use)
{
	JX_UNUSED(ctx);

	jx_ir_use_t* tail = val->m_UsesListHead;
	if (!tail) {
		val->m_UsesListHead = use;
	} else {
		while (tail->m_Next) {
			tail = tail->m_Next;
		}
		tail->m_Next = use;
		use->m_Prev = tail;
	}
}

void jx_ir_valueKillUse(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_use_t* use)
{
	JX_UNUSED(ctx);

	bool found = false;
	jx_ir_use_t* cur = val->m_UsesListHead;
	while (cur) {
		if (cur == use) {
			jx_ir_use_t* next = cur->m_Next;
			jx_ir_use_t* prev = cur->m_Prev;
			if (prev) {
				prev->m_Next = next;
			}
			if (next) {
				next->m_Prev = prev;
			}

			if (cur == val->m_UsesListHead) {
				val->m_UsesListHead = next;
			}

			found = true;
			break;
		}

		cur = cur->m_Next;
	}

	use->m_Value = NULL;
	use->m_Next = NULL;
	use->m_Prev = NULL;

	JX_CHECK(found, "Use not found in value's use list");
}

void jx_ir_valueReplaceAllUsesWith(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_value_t* newVal)
{
	JX_CHECK(newVal, "Expected non-NULL value");
	JX_CHECK(val != newVal, "Cannot replace all uses with the same value!");
	JX_CHECK(val->m_Type == newVal->m_Type, "New value should have the same type as the value it replaces!");

	jx_ir_use_t* use = val->m_UsesListHead;
	while (use) {
		jx_ir_use_t* nextUse = use->m_Next;
		JX_CHECK(use->m_Value == val, "Invalid use value!");

		if (jx_ir_valueToConst(jx_ir_userToValue(use->m_User))) {
			JX_NOT_IMPLEMENTED();
		} else {
			// TODO: If the user is a branch instruction and the value we are replacing
			// is one of the target basic blocks, update predecessors and successors of 
			// both old and new values.
			jx_ir_instruction_t* instr = jx_ir_valueToInstr(jx_ir_userToValue(use->m_User));
			if (instr && instr->m_OpCode == JIR_OP_BRANCH) {
				const uint32_t numOperands = (uint32_t)jx_array_sizeu(use->m_User->m_OperandArr);
				if (numOperands == 1) {
					// Unconditional branch
					JX_NOT_IMPLEMENTED();
				} else if (numOperands == 3) {
					// Conditional branch
					if (val == use->m_User->m_OperandArr[1]->m_Value) {
						JX_NOT_IMPLEMENTED();
					} else if (val == use->m_User->m_OperandArr[2]->m_Value) {
						JX_NOT_IMPLEMENTED();
					} else {
						JX_CHECK(val == use->m_User->m_OperandArr[0]->m_Value, "Expected user to be conditional branch's condition operand.");
					}
				} else {
					JX_CHECK(false, "Unknown branch instruction.");
				}
			}

			jir_useSetValue(ctx, use, newVal);
		}
		
		use = nextUse;
	}

	JX_CHECK(!val->m_UsesListHead, "More uses remaining?");
}

void jx_ir_valuePrint(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_string_buffer_t* sb)
{
	switch (val->m_Kind) {
	case JIR_VALUE_CONSTANT: {
		jx_ir_constPrint(ctx, (jx_ir_constant_t*)val, sb);
	} break;
	case JIR_VALUE_TYPE:
	case JIR_VALUE_ARGUMENT:
	case JIR_VALUE_INSTRUCTION:
	case JIR_VALUE_BASIC_BLOCK: {
		jx_strbuf_printf(sb, "%%%s", val->m_Name);
	} break;
	case JIR_VALUE_FUNCTION: 
	case JIR_VALUE_GLOBAL_VARIABLE: {
		jx_strbuf_printf(sb, "@%s", val->m_Name);
	} break;
	default:
		JX_CHECK(false, "Unknown kind of value!");
		break;
	}
}

static void jir_useCtor(jx_ir_context_t* ctx, jx_ir_use_t* use, jx_ir_value_t* val, jx_ir_user_t* user)
{
	jx_memset(use, 0, sizeof(jx_ir_use_t));
	use->m_Value = val;
	use->m_User = user;

	if (val) {
		jx_ir_valueAddUse(ctx, val, use);
	}
}

static void jir_useDtor(jx_ir_context_t* ctx, jx_ir_use_t* use)
{
	if (use->m_Value) {
		jx_ir_valueKillUse(ctx, use->m_Value, use);
	}
}

static void jir_useSetValue(jx_ir_context_t* ctx, jx_ir_use_t* use, jx_ir_value_t* val)
{
	if (use->m_Value) {
		jx_ir_valueKillUse(ctx, use->m_Value, use);
		use->m_Value = NULL;
	}

	use->m_Value = val;

	if (use->m_Value) {
		jx_ir_valueAddUse(ctx, use->m_Value, use);
	}
}

static bool jir_userCtor(jx_ir_context_t* ctx, jx_ir_user_t* user, jx_ir_type_t* type, jx_ir_value_kind kind, const char* name, uint32_t numOperands)
{
	jx_memset(user, 0, sizeof(jx_ir_user_t));
	if (!jir_valueCtor(ctx, &user->super, type, kind, name)) {
		return false;
	}

	user->m_OperandArr = (jx_ir_use_t**)jx_array_create(ctx->m_Allocator);
	if (!user->m_OperandArr) {
		jir_userDtor(ctx, user);
		return false;
	}

	if (numOperands != 0) {
		jx_array_reserve(user->m_OperandArr, numOperands);
	}

	return true;
}

static void jir_userDtor(jx_ir_context_t* ctx, jx_ir_user_t* user)
{
	if (user->m_OperandArr) {
		const uint32_t numOperands = (uint32_t)jx_array_sizeu(user->m_OperandArr);
		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			jir_useDtor(ctx, user->m_OperandArr[iOperand]);
		}

		jx_array_free(user->m_OperandArr);
		user->m_OperandArr = NULL;
	}

	jir_valueDtor(ctx, &user->super);
}

static void jir_userAddOperand(jx_ir_context_t* ctx, jx_ir_user_t* user, jx_ir_value_t* operand)
{
	jx_ir_use_t* use = (jx_ir_use_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_use_t));
	jir_useCtor(ctx, use, operand, user);
	jx_array_push_back(user->m_OperandArr, use);
}

static void jir_userRemoveOperand(jx_ir_context_t* ctx, jx_ir_user_t* user, uint32_t operandID)
{
	jx_ir_use_t* use = user->m_OperandArr[operandID];
	jir_useDtor(ctx, use);
	jx_array_del(user->m_OperandArr, operandID);
}

static jx_ir_value_t* jir_userReplaceOperand(jx_ir_context_t* ctx, jx_ir_user_t* user, uint32_t operandID, jx_ir_value_t* newVal)
{
	if (operandID >= (uint32_t)jx_array_sizeu(user->m_OperandArr)) {
		JX_CHECK(false, "Invalid operand ID");
		return NULL;
	}

	jx_ir_use_t* use = user->m_OperandArr[operandID];
	jx_ir_value_t* oldVal = use->m_Value;

	jir_useDtor(ctx, use);
	jir_useCtor(ctx, use, newVal, user);

	return oldVal;
}

static bool jir_typeCtor(jx_ir_context_t* ctx, jx_ir_type_t* type, const char* name, jx_ir_type_kind kind, uint32_t flags)
{
	jx_memset(type, 0, sizeof(jx_ir_type_t));
	if (!jir_valueCtor(ctx, &type->super, ctx->m_BuildinTypes[JIR_TYPE_TYPE], JIR_VALUE_TYPE, name)) {
		return false;
	}

	type->m_Kind = kind;
	type->m_Flags = flags;

	return true;
}

static void jir_typeDtor(jx_ir_context_t* ctx, jx_ir_type_t* type)
{
	jir_valueDtor(ctx, &type->super);
}

jx_ir_type_t* jx_ir_typeGetPrimitive(jx_ir_context_t* ctx, jx_ir_type_kind kind)
{
	return (kind < JIR_TYPE_NUM_PRIMITIVE_TYPES)
		? ctx->m_BuildinTypes[kind]
		: NULL
		;
}

jx_ir_type_t* jx_ir_typeGetFunction(jx_ir_context_t* ctx, jx_ir_type_t* retType, uint32_t numArgs, jx_ir_type_t** args, bool isVarArg)
{
	jx_ir_type_function_t* key = &(jx_ir_type_function_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_TYPE,
				.m_Type = ctx->m_BuildinTypes[JIR_TYPE_TYPE],
			},
			.m_Kind = JIR_TYPE_FUNCTION,
			.m_Flags = 0,
		},
		.m_RetType = retType,
		.m_Args = args,
		.m_NumArgs = numArgs,
		.m_IsVarArg = isVarArg,
	};

	jx_ir_type_t** cachedTypePtr = (jx_ir_type_t**)jx_hashmapGet(ctx->m_TypeMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_type_function_t* type = (jx_ir_type_function_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_type_function_t));
	if (!type) {
		return NULL;
	}

	jx_memset(type, 0, sizeof(jx_ir_type_function_t));
	jir_typeCtor(ctx, &type->super, NULL, JIR_TYPE_FUNCTION, 0);
	type->m_IsVarArg = isVarArg;
	type->m_RetType = retType;
	type->m_NumArgs = numArgs;
	if (numArgs) {
		type->m_Args = (jx_ir_type_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_type_t*) * numArgs);
		if (!type->m_Args) {
			return NULL;
		}

		jx_memcpy(type->m_Args, args, sizeof(jx_ir_type_t*) * numArgs);
	}

	jx_hashmapSet(ctx->m_TypeMap, &type);

	return &type->super;
}

jx_ir_type_t* jx_ir_typeGetPointer(jx_ir_context_t* ctx, jx_ir_type_t* baseType)
{
	jx_ir_type_pointer_t* key = &(jx_ir_type_pointer_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_TYPE,
				.m_Type = ctx->m_BuildinTypes[JIR_TYPE_TYPE],
			},
			.m_Kind = JIR_TYPE_POINTER,
			.m_Flags = 0,
		},
		.m_BaseType = baseType
	};

	jx_ir_type_t** cachedTypePtr = (jx_ir_type_t**)jx_hashmapGet(ctx->m_TypeMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_type_pointer_t* type = (jx_ir_type_pointer_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_type_pointer_t));
	if (!type) {
		return NULL;
	}

	jx_memset(type, 0, sizeof(jx_ir_type_pointer_t));
	jir_typeCtor(ctx, &type->super, NULL, JIR_TYPE_POINTER, 0);
	type->m_BaseType = baseType;

	jx_hashmapSet(ctx->m_TypeMap, &type);

	return &type->super;
}

jx_ir_type_t* jx_ir_typeGetArray(jx_ir_context_t* ctx, jx_ir_type_t* baseType, uint32_t sz)
{
	jx_ir_type_array_t* key = &(jx_ir_type_array_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_TYPE,
				.m_Type = ctx->m_BuildinTypes[JIR_TYPE_TYPE],
			},
			.m_Kind = JIR_TYPE_ARRAY,
			.m_Flags = 0,
		},
		.m_BaseType = baseType,
		.m_Size = sz,
	};

	jx_ir_type_t** cachedTypePtr = (jx_ir_type_t**)jx_hashmapGet(ctx->m_TypeMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_type_array_t* type = (jx_ir_type_array_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_type_array_t));
	if (!type) {
		return NULL;
	}

	jx_memset(type, 0, sizeof(jx_ir_type_pointer_t));
	jir_typeCtor(ctx, &type->super, NULL, JIR_TYPE_ARRAY, 0);
	type->m_BaseType = baseType;
	type->m_Size = sz;

	jx_hashmapSet(ctx->m_TypeMap, &type);

	return &type->super;
}

jx_ir_type_t* jx_ir_typeGetStruct(jx_ir_context_t* ctx, uint64_t uniqueID)
{
	jx_ir_type_struct_t* key = &(jx_ir_type_struct_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_TYPE,
				.m_Type = ctx->m_BuildinTypes[JIR_TYPE_TYPE],
				.m_Name = NULL
			},
			.m_Kind = JIR_TYPE_STRUCT,
			.m_Flags = 0,
		},
		.m_UniqueID = uniqueID
	};

	jx_ir_type_t** cachedTypePtr = (jx_ir_type_t**)jx_hashmapGet(ctx->m_TypeMap, &key);
	return cachedTypePtr
		? *cachedTypePtr
		: NULL
		;
}

jx_ir_type_struct_t* jx_ir_typeStructBegin(jx_ir_context_t* ctx, uint64_t uniqueID, uint32_t flags)
{
	jx_ir_type_struct_t* type = (jx_ir_type_struct_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_type_struct_t));
	if (!type) {
		return NULL;
	}

	jx_memset(type, 0, sizeof(jx_ir_type_struct_t));
	jir_typeCtor(ctx, &type->super, NULL, JIR_TYPE_STRUCT, 0);
	type->m_UniqueID = uniqueID;
	type->m_Flags = flags;

	jx_hashmapSet(ctx->m_TypeMap, &type);

	// Mark as incomplete type
	type->m_Flags |= JIR_TYPE_STRUCT_FLAGS_IS_INCOMPLETE_Msk;

	return type;
}

jx_ir_type_t* jx_ir_typeStructEnd(jx_ir_context_t* ctx, jx_ir_type_struct_t* type)
{
	JX_CHECK((type->m_Flags & JIR_TYPE_STRUCT_FLAGS_IS_INCOMPLETE_Msk) != 0, "Expected incomplete struct type!");
	type->m_Flags &= ~JIR_TYPE_STRUCT_FLAGS_IS_INCOMPLETE_Msk;
	return &type->super;
}

bool jx_ir_typeStructSetMembers(jx_ir_context_t* ctx, jx_ir_type_struct_t* structType, uint32_t numMembers, jx_ir_type_t** members)
{
	JX_CHECK((structType->m_Flags & JIR_TYPE_STRUCT_FLAGS_IS_INCOMPLETE_Msk) != 0, "Expected incomplete struct type!");
	JX_CHECK(structType->m_NumMembers == 0 && !structType->m_Members, "Expected empty struct!");

	structType->m_Members = (jx_ir_type_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_type_t*) * numMembers);
	if (!structType->m_Members) {
		return false;
	}

	jx_memcpy(structType->m_Members, members, sizeof(jx_ir_type_t*) * numMembers);
	structType->m_NumMembers = numMembers;

	return true;
}

void jx_ir_typePrint(jx_ir_context_t* ctx, jx_ir_type_t* type, jx_string_buffer_t* sb)
{
	switch (type->m_Kind) {
	case JIR_TYPE_VOID: {
		jx_strbuf_pushCStr(sb, "void");
	} break;
	case JIR_TYPE_BOOL: {
		jx_strbuf_pushCStr(sb, "bool");
	} break;
	case JIR_TYPE_U8: {
		jx_strbuf_pushCStr(sb, "u8");
	} break;
	case JIR_TYPE_I8: {
		jx_strbuf_pushCStr(sb, "i8");
	} break;
	case JIR_TYPE_U16: {
		jx_strbuf_pushCStr(sb, "u16");
	} break;
	case JIR_TYPE_I16: {
		jx_strbuf_pushCStr(sb, "i16");
	} break;
	case JIR_TYPE_U32: {
		jx_strbuf_pushCStr(sb, "u32");
	} break;
	case JIR_TYPE_I32: {
		jx_strbuf_pushCStr(sb, "i32");
	} break;
	case JIR_TYPE_U64: {
		jx_strbuf_pushCStr(sb, "u64");
	} break;
	case JIR_TYPE_I64: {
		jx_strbuf_pushCStr(sb, "i64");
	} break;
	case JIR_TYPE_F32: {
		jx_strbuf_pushCStr(sb, "f32");
	} break;
	case JIR_TYPE_F64: {
		jx_strbuf_pushCStr(sb, "f64");
	} break;
	case JIR_TYPE_TYPE: {
		jx_strbuf_pushCStr(sb, "type");
	} break;
	case JIR_TYPE_LABEL: {
		jx_strbuf_pushCStr(sb, "label");
	} break;
	case JIR_TYPE_FUNCTION: {
		jx_strbuf_pushCStr(sb, "func");
	} break;
	case JIR_TYPE_STRUCT: {
		if (type->super.m_Name) {
			jx_strbuf_printf(sb, "%%struct.%s", type->super.m_Name);
		} else {
			jx_strbuf_printf(sb, "%%struct.%p", type);
		}
	} break;
	case JIR_TYPE_ARRAY: {
		jx_ir_type_array_t* arrType = jx_ir_typeToArray(type);
		jx_strbuf_printf(sb, "[%u x ", arrType->m_Size);
		jx_ir_typePrint(ctx, arrType->m_BaseType, sb);
		jx_strbuf_pushCStr(sb, "]");
	} break;
	case JIR_TYPE_POINTER: {
#if !JX_IR_CONFIG_PRINT_ABSTRACT_POINTERS
		jx_ir_type_pointer_t* ptr = jx_ir_typeToPointer(type);
		jx_ir_typePrint(ctx, ptr->m_BaseType, sb);
		jx_strbuf_pushCStr(sb, "*");
#else
		jx_strbuf_pushCStr(sb, "ptr");
#endif
	} break;
	default: {
		JX_CHECK(false, "Unknown kind of type");
	} break;
	}
}

bool jx_ir_typeIsSigned(jx_ir_type_t* type)
{
	return (type->m_Flags & JIR_TYPE_FLAGS_IS_SIGNED_Msk) != 0;
}

bool jx_ir_typeIsUnsigned(jx_ir_type_t* type)
{
	return (type->m_Flags & JIR_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0;
}

bool jx_ir_typeIsInteger(jx_ir_type_t* type)
{
	return (type->m_Flags & (JIR_TYPE_FLAGS_IS_SIGNED_Msk | JIR_TYPE_FLAGS_IS_UNSIGNED_Msk)) != 0;
}

bool jx_ir_typeIsIntegral(jx_ir_type_t* type)
{
	return jx_ir_typeIsInteger(type) || type->m_Kind == JIR_TYPE_BOOL;
}

bool jx_ir_typeIsFloatingPoint(jx_ir_type_t* type)
{
	return type->m_Kind == JIR_TYPE_F32 || type->m_Kind == JIR_TYPE_F64;
}

bool jx_ir_typeIsPrimitive(jx_ir_type_t* type)
{
	return type->m_Kind < JIR_TYPE_NUM_PRIMITIVE_TYPES;
}

bool jx_ir_typeIsDerived(jx_ir_type_t* type)
{
	return type->m_Kind >= JIR_TYPE_FIRST_DERIVED;
}

bool jx_ir_typeIsFirstClass(jx_ir_type_t* type)
{
	return (type->m_Kind != JIR_TYPE_VOID && type->m_Kind < JIR_TYPE_TYPE) || type->m_Kind == JIR_TYPE_POINTER;
}

bool jx_ir_typeIsSized(jx_ir_type_t* type)
{
	return true
		&& type->m_Kind != JIR_TYPE_VOID
		&& type->m_Kind != JIR_TYPE_TYPE
		&& type->m_Kind != JIR_TYPE_FUNCTION
		&& type->m_Kind != JIR_TYPE_LABEL
		;
}

bool jx_ir_typeIsComposite(jx_ir_type_t* type)
{
	return false
		|| type->m_Kind == JIR_TYPE_ARRAY
		|| type->m_Kind == JIR_TYPE_STRUCT
		|| type->m_Kind == JIR_TYPE_POINTER
		;
}

bool jx_ir_typeIsFuncPtr(jx_ir_type_t* type)
{
	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(type);
	if (!ptrType) {
		return false;
	}

	return jx_ir_typeToFunction(ptrType->m_BaseType) != NULL;
}

bool jx_ir_typeIsSmallPow2Struct(jx_ir_type_t* type)
{
	if (type->m_Kind != JIR_TYPE_STRUCT) {
		return false;
	}

	const uint32_t sz = (uint32_t)jx_ir_typeGetSize(type);
	return sz <= 8 && jx_isPow2_u32(sz);
}

size_t jx_ir_typeGetAlignment(jx_ir_type_t* type)
{
	size_t align = 0;

	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		align = sizeof(bool);
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_I8: {
		align = sizeof(uint8_t);
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		align = sizeof(uint16_t);
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		align = sizeof(uint32_t);
	} break;
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		align = sizeof(uint64_t);
	} break;
	case JIR_TYPE_F32: {
		align = sizeof(float);
	} break;
	case JIR_TYPE_F64: {
		align = sizeof(double);
	} break;
	case JIR_TYPE_STRUCT: {
		// TODO: Packed structs
		size_t largestMemberAlignment = 1;
		jx_ir_type_struct_t* structType = jx_ir_typeToStruct(type);
		const uint32_t numMembers = structType->m_NumMembers;
		for (uint32_t iMember = 0; iMember < numMembers; ++iMember) {
			jx_ir_type_t* memberType = structType->m_Members[iMember];
			const size_t memberAlignment = jx_ir_typeGetAlignment(memberType);
			if (memberAlignment > largestMemberAlignment) {
				largestMemberAlignment = memberAlignment;
			}
		}
		align = largestMemberAlignment;
	} break;
	case JIR_TYPE_ARRAY: {
		jx_ir_type_array_t* arrType = jx_ir_typeToArray(type);
		const size_t baseTypeAlignment = jx_ir_typeGetAlignment(arrType->m_BaseType);
		align = baseTypeAlignment;
	} break;
	case JIR_TYPE_POINTER: {
		align = sizeof(void*);
	} break;
	case JIR_TYPE_VOID:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL:
	case JIR_TYPE_FUNCTION: {
		JX_CHECK(false, "Type is not sized.");
	} break;
	default:
		JX_CHECK(false, "Unknown kind of type.");
		break;
	}

	return align;
}

size_t jx_ir_typeGetSize(jx_ir_type_t* type)
{
	size_t sz = 0;

	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		sz = sizeof(bool);
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_I8: {
		sz = sizeof(uint8_t);
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		sz = sizeof(uint16_t);
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		sz = sizeof(uint32_t);
	} break;
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		sz = sizeof(uint64_t);
	} break;
	case JIR_TYPE_F32: {
		sz = sizeof(float);
	} break;
	case JIR_TYPE_F64: {
		sz = sizeof(double);
	} break;
	case JIR_TYPE_STRUCT: {
		// TODO: Packed structs
		jx_ir_type_struct_t* structType = jx_ir_typeToStruct(type);
		const uint32_t numMembers = structType->m_NumMembers;
		for (uint32_t iMember = 0; iMember < numMembers; ++iMember) {
			jx_ir_type_t* memberType = structType->m_Members[iMember];
			const size_t memberSize = jx_ir_typeGetSize(memberType);
			const size_t memberAlignment = jx_ir_typeGetAlignment(memberType);

			// Make sure current size is aligned to the member alignment.
			if ((sz & (memberAlignment - 1)) != 0) {
				sz = (sz & ~(memberAlignment - 1)) + memberAlignment;
			}
			sz += memberSize;
		}
	} break;
	case JIR_TYPE_ARRAY: {
		jx_ir_type_array_t* arrType = jx_ir_typeToArray(type);
		const size_t baseTypeSize = jx_ir_typeGetSize(arrType->m_BaseType);
		sz = baseTypeSize * arrType->m_Size;
	} break;
	case JIR_TYPE_POINTER: {
		sz = sizeof(void*);
	} break;
	case JIR_TYPE_VOID:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL:
	case JIR_TYPE_FUNCTION: {
		JX_CHECK(false, "Type is not sized.");
	} break;
	default:
		JX_CHECK(false, "Unknown kind of type.");
		break;
	}

	return sz;
}

uint32_t jx_ir_typeGetIntegerConversionRank(jx_ir_type_t* type)
{
	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		return 1;
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_I8: {
		return 2;
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		return 3;
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		return 4;
	} break;
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		return 5;
	} break;
	default:
		JX_CHECK(false, "Invalid type!");
		break;
	}

	return UINT32_MAX;
}

bool jx_ir_typeCanRepresent(jx_ir_type_t* type, jx_ir_type_t* other)
{
	JX_CHECK(jx_ir_typeIsIntegral(type) && jx_ir_typeIsIntegral(other), "Expected integral types");
	const uint32_t thisRank = jx_ir_typeGetIntegerConversionRank(type);
	const uint32_t otherRank = jx_ir_typeGetIntegerConversionRank(other);

	if (thisRank > otherRank) {
		return true;
	} else if (thisRank < otherRank) {
		return false;
	}

	return jx_ir_typeIsUnsigned(type) == jx_ir_typeIsUnsigned(other);
}

jx_ir_type_kind jx_ir_typeToUnsigned(jx_ir_type_kind type)
{
	switch (type) {
	case JIR_TYPE_BOOL:
	case JIR_TYPE_U8:
	case JIR_TYPE_I8: {
		return JIR_TYPE_U8;
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		return JIR_TYPE_U16;
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		return JIR_TYPE_U32;
	} break;
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		return JIR_TYPE_U64;
	} break;
	default:
		JX_CHECK(false, "Invalid type!");
		break;
	}

	return JIR_TYPE_U64;
}

size_t jx_ir_typeStructGetMemberOffset(jx_ir_type_struct_t* structType, uint32_t memberID)
{
	JX_CHECK(memberID < structType->m_NumMembers, "Invalid struct member index");

	size_t offset = 0;
	const uint32_t numMembers = structType->m_NumMembers;
	for (uint32_t iMember = 0; iMember < memberID; ++iMember) {
		jx_ir_type_t* memberType = structType->m_Members[iMember];
		const size_t memberSize = jx_ir_typeGetSize(memberType);
		const size_t memberAlignment = jx_ir_typeGetAlignment(memberType);

		// Make sure current size is aligned to the member alignment.
		if ((offset & (memberAlignment - 1)) != 0) {
			offset = (offset & ~(memberAlignment - 1)) + memberAlignment;
		}
		offset += memberSize;
	}

	return offset;
}

jx_ir_type_pointer_t* jx_ir_typeToPointer(jx_ir_type_t* type)
{
	return type->m_Kind == JIR_TYPE_POINTER
		? (jx_ir_type_pointer_t*)type
		: NULL
		;
}

jx_ir_type_function_t* jx_ir_typeToFunction(jx_ir_type_t* type)
{
	return type->m_Kind == JIR_TYPE_FUNCTION
		? (jx_ir_type_function_t*)type
		: NULL
		;
}

jx_ir_type_array_t* jx_ir_typeToArray(jx_ir_type_t* type)
{
	return type->m_Kind == JIR_TYPE_ARRAY
		? (jx_ir_type_array_t*)type
		: NULL
		;
}

jx_ir_type_struct_t* jx_ir_typeToStruct(jx_ir_type_t* type)
{
	return type->m_Kind == JIR_TYPE_STRUCT
		? (jx_ir_type_struct_t*)type
		: NULL
		;
}

static bool jir_constCtor(jx_ir_context_t* ctx, jx_ir_constant_t* c, jx_ir_type_t* type)
{
	return jir_userCtor(ctx, &c->super, type, JIR_VALUE_CONSTANT, NULL, 0);
}

static void jir_constDtor(jx_ir_context_t* ctx, jx_ir_constant_t* c)
{
	jir_userDtor(ctx, &c->super);
}

jx_ir_constant_t* jx_ir_constGetBool(jx_ir_context_t* ctx, bool val)
{
	return ctx->m_ConstBool[val ? 1 : 0];
}

jx_ir_constant_t* jx_ir_constGetI8(jx_ir_context_t* ctx, int8_t val)
{
	return jir_constGetIntSigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_I8], (int64_t)val);
}

jx_ir_constant_t* jx_ir_constGetU8(jx_ir_context_t* ctx, uint8_t val)
{
	return jir_constGetIntUnsigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_U8], (uint64_t)val);
}

jx_ir_constant_t* jx_ir_constGetI16(jx_ir_context_t* ctx, int16_t val)
{
	return jir_constGetIntSigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_I16], (int64_t)val);
}

jx_ir_constant_t* jx_ir_constGetU16(jx_ir_context_t* ctx, uint16_t val)
{
	return jir_constGetIntUnsigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_U16], (uint64_t)val);
}

jx_ir_constant_t* jx_ir_constGetI32(jx_ir_context_t* ctx, int32_t val)
{
	return jir_constGetIntSigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_I32], (int64_t)val);
}

jx_ir_constant_t* jx_ir_constGetU32(jx_ir_context_t* ctx, uint32_t val)
{
	return jir_constGetIntUnsigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_U32], (uint64_t)val);
}

jx_ir_constant_t* jx_ir_constGetI64(jx_ir_context_t* ctx, int64_t val)
{
	return jir_constGetIntSigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_I64], val);
}

jx_ir_constant_t* jx_ir_constGetU64(jx_ir_context_t* ctx, uint64_t val)
{
	return jir_constGetIntUnsigned(ctx, ctx->m_BuildinTypes[JIR_TYPE_U64], val);
}

jx_ir_constant_t* jx_ir_constGetInteger(jx_ir_context_t* ctx, jx_ir_type_kind type, int64_t val)
{
	switch (type) {
	case JIR_TYPE_I8:
	case JIR_TYPE_I16:
	case JIR_TYPE_I32:
	case JIR_TYPE_I64:
		return jir_constGetIntSigned(ctx, ctx->m_BuildinTypes[type], val);
	case JIR_TYPE_U8:
	case JIR_TYPE_U16:
	case JIR_TYPE_U32:
	case JIR_TYPE_U64:
		return jir_constGetIntUnsigned(ctx, ctx->m_BuildinTypes[type], (uint64_t)val);
	default:
		JX_CHECK(false, "Not an integer type!");
		break;
	}

	return NULL;
}

jx_ir_constant_t* jx_ir_constGetF32(jx_ir_context_t* ctx, float val)
{
	jx_ir_type_t* type = ctx->m_BuildinTypes[JIR_TYPE_F32];

	jx_ir_constant_t* key = &(jx_ir_constant_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_CONSTANT,
				.m_Type = type
			}
		},
		.u.m_F64 = (double)val
	};

	jx_ir_constant_t** cachedTypePtr = (jx_ir_constant_t**)jx_hashmapGet(ctx->m_ConstMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_constant_t* ci = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
	if (!ci) {
		return NULL;
	}

	jx_memset(ci, 0, sizeof(jx_ir_constant_t));
	jir_constCtor(ctx, ci, type);
	ci->u.m_F64 = (double)val;

	jx_hashmapSet(ctx->m_ConstMap, &ci);

	return ci;
}

jx_ir_constant_t* jx_ir_constGetF64(jx_ir_context_t* ctx, double val)
{
	jx_ir_type_t* type = ctx->m_BuildinTypes[JIR_TYPE_F64];

	jx_ir_constant_t* key = &(jx_ir_constant_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_CONSTANT,
				.m_Type = type
			}
		},
		.u.m_F64 = val
	};

	jx_ir_constant_t** cachedTypePtr = (jx_ir_constant_t**)jx_hashmapGet(ctx->m_ConstMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_constant_t* ci = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
	if (!ci) {
		return NULL;
	}

	jx_memset(ci, 0, sizeof(jx_ir_constant_t));
	jir_constCtor(ctx, ci, type);
	ci->u.m_F64 = val;

	jx_hashmapSet(ctx->m_ConstMap, &ci);

	return ci;
}

jx_ir_constant_t* jx_ir_constGetFloat(jx_ir_context_t* ctx, jx_ir_type_kind type, double val)
{
	switch (type) {
	case JIR_TYPE_F32:
		return jx_ir_constGetF32(ctx, (float)val);
	case JIR_TYPE_F64:
		return jx_ir_constGetF64(ctx, val);
	default:
		JX_CHECK(false, "Unknown floating point type!");
		break;
	}

	return NULL;
}

jx_ir_constant_t* jx_ir_constArray(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t numValues, jx_ir_constant_t** values)
{
	JX_CHECK(type->m_Kind == JIR_TYPE_ARRAY, "Constant array type must be an array type");

	jx_ir_constant_t* c = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
	if (!c) {
		return NULL;
	}

	jx_memset(c, 0, sizeof(jx_ir_constant_t));
	jir_constCtor(ctx, c, type);
	jx_array_reserve(c->super.m_OperandArr, numValues);
	for (uint32_t iVal = 0; iVal < numValues; ++iVal) {
		jir_userAddOperand(ctx, &c->super, jx_ir_constToValue(values[iVal]));
	}

	jx_ir_constant_t** cachedTypePtr = (jx_ir_constant_t**)jx_hashmapGet(ctx->m_ConstMap, &c);
	if (cachedTypePtr) {
		jir_constDtor(ctx, c);
		return *cachedTypePtr;
	}

	jx_hashmapSet(ctx->m_ConstMap, &c);

	return c;
}

jx_ir_constant_t* jx_ir_constStruct(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t numMembers, jx_ir_constant_t** memberValues)
{
	JX_CHECK(type->m_Kind == JIR_TYPE_STRUCT, "Constant struct type must be a struct type");

	jx_ir_type_struct_t* structType = (jx_ir_type_struct_t*)type;
	if (structType->m_NumMembers != numMembers) {
		JX_CHECK(false, "Invalid number of members in constant struct.");
		return NULL;
	}

	jx_ir_constant_t* c = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
	if (!c) {
		return NULL;
	}

	jx_memset(c, 0, sizeof(jx_ir_constant_t));
	jir_constCtor(ctx, c, type);
	jx_array_reserve(c->super.m_OperandArr, numMembers);
	for (uint32_t iVal = 0; iVal < numMembers; ++iVal) {
		jir_userAddOperand(ctx, &c->super, jx_ir_constToValue(memberValues[iVal]));
	}

	jx_ir_constant_t** cachedTypePtr = (jx_ir_constant_t**)jx_hashmapGet(ctx->m_ConstMap, &c);
	if (cachedTypePtr) {
		jir_constDtor(ctx, c);
		return *cachedTypePtr;
	}

	jx_hashmapSet(ctx->m_ConstMap, &c);

	return c;
}

jx_ir_constant_t* jx_ir_constPointer(jx_ir_context_t* ctx, jx_ir_type_t* type, void* ptr)
{
	return jir_constGetPointer(ctx, type, (uintptr_t)ptr);
}

jx_ir_constant_t* jx_ir_constPointerNull(jx_ir_context_t* ctx, jx_ir_type_t* type)
{
	return jir_constGetPointer(ctx, type, 0);
}

jx_ir_constant_t* jx_ir_constPointerToGlobalVal(jx_ir_context_t* ctx, jx_ir_global_value_t* gv)
{
	jx_ir_constant_t* c = jir_constGetPointer(ctx, jx_ir_globalValToValue(gv)->m_Type, (uintptr_t)gv);
	if (!c) {
		return NULL;
	}

	c->super.super.m_Flags |= JIR_VALUE_FLAGS_CONST_GLOBAL_VAL_PTR_Msk;
	
	return c;
}

jx_ir_constant_t* jx_ir_constGetZero(jx_ir_context_t* ctx, jx_ir_type_t* type)
{
	jx_ir_constant_t* c = NULL;

	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		c = ctx->m_ConstBool[0];
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_U16:
	case JIR_TYPE_U32:
	case JIR_TYPE_U64: {
		c = jir_constGetIntUnsigned(ctx, type, 0);
	} break;
	case JIR_TYPE_I8:
	case JIR_TYPE_I16:
	case JIR_TYPE_I32:
	case JIR_TYPE_I64: {
		c = jir_constGetIntSigned(ctx, type, 0);
	} break;
	case JIR_TYPE_F32: {
		c = jx_ir_constGetF32(ctx, 0.0f);
	} break;
	case JIR_TYPE_F64: {
		c = jx_ir_constGetF64(ctx, 0.0);
	} break;
	case JIR_TYPE_STRUCT: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JIR_TYPE_ARRAY: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JIR_TYPE_POINTER: {
		c = jx_ir_constPointerNull(ctx, type);
	} break;
	case JIR_TYPE_VOID:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL:
	case JIR_TYPE_FUNCTION:
	default: {
		// Cannot create 0 constant
	} break;
	}

	return c;
}

jx_ir_constant_t* jx_ir_constGetOnes(jx_ir_context_t* ctx, jx_ir_type_t* type)
{
	jx_ir_constant_t* c = NULL;
	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		c = ctx->m_ConstBool[1];
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_U16:
	case JIR_TYPE_U32:
	case JIR_TYPE_U64: {
		c = jir_constGetIntUnsigned(ctx, type, UINT64_MAX);
	} break;
	case JIR_TYPE_I8:
	case JIR_TYPE_I16:
	case JIR_TYPE_I32:
	case JIR_TYPE_I64: {
		c = jir_constGetIntSigned(ctx, type, -1);
	} break;
	case JIR_TYPE_VOID:
	case JIR_TYPE_F32:
	case JIR_TYPE_F64:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL:
	case JIR_TYPE_FUNCTION:
	case JIR_TYPE_STRUCT:
	case JIR_TYPE_ARRAY:
	case JIR_TYPE_POINTER: 
	default: {
		//
	} break;
	}

	return c;
}

void jx_ir_constPrint(jx_ir_context_t* ctx, jx_ir_constant_t* c, jx_string_buffer_t* sb)
{
	jx_ir_value_t* cVal = jx_ir_constToValue(c);
	jx_ir_type_t* cType = cVal->m_Type;
	switch (cType->m_Kind) {
	case JIR_TYPE_BOOL: {
		jx_strbuf_printf(sb, "%s", c->u.m_Bool ? "true" : "false");
	} break;
	case JIR_TYPE_I8: {
		jx_strbuf_printf(sb, "%d", (int8_t)c->u.m_I64);
	} break;
	case JIR_TYPE_I16: {
		jx_strbuf_printf(sb, "%d", (int16_t)c->u.m_I64);
	} break;
	case JIR_TYPE_I32: {
		jx_strbuf_printf(sb, "%d", (int32_t)c->u.m_I64);
	} break;
	case JIR_TYPE_I64: {
		jx_strbuf_printf(sb, "%lld", c->u.m_I64);
	} break;
	case JIR_TYPE_U8: {
		jx_strbuf_printf(sb, "%u", (uint8_t)c->u.m_U64);
	} break;
	case JIR_TYPE_U16: {
		jx_strbuf_printf(sb, "%u", (uint16_t)c->u.m_U64);
	} break;
	case JIR_TYPE_U32: {
		jx_strbuf_printf(sb, "%u", (uint32_t)c->u.m_U64);
	} break;
	case JIR_TYPE_U64: {
		jx_strbuf_printf(sb, "%llu", c->u.m_U64);
	} break;
	case JIR_TYPE_F32: {
		jx_strbuf_printf(sb, "%f", (float)c->u.m_F64);
	} break;
	case JIR_TYPE_F64: {
		jx_strbuf_printf(sb, "%f", c->u.m_F64);
	} break;
	case JIR_TYPE_ARRAY: {
		jx_ir_type_array_t* arrType = jx_ir_typeToArray(jx_ir_constToValue(c)->m_Type);
		JX_CHECK(arrType, "Something has gone really wrong!");
		if (arrType->m_BaseType->m_Kind == JIR_TYPE_I8) {
			jx_strbuf_pushCStr(sb, "c\"");
			const uint32_t numOperands = (uint32_t)jx_array_sizeu(c->super.m_OperandArr);
			for (uint32_t iElem = 0; iElem < numOperands; ++iElem) {
				jx_ir_constant_t* ichar = jx_ir_valueToConst(c->super.m_OperandArr[iElem]->m_Value);
				char ch = (char)ichar->u.m_I64;
				if (jx_isprint(ch)) {
					jx_strbuf_push(sb, &ch, 1);
				} else {
					jx_strbuf_printf(sb, "\\%02X", ch);
				}
			}
			jx_strbuf_pushCStr(sb, "\"");
		} else {
			jx_strbuf_pushCStr(sb, "[");
			const uint32_t numOperands = (uint32_t)jx_array_sizeu(c->super.m_OperandArr);
			for (uint32_t iElem = 0; iElem < numOperands; ++iElem) {
				if (iElem != 0) {
					jx_strbuf_pushCStr(sb, ", ");
				}
				jx_ir_typePrint(ctx, arrType->m_BaseType, sb);
				jx_strbuf_pushCStr(sb, " ");
				jx_ir_constPrint(ctx, jx_ir_valueToConst(c->super.m_OperandArr[iElem]->m_Value), sb);
			}
			jx_strbuf_pushCStr(sb, "]");
		}
	} break;
	case JIR_TYPE_POINTER: {
		if ((cVal->m_Flags & JIR_VALUE_FLAGS_CONST_GLOBAL_VAL_PTR_Msk) != 0) {
			jx_ir_global_value_t* gv = (jx_ir_global_value_t*)c->u.m_Ptr;
			jx_ir_value_t* gvVal = jx_ir_globalValToValue(gv);
			JX_CHECK(gvVal->m_Name, "Unnamed global value?");
			jx_strbuf_printf(sb, "@%s", gvVal->m_Name);
		} else {
			jx_strbuf_printf(sb, "0x%p", c->u.m_Ptr);
		}
	} break;
	case JIR_TYPE_STRUCT: {
		jx_strbuf_pushCStr(sb, "{");
		const uint32_t numOperands = (uint32_t)jx_array_sizeu(c->super.m_OperandArr);
		for (uint32_t iElem = 0; iElem < numOperands; ++iElem) {
			jx_ir_value_t* memberVal = c->super.m_OperandArr[iElem]->m_Value;

			if (iElem != 0) {
				jx_strbuf_pushCStr(sb, ", ");
			}

			jx_ir_typePrint(ctx, memberVal->m_Type, sb);
			jx_strbuf_pushCStr(sb, " ");
			jx_ir_constPrint(ctx, jx_ir_valueToConst(memberVal), sb);
		}
		jx_strbuf_pushCStr(sb, "}");
	} break;
	default:
		JX_CHECK(false, "Not implemented yet?");
		break;
	}
}

static bool jir_globalValCtor(jx_ir_context_t* ctx, jx_ir_global_value_t* gv, jx_ir_type_t* type, jx_ir_value_kind valKind, jx_ir_linkage_kind linkageKind, const char* name)
{
	jx_memset(gv, 0, sizeof(jx_ir_global_value_t));
	if (!jir_userCtor(ctx, &gv->super, type, valKind, name, 0)) {
		return false;
	}

	gv->m_LinkageKind = linkageKind;
	gv->m_ParentModule = NULL;

	return true;
}

static void jir_globalValDtor(jx_ir_context_t* ctx, jx_ir_global_value_t* gv)
{
	jir_userDtor(ctx, &gv->super);
}

void jx_ir_globalValSetParentModule(jx_ir_context_t* ctx, jx_ir_global_value_t* gv, jx_ir_module_t* mod)
{
	gv->m_ParentModule = mod;
}

static bool jir_globalVarCtor(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv, jx_ir_type_t* type, bool isConstant, jx_ir_linkage_kind linkageKind, jx_ir_constant_t* initializer, const char* name)
{
	jx_memset(gv, 0, sizeof(jx_ir_global_variable_t));
	if (!jir_globalValCtor(ctx, &gv->super, jx_ir_typeGetPointer(ctx, type), JIR_VALUE_GLOBAL_VARIABLE, linkageKind, name)) {
		return false;
	}

	gv->m_IsConstantGlobal = isConstant;

	if (initializer) {
		jir_userAddOperand(ctx, jx_ir_globalVarToUser(gv), jx_ir_constToValue(initializer));
	}

	return true;
}

static void jir_globalVarDtor(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv)
{
	jir_globalValDtor(ctx, &gv->super);
}

static bool jir_argCtor(jx_ir_context_t* ctx, jx_ir_argument_t* arg, jx_ir_type_t* type, const char* name, uint32_t id)
{
	jx_memset(arg, 0, sizeof(jx_ir_argument_t));
	if (!jir_valueCtor(ctx, &arg->super, type, JIR_VALUE_ARGUMENT, name)) {
		return false;
	}

	arg->m_ID = id;

	return true;
}

static void jir_argDtor(jx_ir_context_t* ctx, jx_ir_argument_t* arg)
{
	jir_valueDtor(ctx, &arg->super);
}

static bool jir_bbCtor(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, const char* name)
{
	jx_memset(bb, 0, sizeof(jx_ir_basic_block_t));
	if (!jir_valueCtor(ctx, &bb->super, ctx->m_BuildinTypes[JIR_TYPE_LABEL], JIR_VALUE_BASIC_BLOCK, name)) {
		return false;
	}

	bb->m_PredArr = (jx_ir_basic_block_t**)jx_array_create(ctx->m_Allocator);
	bb->m_SuccArr = (jx_ir_basic_block_t**)jx_array_create(ctx->m_Allocator);
	if (!bb->m_PredArr || !bb->m_SuccArr) {
		return false;
	}

	jx_array_reserve(bb->m_SuccArr, 2);

	return true;
}

static void jir_bbDtor(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb)
{
	jx_ir_instruction_t* instr = bb->m_InstrListHead;
	while (instr) {
		jx_ir_instruction_t* next = instr->m_Next;
		jx_ir_bbRemoveInstr(ctx, bb, instr);
		jir_instrDtor(ctx, instr);
		instr = next;
	}
	bb->m_InstrListHead = NULL;

	jx_array_free(bb->m_PredArr);
	jx_array_free(bb->m_SuccArr);

	jir_valueDtor(ctx, &bb->super);
}

static bool jir_bbIncludesInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr)
{
	jx_ir_instruction_t* bbInstr = bb->m_InstrListHead;
	while (bbInstr) {
		if (bbInstr == instr) {
			return true;
		}

		bbInstr = bbInstr->m_Next;
	}

	return false;
}

static void jir_bbAddPred(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* pred)
{
	// TODO: Check if pred is already in the predecessor list
	jx_array_push_back(bb->m_PredArr, pred);
}

static void jir_bbAddSucc(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* succ)
{
	JX_CHECK(jx_array_sizeu(bb->m_SuccArr) <= 1, "Too many successors!");
	jx_array_push_back(bb->m_SuccArr, succ);
}

static void jir_bbRemovePred(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* pred)
{
	// Find predecessor
	bool found = false;
	const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
	for (uint32_t iPred = 0; iPred < numPred; ++iPred) {
		if (bb->m_PredArr[iPred] == pred) {
			found = true;
			jx_array_del(bb->m_PredArr, iPred);
			break;
		}
	}

	if (!found) {
		JX_CHECK(false, "Predecessor not found in basic block's predecessor list.");
		return;
	}

	// Update all phi instructions (if any) and remove this predecessor
	jx_ir_instruction_t* instr = bb->m_InstrListHead;
	while (instr && instr->m_OpCode == JIR_OP_PHI) {
		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
			jx_ir_value_t* phiBB = instr->super.m_OperandArr[iOperand + 1]->m_Value;
			if (jx_ir_valueToBasicBlock(phiBB) == pred) {
				jir_userRemoveOperand(ctx, jx_ir_instrToUser(instr), iOperand + 1);
				jir_userRemoveOperand(ctx, jx_ir_instrToUser(instr), iOperand + 0);
				break;
			}
		}

		instr = instr->m_Next;
	}
}

static void jir_bbRemoveSucc(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* succ)
{
	bool found = false;
	const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
	for (uint32_t iSucc = 0; iSucc < numSucc; ++iSucc) {
		if (bb->m_SuccArr[iSucc] == succ) {
			found = true;
			jx_array_del(bb->m_SuccArr, iSucc);
			break;
		}
	}

	JX_CHECK(found, "Successor not found in basic block's successor list.");
}

static bool jir_bbHasPred(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* pred)
{
	const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
	for (uint32_t iPred = 0; iPred < numPred; ++iPred) {
		if (bb->m_PredArr[iPred] == pred) {
			return true;
		}
	}

	return false;
}

static bool jir_bbHasSucc(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_basic_block_t* succ)
{
	const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
	for (uint32_t iSucc = 0; iSucc < numSucc; ++iSucc) {
		if (bb->m_SuccArr[iSucc] == succ) {
			return true;
		}
	}

	return false;
}

static bool jir_funcCtor(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_type_t* type, jx_ir_linkage_kind linkageKind, const char* name)
{
	if (type->m_Kind != JIR_TYPE_FUNCTION) {
		JX_CHECK(false, "Function's type should be a function type");
		return false;
	}

	// Make sure the return type is a first-class type or void
	jx_ir_type_function_t* funcType = (jx_ir_type_function_t*)type;
	if (funcType->m_RetType->m_Kind != JIR_TYPE_VOID && !jx_ir_typeIsFirstClass(funcType->m_RetType)) {
		JX_CHECK(false, "Function must return a first-class type.");
		return false;
	}

	jx_memset(func, 0, sizeof(jx_ir_function_t));
	if (!jir_globalValCtor(ctx, &func->super, jx_ir_typeGetPointer(ctx, type), JIR_VALUE_FUNCTION, linkageKind, name)) {
		return false;
	}

	// Create arguments list
	jx_ir_argument_t head = { 0 };
	jx_ir_argument_t* prevArg = &head;
	const uint32_t numArgs = funcType->m_NumArgs;
	for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
		jx_ir_type_t* argType = funcType->m_Args[iArg];
		if (argType->m_Kind == JIR_TYPE_VOID) {
			JX_CHECK(false, "Function argument cannot be void");
			return false;
		}

		jx_ir_argument_t* arg = (jx_ir_argument_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_argument_t));
		if (!arg) {
			return false;
		}
		if (!jir_argCtor(ctx, arg, argType, NULL, iArg)) {
			return false;
		}

		arg->m_ParentFunc = func;

		// Insert into linked list
		arg->m_Prev = prevArg;
		prevArg->m_Next = arg;
		prevArg = arg;
	}
	func->m_ArgListHead = head.m_Next;
	if (func->m_ArgListHead) {
		func->m_ArgListHead->m_Prev = NULL;
	}

	return true;
}

static void jir_funcDtor(jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	// Before destructing all basic blocks, remove all instructions.
	// This makes sure the predecessor/successor arrays are cleared correctly
	// before destroying the basic blocks. Otherwise asserts are triggered
	// when trying to remove a pred/succ if they are already 
	// destroyed, because e.g. the block already appeared in the function's
	// block list.
	{
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_ir_instruction_t* nextInstr = instr->m_Next;
				jx_ir_bbRemoveInstr(ctx, bb, instr);
				jir_instrDtor(ctx, instr);
				instr = nextInstr;
			}
			bb->m_InstrListHead = NULL;
			bb = bb->m_Next;
		}
	}

	// Clear basic block list
	{
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_basic_block_t* next = bb->m_Next;
			jir_bbDtor(ctx, bb);
			bb = next;
		}
		func->m_BasicBlockListHead = NULL;
	}

	// Clear arguments list
	jx_ir_argument_t* arg = func->m_ArgListHead;
	while (arg) {
		jx_ir_argument_t* next = arg->m_Next;
		jir_argDtor(ctx, arg);
		arg = next;
	}
	func->m_ArgListHead = NULL;

	jir_globalValDtor(ctx, &func->super);
}

static bool jir_instrCtor(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, jx_ir_type_t* type, uint32_t opcode, const char* name, uint32_t numOperands)
{
	jx_memset(instr, 0, sizeof(jx_ir_instruction_t));
	if (!jir_userCtor(ctx, &instr->super, type, JIR_VALUE_INSTRUCTION, name, numOperands)) {
		return false;
	}

	instr->m_ParentBB = NULL;
	instr->m_OpCode = opcode;

	return true;
}

static void jir_instrDtor(jx_ir_context_t* ctx, jx_ir_instruction_t* instr)
{
	jir_userDtor(ctx, &instr->super);
}

static void jir_instrAddOperand(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, jx_ir_value_t* operand)
{
	jir_userAddOperand(ctx, jx_ir_instrToUser(instr), operand);
}

static void jir_instrRemoveOperand(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, uint32_t operandID)
{
	jir_userRemoveOperand(ctx, jx_ir_instrToUser(instr), operandID);
}

static uint64_t jir_typeHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	JX_UNUSED(udata);
	const jx_ir_type_t* type = *(const jx_ir_type_t**)item;

	uint64_t hash = jx_hashFNV1a(&type->m_Kind, sizeof(jx_ir_type_kind), seed0, seed1);
	hash = jx_hashFNV1a(&type->m_Flags, sizeof(uint32_t), hash, seed1);
	switch (type->m_Kind) {
	case JIR_TYPE_VOID:
	case JIR_TYPE_BOOL:
	case JIR_TYPE_U8:
	case JIR_TYPE_I8:
	case JIR_TYPE_U16:
	case JIR_TYPE_I16:
	case JIR_TYPE_U32:
	case JIR_TYPE_I32:
	case JIR_TYPE_U64:
	case JIR_TYPE_I64:
	case JIR_TYPE_F32:
	case JIR_TYPE_F64:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL: {
		// Nothing else to hash.
	} break;
	case JIR_TYPE_FUNCTION: {
		const jx_ir_type_function_t* funcType = (const jx_ir_type_function_t*)type;
		hash = jir_typeHashCallback(&funcType->m_RetType, hash, seed1, udata);

		const uint32_t numArgs = funcType->m_NumArgs;
		hash = jx_hashFNV1a(&numArgs, sizeof(uint32_t), hash, seed1);
		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			hash = jir_typeHashCallback(&funcType->m_Args[iArg], hash, seed1, udata);
		}

		hash = jx_hashFNV1a(&funcType->m_IsVarArg, sizeof(bool), hash, seed1);
	} break;
	case JIR_TYPE_STRUCT: {
		const jx_ir_type_struct_t* structType = (const jx_ir_type_struct_t*)type;
		hash = jx_hashFNV1a(&structType->m_UniqueID, sizeof(uint64_t), hash, seed1);
	} break;
	case JIR_TYPE_ARRAY: {
		const jx_ir_type_array_t* arrType = (const jx_ir_type_array_t*)type;
		hash = jir_typeHashCallback(&arrType->m_BaseType, hash, seed1, udata);
		hash = jx_hashFNV1a(&arrType->m_Size, sizeof(uint32_t), hash, seed1);
	} break;
	case JIR_TYPE_POINTER: {
		const jx_ir_type_pointer_t* ptrType = (const jx_ir_type_pointer_t*)type;
		hash = jir_typeHashCallback(&ptrType->m_BaseType, hash, seed1, udata);
	} break;
	default:
		JX_CHECK(false, "Unknown type kind!");
		break;
	}

	return hash;
}

static int32_t jir_typeCompareCallback(const void* a, const void* b, void* udata)
{
	JX_UNUSED(udata);
	const jx_ir_type_t* typeA = *(const jx_ir_type_t**)a;
	const jx_ir_type_t* typeB = *(const jx_ir_type_t**)b;
	if (typeA->m_Kind != typeB->m_Kind) {
		return typeA->m_Kind < typeB->m_Kind ? -1 : 1;
	}

	int32_t res = 0;
	switch (typeA->m_Kind) {
	case JIR_TYPE_VOID:
	case JIR_TYPE_BOOL:
	case JIR_TYPE_U8:
	case JIR_TYPE_I8:
	case JIR_TYPE_U16:
	case JIR_TYPE_I16:
	case JIR_TYPE_U32:
	case JIR_TYPE_I32:
	case JIR_TYPE_U64:
	case JIR_TYPE_I64:
	case JIR_TYPE_F32:
	case JIR_TYPE_F64:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL: {
		// Both types are identical.
	} break;
	case JIR_TYPE_FUNCTION: {
		const jx_ir_type_function_t* funcTypeA = (const jx_ir_type_function_t*)typeA;
		const jx_ir_type_function_t* funcTypeB = (const jx_ir_type_function_t*)typeB;
		res = jir_typeCompareCallback(&funcTypeA->m_RetType, &funcTypeB->m_RetType, udata);
		if (res == 0) {
			if (funcTypeA->m_NumArgs != funcTypeB->m_NumArgs) {
				res = funcTypeA->m_NumArgs < funcTypeB->m_NumArgs ? -1 : 1;
			} else {
				const uint32_t numArgs = funcTypeA->m_NumArgs;
				for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
					res = jir_typeCompareCallback(&funcTypeA->m_Args[iArg], &funcTypeB->m_Args[iArg], udata);
					if (res != 0) {
						break;
					}
				}
			}
		}
	} break;
	case JIR_TYPE_STRUCT: {
		const jx_ir_type_struct_t* structTypeA = (const jx_ir_type_struct_t*)typeA;
		const jx_ir_type_struct_t* structTypeB = (const jx_ir_type_struct_t*)typeB;
		res = structTypeA->m_UniqueID < structTypeB->m_UniqueID
			? -1
			: (structTypeA->m_UniqueID > structTypeB->m_UniqueID ? 1 : 0)
			;
	} break;
	case JIR_TYPE_ARRAY: {
		const jx_ir_type_array_t* arrTypeA = (const jx_ir_type_array_t*)typeA;
		const jx_ir_type_array_t* arrTypeB = (const jx_ir_type_array_t*)typeB;
		if (arrTypeA->m_Size != arrTypeB->m_Size) {
			res = arrTypeA->m_Size < arrTypeB->m_Size ? -1 : 1;
		} else {
			res = jir_typeCompareCallback(&arrTypeA->m_BaseType, &arrTypeB->m_BaseType, udata);
		}
	} break;
	case JIR_TYPE_POINTER: {
		const jx_ir_type_pointer_t* ptrTypeA = (const jx_ir_type_pointer_t*)typeA;
		const jx_ir_type_pointer_t* ptrTypeB = (const jx_ir_type_pointer_t*)typeB;
		res = jir_typeCompareCallback(&ptrTypeA->m_BaseType, &ptrTypeB->m_BaseType, udata);
	} break;
	default:
		JX_CHECK(false, "Unknown type kind!");
		res = -1;
		break;
	}

	return res;
}

static uint64_t jir_constHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jx_ir_constant_t* c = *(const jx_ir_constant_t**)item;
	const jx_ir_type_t* type = c->super.super.m_Type;

	uint64_t hash = jx_hashFNV1a(&type->m_Kind, sizeof(uint32_t), seed0, seed1);
	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		hash = jx_hashFNV1a(&c->u.m_Bool, sizeof(bool), hash, seed1);
	} break;
	case JIR_TYPE_I8:
	case JIR_TYPE_U8:
	case JIR_TYPE_I16:
	case JIR_TYPE_U16:
	case JIR_TYPE_I32:
	case JIR_TYPE_U32:
	case JIR_TYPE_I64:
	case JIR_TYPE_U64: {
		hash = jx_hashFNV1a(&c->u.m_U64, sizeof(uint64_t), hash, seed1);
	} break;
	case JIR_TYPE_F32:
	case JIR_TYPE_F64: {
		hash = jx_hashFNV1a(&c->u.m_F64, sizeof(double), hash, seed1);
	} break;
	case JIR_TYPE_STRUCT:
	case JIR_TYPE_ARRAY: {
		const uint32_t numElements = (uint32_t)jx_array_sizeu(c->super.m_OperandArr);
		hash = jx_hashFNV1a(&numElements, sizeof(uint32_t), hash, seed1);
		for (uint32_t iElem = 0; iElem < numElements; ++iElem) {
			hash = jir_constHashCallback(&c->super.m_OperandArr[iElem]->m_Value, hash, seed1, udata);
		}
	} break;
	case JIR_TYPE_POINTER: {
		hash = jx_hashFNV1a(&c->u.m_Ptr, sizeof(uintptr_t), hash, seed1);
	} break;
	case JIR_TYPE_FUNCTION:
	case JIR_TYPE_VOID:
	case JIR_TYPE_TYPE: 
	case JIR_TYPE_LABEL: 
	default: {
		JX_CHECK(false, "Unknown or unsupported constant.");
	} break;
	}

	return hash;
}

static int32_t jir_constCompareCallback(const void* a, const void* b, void* udata)
{
	const jx_ir_constant_t* cA = *(const jx_ir_constant_t**)a;
	const jx_ir_constant_t* cB = *(const jx_ir_constant_t**)b;

	const jx_ir_type_t* typeA = cA->super.super.m_Type;
	const jx_ir_type_t* typeB = cB->super.super.m_Type;
	if (typeA->m_Kind != typeB->m_Kind) {
		return typeA->m_Kind < typeB->m_Kind ? -1 : 1;
	}

	int32_t res = 0;
	switch (typeA->m_Kind) {
	case JIR_TYPE_BOOL: {
		res = cA->u.m_Bool < cB->u.m_Bool
			? -1
			: 0
			;
	} break;
	case JIR_TYPE_I8:
	case JIR_TYPE_U8:
	case JIR_TYPE_I16:
	case JIR_TYPE_U16:
	case JIR_TYPE_I32:
	case JIR_TYPE_U32:
	case JIR_TYPE_I64:
	case JIR_TYPE_U64: {
		res = cA->u.m_U64 < cB->u.m_U64
			? -1
			: (cA->u.m_U64 > cB->u.m_U64 ? 1 : 0)
			;
	} break;
	case JIR_TYPE_F32:
	case JIR_TYPE_F64: {
		res = cA->u.m_F64 < cB->u.m_F64
			? -1
			: (cA->u.m_F64 > cB->u.m_F64 ? 1 : 0)
			;
	} break;
	case JIR_TYPE_STRUCT:
	case JIR_TYPE_ARRAY: {
		const uint32_t numElementsA = (uint32_t)jx_array_sizeu(cA->super.m_OperandArr);
		const uint32_t numElementsB = (uint32_t)jx_array_sizeu(cB->super.m_OperandArr);
		if (numElementsA == numElementsB) {
			for (uint32_t iElem = 0; iElem < numElementsA; ++iElem) {
				res = jir_constCompareCallback(&cA->super.m_OperandArr[iElem]->m_Value, &cB->super.m_OperandArr[iElem]->m_Value, udata);
				if (res != 0) {
					break;
				}
			}
		} else {
			res = numElementsA < numElementsB
				? -1
				: 1
				;
		}
	} break;
	case JIR_TYPE_POINTER: {
		res = cA->u.m_Ptr < cB->u.m_Ptr
			? -1
			: (cA->u.m_Ptr > cB->u.m_Ptr ? 1 : 0)
			;
	} break;
	case JIR_TYPE_VOID:
	case JIR_TYPE_FUNCTION:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL: 
	default: {
		JX_CHECK(false, "Unknown or unsupported constant.");
	} break;
	}

	return res;
}

static jx_ir_constant_t* jir_constGetIntSigned(jx_ir_context_t* ctx, jx_ir_type_t* type, int64_t val)
{
	jx_ir_constant_t* key = &(jx_ir_constant_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_CONSTANT,
				.m_Type = type
			}
		},
		.u.m_I64 = val
	};

	jx_ir_constant_t** cachedTypePtr = (jx_ir_constant_t**)jx_hashmapGet(ctx->m_ConstMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_constant_t* ci = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
	if (!ci) {
		return NULL;
	}

	jx_memset(ci, 0, sizeof(jx_ir_constant_t));
	jir_constCtor(ctx, ci, type);
	ci->u.m_I64 = val;

	jx_hashmapSet(ctx->m_ConstMap, &ci);

	return ci;
}

static jx_ir_constant_t* jir_constGetIntUnsigned(jx_ir_context_t* ctx, jx_ir_type_t* type, uint64_t val)
{
	jx_ir_constant_t* key = &(jx_ir_constant_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_CONSTANT,
				.m_Type = type
			}
		},
		.u.m_U64 = val
	};

	jx_ir_constant_t** cachedTypePtr = (jx_ir_constant_t**)jx_hashmapGet(ctx->m_ConstMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_constant_t* ci = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
	if (!ci) {
		return NULL;
	}

	jx_memset(ci, 0, sizeof(jx_ir_constant_t));
	jir_constCtor(ctx, ci, type);
	ci->u.m_U64 = val;

	jx_hashmapSet(ctx->m_ConstMap, &ci);

	return ci;
}

static jx_ir_constant_t* jir_constGetPointer(jx_ir_context_t* ctx, jx_ir_type_t* type, uintptr_t val)
{
	jx_ir_constant_t* key = &(jx_ir_constant_t){
		.super = {
			.super = {
				.m_Kind = JIR_VALUE_CONSTANT,
				.m_Type = type
			}
		},
		.u.m_Ptr = val
	};

	jx_ir_constant_t** cachedTypePtr = (jx_ir_constant_t**)jx_hashmapGet(ctx->m_ConstMap, &key);
	if (cachedTypePtr) {
		return *cachedTypePtr;
	}

	jx_ir_constant_t* ci = (jx_ir_constant_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_ir_constant_t));
	if (!ci) {
		return NULL;
	}

	jx_memset(ci, 0, sizeof(jx_ir_constant_t));
	jir_constCtor(ctx, ci, type);
	ci->u.m_Ptr = val;

	jx_hashmapSet(ctx->m_ConstMap, &ci);

	return ci;
}

static jx_ir_instruction_t* jir_instrBinaryOp(jx_ir_context_t* ctx, uint32_t opcode, jx_ir_value_t* operand1, jx_ir_value_t* operand2)
{
	if (!operand1 || !operand2 || operand1->m_Type != operand2->m_Type) {
		JX_CHECK(false, "Binary op operands must be of the same type.");
		return NULL;
	}

	return jir_instrBinaryOpTyped(ctx, opcode, operand1, operand2, operand1->m_Type);
}

static bool jir_checkBinaryOpOperands(jx_ir_value_t* operand1, jx_ir_value_t* operand2)
{
	if (!operand1 || !operand2 || !jx_ir_typeIsFirstClass(operand1->m_Type) || !jx_ir_typeIsFirstClass(operand2->m_Type)) {
		return false;
	}

	if (operand1->m_Type == operand2->m_Type) {
		return true;
	}

	return operand1->m_Type->m_Kind == JIR_TYPE_POINTER && operand2->m_Type->m_Kind == JIR_TYPE_POINTER;
}

static jx_ir_instruction_t* jir_instrBinaryOpTyped(jx_ir_context_t* ctx, uint32_t opcode, jx_ir_value_t* operand1, jx_ir_value_t* operand2, jx_ir_type_t* type)
{
	JX_CHECK(jir_checkBinaryOpOperands(operand1, operand2), "Binary op operands must be of the same type");

	// Sanity checks
	switch (opcode) {
	case JIR_OP_ADD:
	case JIR_OP_SUB:
	case JIR_OP_MUL:
	case JIR_OP_DIV:
	case JIR_OP_REM: {
		JX_CHECK(type == operand1->m_Type, "Arithmetic operation should return the same type as operands.");
		JX_CHECK(jx_ir_typeIsInteger(type) || jx_ir_typeIsFloatingPoint(type), "Tried to create arithmetic operation on non-arithmetic type.");
	} break;
	case JIR_OP_AND:
	case JIR_OP_OR:
	case JIR_OP_XOR: {
		JX_CHECK(type == operand1->m_Type, "Logical operation should return the same type as operands.");
		JX_CHECK(jx_ir_typeIsIntegral(type), "Tried to create logical operation on non-integral type.");
	} break;
	case JIR_OP_SET_LE:
	case JIR_OP_SET_GE:
	case JIR_OP_SET_LT:
	case JIR_OP_SET_GT:
	case JIR_OP_SET_EQ:
	case JIR_OP_SET_NE: {
		JX_CHECK(type->m_Kind == JIR_TYPE_BOOL, "SetCC must return bool.");
	} break;
	default:
		break;
	}

	jx_ir_instruction_t* instr = jir_instrAlloc(ctx, type, opcode, 2);
	if (!instr) {
		return NULL;
	}

	jir_instrAddOperand(ctx, instr, operand1);
	jir_instrAddOperand(ctx, instr, operand2);

	return instr;
}

static const char* jir_funcGenTempName(jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	uint32_t tempID = func->m_NextTempID++;
	char str[256];
	jx_snprintf(str, JX_COUNTOF(str), "%u", tempID);
	return jx_strtable_insert(ctx->m_StringTable, str, UINT32_MAX);
}

static bool jir_funcIsExternal(jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	return func->m_BasicBlockListHead == NULL;
}

static void jir_funcApplyPasses(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_function_pass_t* passListHead)
{
	JX_CHECK(jx_ir_funcCheck(ctx, func), "Function's IR and/or CFG is invalid!");
	jx_ir_function_pass_t* pass = passListHead;
	while (pass) {
#if 0
		{
			jx_string_buffer_t* sb = jx_strbuf_create(ctx->m_Allocator);
			jx_ir_funcPrint(ctx, func, sb);
			jx_strbuf_nullTerminate(sb);
			JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
			jx_strbuf_destroy(sb);
		}
#endif

		bool funcModified = pass->run(pass->m_Inst, ctx, func);
		JX_UNUSED(funcModified);
		JX_CHECK(jx_ir_funcCheck(ctx, func), "Function's IR and/or CFG is invalid!");
		pass = pass->m_Next;
	}
}
