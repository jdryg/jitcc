#include "jmir_gen.h"
#include "jmir.h"
#include "jir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/hashmap.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>

#define JX_MIRGEN_CONFIG_INLINE_MEMSET_LIMIT 128
#define JX_MIRGEN_CONFIG_INLINE_MEMCPY_LIMIT 128

typedef struct jmir_func_item_t
{
	jx_ir_function_t* m_IRFunc;
	jx_mir_function_t* m_MIRFunc;
} jmir_func_item_t;

typedef struct jmir_basic_block_item_t
{
	jx_ir_basic_block_t* m_IRBB;
	jx_mir_basic_block_t* m_MIRBB;
} jmir_basic_block_item_t;

typedef struct jmir_value_operand_item_t
{
	jx_ir_value_t* m_IRVal;
	jx_mir_operand_t* m_MIROperand;
} jmir_value_operand_item_t;

typedef struct jx_mirgen_context_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_context_t* m_IRCtx;
	jx_mir_context_t* m_MIRCtx;
	jx_mir_function_t* m_Func;
	jx_mir_basic_block_t* m_BasicBlock;
	jx_ir_instruction_t** m_PhiInstrArr;
	jx_hashmap_t* m_FuncMap;
	jx_hashmap_t* m_BasicBlockMap;
	jx_hashmap_t* m_ValueMap;
} jx_mirgen_context_t;

