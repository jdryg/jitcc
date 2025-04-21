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
static jx_mir_operand_t* jmirgen_instrBuild_setcc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_setcc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_setcc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
static jx_mir_operand_t* jmirgen_instrBuild_setcc(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr);
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
static jx_mir_basic_block_t* jmirgen_getOrCreateBasicBlock(jx_mirgen_context_t* ctx, jx_ir_basic_block_t* irBB);
static jx_mir_operand_t* jmirgen_getOperand(jx_mirgen_context_t* ctx, jx_ir_value_t* val);
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
		jx_mir_globalVarAppendData(mirctx, gv, (const uint8_t*)&init->u.m_F32, sizeof(float));
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
	jx_mir_function_t* func = jx_mir_funcBegin(mirctx, retType, numArgs, args, flags, funcName);
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
			
			jx_mir_funcAppendBasicBlock(mirctx, func, bb);

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
		retReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, mirType, JMIR_HWREG_RET);

		if (mirRetVal->m_Kind == JMIR_OPERAND_STACK_OBJECT || mirRetVal->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, retReg, mirRetVal));
		} else {
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, retReg, mirRetVal));
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

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(addInstr)->m_Type;
	if (jx_ir_typeIsFloatingPoint(instrType)) {
		// Floating point multiplication
		JX_NOT_IMPLEMENTED();
	} else {
		jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, addInstr->super.m_OperandArr[0]->m_Value);
		jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, addInstr->super.m_OperandArr[1]->m_Value);
		dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

		// mov reg, lhs
		// add reg, rhs
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_add(ctx->m_MIRCtx, dstReg, rhs));
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_sub(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_SUB, "Expected xor instruction");

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	if (jx_ir_typeIsFloatingPoint(instrType)) {
		JX_NOT_IMPLEMENTED();
	} else {
		jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
		jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
		dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

		// mov reg, lhs
		// sub reg, rhs
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstReg, lhs));
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_sub(ctx->m_MIRCtx, dstReg, rhs));
	}

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_mul(jx_mirgen_context_t* ctx, jx_ir_instruction_t* mulInstr)
{
	JX_CHECK(mulInstr->m_OpCode == JIR_OP_MUL, "Expected mul instruction");

	jx_mir_operand_t* dstReg = NULL;

	jx_ir_type_t* instrType = jx_ir_instrToValue(mulInstr)->m_Type;
	if (jx_ir_typeIsFloatingPoint(instrType)) {
		// Floating point multiplication
		JX_NOT_IMPLEMENTED();
	} else {
		jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, mulInstr->super.m_OperandArr[0]->m_Value);
		jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, mulInstr->super.m_OperandArr[1]->m_Value);
		dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(instrType));

		// Make sure rhs is either a memory reference/stack object or a register. If it's not (e.g. a constant)
		// move it to a virtual register first.
		if (rhs->m_Kind != JMIR_OPERAND_REGISTER && rhs->m_Kind != JMIR_OPERAND_MEMORY_REF && rhs->m_Kind != JMIR_OPERAND_STACK_OBJECT) {
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

	jx_mir_operand_t* dstReg = NULL;

	if (jx_ir_typeIsFloatingPoint(instrType)) {
		JX_NOT_IMPLEMENTED();
	} else {
		jx_mir_type_kind type = jmirgen_convertType(instrType);
		JX_CHECK(type == JMIR_TYPE_I32 || type == JMIR_TYPE_I64 || type == JMIR_TYPE_PTR, "Expected 32-bit or 64-bit division");

		jx_mir_operand_t* lhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);
		jx_mir_operand_t* rhs = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[1]->m_Value);
		if (rhs->m_Kind == JMIR_OPERAND_CONST) {
			// TODO: Div by constant. Try to avoid div/idiv. Is this the right place to do this or it's better to 
			// add an IR pass?
			jx_mir_operand_t* reg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, rhs->m_Type);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, reg, rhs));
			rhs = reg;
		}

		dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, type);

		jx_mir_operand_t* loReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, type, JMIR_HWREG_A);
		jx_mir_operand_t* hiReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, type, JMIR_HWREG_D);

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

	jx_mir_operand_t* dstReg = NULL;
	if (jx_ir_typeIsFloatingPoint(cmpType)) {
		JX_NOT_IMPLEMENTED();
	} else {
		// cmp lhs, rhs
		// setcc reg8
		dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8);
		jx_ir_condition_code irCC = irInstr->m_OpCode - JIR_OP_SET_CC_BASE;
		jx_mir_condition_code mirCC = jx_ir_typeIsSigned(cmpType)
			? kIRCCToMIRCCSigned[irCC]
			: kIRCCToMIRCCUnsigned[irCC]
			;

		const bool lhsConst = lhs->m_Kind == JMIR_OPERAND_CONST;
		const bool rhsConst = rhs->m_Kind == JMIR_OPERAND_CONST;
		if (lhsConst && rhsConst) {
			JX_CHECK(false, "I think I need an optimization pass!");
		} else if (lhsConst && !rhsConst) {
			// Swap operand and condition code.
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_cmp(ctx->m_MIRCtx, rhs, lhs));
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_setcc(ctx->m_MIRCtx, jx_mir_ccSwapOperands(mirCC), dstReg));
		} else {
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
	if (jx_ir_typeIsFloatingPoint(ptrType->m_BaseType)) {
		JX_NOT_IMPLEMENTED();
	} else {
		jx_mir_type_kind regType = jmirgen_convertType(ptrType->m_BaseType);
		dstReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, regType);

		if (srcOperand->m_Kind != JMIR_OPERAND_REGISTER) {
			if (srcOperand->m_Kind == JMIR_OPERAND_STACK_OBJECT || srcOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
				jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, srcOperand));
				srcOperand = tmpReg;
			} else {
				jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, regType);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, srcOperand));
				srcOperand = tmpReg;
			}
		}

		jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, regType, srcOperand->u.m_RegID, JMIR_MEMORY_REG_NONE, 1, 0);
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

	if (jx_ir_typeIsFloatingPoint(ptrType->m_BaseType)) {
		JX_NOT_IMPLEMENTED();
	} else {
		jx_mir_type_kind regType = jmirgen_convertType(ptrType->m_BaseType);

		jx_mir_operand_t* memRef = NULL;
		if (dstOperand->m_Kind == JMIR_OPERAND_REGISTER) {
			memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, regType, dstOperand->u.m_RegID, JMIR_MEMORY_REG_NONE, 1, 0);
		} else if (dstOperand->m_Kind == JMIR_OPERAND_STACK_OBJECT) {
			memRef = dstOperand;
		} else if (dstOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
			jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, dstOperand));
			memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, regType, tmpReg->u.m_RegID, JMIR_MEMORY_REG_NONE, 1, 0);
		} else {
			JX_CHECK(false, "Unhandle store destination operand.");
		}

		if (srcOperand->m_Kind != JMIR_OPERAND_REGISTER) {
			if (srcOperand->m_Kind == JMIR_OPERAND_STACK_OBJECT || srcOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
				jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, tmpReg, srcOperand));
				srcOperand = tmpReg;
			} else {
				jx_mir_operand_t* tmpReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, regType);
				jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmpReg, srcOperand));
				srcOperand = tmpReg;
			}
		}

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
	if (basePtrOperand->m_Kind == JMIR_OPERAND_STACK_OBJECT || basePtrOperand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
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
					jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_RegID, JMIR_MEMORY_REG_NONE, 1, (int32_t)displacement);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstReg, memRef));
				}
				dstRegType = ptrType->m_BaseType;
			} else if (dstRegType->m_Kind == JIR_TYPE_ARRAY) {
				jx_ir_type_array_t* arrType = jx_ir_typeToArray(dstRegType);
				const size_t itemSize = jx_ir_typeGetSize(arrType->m_BaseType);
				const int64_t displacement = indexOperand->u.m_ConstI64 * (int64_t)itemSize;
				JX_CHECK(displacement <= INT32_MAX, "Displacement too large");
				if (displacement != 0) {
					jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_RegID, JMIR_MEMORY_REG_NONE, 1, (int32_t)displacement);
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
					jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_RegID, JMIR_MEMORY_REG_NONE, 1, (int32_t)memberOffset);
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
				jx_mir_operand_t* memRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, dstReg->u.m_RegID, indexOperand->u.m_RegID, itemSize, 0);
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