static bool jmirgen_globalVarBuild(jx_mirgen_context_t* ctx, const char* namePrefix, jx_ir_global_variable_t* irGV);
static bool jmirgen_globalVarInitializer(jx_mirgen_context_t* ctx, jx_mir_global_variable_t* gv, jx_ir_constant_t* init);
static bool jmirgen_funcBuild(jx_mirgen_context_t* ctx, const char* namePrefix, jx_ir_function_t* irFunc);
static bool jmirgen_instrBuild(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_ret(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_branch(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_add(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_sub(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_mul(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_div_rem(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_and(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_or(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_xor(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_setcc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_alloca(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_load(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_store(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_gep(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_phi(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_call(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_shl(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_shr(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_trunc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_zext(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_sext(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_ptrToInt(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_intToPtr(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_bitcast(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_fpext(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_fptrunc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_fp2ui(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_fp2si(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_ui2fp(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_si2fp(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_basic_block_t* jmirgen_getOrCreateBasicBlock(jx_mirgen_context_t* ctx, jx_ir_basic_block_t* irBB);
static jx_mir_operand_t* jmirgen_getOperand(jx_mirgen_context_t* ctx, jx_ir_value_t* val);
static bool jmirgen_genMemSet(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static bool jmirgen_genMemCpy(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static bool jmirgen_genMov(jx_mirgen_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
static jx_mir_operand_t* jmirgen_ensureOperandRegOrMem(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand);
static jx_mir_operand_t* jmirgen_ensureOperandReg(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand);
static jx_mir_operand_t* jmirgen_ensureOperandI32OrI64(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand, bool signExt);
static jx_mir_operand_t* jmirgen_ensureOperandNotConstI64(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand);
static jx_mir_function_proto_t* jmirgen_funcTypeToProto(jx_mirgen_context_t* ctx, jx_ir_type_function_t* funcType);
static bool jmirgen_processPhis(jx_mirgen_context_t* ctx);
static jx_mir_type_kind jmirgen_convertType(jx_ir_type_t* irType);
static uint64_t jmir_funcItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jmir_funcItemCompare(const void* a, const void* b, void* udata);
static uint64_t jmir_bbItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jmir_bbItemCompare(const void* a, const void* b, void* udata);
static uint64_t jmir_valueOperandItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jmir_valueOperandItemCompare(const void* a, const void* b, void* udata);

typedef jx_mir_operand_t* (*jmirgen_instrBuildFunc)(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);

static jmirgen_instrBuildFunc kIRInstrBuildFunc[] = {
	[JIR_OP_RET]             = jmirgen_instrBuild_ret,
	[JIR_OP_BRANCH]          = jmirgen_instrBuild_branch,
	[JIR_OP_ADD]             = jmirgen_instrBuild_add,
	[JIR_OP_SUB]             = jmirgen_instrBuild_sub,
	[JIR_OP_MUL]             = jmirgen_instrBuild_mul,
	[JIR_OP_DIV]             = jmirgen_instrBuild_div_rem,
	[JIR_OP_REM]             = jmirgen_instrBuild_div_rem,
	[JIR_OP_AND]             = jmirgen_instrBuild_and,
	[JIR_OP_OR]              = jmirgen_instrBuild_or,
	[JIR_OP_XOR]             = jmirgen_instrBuild_xor,
	[JIR_OP_SET_LE]          = jmirgen_instrBuild_setcc,
	[JIR_OP_SET_GE]          = jmirgen_instrBuild_setcc,
	[JIR_OP_SET_LT]          = jmirgen_instrBuild_setcc,
	[JIR_OP_SET_GT]          = jmirgen_instrBuild_setcc,
	[JIR_OP_SET_EQ]          = jmirgen_instrBuild_setcc,
	[JIR_OP_SET_NE]          = jmirgen_instrBuild_setcc,
	[JIR_OP_ALLOCA]          = jmirgen_instrBuild_alloca,
	[JIR_OP_LOAD]            = jmirgen_instrBuild_load,
	[JIR_OP_STORE]           = jmirgen_instrBuild_store,
	[JIR_OP_GET_ELEMENT_PTR] = jmirgen_instrBuild_gep,
	[JIR_OP_PHI]             = jmirgen_instrBuild_phi,
	[JIR_OP_CALL]            = jmirgen_instrBuild_call,
	[JIR_OP_SHL]             = jmirgen_instrBuild_shl,
	[JIR_OP_SHR]             = jmirgen_instrBuild_shr,
	[JIR_OP_TRUNC]           = jmirgen_instrBuild_trunc,
	[JIR_OP_ZEXT]            = jmirgen_instrBuild_zext,
	[JIR_OP_SEXT]            = jmirgen_instrBuild_sext,
	[JIR_OP_PTR_TO_INT]      = jmirgen_instrBuild_ptrToInt,
	[JIR_OP_INT_TO_PTR]      = jmirgen_instrBuild_intToPtr,
	[JIR_OP_BITCAST]         = jmirgen_instrBuild_bitcast,
	[JIR_OP_FPEXT]           = jmirgen_instrBuild_fpext,
	[JIR_OP_FPTRUNC]         = jmirgen_instrBuild_fptrunc,
	[JIR_OP_FP2UI]           = jmirgen_instrBuild_fp2ui,
	[JIR_OP_FP2SI]           = jmirgen_instrBuild_fp2si,
	[JIR_OP_UI2FP]           = jmirgen_instrBuild_ui2fp,
	[JIR_OP_SI2FP]           = jmirgen_instrBuild_si2fp,
};

static const jx_mir_condition_code kIRCCToMIRCCSigned[] = {
	[JIR_CC_LE] = JMIR_CC_LE,
	[JIR_CC_GE] = JMIR_CC_GE,
	[JIR_CC_LT] = JMIR_CC_L,
	[JIR_CC_GT] = JMIR_CC_G,
	[JIR_CC_EQ] = JMIR_CC_E,
	[JIR_CC_NE] = JMIR_CC_NE,
};

static const jx_mir_condition_code kIRCCToMIRCCUnsigned[] = {
	[JIR_CC_LE] = JMIR_CC_BE,
	[JIR_CC_GE] = JMIR_CC_AE,
	[JIR_CC_LT] = JMIR_CC_B,
	[JIR_CC_GT] = JMIR_CC_A,
	[JIR_CC_EQ] = JMIR_CC_E,
	[JIR_CC_NE] = JMIR_CC_NE,
};

jx_mirgen_context_t* jx_mirgen_createContext(jx_ir_context_t* irCtx, jx_mir_context_t* mirCtx, jx_allocator_i* allocator)
{
	jx_mirgen_context_t* ctx = (jx_mirgen_context_t*)JX_ALLOC(allocator, sizeof(jx_mirgen_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_mirgen_context_t));
	ctx->m_Allocator = allocator;
	ctx->m_IRCtx = irCtx;
	ctx->m_MIRCtx = mirCtx;

	ctx->m_FuncMap = jx_hashmapCreate(allocator, sizeof(jmir_func_item_t), 64, 0, 0, jmir_funcItemHash, jmir_funcItemCompare, NULL, NULL);
	if (!ctx->m_FuncMap) {
		jx_mirgen_destroyContext(ctx);
		return NULL;
	}

	ctx->m_BasicBlockMap = jx_hashmapCreate(allocator, sizeof(jmir_basic_block_item_t), 64, 0, 0, jmir_bbItemHash, jmir_bbItemCompare, NULL, NULL);
	if (!ctx->m_BasicBlockMap) {
		jx_mirgen_destroyContext(ctx);
		return NULL;
	}

	ctx->m_ValueMap = jx_hashmapCreate(allocator, sizeof(jmir_value_operand_item_t), 64, 0, 0, jmir_valueOperandItemHash, jmir_valueOperandItemCompare, NULL, NULL);
	if (!ctx->m_ValueMap) {
		jx_mirgen_destroyContext(ctx);
		return NULL;
	}

	ctx->m_PhiInstrArr = (jx_ir_instruction_t**)jx_array_create(allocator);
	if (!ctx->m_PhiInstrArr) {
		jx_mirgen_destroyContext(ctx);
		return NULL;
	}

	return ctx;
}

void jx_mirgen_destroyContext(jx_mirgen_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;
	jx_array_free(ctx->m_PhiInstrArr);

	if (ctx->m_ValueMap) {
		jx_hashmapDestroy(ctx->m_ValueMap);
		ctx->m_ValueMap = NULL;
	}

	if (ctx->m_BasicBlockMap) {
		jx_hashmapDestroy(ctx->m_BasicBlockMap);
		ctx->m_BasicBlockMap = NULL;
	}

	if (ctx->m_FuncMap) {
		jx_hashmapDestroy(ctx->m_FuncMap);
		ctx->m_FuncMap = NULL;
	}

	JX_FREE(allocator, ctx);
}

bool jx_mirgen_moduleGen(jx_mirgen_context_t* ctx, jx_ir_module_t* mod)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;
	
	// Process global variables first
	jx_ir_global_variable_t* irGV = mod->m_GlobalVarListHead;
	while (irGV) {
		if (!jmirgen_globalVarBuild(ctx, mod->m_Name, irGV)) {
			return false;
		}

		irGV = irGV->m_Next;
	}

	// Build all functions
	jx_ir_function_t* irFunc = mod->m_FunctionListHead;
	while (irFunc) {
		if (!jmirgen_funcBuild(ctx, mod->m_Name, irFunc)) {
			return false;
		}

		irFunc = irFunc->m_Next;
	}

	return false;
}

static bool jmirgen_globalVarBuild(jx_mirgen_context_t* ctx, const char* namePrefix, jx_ir_global_variable_t* irGV)
{
	jx_ir_constant_t* gvInit = jx_ir_valueToConst(irGV->super.super.m_OperandArr[0]->m_Value);
	JX_CHECK(gvInit, "Expected constant value operand.");

	const size_t alignment = jx_ir_typeGetAlignment(gvInit->super.super.m_Type);

	jx_mir_global_variable_t* gv = jx_mir_globalVarBegin(ctx->m_MIRCtx, jx_ir_globalVarToValue(irGV)->m_Name, (uint32_t)alignment);
	if (!gv) {
		return false;
	}

	jmirgen_globalVarInitializer(ctx, gv, gvInit);

	jx_mir_globalVarEnd(ctx->m_MIRCtx, gv);

	return true;
}

static bool jmirgen_globalVarInitializer(jx_mirgen_context_t* ctx, jx_mir_global_variable_t* gv, jx_ir_constant_t* init)
{
	jx_mir_context_t* mirctx = ctx->m_MIRCtx;

	jx_ir_value_t* initVal = jx_ir_constToValue(init);
	switch (initVal->m_Type->m_Kind) {
	case JIR_TYPE_VOID: {
		JX_CHECK(false, "void constant?");
	} break;
	case JIR_TYPE_BOOL: {
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_Bool, sizeof(bool));
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_I8: {
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_I64, sizeof(uint8_t));
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_I64, sizeof(uint16_t));
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_I64, sizeof(uint32_t));
	} break;
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_I64, sizeof(uint64_t));
	} break;
	case JIR_TYPE_POINTER: {
		if ((init->super.super.m_Flags & JIR_VALUE_FLAGS_CONST_GLOBAL_VAL_PTR_Msk) != 0) {
			jx_ir_global_value_t* irGV = (jx_ir_global_value_t*)init->u.m_I64;
			const char* symbolName = irGV->super.super.m_Name;
			
			const uint64_t placeholder = 0ull;
			const uint32_t offset = jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&placeholder, sizeof(uint64_t));

			jx_mir_globalVarAddRelocation(mirctx, gv, offset, symbolName);
		} else {
			jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_I64, sizeof(uint64_t));
		}
	} break;
	case JIR_TYPE_F32: {
		const float f = (float)init->u.m_F64;
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&f, sizeof(float));
	} break;
	case JIR_TYPE_F64: {
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_F64, sizeof(double));
	} break;
	case JIR_TYPE_TYPE: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JIR_TYPE_LABEL: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JIR_TYPE_FUNCTION: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JIR_TYPE_STRUCT:
	case JIR_TYPE_ARRAY: {
		jx_ir_user_t* initUser = jx_ir_constToUser(init);
		const uint32_t numElements = (uint32_t)jx_array_sizeu(initUser->m_OperandArr);
		for (uint32_t iElem = 0; iElem < numElements; ++iElem) {
			jx_ir_constant_t* elem = jx_ir_valueToConst(initUser->m_OperandArr[iElem]->m_Value);
			JX_CHECK(elem, "Expected constant struct/array element");
			if (!jmirgen_globalVarInitializer(ctx, gv, elem)) {
				return false;
			}
		}
	} break;
	default: {
		JX_CHECK(false, "Unknown kind of jcc type");
	} break;
	}

	return true;
}

static bool jmirgen_funcBuild(jx_mirgen_context_t* ctx, const char* namePrefix, jx_ir_function_t* irFunc)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;
	jx_mir_context_t* mirctx = ctx->m_MIRCtx;

	jx_ir_type_function_t* irFuncType = jx_ir_funcGetType(irctx, irFunc);

	const jx_mir_type_kind retType = jmirgen_convertType(irFuncType->m_RetType);
	const bool isVarArg = irFuncType->m_IsVarArg;

	jx_mir_type_kind* args = NULL;
	const uint32_t numArgs = irFuncType->m_NumArgs;
	if (numArgs) {
		args = JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_type_kind) * numArgs);
		if (!args) {
			return false;
		}

		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			args[iArg] = jmirgen_convertType(irFuncType->m_Args[iArg]);
		}
	}

#if 0
	char funcName[256];
	jx_snprintf(funcName, JX_COUNTOF(funcName), "%s:%s", namePrefix, irFunc->super.super.super.m_Name);
#else
	const char* funcName = jx_ir_funcToValue(irFunc)->m_Name;
#endif

	const bool isExternal = irFunc->m_BasicBlockListHead == NULL;
	const uint32_t flags = 0
		| (isVarArg ? JMIR_FUNC_FLAGS_VARARG_Msk : 0)
		| (isExternal ? JMIR_FUNC_FLAGS_EXTERNAL_Msk : 0)
		;
	jx_mir_function_proto_t* funcProto = jx_mir_funcProto(mirctx, retType, numArgs, args, flags);
	jx_mir_function_t* func = jx_mir_funcBegin(mirctx, funcName, funcProto);
	if (func) {
		ctx->m_Func = func;

		jx_array_resize(ctx->m_PhiInstrArr, 0);
		jx_hashmapClear(ctx->m_BasicBlockMap, false);

		jx_ir_basic_block_t* irBB = irFunc->m_BasicBlockListHead;
		while (irBB) {
			jx_mir_basic_block_t* bb = jmirgen_getOrCreateBasicBlock(ctx, irBB);
			JX_CHECK(!bb->m_InstrListHead, "Basic block already filled?");

			ctx->m_BasicBlock = bb;

			jx_ir_instruction_t* irInstr = irBB->m_InstrListHead;
			while (irInstr) {
				if (!jmirgen_instrBuild(ctx, irInstr)) {
					return false;
				}
				
				irInstr = irInstr->m_Next;
			}
			
			// NOTE: Append the current basic block (instead of bb) to the function
			// because codegen might have introduced new basic blocks.
			jx_mir_funcAppendBasicBlock(mirctx, func, ctx->m_BasicBlock);

			irBB = irBB->m_Next;
		}

		// Process all phi instructions
		if (!jmirgen_processPhis(ctx)) {
			return false;
		}

		jx_mir_funcEnd(mirctx, func);

		ctx->m_Func = NULL;

		jx_hashmapSet(ctx->m_FuncMap, &(jmir_func_item_t){.m_IRFunc = irFunc, .m_MIRFunc = func});
	}

	JX_FREE(ctx->m_Allocator, args);

	return func != NULL;
}

static bool jmirgen_instrBuild(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	jx_mir_operand_t* resOperand = NULL;

	resOperand = kIRInstrBuildFunc[irInstr->m_OpCode](ctx, irInstr);

	if (resOperand) {
		jx_hashmapSet(ctx->m_ValueMap, &(jmir_value_operand_item_t){ .m_IRVal = jx_ir_instrToValue(irInstr), .m_MIROperand = resOperand });
	} else {
		// Check if the IR instruction has void type. If it's not assert and return false.
		jx_ir_value_t* irInstrVal = jx_ir_instrToValue(irInstr);
		if (irInstrVal->m_Type->m_Kind != JIR_TYPE_VOID) {
			JX_CHECK(false, "Expected result operand from non-void instruction!");
			return false;
		}
	}

	return true;
}

static jx_mir_operand_t* jmirgen_instrBuild_ret(jx_mirgen_context_t* ctx, jx_ir_instruction_t* retInstr)
{
	JX_CHECK(retInstr->m_OpCode == JIR_OP_RET, "Expected ret instruction");

	jx_mir_operand_t* retReg = NULL;
	const uint32_t numOperands = (uint32_t)jx_array_sizeu(retInstr->super.m_OperandArr);
	if (numOperands == 1) {
		jx_ir_value_t* retVal = retInstr->super.m_OperandArr[0]->m_Value;
		jx_mir_operand_t* mirRetVal = jmirgen_getOperand(ctx, retVal);
		jx_mir_type_kind mirType = jmirgen_convertType(retVal->m_Type);

		if (jx_mir_opIsStackObj(mirRetVal) || mirRetVal->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			retReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, kMIRRegGP_A);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, retReg, mirRetVal));
		} else {
			if (jx_mir_typeIsFloatingPoint(mirType)) {
				retReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, mirType, kMIRRegXMM_0);
				if (mirType == JMIR_TYPE_F32) {
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, retReg, mirRetVal));
				} else if (mirType == JMIR_TYPE_F64) {
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, retReg, mirRetVal));
				} else {
					JX_CHECK(false, "Unknown floating point type");
				}
			} else {
				retReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, mirType, kMIRRegGP_A);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, retReg, mirRetVal));
			}
		}
	}

	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_ret(ctx->m_MIRCtx, NULL));

	return retReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_branch(jx_mirgen_context_t* ctx, jx_ir_instruction_t* branchInstr)
{
	JX_CHECK(branchInstr->m_OpCode == JIR_OP_BRANCH, "Expected branch instruction");

	const uint32_t numOperands = (uint32_t)jx_array_sizeu(branchInstr->super.m_OperandArr);
	if (numOperands == 1) {
		// Unconditional branch
		// jmp bb_target
		jx_ir_basic_block_t* targetBB = jx_ir_valueToBasicBlock(branchInstr->super.m_OperandArr[0]->m_Value);
		JX_CHECK(targetBB, "Expected basic block as branch target");

		jx_mir_basic_block_t* mirTargetBB = jmirgen_getOrCreateBasicBlock(ctx, targetBB);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_jmp(ctx->m_MIRCtx, jx_mir_opBasicBlock(ctx->m_MIRCtx, ctx->m_Func, mirTargetBB)));
	} else if (numOperands == 3) {
		// Conditional branch
		// test cond, cond
		// jz bb_false
		// jmp bb_true
		jx_ir_value_t* condVal = branchInstr->super.m_OperandArr[0]->m_Value;
		jx_ir_basic_block_t* trueBB = jx_ir_valueToBasicBlock(branchInstr->super.m_OperandArr[1]->m_Value);
		jx_ir_basic_block_t* falseBB = jx_ir_valueToBasicBlock(branchInstr->super.m_OperandArr[2]->m_Value);
		JX_CHECK(condVal->m_Type->m_Kind == JIR_TYPE_BOOL, "Expected boolean conditional value");
		JX_CHECK(trueBB && falseBB, "Expected basic blocks as branch targets");

		jx_mir_basic_block_t* mirTrueBB = jmirgen_getOrCreateBasicBlock(ctx, trueBB);
		jx_mir_basic_block_t* mirFalseBB = jmirgen_getOrCreateBasicBlock(ctx, falseBB);

		jx_mir_operand_t* condOperand = jmirgen_getOperand(ctx, condVal);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_test(ctx->m_MIRCtx, condOperand, condOperand));
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_jcc(ctx->m_MIRCtx, JMIR_CC_Z, jx_mir_opBasicBlock(ctx->m_MIRCtx, ctx->m_Func, mirFalseBB)));
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_jmp(ctx->m_MIRCtx, jx_mir_opBasicBlock(ctx->m_MIRCtx, ctx->m_Func, mirTrueBB)));
	} else {
		JX_CHECK(false, "Unknown branch type");
	}

	return NULL;
}

static jx_mir_operand_t* jmirgen_instrBuild_add(jx_mirgen_context_t* ctx, jx_ir_instruction_t* addInstr)
{
	JX_CHECK(addInstr->m_OpCode == JIR_OP_ADD, "Expected add instruction");

	jx_ir_type_t* instrType = jx_ir_instrToValue(addInstr)->m_Type;

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, addInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, addInstr->super.m_OperandArr[1]->m_Value);
	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	if (jx_ir_typeIsFloatingPoint(instrType)) {
		if (instrType->m_Kind == JIR_TYPE_F32) {
			// movss reg, lhs
			// addss reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_addss(ctx->m_MIRCtx, dstReg, rhs));
		} else if (instrType->m_Kind == JIR_TYPE_F64) {
			// movsd reg, lhs
			// addsd reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_addsd(ctx->m_MIRCtx, dstReg, rhs));
		} else {
			JX_CHECK(false, "Unknown floating point type.");
		}
	} else {
		// mov reg, lhs
		// add reg, rhs
		rhs = jmirgen_ensureOperandNotConstI64(ctx, rhs);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_add(ctx->m_MIRCtx, dstReg, rhs));
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_sub(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_SUB, "Expected xor instruction");

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	if (jx_ir_typeIsFloatingPoint(instrType)) {
		if (instrType->m_Kind == JIR_TYPE_F32) {
			// movss reg, lhs
			// subss reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_subss(ctx->m_MIRCtx, dstReg, rhs));
		} else if (instrType->m_Kind == JIR_TYPE_F64) {
			// movsd reg, lhs
			// subsd reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_subsd(ctx->m_MIRCtx, dstReg, rhs));
		} else {
			JX_CHECK(false, "Unknown floating point type.");
		}
	} else {
		// mov reg, lhs
		// sub reg, rhs
		rhs = jmirgen_ensureOperandNotConstI64(ctx, rhs);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_sub(ctx->m_MIRCtx, dstReg, rhs));
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_mul(jx_mirgen_context_t* ctx, jx_ir_instruction_t* mulInstr)
{
	JX_CHECK(mulInstr->m_OpCode == JIR_OP_MUL, "Expected mul instruction");

	jx_ir_type_t* instrType = jx_ir_instrToValue(mulInstr)->m_Type;

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, mulInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, mulInstr->super.m_OperandArr[1]->m_Value);
	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	if (jx_ir_typeIsFloatingPoint(instrType)) {
		// Floating point multiplication
		// Make sure rhs is either a memory reference/stack object or a register. If it's not (e.g. a constant)
		// move it to a virtual register first.
		if (rhs->m_Kind != JMIR_OPERAND_REGISTER && rhs->m_Kind != JMIR_OPERAND_MEMORY_REF) {
			jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, rhs->m_Type);
			if (instrType->m_Kind == JIR_TYPE_F32) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, tmp, rhs));
			} else if (instrType->m_Kind == JIR_TYPE_F64) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, tmp, rhs));
			} else {
				JX_CHECK(false, "Unknown floating point type.");
			}
			rhs = tmp;
		}

		if (instrType->m_Kind == JIR_TYPE_F32) {
			// movss reg, lhs
			// mulss reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mulss(ctx->m_MIRCtx, dstReg, rhs));
		} else if (instrType->m_Kind == JIR_TYPE_F64) {
			// movsd reg, lhs
			// mulsd reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mulsd(ctx->m_MIRCtx, dstReg, rhs));
		} else {
			JX_CHECK(false, "Unknown floating point type.");
		}
	} else {
		// Make sure rhs is either a memory reference/stack object or a register. If it's not (e.g. a constant)
		// move it to a virtual register first.
		if (rhs->m_Kind != JMIR_OPERAND_REGISTER && rhs->m_Kind != JMIR_OPERAND_MEMORY_REF) {
			jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, rhs->m_Type);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmp, rhs));
			rhs = tmp;
		}

		if (jx_ir_typeIsSigned(instrType)) {
			// Signed integer multiplication
			// mov reg, lhs
			// imul reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_imul(ctx->m_MIRCtx, dstReg, rhs));
		} else {
			// Unsigned integer multiplication
			// TODO: Is this always right? If yes, merge with code above?
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_imul(ctx->m_MIRCtx, dstReg, rhs));
		}
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_div_rem(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_DIV || irInstr->m_OpCode == JIR_OP_REM, "Expected div/rem instruction");
	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	jx_ir_type_t* instrType = instrVal->m_Type;

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);

	jx_mir_type_kind type = jmirgen_convertType(instrType);
	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, type);

	if (jx_ir_typeIsFloatingPoint(instrType)) {
		JX_CHECK(irInstr->m_OpCode == JIR_OP_DIV, "Expected floating point div instruction.");
		if (instrType->m_Kind == JIR_TYPE_F32) {
			// movss reg, lhs
			// divss reg, rhs
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_divss(ctx->m_MIRCtx, dstReg, rhs));
		} else if (instrType->m_Kind == JIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, dstReg, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_divsd(ctx->m_MIRCtx, dstReg, rhs));
		} else {
			JX_CHECK(false, "Unknown floating point type");
		}
	} else {
		JX_CHECK(type == JMIR_TYPE_I32 || type == JMIR_TYPE_I64 || type == JMIR_TYPE_PTR, "Expected 32-bit or 64-bit division");

		if (rhs->m_Kind == JMIR_OPERAND_CONST) {
			// TODO: Div by constant. Try to avoid div/idiv. Is this the right place to do this or it's better to 
			// add an IR pass?
			jx_mir_operand_t* reg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, rhs->m_Type);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, reg, rhs));
			rhs = reg;
		}

		jx_mir_operand_t* loReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, type, kMIRRegGP_A);
		jx_mir_operand_t* hiReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, type, kMIRRegGP_D);

		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, loReg, lhs));

		// IDIV/DIV use RDX. RDX is also used as the 2nd function argument, so it must be 
		// preserved before executing CDQ/CQO/XOR.
		//
		// TODO: Since every function has shadow space for the first 4 arguments (even if the function
		// does not have 4 args), I can avoid mov'ing RDX to its stack slot at every div (and call)
		// and instead mark it and let the code which inserts the prologue do the mov once. So, instead 
		// of using RDX at every instruction which uses the 2nd function argument, I can use the shadow space
		// slot instead.
		if (jx_ir_typeIsSigned(instrType)) {
			// Signed integer division
			if (type == JMIR_TYPE_I32) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cdq(ctx->m_MIRCtx));
			} else {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cqo(ctx->m_MIRCtx));
			}
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_idiv(ctx->m_MIRCtx, rhs));
		} else {
			// Unsigned integer division
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_xor(ctx->m_MIRCtx, hiReg, hiReg));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_div(ctx->m_MIRCtx, rhs));
		}

		if (irInstr->m_OpCode == JIR_OP_DIV) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, loReg));
		} else if (irInstr->m_OpCode == JIR_OP_REM) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, hiReg));
		}
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_and(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_AND, "Expected and instruction");

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	JX_CHECK(!jx_ir_typeIsFloatingPoint(instrType), "float and?");

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
	dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	// mov reg, lhs
	// and reg, rhs
	rhs = jmirgen_ensureOperandNotConstI64(ctx, rhs);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_and(ctx->m_MIRCtx, dstReg, rhs));

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_or(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_OR, "Expected or instruction");

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	JX_CHECK(!jx_ir_typeIsFloatingPoint(instrType), "float or?");

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
	dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	// mov reg, lhs
	// or reg, rhs
	rhs = jmirgen_ensureOperandNotConstI64(ctx, rhs);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_or(ctx->m_MIRCtx, dstReg, rhs));

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_xor(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_XOR, "Expected xor instruction");

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	JX_CHECK(!jx_ir_typeIsFloatingPoint(instrType), "float xor?");
		
	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
	dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	// mov reg, lhs
	// xor reg, rhs
	rhs = jmirgen_ensureOperandNotConstI64(ctx, rhs);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_xor(ctx->m_MIRCtx, dstReg, rhs));

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_setcc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	jx_ir_value_t* lhsVal = irInstr->super.m_OperandArr[0]->m_Value;
	jx_ir_value_t* rhsVal = irInstr->super.m_OperandArr[1]->m_Value;
	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, lhsVal);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, rhsVal);
	if (!lhs || !rhs) {
		return NULL;
	}

	jx_ir_type_t* cmpType = lhsVal->m_Type;

	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8);;

	jx_ir_condition_code irCC = irInstr->m_OpCode - JIR_OP_SET_CC_BASE;

	if (jx_ir_typeIsFloatingPoint(cmpType)) {
		jx_mir_condition_code mirCC = kIRCCToMIRCCUnsigned[irCC];
			
		if (cmpType->m_Kind == JIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_ucomiss(ctx->m_MIRCtx, lhs, rhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_setcc(ctx->m_MIRCtx, mirCC, dstReg));
		} else if (cmpType->m_Kind == JIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_ucomisd(ctx->m_MIRCtx, lhs, rhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_setcc(ctx->m_MIRCtx, mirCC, dstReg));
		} else {
			JX_CHECK(false, "Unknown floating point type.");
		}
	} else {
		// cmp lhs, rhs
		// setcc reg8
		jx_mir_condition_code mirCC = jx_ir_typeIsSigned(cmpType)
			? kIRCCToMIRCCSigned[irCC]
			: kIRCCToMIRCCUnsigned[irCC]
			;

		const bool lhsConst = lhs->m_Kind == JMIR_OPERAND_CONST;
		const bool rhsConst = rhs->m_Kind == JMIR_OPERAND_CONST;
		if (lhsConst && rhsConst) {
			JX_CHECK(false, "I think I need an optimization pass!");
		} else if (lhsConst && !rhsConst) {
			// Swap operands and condition code.
			lhs = jmirgen_ensureOperandNotConstI64(ctx, lhs);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cmp(ctx->m_MIRCtx, rhs, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_setcc(ctx->m_MIRCtx, jx_mir_ccSwapOperands(mirCC), dstReg));
		} else {
			rhs = jmirgen_ensureOperandNotConstI64(ctx, rhs);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cmp(ctx->m_MIRCtx, lhs, rhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_setcc(ctx->m_MIRCtx, mirCC, dstReg));
		}
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_alloca(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_ALLOCA, "Expected alloca instruction");

	jx_ir_value_t* irInstrVal = jx_ir_instrToValue(irInstr);
	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(irInstrVal->m_Type);

	jx_ir_value_t* arraySizeVal = irInstr->super.m_OperandArr[0]->m_Value;
	jx_ir_constant_t* arraySizeConst = jx_ir_valueToConst(arraySizeVal);
	JX_CHECK(arraySizeConst, "Expected constant array size in alloca.");

	uint64_t arrSize = arraySizeConst->u.m_U64;
	jx_ir_type_t* baseType = ptrType->m_BaseType;
	while (baseType->m_Kind == JIR_TYPE_ARRAY) {
		jx_ir_type_array_t* arrType = jx_ir_typeToArray(baseType);
		arrSize *= arrType->m_Size;
		baseType = arrType->m_BaseType;
	}

	const size_t elemSize = jx_ir_typeGetSize(baseType);
	const size_t allocaSize = elemSize * arrSize;
	const size_t alignment = jx_ir_typeGetAlignment(baseType);

	jx_mir_operand_t* dst = jx_mir_opStackObj(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(baseType), (uint32_t)allocaSize, (uint32_t)alignment);

	return dst;
}

static jx_mir_operand_t* jmirgen_instrBuild_load(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_LOAD, "Expected load instruction");

	jx_ir_value_t* ptrVal = irInstr->super.m_OperandArr[0]->m_Value;
	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(ptrVal->m_Type);
	JX_CHECK(ptrType, "Expected pointer type!");

	jx_mir_operand_t* srcOperand = jmirgen_getOperand(ctx, ptrVal);

	jx_mir_operand_t* dstReg = NULL;

	jx_mir_type_kind regType = jmirgen_convertType(ptrType->m_BaseType);
	dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, regType);

	if (srcOperand->m_Kind != JMIR_OPERAND_REGISTER) {
		if (jx_mir_opIsStackObj(srcOperand) || srcOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, srcOperand));
			srcOperand = tmpReg;
		} else {
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, regType);
			if (regType == JMIR_TYPE_F32) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, tmpReg, srcOperand));
			} else if (regType == JMIR_TYPE_F64) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, tmpReg, srcOperand));
			} else {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, srcOperand));
			}
			srcOperand = tmpReg;
		}
	}

	jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, regType, srcOperand->u.m_Reg, kMIRRegGPNone, 1, 0);
	if (regType == JMIR_TYPE_F32) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, dstReg, memRef));
	} else if (regType == JMIR_TYPE_F64) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, dstReg, memRef));
	} else {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, memRef));
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_store(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_STORE, "Expected store instruction");

	jx_ir_value_t* ptrVal = irInstr->super.m_OperandArr[0]->m_Value;
	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(ptrVal->m_Type);
	JX_CHECK(ptrType, "Expected pointer type!");

	jx_mir_operand_t* dstOperand = jmirgen_getOperand(ctx, ptrVal);
	jx_mir_operand_t* srcOperand = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);

	jx_mir_type_kind regType = jmirgen_convertType(ptrType->m_BaseType);

	jx_mir_operand_t* memRef = NULL;
	if (dstOperand->m_Kind == JMIR_OPERAND_REGISTER) {
		memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, regType, dstOperand->u.m_Reg, kMIRRegGPNone, 1, 0);
	} else if (jx_mir_opIsStackObj(dstOperand)) {
		memRef = dstOperand;
	} else if (dstOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
		jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, dstOperand));
		memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, regType, tmpReg->u.m_Reg, kMIRRegGPNone, 1, 0);
	} else {
		JX_CHECK(false, "Unhandle store destination operand.");
	}

	if (jx_mir_opIsStackObj(srcOperand) || srcOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
		jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, srcOperand));
		srcOperand = tmpReg;
	} else if (srcOperand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
		jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, regType);
		if (regType == JMIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, tmpReg, srcOperand));
		} else if (regType == JMIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, tmpReg, srcOperand));
		} else {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, srcOperand));
		}
		srcOperand = tmpReg;
	}

	if (regType == JMIR_TYPE_F32) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, memRef, srcOperand));
	} else if (regType == JMIR_TYPE_F64) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, memRef, srcOperand));
	} else {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, memRef, srcOperand));
	}

	return NULL;
}