#if 0
	// At this point the phi instruction is incomplete. It will be filled once all 
	// the function basic blocks are processed.
	const uint32_t numOperands = (uint32_t)jx_array_sizeu(phiInstr->super.m_OperandArr);
	jx_mir_instruction_t* mirPhiInstr = jx_mir_phi(ctx->m_MIRCtx, dstReg, numOperands / 2);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, mirPhiInstr);
#endif

	return dstReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_call(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	const uint32_t numOperands = (uint32_t)jx_array_sizeu(irInstr->super.m_OperandArr);
	jx_ir_value_t* funcPtrVal = irInstr->super.m_OperandArr[0]->m_Value;

	// TODO: If funcPtrVal->m_Name is a build-in/intrinsic function, handle it here before generating
	// any code for the actual call. E.g. replace memset/memcpy with movs, etc.

	jx_ir_type_pointer_t* funcPtrType = jx_ir_typeToPointer(funcPtrVal->m_Type);
	JX_CHECK(funcPtrType, "Expected pointer to function");
	jx_ir_type_function_t* funcType = jx_ir_typeToFunction(funcPtrType->m_BaseType);
	JX_CHECK(funcType, "Expected function type");

	// Write the N first arguments (N <= 4) of the current function to their shadow space.
	const uint32_t curFuncNumArgs = ctx->m_Func->m_NumArgs;
	const uint32_t numFuncArgsToStore = jx_min_u32(curFuncNumArgs, 4);
	for (uint32_t iArg = 0; iArg < numFuncArgsToStore; ++iArg) {
		jx_mir_operand_t* argReg = jx_mir_funcGetArgument(ctx->m_MIRCtx, ctx->m_Func, iArg);
		jx_mir_operand_t* argShadowSpaceSlotRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, argReg->m_Type, JMIR_HWREG_BP, JMIR_MEMORY_REG_NONE, 1, 16 + iArg * 8);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, argShadowSpaceSlotRef, argReg));
	}

	// Make sure the stack has enough space for N-argument call
	jx_mir_funcAllocStackForCall(ctx->m_MIRCtx, ctx->m_Func, numOperands - 1);

	for (uint32_t iOperand = 1; iOperand < numOperands; ++iOperand) {
		jx_ir_value_t* argVal = irInstr->super.m_OperandArr[iOperand]->m_Value;
		jx_ir_type_t* argType = argVal->m_Type;
		jx_mir_operand_t* srcArgOp = jmirgen_getOperand(ctx, argVal);

		// If the arg operand is a rargN register, use the stack variable instead to load the value.
		// This is needed because the rargN might have already be overwritten by the previous operand
		// e.g. when calling a function with the same operands as the current function but with a different
		// order (f(x, y, z) { return g(y, x, z); })
		if (srcArgOp->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regIsArg(srcArgOp->u.m_RegID)) {
			const uint32_t argID = jx_mir_regGetArgID(srcArgOp->u.m_RegID);
			JX_CHECK(argID != UINT32_MAX, "Expected argument ID");
			srcArgOp = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, srcArgOp->m_Type, JMIR_HWREG_BP, JMIR_MEMORY_REG_NONE, 1, 16 + argID * 8);
		}

		const uint32_t argID = iOperand - 1;
		if (argID < JX_COUNTOF(kMIRFuncArgIReg)) {
			if (jx_ir_typeIsFloatingPoint(argType)) {
				JX_NOT_IMPLEMENTED();
			} else {
				if (srcArgOp->m_Kind == JMIR_OPERAND_STACK_OBJECT || srcArgOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					jx_mir_operand_t* dstArgReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, kMIRFuncArgIReg[argID]);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_lea(ctx->m_MIRCtx, dstArgReg, srcArgOp));
				} else {
					jx_mir_operand_t* dstArgReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, jmirgen_convertType(argType), kMIRFuncArgIReg[argID]);
					jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, dstArgReg, srcArgOp));
				}
			}
		} else {
			// Push on stack...
			JX_NOT_IMPLEMENTED();
		}
	}

	jx_mir_operand_t* funcOp = jmirgen_getOperand(ctx, funcPtrVal);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_call(ctx->m_MIRCtx, funcOp));

	// Get result from rret register into a virtual register
	jx_mir_operand_t* resReg = NULL;
	if (funcType->m_RetType->m_Kind != JIR_TYPE_VOID) {
		jx_mir_type_kind retType = jmirgen_convertType(funcType->m_RetType);
		resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, retType);
		jx_mir_operand_t* retReg = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, retType, JMIR_HWREG_RET);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, resReg, retReg));
	}

	// Restore the N fist arguments (N <= 4) of the current function from their shadow space.
	for (uint32_t iArg = 0; iArg < numFuncArgsToStore; ++iArg) {
		jx_mir_operand_t* argReg = jx_mir_funcGetArgument(ctx->m_MIRCtx, ctx->m_Func, iArg);
		jx_mir_operand_t* argShadowSpaceSlotRef = jx_mir_opMemoryRef(ctx->m_MIRCtx, ctx->m_Func, argReg->m_Type, JMIR_HWREG_BP, JMIR_MEMORY_REG_NONE, 1, 16 + iArg * 8);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, argReg, argShadowSpaceSlotRef));
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
		jx_mir_operand_t* cl = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, JMIR_HWREG_C);
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
		jx_mir_operand_t* cl = jx_mir_opHWReg(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_I8, JMIR_HWREG_C);
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
	JX_CHECK(irInstr->m_OpCode == JIR_OP_TRUNC, "Expected shr instruction");

	jx_ir_type_t* instrType = jx_ir_instrToValue(irInstr)->m_Type;
	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, irInstr->super.m_OperandArr[0]->m_Value);

	if (operand->m_Kind != JMIR_OPERAND_REGISTER) {
		if (operand->m_Kind == JMIR_OPERAND_STACK_OBJECT || operand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
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
	JX_CHECK(irInstr->m_OpCode == JIR_OP_ZEXT, "Expected shr instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsInteger(instrVal->m_Type), "Expected integer target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsIntegral(operandVal->m_Type), "Expected integer operand!");
	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	jx_mir_type_kind operandType = jmirgen_convertType(operandVal->m_Type);
	JX_CHECK(targetType > operandType, "Expected target type to be larger than operand type!");

	jx_mir_operand_t* resReg = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, targetType);
	jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_movzx(ctx->m_MIRCtx, resReg, operand));

	return resReg;
}