static jx_mir_operand_t* jmirgen_instrBuild_gep(jx_mirgen_context_t* ctx, jx_ir_instruction_t* gepInstr)
{
	JX_CHECK(gepInstr->m_OpCode == JIR_OP_GET_ELEMENT_PTR, "Expected GEP instruction");

	// Result is always a pointer.
	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);

	const uint32_t numOperands = (uint32_t)jx_array_sizeu(gepInstr->super.m_OperandArr);
	JX_CHECK(numOperands >= 2, "GEP instruction expected to have at least 2 operands!");

	jx_ir_value_t* basePtrVal = gepInstr->super.m_OperandArr[0]->m_Value;
	jx_ir_type_pointer_t* basePtrType = jx_ir_typeToPointer(basePtrVal->m_Type);
	JX_CHECK(basePtrType, "Expected pointer type");

	jx_mir_operand_t* basePtrOperand = jmirgen_getOperand(ctx, basePtrVal);
	if (jx_mir_opIsStackObj(basePtrOperand) || basePtrOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstReg, basePtrOperand));
	} else {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, basePtrOperand));
	}
	jx_ir_type_t* dstRegType = basePtrVal->m_Type;

	for (uint32_t iOperand = 1; iOperand < numOperands; ++iOperand) {
		jx_ir_value_t* operandVal = gepInstr->super.m_OperandArr[iOperand]->m_Value;
		JX_CHECK(jx_ir_typeIsInteger(operandVal->m_Type), "Expeced index to have integer type!");
		jx_mir_operand_t* indexOperand = jmirgen_getOperand(ctx, operandVal);
		if (indexOperand->m_Kind == JMIR_OPERAND_CONST) {
			if (dstRegType->m_Kind == JIR_TYPE_POINTER) {
				JX_CHECK(iOperand == 1, "Only first index can be on a pointer type.");
				jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(dstRegType);
				const size_t itemSize = jx_ir_typeGetSize(ptrType->m_BaseType);
				const int64_t displacement = indexOperand->u.m_ConstI64 * (int64_t)itemSize;
				JX_CHECK(displacement <= INT32_MAX, "Displacement too large");
				if (displacement != 0) {
					jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_Reg, kMIRRegGPNone, 1, (int32_t)displacement);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstReg, memRef));
				}
				dstRegType = ptrType->m_BaseType;
			} else if (dstRegType->m_Kind == JIR_TYPE_ARRAY) {
				jx_ir_type_array_t* arrType = jx_ir_typeToArray(dstRegType);
				const size_t itemSize = jx_ir_typeGetSize(arrType->m_BaseType);
				const int64_t displacement = indexOperand->u.m_ConstI64 * (int64_t)itemSize;
				JX_CHECK(displacement <= INT32_MAX, "Displacement too large");
				if (displacement != 0) {
					jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_Reg, kMIRRegGPNone, 1, (int32_t)displacement);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstReg, memRef));
				}
				dstRegType = arrType->m_BaseType;
			} else if (dstRegType->m_Kind == JIR_TYPE_STRUCT) {
				jx_ir_type_struct_t* structType = jx_ir_typeToStruct(dstRegType);
				JX_CHECK(indexOperand->u.m_ConstI64 < structType->m_NumMembers, "Invalid struct member index!");
				jx_ir_type_t* memberType = structType->m_Members[indexOperand->u.m_ConstI64];
				const uint32_t memberOffset = (uint32_t)jx_ir_typeStructGetMemberOffset(structType, (uint32_t)indexOperand->u.m_ConstI64);
				JX_CHECK(memberOffset <= INT32_MAX, "Displacement too large");
				if (memberOffset != 0) {
					jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_Reg, kMIRRegGPNone, 1, (int32_t)memberOffset);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstReg, memRef));
				}
				dstRegType = memberType;
			} else {
				JX_CHECK(false, "Unexpected type in GEP index list");
			}
		} else {
			jx_ir_type_t* baseType = NULL;
			if (dstRegType->m_Kind == JIR_TYPE_POINTER) {
				JX_CHECK(iOperand == 1, "Only first index can be on a pointer type.");
				jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(dstRegType);
				baseType = ptrType->m_BaseType;
			} else if (dstRegType->m_Kind == JIR_TYPE_ARRAY) {
				jx_ir_type_array_t* arrType = jx_ir_typeToArray(dstRegType);
				baseType = arrType->m_BaseType;
			} else {
				JX_CHECK(false, "Unexpected type in GEP index list");
			}

			JX_CHECK(baseType, "This will fail!");

			if (indexOperand->m_Kind != JMIR_OPERAND_REGISTER) {
				jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
				if (indexOperand->m_Type != JMIR_TYPE_PTR && indexOperand->m_Type != JMIR_TYPE_I64) {
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsx(ctx->m_MIRCtx, tmp, indexOperand));
				} else {
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmp, indexOperand));
				}
				indexOperand = tmp;
			} else {
				if (indexOperand->m_Type != JMIR_TYPE_PTR && indexOperand->m_Type != JMIR_TYPE_I64) {
					jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsx(ctx->m_MIRCtx, tmp, indexOperand));
					indexOperand = tmp;
				}
			}

			const uint32_t itemSize = (uint32_t)jx_ir_typeGetSize(baseType);
			if (itemSize <= 8 && jx_isPow2_u32(itemSize)) {
				jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_Reg, indexOperand->u.m_Reg, itemSize, 0);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstReg, memRef));
			} else {
				jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmp, indexOperand));
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_imul(ctx->m_MIRCtx, tmp, jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64, itemSize)));
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_add(ctx->m_MIRCtx, dstReg, tmp));
			}

			dstRegType = baseType;
		}
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_phi(jx_mirgen_context_t* ctx, jx_ir_instruction_t* phiInstr)
{
	JX_CHECK(phiInstr->m_OpCode == JIR_OP_PHI, "Expected phi instruction");

	jx_mir_type_kind mirType = jmirgen_convertType(phiInstr->super.super.m_Type);
	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, mirType);

	jx_array_push_back(ctx->m_PhiInstrArr, phiInstr);

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_call(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	const uint32_t numOperands = (uint32_t)jx_array_sizeu(irInstr->super.m_OperandArr);
	jx_ir_value_t* funcPtrVal = irInstr->super.m_OperandArr[0]->m_Value;

	// If funcPtrVal->m_Name is a build-in/intrinsic function, handle it here before generating
	// any code for the actual call. E.g. replace memset/memcpy with movs, etc.
	const char* funcName = funcPtrVal->m_Name;
	if (!jx_strncmp(funcName, "jir.", 4)) {
		// Intrinsic function
		if (!jx_strncmp(funcName, "jir.memset.", 11)) {
			if (jmirgen_genMemSet(ctx, irInstr)) {
				return NULL;
			}
		} else if (!jx_strncmp(funcName, "jir.memcpy.", 11)) {
			if (jmirgen_genMemCpy(ctx, irInstr)) {
				return NULL;
			}
		} else {
			JX_CHECK(false, "Unknown intrinsic function");
		}
	}

	jx_ir_type_pointer_t* funcPtrType = jx_ir_typeToPointer(funcPtrVal->m_Type);
	JX_CHECK(funcPtrType, "Expected pointer to function");
	jx_ir_type_function_t* funcType = jx_ir_typeToFunction(funcPtrType->m_BaseType);
	JX_CHECK(funcType, "Expected function type");

	// Write the N first arguments (N <= 4) of the current function to their shadow space.
	const uint32_t curFuncNumArgs = ctx->m_Func->m_Prototype->m_NumArgs;
	const uint32_t numFuncArgsToStore = jx_min_u32(curFuncNumArgs, JX_COUNTOF(kMIRFuncArgIReg));
	for (uint32_t iArg = 0; iArg < numFuncArgsToStore; ++iArg) {
		jx_mir_operand_t* argReg = jx_mir_funcGetArgument(ctx->m_MIRCtx, ctx->m_Func, iArg);
		jx_mir_operand_t* argShadowSpaceSlotRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, argReg->m_Type, kMIRRegGP_BP, kMIRRegGPNone, 1, 16 + iArg * 8);
		if (argReg->m_Type == JMIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, argShadowSpaceSlotRef, argReg));
		} else if (argReg->m_Type == JMIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, argShadowSpaceSlotRef, argReg));
		} else {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, argShadowSpaceSlotRef, argReg));
		}
	}

	// Make sure the stack has enough space for N-argument call
	jx_mir_funcAllocStackForCall(ctx->m_MIRCtx, ctx->m_Func, numOperands - 1);

	for (uint32_t iOperand = 1; iOperand < numOperands; ++iOperand) {
		jx_ir_value_t* argVal = irInstr->super.m_OperandArr[iOperand]->m_Value;
		jx_ir_type_t* argType = argVal->m_Type;
		jx_mir_operand_t* srcArgOp = jmirgen_getOperand(ctx, argVal);

#if 0
		// If the arg operand is a rargN register, use the stack variable instead to load the value.
		// This is needed because the rargN might have already be overwritten by the previous operand
		// e.g. when calling a function with the same operands as the current function but with a different
		// order (f(x, y, z) { return g(y, x, z); })
		if (srcArgOp->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regIsArg(srcArgOp->u.m_Reg)) {
			const uint32_t argID = jx_mir_regGetArgID(srcArgOp->u.m_Reg);
			JX_CHECK(argID != UINT32_MAX, "Expected argument ID");
			srcArgOp = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, srcArgOp->m_Type, kMIRRegGP_BP, kMIRRegGPNone, 1, 16 + argID * 8);
		}
#endif

		const uint32_t argID = iOperand - 1;
		if (argID < JX_COUNTOF(kMIRFuncArgIReg)) {
			if (jx_mir_opIsStackObj(srcArgOp) || srcArgOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
				jx_mir_operand_t* dstArgReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, kMIRFuncArgIReg[argID]);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstArgReg, srcArgOp));
			} else {
				if (argType->m_Kind == JIR_TYPE_F32) {
					jx_mir_operand_t* dstArgReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(argType), kMIRFuncArgFReg[argID]);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, dstArgReg, srcArgOp));

					if (funcType->m_IsVarArg && argID >= funcType->m_NumArgs) {
						// Win64 ABI: Floating-point values are only placed in the integer registers RCX, RDX, R8, and R9 
						// when there are varargs arguments. 
						// For floating-point values only, both the integer register and the floating-point register must 
						// contain the value, in case the callee expects the value in the integer registers.
						jx_mir_operand_t* ireg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I32, kMIRFuncArgIReg[argID]);
						jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movd(ctx->m_MIRCtx, ireg, dstArgReg));
					}
				} else if (argType->m_Kind == JIR_TYPE_F64) {
					jx_mir_operand_t* dstArgReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(argType), kMIRFuncArgFReg[argID]);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, dstArgReg, srcArgOp));

					if (funcType->m_IsVarArg && argID >= funcType->m_NumArgs) {
						// Win64 ABI: Floating-point values are only placed in the integer registers RCX, RDX, R8, and R9 
						// when there are varargs arguments. 
						// For floating-point values only, both the integer register and the floating-point register must 
						// contain the value, in case the callee expects the value in the integer registers.
						jx_mir_operand_t* ireg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64, kMIRFuncArgIReg[argID]);
						jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movq(ctx->m_MIRCtx, ireg, dstArgReg));
					}
				} else {
					jx_mir_operand_t* dstArgReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(argType), kMIRFuncArgIReg[argID]);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstArgReg, srcArgOp));
				}
			}
		} else {
			// Push on stack...
			if (jx_mir_opIsStackObj(srcArgOp) || srcArgOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
				jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmp, srcArgOp));

				jx_mir_operand_t* dstArgReg = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, kMIRRegGP_SP, kMIRRegGPNone, 1, 32 + (argID - JX_COUNTOF(kMIRFuncArgIReg) * 8));
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstArgReg, tmp));
			} else {
				jx_mir_operand_t* dstArgReg = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(argType), kMIRRegGP_SP, kMIRRegGPNone, 1, 32 + (argID - JX_COUNTOF(kMIRFuncArgIReg)) * 8);
				jmirgen_genMov(ctx, dstArgReg, srcArgOp);
			}
		}
	}

	jx_mir_operand_t* funcOp = jmirgen_getOperand(ctx, funcPtrVal);
	jx_mir_function_proto_t* funcProto = jmirgen_funcTypeToProto(ctx, funcType);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_call(ctx->m_MIRCtx, funcOp, funcProto));

	// Get result from rret register into a virtual register
	jx_mir_operand_t* resReg = NULL;
	if (funcType->m_RetType->m_Kind != JIR_TYPE_VOID) {
		jx_mir_type_kind retType = jmirgen_convertType(funcType->m_RetType);
		resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, retType);

		if (jx_mir_typeIsFloatingPoint(retType)) {
			jx_mir_operand_t* retReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, retType, kMIRRegXMM_0);
			if (retType == JMIR_TYPE_F32) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, resReg, retReg));
			} else if (retType == JMIR_TYPE_F64) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, resReg, retReg));
			} else {
				JX_CHECK(false, "Unknown floating point type!");
			}
		} else {
			jx_mir_operand_t* retReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, retType, kMIRRegGP_A);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, resReg, retReg));
		}
	}

	// Restore the N fist arguments (N <= 4) of the current function from their shadow space.
	for (uint32_t iArg = 0; iArg < numFuncArgsToStore; ++iArg) {
		jx_mir_operand_t* argReg = jx_mir_funcGetArgument(ctx->m_MIRCtx, ctx->m_Func, iArg);
		jx_mir_operand_t* argShadowSpaceSlotRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, argReg->m_Type, kMIRRegGP_BP, kMIRRegGPNone, 1, 16 + iArg * 8);
		if (argReg->m_Type == JMIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, argReg, argShadowSpaceSlotRef));
		} else if (argReg->m_Type == JMIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, argReg, argShadowSpaceSlotRef));
		} else {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, argReg, argShadowSpaceSlotRef));
		}
	}

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_shl(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_SHL, "Expected shr instruction");

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	JX_CHECK(!jx_ir_typeIsFloatingPoint(instrType), "float shr?");

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
	dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));

	if (rhs->m_Kind == JMIR_OPERAND_CONST) {
		// TODO: Check if constant is small enough to fit into imm8
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_shl(ctx->m_MIRCtx, dstReg, rhs));
	} else {
		jx_mir_operand_t* cl = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, kMIRRegGP_C);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, cl, rhs));
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_shl(ctx->m_MIRCtx, dstReg, cl));
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_shr(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_SHR, "Expected shr instruction");

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	JX_CHECK(!jx_ir_typeIsFloatingPoint(instrType), "float shr?");

	jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
	dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));

	if (rhs->m_Kind == JMIR_OPERAND_CONST) {
		// TODO: Check if constant is small enough to fit into imm8
	} else {
		jx_mir_operand_t* cl = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, kMIRRegGP_C);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, cl, rhs));
		rhs = cl;
	}

	if (jx_ir_typeIsSigned(instrType)) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_sar(ctx->m_MIRCtx, dstReg, rhs));
	} else {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_shr(ctx->m_MIRCtx, dstReg, rhs));
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_trunc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_TRUNC, "Expected trunc instruction");

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);

	if (operand->m_Kind != JMIR_OPERAND_REGISTER) {
		if (jx_mir_opIsStackObj(operand) || operand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, operand));
			operand = tmpReg;
		} else {
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, operand->m_Type);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, operand));
			operand = tmpReg;
		}
	}

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, resReg, operand));

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_zext(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_ZEXT, "Expected zext instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsInteger(instrVal->m_Type), "Expected integer target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsIntegral(operandVal->m_Type), "Expected integer operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	JX_CHECK(targetType > operand->m_Type, "Expected target type to be larger than operand type!");

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movzx(ctx->m_MIRCtx, resReg, operand));

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_sext(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_SEXT, "Expected sext instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsInteger(instrVal->m_Type), "Expected integer target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsIntegral(operandVal->m_Type), "Expected integer operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	JX_CHECK(targetType > operand->m_Type, "Expected target type to be larger than operand type!");

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsx(ctx->m_MIRCtx, resReg, operand));

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_ptrToInt(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	JX_CHECK(jx_ir_typeIsInteger(instrType), "Expected integer type");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	jx_mir_operand_t* dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, operand));

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_intToPtr(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	return jmirgen_instrBuild_bitcast(ctx, irInstr);
}