static jx_mir_operand_t* jmirgen_instrBuild_sext(jx_mirgen_context_t* ctx, jx_ir_instruction_t* irInstr)
{
	JX_CHECK(irInstr->m_OpCode == JIR_OP_SEXT, "Expected shr instruction");

	jx_ir_value_t* instrVal = jx_ir_instrToValue(irInstr);
	JX_CHECK(jx_ir_typeIsInteger(instrVal->m_Type), "Expected integer target type!");

	jx_ir_value_t* operandVal = irInstr->super.m_OperandArr[0]->m_Value;
	JX_CHECK(jx_ir_typeIsIntegral(operandVal->m_Type), "Expected integer operand!");
	jx_mir_operand_t* operand = jmirgen_getOperand(ctx, operandVal);

	jx_mir_type_kind targetType = jmirgen_convertType(instrVal->m_Type);
	jx_mir_type_kind operandType = jmirgen_convertType(operandVal->m_Type);
	JX_CHECK(targetType > operandType, "Expected target type to be larger than operand type!");

	if (operand->m_Kind != JMIR_OPERAND_REGISTER && operand->m_Kind != JMIR_OPERAND_MEMORY_REF && operand->m_Kind != JMIR_OPERAND_STACK_OBJECT) {
		jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx->m_MIRCtx, ctx->m_Func, operand->m_Type);
		jx_mir_bbAppendInstr(ctx->m_MIRCtx, ctx->m_BasicBlock, jx_mir_mov(ctx->m_MIRCtx, tmp, operand));
		operand = tmp;
	}

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
		if (operand->m_Kind == JMIR_OPERAND_STACK_OBJECT || operand->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
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
			JX_NOT_IMPLEMENTED();
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
				operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, "memset");
			} else if (!jx_strncmp(funcName, "jir.memcpy.", 11)) {
				operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, "memcpy");
			} else {
				JX_CHECK(false, "Unknown intrinsic function");
			}
		} else {
			operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, funcName);
		}
	} else if (val->m_Kind == JIR_VALUE_GLOBAL_VARIABLE) {
		operand = jx_mir_opExternalSymbol(ctx->m_MIRCtx, ctx->m_Func, JMIR_TYPE_PTR, val->m_Name);
	} else {
		// TODO: Other IR values
		JX_NOT_IMPLEMENTED();
	}

	JX_CHECK(operand, "Failed to find operand for value!");

	return operand;
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

			jx_mir_instruction_t* movInstr = jx_mir_mov(ctx->m_MIRCtx, dstReg, srcReg);

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