static jx_mir_operand_t* jmirgen_instrBuild_bitcast(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
	if (operand->m_Kind != JMIR_OPERAND_REGISTER) {
		if (jx_mir_opIsStackObj(operand) || operand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, operand));
			operand = tmpReg;
		} else {
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, operand));
			operand = tmpReg;
		}
	}
	return operand;
}

static jx_mir_operand_t* jmirgen_instrBuild_fpext(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_FPEXT, "Expected fpext instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsFloatingPoint(instrVal->m_Type), "Expected floating point target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsFloatingPoint(operandVal->m_Type), "Expected floating point operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	JX_CHECK(targetType == JMIR_TYPE_F64 && operand->m_Type == JMIR_TYPE_F32, "Only know how to extend 32-bit FP to 64-bit FP.");

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtss2sd(ctx->m_MIRCtx, resReg, operand));

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_fptrunc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_FPTRUNC, "Expected fptrunc instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsFloatingPoint(instrVal->m_Type), "Expected floating point target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsFloatingPoint(operandVal->m_Type), "Expected floating point operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	JX_CHECK(targetType == JMIR_TYPE_F32 && operand->m_Type == JMIR_TYPE_F64, "Only know how to truncate 64-bit FP to 32-bit FP.");

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsd2ss(ctx->m_MIRCtx, resReg, operand));

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_fp2ui(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_FP2UI, "Expected fp2ui instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsInteger(instrVal->m_Type) && jx_ir_typeIsUnsigned(instrVal->m_Type), "Expected unsigned integer target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsFloatingPoint(operandVal->m_Type), "Expected floating point operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);

	jx_mir_type_kind instrValType = jmirgen_convertType(instrVal->m_Type);
	
	jx_mir_type_kind targetType = (instrValType == JMIR_TYPE_I8 || instrValType == JMIR_TYPE_I16)
		? JMIR_TYPE_I32
		: JMIR_TYPE_I64
		;

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	if (instrValType == JMIR_TYPE_I64) {
		if (operand->m_Type == JMIR_TYPE_F32) {
			// ...
			// cvttss2si tmpReg, xmm_op
			// mov tempReg2, tempReg
			// subss xmm_op, { 0x5f000000 }
			// cvttss2si resReg, xmm_op
			// sar tempReg2, 63
			// and resReg, tempReg2
			// or resReg, tempReg
			// ...
			jx_mir_operand_t* operandCopy = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_F32);
			jx_mir_operand_t* tmpReg1 = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
			jx_mir_operand_t* tmpReg2 = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, operandCopy, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvttss2si(ctx->m_MIRCtx, tmpReg1, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg2, tmpReg1));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_subss(ctx->m_MIRCtx, operandCopy, jx_mir_opFConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_F32, 0x1p+63)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvttss2si(ctx->m_MIRCtx, resReg, operandCopy));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_sar(ctx->m_MIRCtx, tmpReg2, jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, 63)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_and(ctx->m_MIRCtx, resReg, tmpReg2));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_or(ctx->m_MIRCtx, resReg, tmpReg1));
		} else if (operand->m_Type == JMIR_TYPE_F64) {
			// ...
			// cvttsd2si tmpReg, xmm_op
			// mov tempReg2, tempReg
			// subsd xmm_op, { 0x43e0000000000000 }
			// cvttsd2si resReg, xmm_op
			// sar tempReg2, 63
			// and resReg, tempReg2
			// or resReg, tempReg
			// ...
			jx_mir_operand_t* operandCopy = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_F32);
			jx_mir_operand_t* tmpReg1 = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
			jx_mir_operand_t* tmpReg2 = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, operandCopy, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvttsd2si(ctx->m_MIRCtx, tmpReg1, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg2, tmpReg1));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_subsd(ctx->m_MIRCtx, operandCopy, jx_mir_opFConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_F64, 0x1p+63)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvttsd2si(ctx->m_MIRCtx, resReg, operandCopy));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_sar(ctx->m_MIRCtx, tmpReg2, jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, 63)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_and(ctx->m_MIRCtx, resReg, tmpReg2));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_or(ctx->m_MIRCtx, resReg, tmpReg1));
		} else {
			JX_CHECK(false, "Unknown floating point type");
		}
	} else {
		if (operand->m_Type == JMIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvttss2si(ctx->m_MIRCtx, resReg, operand));
		} else if (operand->m_Type == JMIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvttsd2si(ctx->m_MIRCtx, resReg, operand));
		} else {
			JX_CHECK(false, "Unknown floating point type");
		}
	}

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_fp2si(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_FP2SI, "Expected fp2si instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsInteger(instrVal->m_Type) && jx_ir_typeIsSigned(instrVal->m_Type), "Expected signed integer target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsFloatingPoint(operandVal->m_Type), "Expected floating point operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	if (targetType != JMIR_TYPE_I32 && targetType != JMIR_TYPE_I64) {
		targetType = JMIR_TYPE_I32;
	}

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	if (operand->m_Type == JMIR_TYPE_F32) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtss2si(ctx->m_MIRCtx, resReg, operand));
	} else if (operand->m_Type == JMIR_TYPE_F64) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsd2si(ctx->m_MIRCtx, resReg, operand));
	} else {
		JX_CHECK(false, "Unknown floating point type");
	}

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_ui2fp(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_UI2FP, "Expected ui2fp instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsFloatingPoint(instrVal->m_Type), "Expected floating point target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsInteger(operandVal->m_Type) && jx_ir_typeIsUnsigned(operandVal->m_Type), "Expected unsigned integer operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);
	operand = jmirgen_ensureOperandI32OrI64(ctx, operand, false);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);

	if (operand->m_Type == JMIR_TYPE_I64) {
		// NOTE: LLVM generates the following:
		// - when converting a 64-bit unsigned integer to 32-bit float
		//    float res = (x & 0x8000000000000000llu) != 0 ? 2.0f * (float)((x & 1) | (x >> 1)) : (float)x;
		// Note that the right shift is unsigned, i.e. does not preserve the sign bit.
		// The reason (afaiu) is the following: cvtsi2ss treats the integer operand as signed. If the MSB of
		// the uint64_t is set it will produce a negative floating point value. The unsigned right shift 
		// resets the sign bit. Converting to f32 and then multiplying by 2 brings back the result to the 
		// correct range. It will probably be wrong (not enough precision) but at least it won't be negative.
		// I haven't figured out what ORing with (x & 1) does though.
		// 
		// - when converting a 64-bit unsigned integer to 64-bit float things are a bit more complicated.
		// LLVM breaks the uint64 into lower and upper 32-bit halves. The lower half is ORed with 
		// 0x4330000000000000 which is the first double with an exponent of 1075 (1023 base exponent + 52 
		// mantissa bits). The upper half is ORed with 0x4530000000000000 which is the first double with an
		// exponent of 1107 (1075 + 32 bits from the lower half). Each one of those doubles is subtracted from
		// its corresponding base (i.e. (double)0x45300000xxxxxxxx - (double)0x4530000000000000) which results 
		// in the corresponding uint32 part in double floating point. The two parts are then added together to 
		// get the final result.
		// 
		// This works because for the lower part each mantissa bit changes the double value by 1.0 and for the 
		// higher part each mantissa bit changes the double value by 4294967296.0.
		if (targetType == JMIR_TYPE_F32) {
			operand = jmirgen_ensureOperandReg(ctx, operand);

			//   ...
			//   test     op_reg, op_reg
			//   js       lbl_negative
			// lbl_positive:
			//   cvtsi2ss xmm_reg, op_reg
			//   jmp      lbl_end
			// lbl_negative:
			//   mov      tmp, op_reg
			//   shr      tmp, 1
			//   and      op_reg, 1
			//   or       op_reg, tmp
			//   cvtsi2ss xmm_reg, op_reg
			//   addss    xmm_reg, xmm_reg
			// lbl_end:
			//   ...
			jx_mir_basic_block_t* bbNegative = jx_mir_bbAlloc(ctx->m_MIRCtx);
			jx_mir_basic_block_t* bbPositive = jx_mir_bbAlloc(ctx->m_MIRCtx);
			jx_mir_basic_block_t* bbEnd = jx_mir_bbAlloc(ctx->m_MIRCtx);

			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_test(ctx->m_MIRCtx, operand, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_jcc(ctx->m_MIRCtx, JMIR_CC_S, jx_mir_opBasicBlock(ctx->m_MIRCtx, ctx->m_Func, bbNegative)));
			jx_mir_funcAppendBasicBlock(ctx->m_MIRCtx, ctx->m_Func, ctx->m_BasicBlock);
			
			ctx->m_BasicBlock = bbPositive;
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsi2ss(ctx->m_MIRCtx, resReg, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_jmp(ctx->m_MIRCtx, jx_mir_opBasicBlock(ctx->m_MIRCtx, ctx->m_Func, bbEnd)));
			jx_mir_funcAppendBasicBlock(ctx->m_MIRCtx, ctx->m_Func, ctx->m_BasicBlock);

			ctx->m_BasicBlock = bbNegative;
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_shr(ctx->m_MIRCtx, tmpReg, jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, 1)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_and(ctx->m_MIRCtx, tmpReg, jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, 1)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_or(ctx->m_MIRCtx, operand, tmpReg));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsi2ss(ctx->m_MIRCtx, resReg, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_addss(ctx->m_MIRCtx, resReg, resReg));
			jx_mir_funcAppendBasicBlock(ctx->m_MIRCtx, ctx->m_Func, ctx->m_BasicBlock);

			ctx->m_BasicBlock = bbEnd;
		} else if (targetType == JMIR_TYPE_F64) {
			// ...
			// movq      xmm_reg, operand
			// punpckldq xmm_reg, { 0x43300000, 0x45300000, 0, 0 }
			// subpd     xmm_reg, { 0x4330000000000000, 0x4530000000000000 }
			// movapd    res, xmm_reg
			// unpckhpd  res, xmm_reg
			// addsd     res, xmm_reg
			// ...
			if (!jx_mir_getGlobalVarByName(ctx->m_MIRCtx, "$__ui64_to_f64_c0__$")) {
				static const uint32_t ui64_to_f64_c0[4] = { 0x43300000, 0x45300000, 0, 0 };
				jx_mir_global_variable_t* gv = jx_mir_globalVarBegin(ctx->m_MIRCtx, "$__ui64_to_f64_c0__$", 16);
				jx_mir_globalVarAppendData(ctx->m_MIRCtx, gv, (const uint8_t*)&ui64_to_f64_c0[0], sizeof(uint32_t) * 4);
				jx_mir_globalVarEnd(ctx->m_MIRCtx, gv);
			}
			if (!jx_mir_getGlobalVarByName(ctx->m_MIRCtx, "$__ui64_to_f64_c1__$")) {
				static const uint64_t ui64_to_f64_c1[2] = { 0x4330000000000000ull, 0x4530000000000000ull };
				jx_mir_global_variable_t* gv = jx_mir_globalVarBegin(ctx->m_MIRCtx, "$__ui64_to_f64_c1__$", 16);
				jx_mir_globalVarAppendData(ctx->m_MIRCtx, gv, (const uint8_t*)&ui64_to_f64_c1[0], sizeof(uint64_t) * 2);
				jx_mir_globalVarEnd(ctx->m_MIRCtx, gv);
			}

			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_F64);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movq(ctx->m_MIRCtx, tmpReg, operand));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_punpckldq(ctx->m_MIRCtx, tmpReg, jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_F128, "$__ui64_to_f64_c0__$", 0)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_subpd(ctx->m_MIRCtx, tmpReg, jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_F128, "$__ui64_to_f64_c1__$", 0)));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movapd(ctx->m_MIRCtx, resReg, tmpReg));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_unpckhpd(ctx->m_MIRCtx, resReg, tmpReg));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_addsd(ctx->m_MIRCtx, resReg, tmpReg));
		} else {
			JX_CHECK(false, "Unknown floating point type");
		}
	} else {
		if (targetType == JMIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsi2ss(ctx->m_MIRCtx, resReg, operand));
		} else if (targetType == JMIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsi2sd(ctx->m_MIRCtx, resReg, operand));
		} else {
			JX_CHECK(false, "Unknown floating point type");
		}
	}

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_si2fp(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_SI2FP, "Expected si2fp instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsFloatingPoint(instrVal->m_Type), "Expected floating point target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsInteger(operandVal->m_Type) && jx_ir_typeIsSigned(operandVal->m_Type), "Expected signed integer operand!");

	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);
	operand = jmirgen_ensureOperandRegOrMem(ctx, operand);
	operand = jmirgen_ensureOperandI32OrI64(ctx, operand, true);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	if (targetType == JMIR_TYPE_F32) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsi2ss(ctx->m_MIRCtx, resReg, operand));
	} else if (targetType == JMIR_TYPE_F64) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cvtsi2sd(ctx->m_MIRCtx, resReg, operand));
	} else {
		JX_CHECK(false, "Unknown floating point type");
	}

	return resReg;
}

static jx_mir_basic_block_t* jmirgen_getOrCreateBasicBlock(jx_mirgen_context_t* ctx, jx_ir_basic_block_t* irBB)
{
	jmir_basic_block_item_t* key = &(jmir_basic_block_item_t){
		.m_IRBB = irBB
	};
	jmir_basic_block_item_t* existingItem = (jmir_basic_block_item_t*)jx_hashmapGet(ctx->m_BasicBlockMap, key);
	if (existingItem) {
		return existingItem->m_MIRBB;
	}

	jx_mir_basic_block_t* mirBB = jx_mir_bbAlloc(ctx->m_MIRCtx);
	if (!mirBB) {
		return NULL;
	}

	jmir_basic_block_item_t* item = &(jmir_basic_block_item_t){
		.m_IRBB = irBB,
		.m_MIRBB = mirBB
	};
	jx_hashmapSet(ctx->m_BasicBlockMap, item);

	return mirBB;
}

static jx_mir_basic_block_t* jmirgen_getBasicBlock(jx_mirgen_context_t* ctx, jx_ir_basic_block_t* irBB)
{
	jmir_basic_block_item_t* key = &(jmir_basic_block_item_t){
		.m_IRBB = irBB
	};

	jmir_basic_block_item_t* existingItem = (jmir_basic_block_item_t*)jx_hashmapGet(ctx->m_BasicBlockMap, key);
	return existingItem
		? existingItem->m_MIRBB
		: NULL
		;
}

static jx_mir_operand_t* jmirgen_getOperand(jx_mirgen_context_t* ctx, jx_ir_value_t* val)
{
	jx_mir_operand_t* operand = NULL;
	if (val->m_Kind == JIR_VALUE_INSTRUCTION) {
		jmir_value_operand_item_t* item = (jmir_value_operand_item_t*)jx_hashmapGet(ctx->m_ValueMap, &(jmir_value_operand_item_t){.m_IRVal = val });
		operand = item
			? item->m_MIROperand
			: NULL
			;
	} else if (val->m_Kind == JIR_VALUE_ARGUMENT) {
		jx_ir_argument_t* arg = jx_ir_valueToArgument(val);
		JX_CHECK(arg, "Expected function argument!");
		operand = jx_mir_funcGetArgument(ctx->m_MIRCtx, ctx->m_Func, arg->m_ID);
	} else if (val->m_Kind == JIR_VALUE_CONSTANT) {
		jx_ir_constant_t* c = jx_ir_valueToConst(val);
		JX_CHECK(c, "Expected constant!");
		if (jx_ir_typeIsFloatingPoint(val->m_Type)) {
			operand = jx_mir_opFConst(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(val->m_Type), c->u.m_F64);
		} else if (jx_ir_typeIsInteger(val->m_Type)) {
			operand = jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(val->m_Type), c->u.m_I64);
		} else if (val->m_Type->m_Kind == JIR_TYPE_BOOL) {
			operand = jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, c->u.m_Bool ? 1 : 0);
		} else if (val->m_Type->m_Kind == JIR_TYPE_POINTER) {
			operand = jx_mir_opIConst(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, (int64_t)c->u.m_Ptr);
		} else {
			JX_NOT_IMPLEMENTED();
		}
	} else if (val->m_Kind == JIR_VALUE_FUNCTION) {
		const char* funcName = val->m_Name;
		JX_CHECK(funcName, "Expected named function value!");
		if (!jx_strncmp(funcName, "jir.", 4)) {
			// Intrinsic function
			if (!jx_strncmp(funcName, "jir.memset.", 11)) {
				operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, "memset", 0);
			} else if (!jx_strncmp(funcName, "jir.memcpy.", 11)) {
				operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, "memcpy", 0);
			} else {
				JX_CHECK(false, "Unknown intrinsic function");
			}
		} else {
			operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, funcName, 0);
		}
	} else if (val->m_Kind == JIR_VALUE_GLOBAL_VARIABLE) {
		operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, val->m_Name, 0);
	} else {
		// TODO: Other IR values
		JX_NOT_IMPLEMENTED();
	}

	JX_CHECK(operand, "Failed to find operand for value!");

	return operand;
}

static bool jmirgen_genMemSet(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	jx_ir_value_t* ptrVal = irInstr->super.m_OperandArr[1]->m_Value;
	jx_ir_value_t* valVal = irInstr->super.m_OperandArr[2]->m_Value;
	jx_ir_value_t* sizeVal = irInstr->super.m_OperandArr[3]->m_Value;

	// TODO: Do the same even if value is not constant or not 0 (imul val_reg64, 0x0101010101010101).
	// TODO: Try rep stosb for larger sizes (e.g. 128 to 512 bytes) if ERMSB is supported.
	jx_ir_constant_t* valConst = jx_ir_valueToConst(valVal);
	jx_ir_constant_t* sizeConst = jx_ir_valueToConst(sizeVal);
	if (valConst && sizeConst && valConst->u.m_I64 == 0 && sizeConst->u.m_I64 <= JX_MIRGEN_CONFIG_INLINE_MEMSET_LIMIT) {
		// memset(ptr, 0, size);
		int64_t sz = sizeConst->u.m_I64;

		// xor rax, rax
		jx_mir_operand_t* zeroReg64 = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64, kMIRRegGP_A);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_xor(ctx->m_MIRCtx, zeroReg64, zeroReg64));

		// lea vr, [ptr]
		jx_mir_operand_t* ptrOp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, ptrOp, jmirgen_getOperand(ctx, ptrVal)));

		int32_t offset = 0;
		while (sz > 0) {
			jx_mir_type_kind movType = JMIR_TYPE_I8;
			if (sz >= 8) {
				movType = JMIR_TYPE_I64;
			} else if (sz >= 4) {
				movType = JMIR_TYPE_I32;
			} else if (sz >= 2) {
				movType = JMIR_TYPE_I16;
			}

			// mov [vr + offset], rax/eax/ax/al
			jx_mir_operand_t* zeroReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, movType, kMIRRegGP_A);
			jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, movType, ptrOp->u.m_Reg, kMIRRegGPNone, 1, offset);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, memRef, zeroReg));

			const uint32_t typeSz = jx_mir_typeGetSize(movType);
			sz -= typeSz;
			offset += typeSz;
		}

		return true;
	}

	return false;
}

static bool jmirgen_genMemCpy(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	jx_ir_constant_t* sizeConst = jx_ir_valueToConst(irInstr->super.m_OperandArr[3]->m_Value);
	if (sizeConst && sizeConst->u.m_I64 <= JX_MIRGEN_CONFIG_INLINE_MEMCPY_LIMIT) {
		int64_t sz = sizeConst->u.m_I64;
		
		// lea dst_vr, [dstPtr]
		jx_mir_operand_t* dstOp = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
		jx_mir_operand_t* dstPtrOp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstPtrOp, dstOp));

		// lea src_vr, [srcPtr]
		jx_mir_operand_t* srcOp = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[2]->m_Value);
		jx_mir_operand_t* srcPtrOp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, srcPtrOp, srcOp));

		int32_t offset = 0;
		while (sz > 0) {
			jx_mir_type_kind movType = JMIR_TYPE_I8;
			if (sz >= 8) {
				movType = JMIR_TYPE_I64;
			} else if (sz >= 4) {
				movType = JMIR_TYPE_I32;
			} else if (sz >= 2) {
				movType = JMIR_TYPE_I16;
			}

			// mov tmp, [src_vr + offset]
			// mov [dst_vr + offset], tmp
			jx_mir_operand_t* tmpReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, movType, kMIRRegGP_A);
			jx_mir_operand_t* srcMemRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, movType, srcPtrOp->u.m_Reg, kMIRRegGPNone, 1, offset);
			jx_mir_operand_t* dstMemRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, movType, dstPtrOp->u.m_Reg, kMIRRegGPNone, 1, offset);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, srcMemRef));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstMemRef, tmpReg));

			const uint32_t typeSz = jx_mir_typeGetSize(movType);
			sz -= typeSz;
			offset += typeSz;
		}

		return true; // memcpy call handled. 
	}

	return false; // let the caller generate a call to CRT's memcpy
}

static bool jmirgen_genMov(jx_mirgen_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	if (jx_mir_typeIsFloatingPoint(dst->m_Type)) {
		// If dst is a memory reference, make sure src is a reg.
		if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, dst->m_Type);
			if (!jmirgen_genMov(ctx, tmp, src)) {
				return false;
			}
			src = tmp;
		} else if (dst->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			JX_NOT_IMPLEMENTED();
		} else {
			JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "Expected register operand");
		}

		if (dst->m_Type == JMIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movss(ctx->m_MIRCtx, dst, src));
		} else if (dst->m_Type == JMIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsd(ctx->m_MIRCtx, dst, src));
		} else {
			JX_CHECK(false, "Unknown floating point type");
			return false;
		}
	} else {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dst, src));
	}

	return true;
}

static jx_mir_operand_t* jmirgen_ensureOperandRegOrMem(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand)
{
	JX_CHECK(operand, "Invalid operand!");

	const bool isRegOrMem = false
		|| operand->m_Kind == JMIR_OPERAND_REGISTER
		|| operand->m_Kind == JMIR_OPERAND_MEMORY_REF
		;
	if (isRegOrMem) {
		return operand;
	}

	jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, operand->m_Type);
	jmirgen_genMov(ctx, tmpReg, operand);
	return tmpReg;
}

static jx_mir_operand_t* jmirgen_ensureOperandReg(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand)
{
	JX_CHECK(operand, "Invalid operand!");

	const bool isReg = operand->m_Kind == JMIR_OPERAND_REGISTER;
	if (isReg) {
		return operand;
	}

	jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, operand->m_Type);
	jmirgen_genMov(ctx, tmpReg, operand);
	return tmpReg;
}

static jx_mir_operand_t* jmirgen_ensureOperandI32OrI64(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand, bool signExt)
{
	JX_CHECK(operand, "Invalid operand!");

	const bool isI32OrI64 = false
		|| operand->m_Type == JMIR_TYPE_I32
		|| operand->m_Type == JMIR_TYPE_I64
		;
	if (isI32OrI64) {
		return operand;
	}

	jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I32);
	if (signExt) {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movsx(ctx->m_MIRCtx, tmpReg, operand));
	} else {
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movzx(ctx->m_MIRCtx, tmpReg, operand));
	}
	return tmpReg;
}

static jx_mir_operand_t* jmirgen_ensureOperandNotConstI64(jx_mirgen_context_t* ctx, jx_mir_operand_t* operand)
{
	JX_CHECK(operand, "Invalid operand!");

	const bool isConstI64 = true
		&& operand->m_Kind == JMIR_OPERAND_CONST 
		&& operand->m_Type == JMIR_TYPE_I64 
		&& (operand->u.m_ConstI64 < INT32_MIN || operand->u.m_ConstI64 > INT32_MAX)
		;
	if (!isConstI64) {
		return operand;
	}

	jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I64);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, operand));

	return tmpReg;
}

static jx_mir_function_proto_t* jmirgen_funcTypeToProto(jx_mirgen_context_t* ctx, jx_ir_type_function_t* funcType)
{
	JX_CHECK(funcType, "Expected valid function type");

	jx_mir_type_kind* args = NULL;
	const uint32_t numArgs = funcType->m_NumArgs;
	if (numArgs) {
		args = (jx_mir_type_kind*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_type_kind) * numArgs);
		if (!args) {
			return NULL;
		}

		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			args[iArg] = jmirgen_convertType(funcType->m_Args[iArg]);
		}
	}

	jx_mir_function_proto_t* funcProto = jx_mir_funcProto(ctx->m_MIRCtx, jmirgen_convertType(funcType->m_RetType), numArgs, args, funcType->m_IsVarArg ? JMIR_FUNC_FLAGS_VARARG_Msk : 0);

	JX_FREE(ctx->m_Allocator, args);

	return funcProto;
}

static bool jmirgen_processPhis(jx_mirgen_context_t* ctx)
{
	const uint32_t numPhiInstructions = (uint32_t)jx_array_sizeu(ctx->m_PhiInstrArr);
	for (uint32_t iPhi = 0; iPhi < numPhiInstructions; ++iPhi) {
		jx_ir_instruction_t* phiInstr = ctx->m_PhiInstrArr[iPhi];

		jx_mir_operand_t* dstReg = jmirgen_getOperand(ctx, jx_ir_instrToValue(phiInstr));

		const uint32_t numOperands = (uint32_t)jx_array_sizeu(phiInstr->super.m_OperandArr);
		for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
			jx_ir_value_t* irVal = phiInstr->super.m_OperandArr[iOperand + 0]->m_Value;
			jx_ir_basic_block_t* irBB = jx_ir_valueToBasicBlock(phiInstr->super.m_OperandArr[iOperand + 1]->m_Value);

			jx_mir_operand_t* srcReg = jmirgen_getOperand(ctx, irVal);
			if (!srcReg) {
				JX_CHECK(false, "Operand not found!");
				return false;
			}

			jx_mir_basic_block_t* bb = jmirgen_getBasicBlock(ctx, irBB);
			if (!bb) {
				JX_CHECK(false, "Basic block not found!");
				return false;
			}

			jx_mir_instruction_t* movInstr = NULL; 
			if (dstReg->m_Type == JMIR_TYPE_F32) {
				movInstr = jx_mir_movss(ctx->m_MIRCtx, dstReg, srcReg);
			} else if (dstReg->m_Type == JMIR_TYPE_F64) {
				movInstr = jx_mir_movsd(ctx->m_MIRCtx, dstReg, srcReg);
			} else {
				movInstr = jx_mir_mov(ctx->m_MIRCtx, dstReg, srcReg);
			}

			jx_mir_instruction_t* firstTerminator = jx_mir_bbGetFirstTerminatorInstr(ctx->m_MIRCtx, bb);
			if (!firstTerminator) {
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, bb, movInstr);
			} else {
				jx_mir_bbInsertInstrBefore(ctx->m_MIRCtx, bb, firstTerminator, movInstr);
			}
		}
	}

	return true;
}

static jx_mir_type_kind jmirgen_convertType(jx_ir_type_t* irType)
{
	switch (irType->m_Kind) {
	case JIR_TYPE_VOID: {
		return JMIR_TYPE_VOID;
	} break;
	case JIR_TYPE_BOOL:
	case JIR_TYPE_U8:
	case JIR_TYPE_I8: {
		return JMIR_TYPE_I8;
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		return JMIR_TYPE_I16;
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		return JMIR_TYPE_I32;
	} break;
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		return JMIR_TYPE_I64;
	} break;
	case JIR_TYPE_F32: {
		return JMIR_TYPE_F32;
	} break;
	case JIR_TYPE_F64: {
		return JMIR_TYPE_F64;
	} break;
	case JIR_TYPE_POINTER: {
		return JMIR_TYPE_PTR;
	} break;
	case JIR_TYPE_STRUCT: {
		const uint32_t structSize = (uint32_t)jx_ir_typeGetSize(irType);
		if (structSize <= 8 && jx_isPow2_u32(structSize)) {
			if (structSize == 8) {
				return JMIR_TYPE_I64;
			} else if (structSize == 4) {
				return JMIR_TYPE_I32;
			} else if (structSize == 2) {
				return JMIR_TYPE_I16;
			}
			JX_CHECK(structSize == 1, "WTF?");
			return JMIR_TYPE_I8;
		} else {
			return JMIR_TYPE_PTR;
		}
	} break;
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL:
	case JIR_TYPE_FUNCTION:
	case JIR_TYPE_ARRAY: {
		JX_NOT_IMPLEMENTED();
	} break;
	default:
		JX_CHECK(false, "Unknown IR type");
		break;
	}

	return JMIR_TYPE_VOID;
}

static uint64_t jmir_funcItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jmir_func_item_t* funcItem = (const jmir_func_item_t*)item;
	return (uint64_t)(uintptr_t)funcItem->m_IRFunc;
}

static int32_t jmir_funcItemCompare(const void* a, const void* b, void* udata)
{
	const jmir_func_item_t* funcItemA = (const jmir_func_item_t*)a;
	const jmir_func_item_t* funcItemB = (const jmir_func_item_t*)b;
	return (uintptr_t)funcItemA->m_IRFunc < (uintptr_t)funcItemB->m_IRFunc
		? -1
		: (((uintptr_t)funcItemA->m_IRFunc > (uintptr_t)funcItemB->m_IRFunc) ? 1 : 0)
		;
}

static uint64_t jmir_bbItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jmir_basic_block_item_t* funcItem = (const jmir_basic_block_item_t*)item;
	return (uint64_t)(uintptr_t)funcItem->m_IRBB;
}

static int32_t jmir_bbItemCompare(const void* a, const void* b, void* udata)
{
	const jmir_basic_block_item_t* bbItemA = (const jmir_basic_block_item_t*)a;
	const jmir_basic_block_item_t* bbItemB = (const jmir_basic_block_item_t*)b;
	return (uintptr_t)bbItemA->m_IRBB < (uintptr_t)bbItemB->m_IRBB
		? -1
		: (((uintptr_t)bbItemA->m_IRBB > (uintptr_t)bbItemB->m_IRBB) ? 1 : 0)
		;
}

static uint64_t jmir_valueOperandItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jmir_value_operand_item_t* funcItem = (const jmir_value_operand_item_t*)item;
	return (uint64_t)(uintptr_t)funcItem->m_IRVal;
}

static int32_t jmir_valueOperandItemCompare(const void* a, const void* b, void* udata)
{
	const jmir_value_operand_item_t* valItemA = (const jmir_value_operand_item_t*)a;
	const jmir_value_operand_item_t* valItemB = (const jmir_value_operand_item_t*)b;
	return (uintptr_t)valItemA->m_IRVal < (uintptr_t)valItemB->m_IRVal
		? -1
		: (((uintptr_t)valItemA->m_IRVal > (uintptr_t)valItemB->m_IRVal) ? 1 : 0)
		;
}
