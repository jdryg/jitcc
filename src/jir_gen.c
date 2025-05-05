// IR generator: Converts jcc AST into IR.
// TODO: 
// - float constants (JCC_NODE_NUMBER)
// - bitfields
#include "jir_gen.h"
#include "jir.h"
#include "jcc.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/hashmap.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>

// Hashmap items
typedef struct jccObj_to_irVal_item_t
{
	jx_cc_object_t* m_ccObj;
	jx_ir_value_t* m_irVal;
} jccObj_to_irVal_item_t;

typedef struct jccLabel_to_irBB_item_t
{
	jx_cc_label_t m_Label;
	jx_ir_basic_block_t* m_BasicBlock;
} jccLabel_to_irBB_item_t;

typedef struct jx_irgen_context_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_context_t* m_IRCtx;
	jx_ir_module_t* m_Module;
	jx_ir_function_t* m_Func;
	jx_ir_basic_block_t* m_BasicBlock;
	jx_hashmap_t* m_LocalVarMap;
	jx_hashmap_t* m_LabeledBBMap;
} jx_irgen_context_t;

typedef jx_ir_instruction_t* (*irUnaryOpFunc)(jx_ir_context_t* ctx, jx_ir_value_t* op);
typedef jx_ir_instruction_t* (*irBinaryOpFunc)(jx_ir_context_t* ctx, jx_ir_value_t* op1, jx_ir_value_t* op2);

static irUnaryOpFunc kIRUnaryOps[] = {
	[JCC_NODE_EXPR_NEG] = jx_ir_instrNeg,
	[JCC_NODE_EXPR_BITWISE_NOT] = jx_ir_instrNot,
};

static irBinaryOpFunc kIRBinaryOps[] = {
	[JCC_NODE_EXPR_ADD] = jx_ir_instrAdd,
	[JCC_NODE_EXPR_SUB] = jx_ir_instrSub,
	[JCC_NODE_EXPR_MUL] = jx_ir_instrMul,
	[JCC_NODE_EXPR_DIV] = jx_ir_instrDiv,
	[JCC_NODE_EXPR_MOD] = jx_ir_instrRem,
	[JCC_NODE_EXPR_BITWISE_AND] = jx_ir_instrAnd,
	[JCC_NODE_EXPR_BITWISE_OR] = jx_ir_instrOr,
	[JCC_NODE_EXPR_BITWISE_XOR] = jx_ir_instrXor,
	[JCC_NODE_EXPR_LSHIFT] = jx_ir_instrShl,
	[JCC_NODE_EXPR_RSHIFT] = jx_ir_instrShr,
	[JCC_NODE_EXPR_EQUAL] = jx_ir_instrSetEQ,
	[JCC_NODE_EXPR_NOT_EQUAL] = jx_ir_instrSetNE,
	[JCC_NODE_EXPR_LESS_THAN] = jx_ir_instrSetLT,
	[JCC_NODE_EXPR_LESS_EQUAL] = jx_ir_instrSetLE,
};

static jx_ir_basic_block_t* jirgenSwitchBasicBlock(jx_irgen_context_t* ctx, jx_ir_basic_block_t* newBB);
static jx_ir_basic_block_t* jirgenGetOrCreateLabeledBB(jx_irgen_context_t* ctx, jx_cc_label_t lbl);
static jx_ir_constant_t* jirgenGlobalVarInitializer(jx_irgen_context_t* ctx, jx_cc_type_t* type, const uint8_t* data, uint64_t offset, const jx_cc_relocation_t** relocations);
static bool jirgenGenStatement(jx_irgen_context_t* ctx, jx_cc_ast_stmt_t* stmt);
static jx_ir_value_t* jirgenGenExpression(jx_irgen_context_t* ctx, jx_cc_ast_expr_t* expr);
static jx_ir_value_t* jirgenGenAddress(jx_irgen_context_t* ctx, jx_cc_ast_expr_t* expr);
static jx_ir_value_t* jirgenGenLoad(jx_irgen_context_t* ctx, jx_ir_value_t* ptr, jx_cc_type_t* ccType);
static void jirgenGenStore(jx_irgen_context_t* ctx, jx_ir_value_t* ptr, jx_ir_value_t* val);
static void jirgenGemMemZero(jx_irgen_context_t* ctx, jx_ir_value_t* addr);
static void jirgenGenMemCopy(jx_irgen_context_t* ctx, jx_ir_value_t* dstVal, jx_ir_value_t* srcVal);
static jx_ir_value_t* jirgenConvertToBool(jx_irgen_context_t* ctx, jx_ir_value_t* val);
static jx_ir_type_t* jirgenIntegerPromotion(jx_irgen_context_t* ctx, jx_ir_type_t* type);
static jx_ir_type_t* jirgenUsualArithmeticConversions(jx_irgen_context_t* ctx, jx_ir_type_t* lhsType, jx_ir_type_t* rhsType);
static jx_ir_value_t* jirgenConvertType(jx_irgen_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* type);

static jx_ir_type_t* jccTypeToIRType(jx_irgen_context_t* ctx, jx_cc_type_t* ccType);
static jx_ir_type_t* jccFuncArgGetType(jx_irgen_context_t* ctx, jx_cc_type_t* ccType);
static jx_ir_type_t* jccFuncRetGetType(jx_irgen_context_t* ctx, jx_cc_type_t* ccType, bool* addAsArg);
static const char* jccTypeGetStructName(const jx_cc_type_t* type);
static uint64_t jccObjHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jccObjCompareCallback(const void* a, const void* b, void* udata);
static uint64_t jccLabelHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jccLabelCompareCallback(const void* a, const void* b, void* udata);

jx_irgen_context_t* jx_irgen_createContext(jx_ir_context_t* irCtx, jx_allocator_i* allocator)
{
	jx_irgen_context_t* ctx = (jx_irgen_context_t*)JX_ALLOC(allocator, sizeof(jx_irgen_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_irgen_context_t));
	ctx->m_Allocator = allocator;
	ctx->m_IRCtx = irCtx;

	return ctx;
}

void jx_irgen_destroyContext(jx_irgen_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	JX_FREE(allocator, ctx);
}

bool jx_irgen_moduleGen(jx_irgen_context_t* ctx, const char* moduleName, jx_cc_translation_unit_t* tu)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;
	jx_ir_module_t* mod = jx_ir_moduleBegin(irctx, moduleName);
	if (!mod) {
		return false;
	}

	ctx->m_Module = mod;

	// Declarations
	{
		jx_cc_object_t* global = tu->m_Globals;
		while (global) {
			const bool isLive = (global->m_Flags & JCC_OBJECT_FLAGS_IS_LIVE_Msk) != 0;
//			if (isLive) 
			{
				const char* name = global->m_Name;
				const bool isStatic = (global->m_Flags & JCC_OBJECT_FLAGS_IS_STATIC_Msk) != 0;
				jx_ir_type_t* type = jccTypeToIRType(ctx, global->m_Type);

				if (!jx_ir_moduleDeclareGlobalVal(irctx, mod, name, type, isStatic ? JIR_LINKAGE_INTERNAL : JIR_LINKAGE_EXTERNAL)) {
					goto error;
				}
			}

			global = global->m_Next;
		}
	}

	// Definitions
	{
		jx_cc_object_t* global = tu->m_Globals;
		while (global) {
			const bool isFunction = (global->m_Flags & JCC_OBJECT_FLAGS_IS_FUNCTION_Msk) != 0;
			if (isFunction) {
				const bool funcIsDefinition = (global->m_Flags & JCC_OBJECT_FLAGS_IS_DEFINITION_Msk) != 0;
				const bool funcIsLive = (global->m_Flags & JCC_OBJECT_FLAGS_IS_LIVE_Msk) != 0;
				if (funcIsLive && funcIsDefinition) {
					// Generate IR for the function body.
					jx_ir_function_t* func = jx_ir_moduleGetFunc(irctx, mod, global->m_Name);
					if (!func) {
						JX_CHECK(false, "Function not declared!");
						goto error;
					}

					if (jx_ir_funcBegin(irctx, func)) {
						ctx->m_Func = func;
						ctx->m_LocalVarMap = jx_hashmapCreate(ctx->m_Allocator, sizeof(jccObj_to_irVal_item_t), 64, 0, 0, jccObjHashCallback, jccObjCompareCallback, NULL, NULL);
						ctx->m_LabeledBBMap = jx_hashmapCreate(ctx->m_Allocator, sizeof(jccLabel_to_irBB_item_t), 64, 0, 0, jccLabelHashCallback, jccLabelCompareCallback, NULL, NULL);

						jx_ir_basic_block_t* bbEntry = jx_ir_bbAlloc(irctx, "entry");
						ctx->m_BasicBlock = bbEntry;

						// Generate allocas for each function argument.
						{
							uint32_t argID = 0;
							jx_cc_object_t* ccArg = global->m_FuncParams;
							while (ccArg) {
								jx_ir_argument_t* arg = jx_ir_funcGetArgument(irctx, func, argID);
								jx_ir_valueSetName(irctx, jx_ir_argToValue(arg), ccArg->m_Name);

								jx_ir_type_t* argType = jccFuncArgGetType(ctx, ccArg->m_Type);
								jx_ir_instruction_t* argAlloca = jx_ir_instrAlloca(irctx, argType, NULL);
								JX_CHECK(argAlloca, "Failed to allocate alloca instruction.");

								char argName[256];
								jx_snprintf(argName, JX_COUNTOF(argName), "%s.addr", ccArg->m_Name);
								jx_ir_valueSetName(irctx, jx_ir_instrToValue(argAlloca), argName);

								jx_ir_bbAppendInstr(irctx, bbEntry, argAlloca);

								jx_hashmapSet(ctx->m_LocalVarMap, &(jccObj_to_irVal_item_t) {.m_ccObj = ccArg, .m_irVal = jx_ir_instrToValue(argAlloca) });

								ccArg = ccArg->m_Next;
								++argID;
							}
						}

						// Generate allocas for each local variable.
						{
							jx_cc_object_t* ccLocal = global->m_FuncLocals;
							while (ccLocal) {
								jx_cc_type_t* ccLocalType = ccLocal->m_Type;
								jx_ir_instruction_t* localAlloca = jx_ir_instrAlloca(irctx, jccTypeToIRType(ctx, ccLocal->m_Type), NULL);
								jx_ir_valueSetName(irctx, jx_ir_instrToValue(localAlloca), ccLocal->m_Name);
								jx_ir_bbAppendInstr(irctx, bbEntry, localAlloca);

								jx_hashmapSet(ctx->m_LocalVarMap, &(jccObj_to_irVal_item_t) {.m_ccObj = ccLocal, .m_irVal = jx_ir_instrToValue(localAlloca) });

								ccLocal = ccLocal->m_Next;
							}
						}

						// Copy function arguments to allocated space
						{
							uint32_t argID = 0;
							jx_cc_object_t* ccArg = global->m_FuncParams;
							while (ccArg) {
								jx_ir_argument_t* arg = jx_ir_funcGetArgument(irctx, func, argID);
								jccObj_to_irVal_item_t* hashItem = (jccObj_to_irVal_item_t*)jx_hashmapGet(ctx->m_LocalVarMap, &(jccObj_to_irVal_item_t){.m_ccObj = ccArg });
								JX_CHECK(hashItem, "Function argument not found in hashmap.");
								jx_ir_bbAppendInstr(irctx, bbEntry, jx_ir_instrStore(irctx, hashItem->m_irVal, jx_ir_argToValue(arg)));
								
								ccArg = ccArg->m_Next;
								++argID;
							}
						}

						// Make sure the entry block is appended to the function before generating the 
						// body code because instructions might need to place allocas to it before it's 
						// added.
						jx_ir_basic_block_t* bbNext = jx_ir_bbAlloc(irctx, NULL);
						jx_ir_bbAppendInstr(irctx, bbEntry, jx_ir_instrBranch(irctx, bbNext));

						jirgenSwitchBasicBlock(ctx, bbNext);
						if (!jirgenGenStatement(ctx, global->m_FuncBody)) {
							return false;
						}

						if (ctx->m_BasicBlock && (ctx->m_BasicBlock->m_InstrListHead || ctx->m_BasicBlock->super.m_Name)) {
							jx_ir_instruction_t* lastInstr = jx_ir_bbGetLastInstr(irctx, ctx->m_BasicBlock);
							if (!lastInstr || !jx_ir_opcodeIsTerminator(lastInstr->m_OpCode)) {
								// Make sure the function does not return a value, otherwise we cannot automatically
								// append a ret instruction.
								jx_ir_type_function_t* typeFunc = jx_ir_funcGetType(irctx, func);
								JX_CHECK(typeFunc, "Expected a function type!");
								if (typeFunc->m_RetType->m_Kind == JIR_TYPE_VOID) {
									jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrRet(irctx, NULL));
								} else {
									// TODO: Warning: Not all control paths return a value!
									jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrRet(irctx, jx_ir_constToValue(jx_ir_constGetZero(irctx, typeFunc->m_RetType))));
								}
							}

							jx_ir_funcAppendBasicBlock(irctx, func, ctx->m_BasicBlock);
						} else {
							jx_ir_bbFree(irctx, ctx->m_BasicBlock);
						}
						ctx->m_BasicBlock = NULL;

						jx_ir_funcEnd(irctx, func);
						ctx->m_Func = NULL;

						jx_hashmapDestroy(ctx->m_LabeledBBMap);
						ctx->m_LabeledBBMap = NULL;
							
						jx_hashmapDestroy(ctx->m_LocalVarMap);
						ctx->m_LocalVarMap = NULL;
					} else {
						goto error;
					}
				}
			} else {
				// Global variable
				jx_ir_global_variable_t* gv = jx_ir_moduleGetGlobalVar(irctx, mod, global->m_Name);
				if (gv) {
					uint8_t* initData = (uint8_t*)JX_ALLOC(ctx->m_Allocator, global->m_Type->m_Size);
					if (!initData) {
						goto error;
					}

					jx_memset(initData, 0, global->m_Type->m_Size);
					if (global->m_GlobalInitData) {
						jx_memcpy(initData, global->m_GlobalInitData, global->m_Type->m_Size);
					}

					const jx_cc_relocation_t* gvReloc = global->m_GlobalRelocations;
					jx_ir_constant_t* gvInitializer = jirgenGlobalVarInitializer(ctx, global->m_Type, initData, 0, &gvReloc);

					JX_FREE(ctx->m_Allocator, initData);

					const bool isConst = false; // TODO: 
					jx_ir_globalVarDefine(irctx, gv, isConst, gvInitializer);
				} else {
					goto error;
				}
			}

			global = global->m_Next;
		}
	}
	
	jx_ir_moduleEnd(irctx, mod);
	ctx->m_Module = NULL;

	return true;

error:
	jx_ir_moduleEnd(irctx, mod);
	ctx->m_Module = NULL;

	return false;
}

static jx_ir_basic_block_t* jirgenSwitchBasicBlock(jx_irgen_context_t* ctx, jx_ir_basic_block_t* newBB)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;
	jx_ir_basic_block_t* curBB = ctx->m_BasicBlock;
	JX_CHECK(!curBB->m_Next && !curBB->m_Prev && !curBB->m_ParentFunc, "Current basic block is already part of a function.");
	JX_CHECK(!newBB || (!newBB->m_Next && !newBB->m_Prev && !newBB->m_ParentFunc), "New basic block is already part of a function.");

	jx_ir_funcAppendBasicBlock(irctx, ctx->m_Func, curBB);

	newBB = newBB != NULL
		? newBB
		: jx_ir_bbAlloc(irctx, NULL)
		;
	ctx->m_BasicBlock = newBB;

	return newBB;
}

static jx_ir_basic_block_t* jirgenGetOrCreateLabeledBB(jx_irgen_context_t* ctx, jx_cc_label_t lbl)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jccLabel_to_irBB_item_t* key = &(jccLabel_to_irBB_item_t){
		.m_Label = lbl
	};
	jccLabel_to_irBB_item_t* existinItem = (jccLabel_to_irBB_item_t*)jx_hashmapGet(ctx->m_LabeledBBMap, key);
	if (existinItem) {
		return existinItem->m_BasicBlock;
	}

	char bbName[256];
	jx_snprintf(bbName, JX_COUNTOF(bbName), "BB%u", lbl.m_ID);
	jx_ir_basic_block_t* newBasicBlock = jx_ir_bbAlloc(irctx, bbName);

	jccLabel_to_irBB_item_t* hashItem = &(jccLabel_to_irBB_item_t){
		.m_Label = lbl,
		.m_BasicBlock = newBasicBlock
	};
	jx_hashmapSet(ctx->m_LabeledBBMap, hashItem);

	return newBasicBlock;
}

static jx_ir_constant_t* jirgenGlobalVarInitializer(jx_irgen_context_t* ctx, jx_cc_type_t* type, const uint8_t* data, uint64_t offset, const jx_cc_relocation_t** relocations)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	const uint8_t* ptr = &data[offset];

	jx_ir_constant_t* c = NULL;
	switch (type->m_Kind) {
	case JCC_TYPE_VOID: {
		JX_CHECK(false, "Constant void?");
	} break;
	case JCC_TYPE_BOOL: {
		c = jx_ir_constGetBool(irctx, *(const bool*)ptr);
	} break;
	case JCC_TYPE_CHAR: {
		c = type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk
			? jx_ir_constGetU8(irctx, *(const uint8_t*)ptr)
			: jx_ir_constGetI8(irctx, *(const int8_t*)ptr)
			;
	} break;
	case JCC_TYPE_SHORT: {
		c = type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk
			? jx_ir_constGetU16(irctx, *(const uint16_t*)ptr)
			: jx_ir_constGetI16(irctx, *(const int16_t*)ptr)
			;
	} break;
	case JCC_TYPE_INT: {
		c = type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk
			? jx_ir_constGetU32(irctx, *(const uint32_t*)ptr)
			: jx_ir_constGetI32(irctx, *(const int32_t*)ptr)
			;
	} break;
	case JCC_TYPE_LONG: {
		c = type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk
			? jx_ir_constGetU64(irctx, *(const uint64_t*)ptr)
			: jx_ir_constGetI64(irctx, *(const int64_t*)ptr)
			;
	} break;
	case JCC_TYPE_FLOAT: {
		c = jx_ir_constGetF32(irctx, *(const float*)ptr);
	} break;
	case JCC_TYPE_DOUBLE: {
		c = jx_ir_constGetF64(irctx, *(const double*)ptr);
	} break;
	case JCC_TYPE_ENUM: {
		c = jx_ir_constGetI32(irctx, *(const int32_t*)ptr);
	} break;
	case JCC_TYPE_PTR: {
		if (*relocations && (*relocations)->m_Offset == offset) {
			jx_ir_global_value_t* gv = jx_ir_moduleGetGlobalVal(irctx, ctx->m_Module, *(*relocations)->m_Label);
			if (!gv) {
				JX_CHECK(false, "Global value not found.");
				return NULL;
			}
			
			c = jx_ir_constPointerToGlobalVal(irctx, gv);

			*relocations = (*relocations)->m_Next;
		} else {
			c = jx_ir_constPointer(irctx, jccTypeToIRType(ctx, type), *(void**)ptr);
		}
	} break;
	case JCC_TYPE_FUNC: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JCC_TYPE_ARRAY: {
		JX_CHECK(type->m_ArrayLen > 0, "Invalid constant array length.");
		// TODO: Avoid allocations
		jx_ir_constant_t** arrElements = (jx_ir_constant_t**)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_constant_t*) * type->m_ArrayLen);
		if (!arrElements) {
			return NULL;
		}

		const uint32_t stride = type->m_BaseType->m_Size;
		for (uint32_t iElem = 0; iElem < (uint32_t)type->m_ArrayLen; ++iElem) {
			arrElements[iElem] = jirgenGlobalVarInitializer(ctx, type->m_BaseType, data, offset + iElem * stride, relocations);
		}
		c = jx_ir_constArray(irctx, jccTypeToIRType(ctx, type), type->m_ArrayLen, arrElements);

		JX_FREE(ctx->m_Allocator, arrElements);
	} break;
	case JCC_TYPE_STRUCT: {
		uint32_t numMembers = 0;
		jx_cc_struct_member_t* ccMember = type->m_StructMembers;
		while (ccMember) {
			numMembers++;
			ccMember = ccMember->m_Next;
		}

		jx_ir_constant_t** members = (jx_ir_constant_t**)JX_ALLOC(ctx->m_Allocator, sizeof(jx_ir_constant_t*) * numMembers);
		if (!members) {
			return NULL;
		}

		jx_ir_constant_t** nextMember = members;
		ccMember = type->m_StructMembers;
		while (ccMember) {
			*nextMember++ = jirgenGlobalVarInitializer(ctx, ccMember->m_Type, data, offset + ccMember->m_Offset, relocations);
			ccMember = ccMember->m_Next;
		}
		c = jx_ir_constStruct(irctx, jccTypeToIRType(ctx, type), numMembers, members);

		JX_FREE(ctx->m_Allocator, members);
	} break;
	case JCC_TYPE_UNION: {
		// Unions are modeled as structs with maximum 2 members. The first member is the first union member 
		// with the largest alignment and the second member is an array of i8 big enough to cover the 
		// union's memory footprint.
		jx_ir_type_t* irType = jccTypeToIRType(ctx, type);
		jx_ir_type_struct_t* irUnionType = jx_ir_typeToStruct(irType);

		jx_ir_constant_t* members[2] = { NULL, NULL };

		// First member always present and at relative offset 0
		uint32_t secondMemberOffset = 0;
		{
			// Check which jcc union member corresponds to the first ir struct member.
			uint32_t maxMemberSize = 0;
			uint32_t maxMemberAlignment = 0;
			jx_cc_struct_member_t* ccMember = type->m_StructMembers;
			jx_cc_struct_member_t* firstMember = NULL;
			while (ccMember) {
				jx_ir_type_t* memberType = jccTypeToIRType(ctx, ccMember->m_Type);
				const uint32_t memberSize = (uint32_t)jx_ir_typeGetSize(memberType);
				const uint32_t memberAlignment = (uint32_t)jx_ir_typeGetAlignment(memberType);

				if (memberSize > maxMemberSize) {
					maxMemberSize = memberSize;
				}
				if (memberAlignment > maxMemberAlignment) {
					maxMemberAlignment = memberAlignment;
					firstMember = ccMember;
				}

				ccMember = ccMember->m_Next;
			}
			JX_CHECK(firstMember, "Union expected to have at least 1 member.");

			members[0] = jirgenGlobalVarInitializer(ctx, firstMember->m_Type, data, offset, relocations);
			secondMemberOffset += (uint32_t)jx_ir_typeGetSize(jccTypeToIRType(ctx, firstMember->m_Type));
		}

		if (irUnionType->m_NumMembers != 1) {
			JX_NOT_IMPLEMENTED();
		}

		c = jx_ir_constStruct(irctx, jccTypeToIRType(ctx, type), irUnionType->m_NumMembers, members);
	} break;
	default: {
		JX_CHECK(false, "Unknown kind of jcc type");
	} break;
	}

	return c;
}

static bool jirgenGenStatement(jx_irgen_context_t* ctx, jx_cc_ast_stmt_t* stmt)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	switch (stmt->super.m_Kind) {
	case JCC_NODE_STMT_RETURN: {
		jx_cc_ast_stmt_expr_t* retNode = (jx_cc_ast_stmt_expr_t*)stmt;

		if (!retNode->m_Expr) {
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrRet(irctx, NULL));
		} else {
			jx_ir_value_t* retVal = jirgenGenExpression(ctx, retNode->m_Expr);

			jx_cc_type_t* exprType = retNode->m_Expr->m_Type;

			if (exprType->m_Kind == JCC_TYPE_STRUCT || exprType->m_Kind == JCC_TYPE_UNION) {
				const bool hasBeenConvertedToPtr = false
					|| exprType->m_Size > 8
					|| !jx_isPow2_u32(exprType->m_Size)
					;
				if (hasBeenConvertedToPtr) {
					// Copy retVal to hidden 1st function argument and return that pointer.
					jx_ir_argument_t* retBufArg = jx_ir_funcGetArgument(irctx, ctx->m_Func, 0);
					jx_ir_value_t* retBufVal = jx_ir_argToValue(retBufArg);

					jirgenGenMemCopy(ctx, retBufVal, retVal);

					retVal = NULL; // Function return type has been converted to void.
				} else {
					JX_CHECK(jx_ir_typeToPointer(retVal->m_Type), "Return type is a struct. Expected pointer value!");

					jx_ir_type_function_t* funcType = jx_ir_funcGetType(irctx, ctx->m_Func);
					jx_ir_instruction_t* bitcastInstr = jx_ir_instrBitcast(irctx, retVal, jx_ir_typeGetPointer(irctx, funcType->m_RetType));
					jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, bitcastInstr);

					jx_ir_instruction_t* loadInstr = jx_ir_instrLoad(irctx, funcType->m_RetType, jx_ir_instrToValue(bitcastInstr));
					jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, loadInstr);
					retVal = jx_ir_instrToValue(loadInstr);
				}
			} else {
				jx_ir_type_function_t* funcType = jx_ir_funcGetType(irctx, ctx->m_Func);
				if (retVal->m_Type != funcType->m_RetType) {
					// TODO: Turn this into a function. The same code is used in JCC_NODE_EXPR_CAST
					jx_ir_type_t* srcType = retVal->m_Type;
					jx_ir_type_t* dstType = funcType->m_RetType;

					if (jx_ir_typeIsIntegral(srcType) && jx_ir_typeIsIntegral(dstType)) {
						retVal = jirgenConvertType(ctx, retVal, dstType);
					} else {
						const bool srcIsInteger = jx_ir_typeIsInteger(srcType);
						const bool dstIsInteger = jx_ir_typeIsInteger(dstType);
						const bool srcIsPointer = jx_ir_typeToPointer(srcType) != NULL;
						const bool dstIsPointer = jx_ir_typeToPointer(dstType) != NULL;
						const uint32_t srcSize = (uint32_t)jx_ir_typeGetSize(srcType);
						const uint32_t dstSize = (uint32_t)jx_ir_typeGetSize(dstType);

						jx_ir_instruction_t* castInstr = NULL;
						if (srcIsInteger && dstIsPointer) {
							castInstr = jx_ir_instrIntToPtr(irctx, retVal, dstType);
						} else if (srcIsPointer && dstIsInteger) {
							castInstr = jx_ir_instrPtrToInt(irctx, retVal, dstType);
						} else if (srcSize == dstSize) {
							castInstr = jx_ir_instrBitcast(irctx, retVal, dstType);
						} else {
							JX_NOT_IMPLEMENTED();
						}

						if (castInstr) {
							jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, castInstr);
							retVal = jx_ir_instrToValue(castInstr);
						}
					}
				}
			}

			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrRet(irctx, retVal));
		}

		jirgenSwitchBasicBlock(ctx, NULL);
	} break;
	case JCC_NODE_STMT_IF: {
		jx_cc_ast_stmt_if_t* ifNode = (jx_cc_ast_stmt_if_t*)stmt;

		jx_ir_basic_block_t* bbThen = jirgenGetOrCreateLabeledBB(ctx, ifNode->m_ThenLbl);
		jx_ir_basic_block_t* bbEnd = jirgenGetOrCreateLabeledBB(ctx, ifNode->m_EndLbl);
		jx_ir_basic_block_t* bbElse = ifNode->m_ElseStmt 
			? jirgenGetOrCreateLabeledBB(ctx, ifNode->m_ElseLbl) 
			: bbEnd
			;

		// Generate conditional expression and branch in current basic block.
		// NOTE: Always branch to bbElse on false. bbElse is equal to bbEnd if there 
		// is no else statement
		jx_ir_value_t* condVal = jirgenGenExpression(ctx, ifNode->m_CondExpr);
		condVal = jirgenConvertToBool(ctx, condVal);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, condVal, bbThen, bbElse));

		// Generate then basic block statement and branch to end
		jirgenSwitchBasicBlock(ctx, bbThen);
		if (!jirgenGenStatement(ctx, ifNode->m_ThenStmt)) {
			return false;
		}
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));

		if (ifNode->m_ElseStmt) {
			// Generate else statement and branch to end
			JX_CHECK(bbElse != bbEnd, "Expected else basic block to be different than end basic block!");
			jirgenSwitchBasicBlock(ctx, bbElse);
			jirgenGenStatement(ctx, ifNode->m_ElseStmt);
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		}

		// Switch to end block.
		jirgenSwitchBasicBlock(ctx, bbEnd);
	} break;
	case JCC_NODE_STMT_FOR: {
		jx_cc_ast_stmt_for_t* forNode = (jx_cc_ast_stmt_for_t*)stmt;

		jx_ir_basic_block_t* bbCond = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbBody = jirgenGetOrCreateLabeledBB(ctx, forNode->m_BodyLbl);
		jx_ir_basic_block_t* bbEnd = jirgenGetOrCreateLabeledBB(ctx, forNode->m_BreakLbl);
		jx_ir_basic_block_t* bbInc = jirgenGetOrCreateLabeledBB(ctx, forNode->m_ContinueLbl);

		if (forNode->m_InitStmt) {
			jirgenGenStatement(ctx, forNode->m_InitStmt);
		}
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbCond));
		jirgenSwitchBasicBlock(ctx, bbCond);

		if (forNode->m_CondExpr) {
			jx_ir_value_t* condVal = jirgenGenExpression(ctx, forNode->m_CondExpr);
			condVal = jirgenConvertToBool(ctx, condVal);
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, condVal, bbBody, bbEnd));
		} else {
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbBody));
		}
		jirgenSwitchBasicBlock(ctx, bbBody);

		jirgenGenStatement(ctx, forNode->m_BodyStmt);

		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbInc));
		jirgenSwitchBasicBlock(ctx, bbInc);

		if (forNode->m_IncExpr) {
			jirgenGenExpression(ctx, forNode->m_IncExpr);
		}
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbCond));
		jirgenSwitchBasicBlock(ctx, bbEnd);
	} break;
	case JCC_NODE_STMT_DO: {
		jx_cc_ast_stmt_do_t* doNode = (jx_cc_ast_stmt_do_t*)stmt;
		
		jx_ir_basic_block_t* bbBody = jirgenGetOrCreateLabeledBB(ctx, doNode->m_BodyLbl);
		jx_ir_basic_block_t* bbCond = jirgenGetOrCreateLabeledBB(ctx, doNode->m_ContinueLbl);
		jx_ir_basic_block_t* bbEnd = jirgenGetOrCreateLabeledBB(ctx, doNode->m_BreakLbl);

		jirgenSwitchBasicBlock(ctx, bbBody);

		jirgenGenStatement(ctx, doNode->m_BodyStmt);

		jirgenSwitchBasicBlock(ctx, bbCond);

		jx_ir_value_t* condVal = jirgenGenExpression(ctx, doNode->m_CondExpr);
		condVal = jirgenConvertToBool(ctx, condVal);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, condVal, bbBody, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbEnd);
	} break;
	case JCC_NODE_STMT_SWITCH: {
		jx_cc_ast_stmt_switch_t* switchNode = (jx_cc_ast_stmt_switch_t*)stmt;

		jx_ir_basic_block_t* bbEnd = jirgenGetOrCreateLabeledBB(ctx, switchNode->m_BreakLbl);
		
		jx_ir_value_t* condVal = jirgenGenExpression(ctx, switchNode->m_CondExpr);
		JX_CHECK(jx_ir_typeIsInteger(condVal->m_Type), "switch conditional expression expected to have an integer type");

		jx_cc_ast_stmt_case_t* caseNode = switchNode->m_CaseListHead;
		while (caseNode) {
			JX_CHECK(caseNode->m_Range[0] == caseNode->m_Range[1], "Case ranges are not supported!");

			jx_ir_basic_block_t* caseBB = jirgenGetOrCreateLabeledBB(ctx, caseNode->m_Lbl);
			jx_ir_basic_block_t* nextTestBB = jx_ir_bbAlloc(irctx, NULL);

			jx_ir_instruction_t* cmpInstr = jx_ir_instrSetEQ(irctx, condVal, jx_ir_constToValue(jx_ir_constGetInteger(irctx, condVal->m_Type->m_Kind, caseNode->m_Range[0])));
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, cmpInstr);

			jx_ir_instruction_t* branchInstr = jx_ir_instrBranchIf(irctx, jx_ir_instrToValue(cmpInstr), caseBB, nextTestBB);
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, branchInstr);
			jirgenSwitchBasicBlock(ctx, nextTestBB);

			caseNode = caseNode->m_NextCase;
		}

		if (switchNode->m_DefaultCase) {
			jx_ir_basic_block_t* bbDefault = jirgenGetOrCreateLabeledBB(ctx, switchNode->m_DefaultCase->m_Lbl);
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbDefault));
			jirgenSwitchBasicBlock(ctx, NULL);
		}

		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, NULL);

		jirgenGenStatement(ctx, switchNode->m_BodyStmt);
		
		jirgenSwitchBasicBlock(ctx, bbEnd);
	} break;
	case JCC_NODE_STMT_CASE: {
		jx_cc_ast_stmt_case_t* caseNode = (jx_cc_ast_stmt_case_t*)stmt;
		jx_ir_basic_block_t* bbCase = jirgenGetOrCreateLabeledBB(ctx, caseNode->m_Lbl);
		jirgenSwitchBasicBlock(ctx, bbCase);
		jirgenGenStatement(ctx, caseNode->m_BodyStmt);
	} break;
	case JCC_NODE_STMT_BLOCK: {
		jx_cc_ast_stmt_block_t* blockNode = (jx_cc_ast_stmt_block_t*)stmt;
		const uint32_t numChildren = blockNode->m_NumChildren;
		for (uint32_t iChild = 0; iChild < numChildren; ++iChild) {
			if (!jirgenGenStatement(ctx, blockNode->m_Children[iChild])) {
				return false;
			}
		}
	} break;
	case JCC_NODE_STMT_GOTO: {
		jx_cc_ast_stmt_goto_t* gotoNode = (jx_cc_ast_stmt_goto_t*)stmt;
		jx_ir_basic_block_t* targetBB = jirgenGetOrCreateLabeledBB(ctx, gotoNode->m_UniqueLabel);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, targetBB));
		jirgenSwitchBasicBlock(ctx, NULL);
	} break;
	case JCC_NODE_STMT_LABEL: {
		jx_cc_ast_stmt_label_t* labelNode = (jx_cc_ast_stmt_label_t*)stmt;
		jx_ir_basic_block_t* bbLabel = jirgenGetOrCreateLabeledBB(ctx, labelNode->m_UniqueLabel);
		jirgenSwitchBasicBlock(ctx, bbLabel);
		jirgenGenStatement(ctx, labelNode->m_Stmt);
	} break;
	case JCC_NODE_STMT_EXPR: {
		jx_cc_ast_stmt_expr_t* exprNode = (jx_cc_ast_stmt_expr_t*)stmt;
		if (!jirgenGenExpression(ctx, exprNode->m_Expr)) {
			return false;
		}
	} break;
	case JCC_NODE_STMT_ASM: {
		JX_NOT_IMPLEMENTED();
	} break;
	default: {
		JX_CHECK(false, "Unknown statement node");
	} break;
	}

	return true;
}

static jx_ir_value_t* jirgenGenExpression(jx_irgen_context_t* ctx, jx_cc_ast_expr_t* expr)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_value_t* val = NULL;

	switch (expr->super.m_Kind) {
	case JCC_NODE_EXPR_NULL : {
		JX_NOT_IMPLEMENTED();
	} break;
	case JCC_NODE_EXPR_ADD: 
	case JCC_NODE_EXPR_SUB:
	case JCC_NODE_EXPR_MUL:
	case JCC_NODE_EXPR_DIV:
	case JCC_NODE_EXPR_MOD:
	case JCC_NODE_EXPR_BITWISE_AND:
	case JCC_NODE_EXPR_BITWISE_OR:
	case JCC_NODE_EXPR_BITWISE_XOR:
	case JCC_NODE_EXPR_LSHIFT:
	case JCC_NODE_EXPR_RSHIFT:
	case JCC_NODE_EXPR_EQUAL:
	case JCC_NODE_EXPR_NOT_EQUAL:
	case JCC_NODE_EXPR_LESS_THAN:
	case JCC_NODE_EXPR_LESS_EQUAL: {
		jx_cc_ast_expr_binary_t* binExpr = (jx_cc_ast_expr_binary_t*)expr;
		jx_ir_value_t* lhs = jirgenGenExpression(ctx, binExpr->m_ExprLHS);
		jx_ir_value_t* rhs = jirgenGenExpression(ctx, binExpr->m_ExprRHS);

		if (expr->super.m_Kind != JCC_NODE_EXPR_LSHIFT && expr->super.m_Kind != JCC_NODE_EXPR_RSHIFT) {
			jx_ir_type_t* commonType = jirgenUsualArithmeticConversions(ctx, lhs->m_Type, rhs->m_Type);
			if (commonType) {
				if (commonType != lhs->m_Type) {
					lhs = jirgenConvertType(ctx, lhs, commonType);
				}
				if (commonType != rhs->m_Type) {
					rhs = jirgenConvertType(ctx, rhs, commonType);
				}
			}
		} else {
			jx_ir_type_t* promotedLhsType = jirgenIntegerPromotion(ctx, lhs->m_Type);
			if (promotedLhsType != lhs->m_Type) {
				lhs = jirgenConvertType(ctx, lhs, promotedLhsType);
			}
			jx_ir_type_t* promotedRhsType = jirgenIntegerPromotion(ctx, rhs->m_Type);
			if (promotedRhsType != rhs->m_Type) {
				rhs = jirgenConvertType(ctx, rhs, promotedRhsType);
			}
		}

		jx_ir_instruction_t* binInstr = kIRBinaryOps[expr->super.m_Kind](irctx, lhs, rhs);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, binInstr);
		val = jx_ir_instrToValue(binInstr);
	} break;
	case JCC_NODE_EXPR_ASSIGN: {
		jx_cc_ast_expr_binary_t* assignExpr = (jx_cc_ast_expr_binary_t*)expr;
		jx_ir_value_t* rhs = jirgenGenExpression(ctx, assignExpr->m_ExprRHS);

		jx_ir_type_t* rhsExprType = jccTypeToIRType(ctx, assignExpr->m_ExprRHS->m_Type);
		if (rhsExprType != rhs->m_Type && !((rhsExprType->m_Kind == JIR_TYPE_ARRAY || rhsExprType->m_Kind == JIR_TYPE_STRUCT) && rhs->m_Type->m_Kind == JIR_TYPE_POINTER)) {
			// This can happen when assigning (e.g.) a boolean (setcc instruction) to an integer (c-testsuite 00133.c).
			// If I don't cast rhs to the correct type, jirgenGenStore() asserts.
			if (jx_ir_typeIsIntegral(rhsExprType) && jx_ir_typeIsIntegral(rhs->m_Type)) {
				rhs = jirgenConvertType(ctx, rhs, rhsExprType);
			} else if (rhsExprType->m_Kind == JIR_TYPE_STRUCT && jx_ir_typeIsIntegral(rhs->m_Type) && jx_ir_typeGetSize(rhsExprType) == jx_ir_typeGetSize(rhs->m_Type)) {
				jx_ir_instruction_t* bitcastInstr = jx_ir_instrBitcast(irctx, rhs, rhsExprType);
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, bitcastInstr);
				rhs = jx_ir_instrToValue(bitcastInstr);
			} else {
				JX_NOT_IMPLEMENTED();
			}
		}

		jx_ir_value_t* lhsAddr = jirgenGenAddress(ctx, assignExpr->m_ExprLHS);

		if (assignExpr->m_ExprLHS->super.m_Kind == JCC_NODE_EXPR_MEMBER) {
			jx_cc_ast_expr_member_t* memberExpr = (jx_cc_ast_expr_member_t*)assignExpr->m_ExprLHS;
			if (memberExpr->m_Member->m_IsBitfield) {
				const uint32_t mask = (1u << memberExpr->m_Member->m_BitWidth) - 1;
				jx_ir_instruction_t* maskRHS = jx_ir_instrAnd(irctx, rhs, jx_ir_constToValue(jx_ir_constGetInteger(irctx, rhs->m_Type->m_Kind, mask)));
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, maskRHS);

				jx_ir_instruction_t* shiftRHS = jx_ir_instrShl(irctx, jx_ir_instrToValue(maskRHS), jx_ir_constToValue(jx_ir_constGetI8(irctx, memberExpr->m_Member->m_BitOffset)));
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, shiftRHS);

				jx_ir_value_t* curVal = jirgenGenLoad(ctx, lhsAddr, memberExpr->m_Member->m_Type);

				jx_ir_instruction_t* maskCurVal = jx_ir_instrAnd(irctx, curVal, jx_ir_constToValue(jx_ir_constGetInteger(irctx, curVal->m_Type->m_Kind, ~(mask << memberExpr->m_Member->m_BitOffset))));
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, maskCurVal);

				jx_ir_instruction_t* newVal = jx_ir_instrOr(irctx, jx_ir_instrToValue(maskCurVal), jx_ir_instrToValue(shiftRHS));
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, newVal);

				rhs = jx_ir_instrToValue(newVal);
			}
		}

		jirgenGenStore(ctx, lhsAddr, rhs);
		val = rhs;
	} break;
	case JCC_NODE_EXPR_CONDITIONAL: {
		jx_cc_ast_expr_cond_t* condExpr = (jx_cc_ast_expr_cond_t*)expr;

		jx_ir_basic_block_t* bbTrue = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbFalse = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbEnd = jx_ir_bbAlloc(irctx, NULL);

		jx_ir_type_t* valType = jccTypeToIRType(ctx, expr->m_Type);

#if 1
		// Use phi instructions
		jx_ir_value_t* condVal = jirgenGenExpression(ctx, condExpr->m_CondExpr);

		jx_ir_instruction_t* testInstr = jx_ir_instrSetCC(irctx, JIR_CC_NE, condVal, jx_ir_constToValue(jx_ir_constGetZero(irctx, condVal->m_Type)));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, testInstr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, jx_ir_instrToValue(testInstr), bbTrue, bbFalse));
		jirgenSwitchBasicBlock(ctx, bbTrue);

		jx_ir_value_t* trueVal = jirgenGenExpression(ctx, condExpr->m_ThenExpr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbFalse);

		jx_ir_value_t* falseVal = jirgenGenExpression(ctx, condExpr->m_ElseExpr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbEnd);

		jx_ir_instruction_t* phiInstr = jx_ir_instrPhi(irctx, valType);
		jx_ir_instrPhiAddValue(irctx, phiInstr, bbTrue, trueVal);
		jx_ir_instrPhiAddValue(irctx, phiInstr, bbFalse, falseVal);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, phiInstr); // NOTE: Append works because the basic block is new.

		val = jx_ir_instrToValue(phiInstr);
#else
		// Use memory/alloca for temporary
		jx_ir_instruction_t* valPtr = jx_ir_instrAlloca(irctx, valType, NULL);
		jx_ir_bbPrependInstr(irctx, ctx->m_Func->m_BasicBlockListHead, valPtr);

		jx_ir_value_t* condVal = jirgenGenExpression(ctx, condExpr->m_CondExpr);

		jx_ir_instruction_t* testInstr = jx_ir_instrSetCC(irctx, JIR_CC_NE, condVal, jx_ir_constToValue(jx_ir_constGetZero(irctx, condVal->m_Type)));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, testInstr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, jx_ir_instrToValue(testInstr), bbTrue, bbFalse));
		jirgenSwitchBasicBlock(ctx, bbTrue);

		jx_ir_value_t* trueVal = jirgenGenExpression(ctx, condExpr->m_ThenExpr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrStore(irctx, jx_ir_instrToValue(valPtr), trueVal));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbFalse);

		jx_ir_value_t* falseVal = jirgenGenExpression(ctx, condExpr->m_ElseExpr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrStore(irctx, jx_ir_instrToValue(valPtr), falseVal));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbEnd);

		jx_ir_instruction_t* loadInstr = jx_ir_instrLoad(irctx, valType, jx_ir_instrToValue(valPtr));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, loadInstr);

		val = jx_ir_instrToValue(loadInstr);
#endif
	} break;
	case JCC_NODE_EXPR_COMMA: {
		jx_cc_ast_expr_binary_t* binExpr = (jx_cc_ast_expr_binary_t*)expr;
		jirgenGenExpression(ctx, binExpr->m_ExprLHS);
		val = jirgenGenExpression(ctx, binExpr->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_MEMBER: {
		jx_cc_ast_expr_member_t* memberNode = (jx_cc_ast_expr_member_t*)expr;

		jx_ir_value_t* ptr = jirgenGenAddress(ctx, memberNode->m_Expr);

		jx_cc_type_t* memberExprType = memberNode->m_Expr->m_Type;
		if (memberExprType->m_Kind == JCC_TYPE_STRUCT) {
			jx_ir_value_t* indices[] = {
				jx_ir_constToValue(jx_ir_constGetI32(irctx, 0)),
				jx_ir_constToValue(jx_ir_constGetI32(irctx, memberNode->m_Member->m_GEPIndex))
			};
			jx_ir_instruction_t* gepInstr = jx_ir_instrGetElementPtr(irctx, ptr, 2, indices);
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, gepInstr);

			val = jirgenGenLoad(ctx, jx_ir_instrToValue(gepInstr), memberNode->m_Member->m_Type);

			if (memberNode->m_Member->m_IsBitfield) {
				if (memberNode->m_Member->m_BitOffset != 0) {
					jx_ir_instruction_t* shiftInstr = jx_ir_instrShr(irctx, val, jx_ir_constToValue(jx_ir_constGetI8(irctx, memberNode->m_Member->m_BitOffset)));
					jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, shiftInstr);
					val = jx_ir_instrToValue(shiftInstr);
				}
				
				const uint32_t valTypeSize = (uint32_t)jx_ir_typeGetSize(val->m_Type);
				if (memberNode->m_Member->m_BitWidth != valTypeSize * 8) {
					jx_ir_instruction_t* andInstr = jx_ir_instrAnd(irctx, val, jx_ir_constToValue(jx_ir_constGetInteger(irctx, val->m_Type->m_Kind, (1 << memberNode->m_Member->m_BitWidth) - 1)));
					jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, andInstr);
					val = jx_ir_instrToValue(andInstr);
				}
			}
		} else if (memberExprType->m_Kind == JCC_TYPE_UNION) {
			jx_cc_type_t* memberType = memberNode->m_Member->m_Type;

			jx_ir_instruction_t* bitcastInstr = jx_ir_instrBitcast(irctx, ptr, jx_ir_typeGetPointer(irctx, jccTypeToIRType(ctx, memberType)));
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, bitcastInstr);

			val = jirgenGenLoad(ctx, jx_ir_instrToValue(bitcastInstr), memberType);
		} else {
			JX_CHECK(false, "Unknown base type for member node!");
		}
	} break;
	case JCC_NODE_EXPR_ADDR: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)expr;
		val = jirgenGenAddress(ctx, unaryNode->m_Expr);
	} break;
	case JCC_NODE_EXPR_DEREF: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)expr;
		jx_ir_value_t* exprVal = jirgenGenExpression(ctx, unaryNode->m_Expr);
		val = jirgenGenLoad(ctx, exprVal, expr->m_Type);
	} break; 
	case JCC_NODE_EXPR_NOT: {
		// C11/6.5.3.3: The result of the logical negation operator ! is 0 if the value of its operand compares
		// unequal to 0, 1 if the value of its operand compares equal to 0. The result has type int. The expression 
		// !E is equivalent to (0 == E)
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)expr;
		jx_ir_value_t* exprVal = jirgenGenExpression(ctx, unaryNode->m_Expr);

		// Convert to boolean
		jx_ir_value_t* toBoolVal = jirgenConvertToBool(ctx, exprVal);

		// Invert boolean
		jx_ir_instruction_t* toBoolNotInstr = jx_ir_instrNot(irctx, toBoolVal);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, toBoolNotInstr);
		jx_ir_value_t* toBoolNotVal = jx_ir_instrToValue(toBoolNotInstr);

		// Zero-extend to i32
		jx_ir_instruction_t* zextInstr = jx_ir_instrZeroExt(irctx, toBoolNotVal, jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I32));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, zextInstr);
		val = jx_ir_instrToValue(zextInstr);
	} break;
	case JCC_NODE_EXPR_NEG:
	case JCC_NODE_EXPR_BITWISE_NOT: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)expr;
		jx_ir_value_t* exprVal = jirgenGenExpression(ctx, unaryNode->m_Expr);
		jx_ir_instruction_t* instr = kIRUnaryOps[expr->super.m_Kind](irctx, exprVal);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, instr);
		val = jx_ir_instrToValue(instr);
	} break;
	case JCC_NODE_EXPR_LOGICAL_AND: {
		jx_cc_ast_expr_binary_t* binNode = (jx_cc_ast_expr_binary_t*)expr;

		jx_ir_basic_block_t* bbRHSTest = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbTrue = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbFalse = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbEnd = jx_ir_bbAlloc(irctx, NULL);

		jx_ir_value_t* lhsVal = jirgenGenExpression(ctx, binNode->m_ExprLHS);

		jx_ir_instruction_t* lhsInstr = jx_ir_instrSetCC(irctx, JIR_CC_NE, lhsVal, jx_ir_constToValue(jx_ir_constGetZero(irctx, lhsVal->m_Type)));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, lhsInstr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, jx_ir_instrToValue(lhsInstr), bbRHSTest, bbFalse));
		jirgenSwitchBasicBlock(ctx, bbRHSTest);

		jx_ir_value_t* rhsVal = jirgenGenExpression(ctx, binNode->m_ExprRHS);

		jx_ir_instruction_t* rhsInstr = jx_ir_instrSetCC(irctx, JIR_CC_NE, rhsVal, jx_ir_constToValue(jx_ir_constGetZero(irctx, rhsVal->m_Type)));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, rhsInstr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, jx_ir_instrToValue(rhsInstr), bbTrue, bbFalse));
		jirgenSwitchBasicBlock(ctx, bbTrue);

		jx_ir_instruction_t* boolPtr = jx_ir_instrAlloca(irctx, jx_ir_typeGetPrimitive(irctx, JIR_TYPE_BOOL), NULL);
		jx_ir_bbPrependInstr(irctx, ctx->m_Func->m_BasicBlockListHead, boolPtr);

		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrStore(irctx, jx_ir_instrToValue(boolPtr), jx_ir_constToValue(jx_ir_constGetBool(irctx, true))));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbFalse);

		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrStore(irctx, jx_ir_instrToValue(boolPtr), jx_ir_constToValue(jx_ir_constGetBool(irctx, false))));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbEnd);

		jx_ir_instruction_t* valInstr = jx_ir_instrLoad(irctx, jx_ir_typeGetPrimitive(irctx, JIR_TYPE_BOOL), jx_ir_instrToValue(boolPtr));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, valInstr);

		val = jx_ir_instrToValue(valInstr);
	} break;
	case JCC_NODE_EXPR_LOGICAL_OR: {
		jx_cc_ast_expr_binary_t* binNode = (jx_cc_ast_expr_binary_t*)expr;

		jx_ir_basic_block_t* bbRHSTest = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbTrue = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbFalse = jx_ir_bbAlloc(irctx, NULL);
		jx_ir_basic_block_t* bbEnd = jx_ir_bbAlloc(irctx, NULL);

		jx_ir_value_t* lhsVal = jirgenGenExpression(ctx, binNode->m_ExprLHS);

		jx_ir_instruction_t* lhsInstr = jx_ir_instrSetCC(irctx, JIR_CC_NE, lhsVal, jx_ir_constToValue(jx_ir_constGetZero(irctx, lhsVal->m_Type)));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, lhsInstr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, jx_ir_instrToValue(lhsInstr), bbTrue, bbRHSTest));
		jirgenSwitchBasicBlock(ctx, bbRHSTest);

		jx_ir_value_t* rhsVal = jirgenGenExpression(ctx, binNode->m_ExprRHS);

		jx_ir_instruction_t* rhsInstr = jx_ir_instrSetCC(irctx, JIR_CC_NE, rhsVal, jx_ir_constToValue(jx_ir_constGetZero(irctx, rhsVal->m_Type)));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, rhsInstr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranchIf(irctx, jx_ir_instrToValue(rhsInstr), bbTrue, bbFalse));
		jirgenSwitchBasicBlock(ctx, bbTrue);

		jx_ir_instruction_t* boolPtr = jx_ir_instrAlloca(irctx, jx_ir_typeGetPrimitive(irctx, JIR_TYPE_BOOL), NULL);
		jx_ir_bbPrependInstr(irctx, ctx->m_Func->m_BasicBlockListHead, boolPtr);

		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrStore(irctx, jx_ir_instrToValue(boolPtr), jx_ir_constToValue(jx_ir_constGetBool(irctx, true))));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbFalse);

		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrStore(irctx, jx_ir_instrToValue(boolPtr), jx_ir_constToValue(jx_ir_constGetBool(irctx, false))));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, jx_ir_instrBranch(irctx, bbEnd));
		jirgenSwitchBasicBlock(ctx, bbEnd);

		jx_ir_instruction_t* valInstr = jx_ir_instrLoad(irctx, jx_ir_typeGetPrimitive(irctx, JIR_TYPE_BOOL), jx_ir_instrToValue(boolPtr));
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, valInstr);

		val = jx_ir_instrToValue(valInstr);
	} break;
	case JCC_NODE_EXPR_CAST: {
		jx_cc_ast_expr_unary_t* castNode = (jx_cc_ast_expr_unary_t*)expr;
		jx_ir_value_t* originalVal = jirgenGenExpression(ctx, castNode->m_Expr);

		jx_ir_type_t* srcType = originalVal->m_Type;
		jx_ir_type_t* dstType = jccTypeToIRType(ctx, expr->m_Type);
		if (jx_ir_typeIsIntegral(srcType) && jx_ir_typeIsIntegral(dstType)) {
			val = jirgenConvertType(ctx, originalVal, dstType);
		} else {
			const bool srcIsInteger = jx_ir_typeIsInteger(srcType);
			const bool dstIsInteger = jx_ir_typeIsInteger(dstType);
			const bool srcIsFP = jx_ir_typeIsFloatingPoint(srcType);
			const bool dstIsFP = jx_ir_typeIsFloatingPoint(dstType);
			const bool srcIsPointer = jx_ir_typeToPointer(srcType) != NULL;
			const bool dstIsPointer = jx_ir_typeToPointer(dstType) != NULL;
			const uint32_t srcSize = (uint32_t)jx_ir_typeGetSize(srcType);
			const uint32_t dstSize = (uint32_t)jx_ir_typeGetSize(dstType);

			jx_ir_instruction_t* castInstr = NULL;
			if (srcIsInteger && dstIsPointer) {
				castInstr = jx_ir_instrIntToPtr(irctx, originalVal, dstType);
			} else if (srcIsPointer && dstIsInteger) {
				castInstr = jx_ir_instrPtrToInt(irctx, originalVal, dstType);
			} else if (srcIsFP && dstIsFP) {
				if (srcSize < dstSize) {
					castInstr = jx_ir_instrFPExt(irctx, originalVal, dstType);
				} else {
					JX_CHECK(srcSize > dstSize, "Cast between same size FP types?");
					castInstr = jx_ir_instrFPTrunc(irctx, originalVal, dstType);
				}
			} else if (srcIsFP && dstIsInteger) {
				if (jx_ir_typeIsUnsigned(dstType)) {
					castInstr = jx_ir_instrFP2UI(irctx, originalVal, dstType);
				} else {
					castInstr = jx_ir_instrFP2SI(irctx, originalVal, dstType);
				}
			} else if (srcIsInteger && dstIsFP) {
				if (jx_ir_typeIsUnsigned(srcType)) {
					castInstr = jx_ir_instrUI2FP(irctx, originalVal, dstType);
				} else {
					castInstr = jx_ir_instrSI2FP(irctx, originalVal, dstType);
				}
			} else if (srcIsPointer && dstIsPointer) {
				castInstr = jx_ir_instrBitcast(irctx, originalVal, dstType);
			} else if (srcSize == dstSize) {
#if 1
				JX_NOT_IMPLEMENTED(); // TODO: Check when this is hit and fix/enable again.
#else
				castInstr = jx_ir_instrBitcast(irctx, originalVal, dstType);
#endif
			} else {
				JX_NOT_IMPLEMENTED();
			}

			if (castInstr) {
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, castInstr);
				val = jx_ir_instrToValue(castInstr);
			} else {
				val = originalVal;
			}
		}
	} break;
	case JCC_NODE_EXPR_MEMZERO: {
		jx_cc_ast_expr_variable_t* varNode = (jx_cc_ast_expr_variable_t*)expr;

		jx_ir_value_t* addr = jirgenGenAddress(ctx, expr);
		jirgenGemMemZero(ctx, addr);
	} break;
	case JCC_NODE_EXPR_FUNC_CALL: {
		jx_cc_ast_expr_funccall_t* funcCallNode = (jx_cc_ast_expr_funccall_t*)expr;

		jx_ir_value_t** argValsArr = (jx_ir_value_t**)jx_array_create(ctx->m_Allocator);

		// Check if we have to allocate a temporary object for the return buffer.
		jx_cc_type_t* funcType = funcCallNode->m_FuncExpr->m_Type;
		if (funcType->m_Kind == JCC_TYPE_PTR && funcType->m_BaseType->m_Kind == JCC_TYPE_FUNC) {
			funcType = funcType->m_BaseType;
		}
		JX_CHECK(funcType->m_Kind == JCC_TYPE_FUNC, "Expected function type");
		jx_cc_type_t* funcRetType = funcType->m_FuncRetType;
		bool addHiddenRetArg = false;
		if (funcRetType->m_Kind == JCC_TYPE_STRUCT || funcRetType->m_Kind == JCC_TYPE_UNION) {
			addHiddenRetArg = false
				|| funcRetType->m_Size > 8
				|| !jx_isPow2_u32(funcRetType->m_Size)
				;

			if (addHiddenRetArg) {
				jx_ir_instruction_t* retArgAlloca = jx_ir_instrAlloca(irctx, jccTypeToIRType(ctx, funcRetType), NULL);
				jx_ir_bbPrependInstr(irctx, ctx->m_Func->m_BasicBlockListHead, retArgAlloca);
				jx_array_push_back(argValsArr, jx_ir_instrToValue(retArgAlloca));
			}
		}

		const bool isVariadic = (funcType->m_Flags & JCC_TYPE_FLAGS_IS_VARIADIC_Msk) != 0;
		uint32_t numFuncArgs = 0;
		// Count function arguments
		{
			jx_cc_type_t* arg = funcType->m_FuncParams;
			while (arg) {
				++numFuncArgs;
				arg = arg->m_Next;
			}
		}

		const uint32_t numArgs = funcCallNode->m_NumArgs;
		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			jx_cc_ast_expr_t* argExpr = funcCallNode->m_Args[iArg];
			jx_ir_value_t* argVal = jirgenGenExpression(ctx, argExpr);
			JX_CHECK(argVal, "Failed to generate function argument expression!");

			// Check if the expression type prevented a possible load to be generated (see jirgenGenLoad()).
			// In this case, argVal should be a pointer to the value. We have to allocate a temporary on 
			// the stack, copy the value pointed by argVal to it and pass the pointer to the function.
			// TODO: Check all possible struct sizes. This code assumes that the struct does not fit in a
			// register so it has to be converted to pointer.
			jx_cc_type_t* ccArgType = argExpr->m_Type;
			if (ccArgType->m_Kind == JCC_TYPE_STRUCT) {
				JX_CHECK(jx_ir_typeToPointer(argVal->m_Type), "Argument value expected to have pointer type!");
				jx_ir_instruction_t* tmpAlloca = jx_ir_instrAlloca(irctx, jccTypeToIRType(ctx, ccArgType), NULL);
				jx_ir_bbPrependInstr(irctx, ctx->m_Func->m_BasicBlockListHead, tmpAlloca);
				jirgenGenMemCopy(ctx, jx_ir_instrToValue(tmpAlloca), argVal);
				argVal = jx_ir_instrToValue(tmpAlloca);
			} else if (ccArgType->m_Kind == JCC_TYPE_UNION) {
				JX_NOT_IMPLEMENTED();
			} else if (ccArgType->m_Kind == JCC_TYPE_ARRAY) {
//				JX_NOT_IMPLEMENTED();
			} else if (ccArgType->m_Kind == JCC_TYPE_FUNC) {
//				JX_NOT_IMPLEMENTED();
			}

			// default argument promotions
			if (isVariadic && iArg >= numFuncArgs) {
				const bool convertInt = false
					|| argVal->m_Type->m_Kind == JIR_TYPE_BOOL
					|| argVal->m_Type->m_Kind == JIR_TYPE_I8
					|| argVal->m_Type->m_Kind == JIR_TYPE_U8
					|| argVal->m_Type->m_Kind == JIR_TYPE_I16
					|| argVal->m_Type->m_Kind == JIR_TYPE_U16
					;
				if (convertInt) {
					argVal = jirgenConvertType(ctx, argVal, jx_ir_typeGetPrimitive(irctx, jx_ir_typeIsUnsigned(argVal->m_Type) ? JIR_TYPE_U32 : JIR_TYPE_I32));
				} else if (argVal->m_Kind == JIR_TYPE_F32) {
					JX_NOT_IMPLEMENTED();
				}
			}

			jx_array_push_back(argValsArr, argVal);
		}

		jx_ir_value_t* funcVal = jirgenGenExpression(ctx, funcCallNode->m_FuncExpr);
		if (!funcVal) {
			return NULL;
		}
		jx_ir_instruction_t* callInstr = jx_ir_instrCall(irctx, funcVal, (uint32_t)jx_array_sizeu(argValsArr), argValsArr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, callInstr);
		val = addHiddenRetArg 
			? argValsArr[0] 
			: jx_ir_instrToValue(callInstr)
			;

		jx_array_free(argValsArr);
	} break;
	case JCC_NODE_EXPR_COMPOUND_ASSIGN: {
		// Compound assignments (e.g. +=, -=, etc.) might end up producing operations between
		// different types because the frontend does not currently perform the "usual arithmetic conversions" 
		// for these nodes. Handle this here.
		jx_cc_ast_expr_compound_assign_t* assignExpr = (jx_cc_ast_expr_compound_assign_t*)expr;
		jx_ir_value_t* rhs = jirgenGenExpression(ctx, assignExpr->m_ExprRHS);
		jx_ir_value_t* lhsAddr = jirgenGenAddress(ctx, assignExpr->m_ExprLHS);
		jx_ir_type_t* lhsType = jx_ir_typeToPointer(lhsAddr->m_Type)->m_BaseType;
		jx_ir_instruction_t* loadInstr = jx_ir_instrLoad(irctx, lhsType, lhsAddr);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, loadInstr);

		jx_ir_value_t* lhs = jx_ir_instrToValue(loadInstr);

		if (assignExpr->m_Op != JCC_NODE_EXPR_LSHIFT && assignExpr->m_Op != JCC_NODE_EXPR_RSHIFT) {
			jx_ir_type_t* commonType = jirgenUsualArithmeticConversions(ctx, lhs->m_Type, rhs->m_Type);
			if (commonType) {
				if (commonType != lhs->m_Type) {
					lhs = jirgenConvertType(ctx, lhs, commonType);
				}
				if (commonType != rhs->m_Type) {
					rhs = jirgenConvertType(ctx, rhs, commonType);
				}
			}
		} else {
			JX_NOT_IMPLEMENTED();
//			lhs = jirgenIntegerPromotion(ctx, lhs);
//			rhs = jirgenIntegerPromotion(ctx, rhs);
		}

		jx_ir_instruction_t* binInstr = kIRBinaryOps[assignExpr->m_Op](irctx, lhs, rhs);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, binInstr);

		jx_ir_value_t* resVal = jx_ir_instrToValue(binInstr);
		if (resVal->m_Type != lhsType) {
			resVal = jirgenConvertType(ctx, resVal, lhsType);
		}

		jx_ir_instruction_t* storeInstr = jx_ir_instrStore(irctx, lhsAddr, resVal);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, storeInstr);

		val = jx_ir_instrToValue(binInstr);
	} break;
	case JCC_NODE_VARIABLE: {
		jx_ir_value_t* ptr = jirgenGenAddress(ctx, expr);
		val = jirgenGenLoad(ctx, ptr, expr->m_Type);
	} break;
	case JCC_NODE_NUMBER: {
		jx_ir_constant_t* c = NULL;
		if (jx_cc_typeIsFloat(expr->m_Type)) {
			jx_cc_ast_expr_fconst_t* fconstNode = (jx_cc_ast_expr_fconst_t*)expr;
			switch (expr->m_Type->m_Kind) {
			case JCC_TYPE_FLOAT:
				c = jx_ir_constGetF32(irctx, (float)fconstNode->m_Value);
				break;
			case JCC_TYPE_DOUBLE:
				c = jx_ir_constGetF64(irctx, fconstNode->m_Value);
				break;
			default:
				JX_CHECK(false, "Unknown float constant.");
				break;
			}
		} else {
			jx_cc_ast_expr_iconst_t* iconstNode = (jx_cc_ast_expr_iconst_t*)expr;
			switch (expr->m_Type->m_Kind) {
			case JCC_TYPE_BOOL: {
				c = jx_ir_constGetBool(irctx, iconstNode->m_Value != 0);
			} break;
			case JCC_TYPE_CHAR: {
				c = (expr->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
					? jx_ir_constGetU8(irctx, (uint8_t)iconstNode->m_Value)
					: jx_ir_constGetI8(irctx, (int8_t)iconstNode->m_Value)
					;
			} break;
			case JCC_TYPE_SHORT: {
				c = (expr->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
					? jx_ir_constGetU16(irctx, (uint16_t)iconstNode->m_Value)
					: jx_ir_constGetI16(irctx, (int16_t)iconstNode->m_Value)
					;
			} break;
			case JCC_TYPE_INT: {
				c = (expr->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
					? jx_ir_constGetU32(irctx, (uint32_t)iconstNode->m_Value)
					: jx_ir_constGetI32(irctx, (int32_t)iconstNode->m_Value)
					;
			} break;
			case JCC_TYPE_LONG: {
				c = (expr->m_Type->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
					? jx_ir_constGetU64(irctx, (uint64_t)iconstNode->m_Value)
					: jx_ir_constGetI64(irctx, iconstNode->m_Value)
					;
			} break;
			default:
				JX_CHECK(false, "Unknown integer constant type");
				break;
			}
		}
		if (c) {
			val = jx_ir_constToValue(c);
		}
	} break;
	case JCC_NODE_EXPR_GET_ELEMENT_PTR: {
		jx_cc_ast_expr_get_element_ptr_t* gepNode = (jx_cc_ast_expr_get_element_ptr_t*)expr;
		jx_ir_value_t* ptrVal = jirgenGenExpression(ctx, gepNode->m_ExprPtr);
		jx_ir_value_t* idxVal = jirgenGenExpression(ctx, gepNode->m_ExprIndex);

		// If gepNode->m_ExprPtr is an array, use a 2 index GEP because the first index
		// refers to the array itself.
		const bool isArray = gepNode->m_ExprPtr->m_Type->m_Kind == JCC_TYPE_ARRAY;

		jx_ir_value_t* indices[2] = {
			jx_ir_constToValue(jx_ir_constGetI32(irctx, 0)),
			idxVal
		};
		jx_ir_instruction_t* gepInstr = jx_ir_instrGetElementPtr(irctx, ptrVal, 1 + isArray, &indices[1 - isArray]);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, gepInstr);
		val = jx_ir_instrToValue(gepInstr);
	} break;
	default: {
		JX_CHECK(false, "Unknown expression node.");
	} break;
	}

	return val;
}

static jx_ir_value_t* jirgenGenAddress(jx_irgen_context_t* ctx, jx_cc_ast_expr_t* expr)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_value_t* val = NULL;

	switch (expr->super.m_Kind) {
	case JCC_NODE_EXPR_MEMBER: {
		jx_cc_ast_expr_member_t* memberNode = (jx_cc_ast_expr_member_t*)expr;
		jx_ir_value_t* baseAddr = jirgenGenAddress(ctx, memberNode->m_Expr);

		jx_cc_type_t* memberExprType = memberNode->m_Expr->m_Type;
		if (memberExprType->m_Kind == JCC_TYPE_STRUCT) {
			jx_ir_value_t* indices[] = {
				jx_ir_constToValue(jx_ir_constGetI32(irctx, 0)),
				jx_ir_constToValue(jx_ir_constGetI32(irctx, memberNode->m_Member->m_GEPIndex))
			};
			jx_ir_instruction_t* gepInstr = jx_ir_instrGetElementPtr(irctx, baseAddr, 2, indices);
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, gepInstr);
			val = jx_ir_instrToValue(gepInstr);
		} else if (memberExprType->m_Kind == JCC_TYPE_UNION) {
			// Bitcast the base address from the union type to the expected type.
			jx_cc_type_t* memberType = memberNode->m_Member->m_Type;
			jx_ir_instruction_t* bitcastInstr = jx_ir_instrBitcast(irctx, baseAddr, jx_ir_typeGetPointer(irctx, jccTypeToIRType(ctx, memberType)));
			jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, bitcastInstr);
			val = jx_ir_instrToValue(bitcastInstr);
		} else {
			JX_CHECK(false, "Unknown base type for member node!");
		}
	} break;
	case JCC_NODE_EXPR_COMMA: {
		jx_cc_ast_expr_binary_t* commaExpr = (jx_cc_ast_expr_binary_t*)expr;
		jirgenGenExpression(ctx, commaExpr->m_ExprLHS);
		val = jirgenGenAddress(ctx, commaExpr->m_ExprRHS);
	} break;
	case JCC_NODE_EXPR_ADDR: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JCC_NODE_EXPR_DEREF: {
		jx_cc_ast_expr_unary_t* unaryNode = (jx_cc_ast_expr_unary_t*)expr;
		val = jirgenGenExpression(ctx, unaryNode->m_Expr);
	} break;
	case JCC_NODE_EXPR_MEMZERO:
	case JCC_NODE_VARIABLE: {
		jx_cc_ast_expr_variable_t* varNode = (jx_cc_ast_expr_variable_t*)expr;
		jx_cc_object_t* var = varNode->m_Var;
		if ((var->m_Flags & JCC_OBJECT_FLAGS_IS_LOCAL_Msk) != 0) {
			jccObj_to_irVal_item_t* hashItem = jx_hashmapGet(ctx->m_LocalVarMap, &(jccObj_to_irVal_item_t){.m_ccObj = var });
			JX_CHECK(hashItem, "Local variable not found in hashmap");

			// Check if this is a function argument of a composite type which has been changed to pointer.
			// See jccFuncArgGetType for details.
			// 
			// Since all local variables are pointers (alloca result), in order for the above to be true,
			// the value type should be a pointer to pointer to the true type.
			//
			// TODO: Make this cleaner/easier to understand by introducing a flag somewhere and avoid
			// calling jccTypeToIRType().
			jx_ir_type_pointer_t* curTypePtr = jx_ir_typeToPointer(hashItem->m_irVal->m_Type);
			jx_ir_type_pointer_t* curType = jx_ir_typeToPointer(curTypePtr->m_BaseType);
			jx_ir_type_t* trueType = jccTypeToIRType(ctx, var->m_Type);
			if (curType && curType->m_BaseType == trueType) {
				jx_ir_instruction_t* loadPtrInstr = jx_ir_instrLoad(irctx, curTypePtr->m_BaseType, hashItem->m_irVal);
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, loadPtrInstr);
				val = jx_ir_instrToValue(loadPtrInstr);
			} else {
				val = hashItem->m_irVal;
			}
		} else {
			if ((var->m_Flags & JCC_OBJECT_FLAGS_IS_TLS_Msk) != 0) {
				// Thread local var
				JX_NOT_IMPLEMENTED();
			} else {
				if (expr->m_Type->m_Kind == JCC_TYPE_FUNC) {
					// Function
					jx_ir_function_t* func = jx_ir_moduleGetFunc(irctx, ctx->m_Module, var->m_Name);
					val = jx_ir_funcToValue(func);
				} else {
					// Global
					jx_ir_global_variable_t* gv = jx_ir_moduleGetGlobalVar(irctx, ctx->m_Module, var->m_Name);
					val = jx_ir_globalVarToValue(gv);
				}
			}
		}
	} break;
	default: {
		JX_CHECK(false, "Cannot calculate expression address or unknown expression node.");
	} break;
	}

	return val;
}

static jx_ir_value_t* jirgenGenLoad(jx_irgen_context_t* ctx, jx_ir_value_t* ptr, jx_cc_type_t* ccType)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	const bool canLoad = true
		&& ccType->m_Kind != JCC_TYPE_ARRAY
		&& ccType->m_Kind != JCC_TYPE_STRUCT
		&& ccType->m_Kind != JCC_TYPE_UNION
		&& ccType->m_Kind != JCC_TYPE_FUNC
		;
	if (!canLoad) {
		return ptr;
	}

	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(ptr->m_Type);
	JX_CHECK(ptrType, "Expected a pointer type");

	jx_ir_type_t* baseType = ptrType->m_BaseType;
	jx_ir_type_array_t* arrType = jx_ir_typeToArray(baseType);
	if (arrType) {
		// Dereference of a pointer to an array should load the first element of the array
		// (i.e. treat the array as pointer)
		jx_ir_value_t* indices[] = {
			jx_ir_constToValue(jx_ir_constGetI32(irctx, 0)),
			jx_ir_constToValue(jx_ir_constGetI32(irctx, 0)),
		};
		jx_ir_instruction_t* gepInstr = jx_ir_instrGetElementPtr(irctx, ptr, 2, indices);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, gepInstr);
		ptr = jx_ir_instrToValue(gepInstr);
		ptrType = jx_ir_typeToPointer(ptr->m_Type);
	}

	jx_ir_instruction_t* loadInstr = jx_ir_instrLoad(irctx, ptrType->m_BaseType, ptr);
	jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, loadInstr);
	return jx_ir_instrToValue(loadInstr);
}

static void jirgenGenStore(jx_irgen_context_t* ctx, jx_ir_value_t* ptr, jx_ir_value_t* val)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(ptr->m_Type);
	JX_CHECK(ptrType, "Expected a pointer type");

	jx_ir_type_t* baseType = ptrType->m_BaseType;
	if (baseType == val->m_Type) {
		JX_CHECK(jx_ir_typeIsFirstClass(baseType) || jx_ir_typeIsSmallPow2Struct(baseType), "store value operand expected to have a first-class type");
		jx_ir_instruction_t* storeInstr = jx_ir_instrStore(irctx, ptr, val);
		jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, storeInstr);
	} else {
		jx_ir_type_pointer_t* valPtrType = jx_ir_typeToPointer(val->m_Type);
		if (valPtrType == ptrType) {
			jirgenGenMemCopy(ctx, ptr, val);
		} else {
			const uint32_t valSize = (uint32_t)jx_ir_typeGetSize(val->m_Type);
			const uint32_t baseTypeSize = (uint32_t)jx_ir_typeGetSize(baseType);
			if (valSize == baseTypeSize) {
				jx_ir_instruction_t* bitcastInstr = jx_ir_instrBitcast(irctx, ptr, jx_ir_typeGetPointer(irctx, val->m_Type));
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, bitcastInstr);
				jx_ir_instruction_t* storeInstr = jx_ir_instrStore(irctx, jx_ir_instrToValue(bitcastInstr), val);
				jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, storeInstr);
			} else {
				JX_NOT_IMPLEMENTED();
			}
		}
	}
}

static void jirgenGemMemZero(jx_irgen_context_t* ctx, jx_ir_value_t* addr)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_type_pointer_t* addrTypePtr = jx_ir_typeToPointer(addr->m_Type);
	JX_CHECK(addrTypePtr, "Address expected to have pointer type!");

	jx_ir_instruction_t* memsetInstr = jx_ir_instrMemSet(irctx, addr, jx_ir_constToValue(jx_ir_constGetI8(irctx, 0)), jx_ir_constToValue(jx_ir_constGetI64(irctx, jx_ir_typeGetSize(addrTypePtr->m_BaseType))));
	jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, memsetInstr);
}

static void jirgenGenMemCopy(jx_irgen_context_t* ctx, jx_ir_value_t* dstVal, jx_ir_value_t* srcVal)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_type_t* dstType = dstVal->m_Type;
	jx_ir_type_t* srcType = srcVal->m_Type;

	jx_ir_type_pointer_t* dstPtrType = jx_ir_typeToPointer(dstType);
	JX_CHECK(dstPtrType, "memcpy destination operand expected to have a pointer type.");
	JX_CHECK(dstType == srcType, "memcpy expects both operands to have the same type");

	jx_ir_instruction_t* memcpyInstr = jx_ir_instrMemCopy(irctx, dstVal, srcVal, jx_ir_constToValue(jx_ir_constGetI64(irctx, (int64_t)jx_ir_typeGetSize(dstPtrType->m_BaseType))));
	jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, memcpyInstr);
}

static jx_ir_value_t* jirgenConvertToBool(jx_irgen_context_t* ctx, jx_ir_value_t* val)
{
	if (val->m_Type->m_Kind == JIR_TYPE_BOOL) {
		return val;
	}

	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_instruction_t* cmpInstr = jx_ir_instrSetNE(irctx, val, jx_ir_constToValue(jx_ir_constGetZero(irctx, val->m_Type)));
	jx_ir_bbAppendInstr(irctx, ctx->m_BasicBlock, cmpInstr);

	return jx_ir_instrToValue(cmpInstr);
}

static jx_ir_type_t* jirgenIntegerPromotion(jx_irgen_context_t* ctx, jx_ir_type_t* type)
{
	switch (type->m_Kind) {
	case JIR_TYPE_BOOL:
	case JIR_TYPE_U8:
	case JIR_TYPE_U16:
	case JIR_TYPE_I8:
	case JIR_TYPE_I16: {
		type = jx_ir_typeGetPrimitive(ctx->m_IRCtx, JIR_TYPE_I32);
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32:
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: 
	case JIR_TYPE_POINTER: {
		// No integer promotions for large integer types
	} break;
	default:
		JX_CHECK(false, "Invalid operand type?");
		break;
	}

	return type;
}

static jx_ir_type_t* jirgenUsualArithmeticConversions(jx_irgen_context_t* ctx, jx_ir_type_t* lhsType, jx_ir_type_t* rhsType)
{
	// ... if the corresponding real type of either operand is double, the other
	// operand is converted, without change of type domain, to a type whose
	// corresponding real type is double.
	if (lhsType->m_Kind == JIR_TYPE_F64) {
		return lhsType;
	} else if (rhsType->m_Kind == JIR_TYPE_F64) {
		return rhsType;
	}

	// Otherwise, if the corresponding real type of either operand is float, the other
	// operand is converted, without change of type domain, to a type whose
	// corresponding real type is float.
	if (lhsType->m_Kind == JIR_TYPE_F32) {
		return lhsType;
	} else if (rhsType->m_Kind == JIR_TYPE_F32) {
		return rhsType;
	}

	// Otherwise, the integer promotions are performed on both operands.
	lhsType = jirgenIntegerPromotion(ctx, lhsType);
	rhsType = jirgenIntegerPromotion(ctx, rhsType);

	// Then the following rules are applied to the promoted operands:
	// If both operands have the same type, then no further conversion is needed
	if (lhsType == rhsType) {
		return lhsType;
	}

	if (!jx_ir_typeIsInteger(lhsType) || !jx_ir_typeIsInteger(rhsType)) {
		return NULL;
	}

	const bool lhsIsUnsigned = jx_ir_typeIsUnsigned(lhsType);
	const bool rhsIsUnsigned = jx_ir_typeIsUnsigned(rhsType);
	const uint32_t lhsRank = jx_ir_typeGetIntegerConversionRank(lhsType);
	const uint32_t rhsRank = jx_ir_typeGetIntegerConversionRank(rhsType);

	// Otherwise, if both operands have signed integer types or both have unsigned
	// integer types, the operand with the type of lesser integer conversion rank is
	// converted to the type of the operand with greater rank.
	if (lhsIsUnsigned == rhsIsUnsigned) {
		return lhsRank < rhsRank
			? rhsType
			: lhsType
			;
	} 
	
	// Otherwise, if the operand that has unsigned integer type has rank greater or
	// equal to the rank of the type of the other operand, then the operand with
	// signed integer type is converted to the type of the operand with unsigned
	// integer type.
	if (lhsIsUnsigned && lhsRank >= rhsRank) {
		return lhsType;
	} else if (rhsIsUnsigned && rhsRank >= lhsRank) {
		return rhsType;
	}

	// Otherwise, if the type of the operand with signed integer type can represent
	// all of the values of the type of the operand with unsigned integer type, then
	// the operand with unsigned integer type is converted to the type of the
	// operand with signed integer type.
	if (!lhsIsUnsigned && jx_ir_typeCanRepresent(lhsType, rhsType)) {
		return lhsType;
	} else if (!rhsIsUnsigned && jx_ir_typeCanRepresent(rhsType, lhsType)) {
		return rhsType;
	}

	// Otherwise, both operands are converted to the unsigned integer type
	// corresponding to the type of the operand with signed integer type.
	if (!lhsIsUnsigned) {
		return jx_ir_typeGetPrimitive(ctx->m_IRCtx, jx_ir_typeToUnsigned(lhsType->m_Kind));
	}

	return jx_ir_typeGetPrimitive(ctx->m_IRCtx, jx_ir_typeToUnsigned(rhsType->m_Kind));
}

static jx_ir_value_t* jirgenConvertType(jx_irgen_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* type)
{
	JX_CHECK(jx_ir_typeIsIntegral(val->m_Type), "Can only convert integral values.");
	JX_CHECK(jx_ir_typeIsIntegral(type), "Can only convert to integral types.");

	if (type->m_Kind == JIR_TYPE_BOOL) {
		return jirgenConvertToBool(ctx, val);
	}

	const uint32_t valSz = (uint32_t)jx_ir_typeGetSize(val->m_Type);
	const uint32_t typeSz = (uint32_t)jx_ir_typeGetSize(type);

	if (typeSz > valSz) {
		// Extension
		if (jx_ir_typeIsUnsigned(val->m_Type)) {
			jx_ir_instruction_t* zextInstr = jx_ir_instrZeroExt(ctx->m_IRCtx, val, type);
			jx_ir_bbAppendInstr(ctx->m_IRCtx, ctx->m_BasicBlock, zextInstr);
			val = jx_ir_instrToValue(zextInstr);
		} else {
			jx_ir_instruction_t* sextInstr = jx_ir_instrSignExt(ctx->m_IRCtx, val, type);
			jx_ir_bbAppendInstr(ctx->m_IRCtx, ctx->m_BasicBlock, sextInstr);
			val = jx_ir_instrToValue(sextInstr);
		}
	} else if (typeSz < valSz) {
		// Truncation
		jx_ir_instruction_t* truncInstr = jx_ir_instrTrunc(ctx->m_IRCtx, val, type);
		jx_ir_bbAppendInstr(ctx->m_IRCtx, ctx->m_BasicBlock, truncInstr);
		val = jx_ir_instrToValue(truncInstr);
	} else {
		// Bitcast
		jx_ir_instruction_t* bitcastInstr = jx_ir_instrBitcast(ctx->m_IRCtx, val, type);
		jx_ir_bbAppendInstr(ctx->m_IRCtx, ctx->m_BasicBlock, bitcastInstr);
		val = jx_ir_instrToValue(bitcastInstr);
	}

	return val;
}

static jx_ir_type_t* jccTypeToIRType(jx_irgen_context_t* ctx, jx_cc_type_t* ccType)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_type_t* irType = NULL;
	switch (ccType->m_Kind) {
	case JCC_TYPE_VOID: {
		irType = jx_ir_typeGetPrimitive(irctx, JIR_TYPE_VOID);
	} break;
	case JCC_TYPE_BOOL: {
		irType = jx_ir_typeGetPrimitive(irctx, JIR_TYPE_BOOL);
	} break;
	case JCC_TYPE_CHAR: {
		irType = (ccType->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
			? jx_ir_typeGetPrimitive(irctx, JIR_TYPE_U8)
			: jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I8)
			;
	} break;
	case JCC_TYPE_SHORT: {
		irType = (ccType->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
			? jx_ir_typeGetPrimitive(irctx, JIR_TYPE_U16)
			: jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I16)
			;
	} break;
	case JCC_TYPE_INT: {
		irType = (ccType->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
			? jx_ir_typeGetPrimitive(irctx, JIR_TYPE_U32)
			: jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I32)
			;
	} break;
	case JCC_TYPE_LONG: {
		irType = (ccType->m_Flags & JCC_TYPE_FLAGS_IS_UNSIGNED_Msk) != 0
			? jx_ir_typeGetPrimitive(irctx, JIR_TYPE_U64)
			: jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I64)
			;
	} break;
	case JCC_TYPE_FLOAT: {
		irType = jx_ir_typeGetPrimitive(irctx, JIR_TYPE_F32);
	} break;
	case JCC_TYPE_DOUBLE: {
		irType = jx_ir_typeGetPrimitive(irctx, JIR_TYPE_F64);
	} break;
	case JCC_TYPE_ENUM: {
		irType = jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I32);
	} break;
	case JCC_TYPE_PTR: {
		irType = jx_ir_typeGetPointer(irctx, jccTypeToIRType(ctx, ccType->m_BaseType));
	} break;
	case JCC_TYPE_FUNC: {
		// Return types follow (more or less, with the exception of __m128 types the compiler
		// does not currently support) the same restrictions as the function arguments.
		bool shouldAddRetPtr = false;
		jx_ir_type_t* retType = jccFuncRetGetType(ctx, ccType->m_FuncRetType, &shouldAddRetPtr);

		jx_ir_type_t** args = (jx_ir_type_t**)jx_array_create(ctx->m_Allocator);

		if (shouldAddRetPtr) {
			jx_array_push_back(args, retType);
		}

		jx_cc_type_t* ccArg = ccType->m_FuncParams;
		while (ccArg) {
			jx_ir_type_t* argType = jccFuncArgGetType(ctx, ccArg);
			jx_array_push_back(args, argType);
			ccArg = ccArg->m_Next;
		}
		
		const uint32_t numArgs = (uint32_t)jx_array_sizeu(args);
		const bool isVarArg = (ccType->m_Flags & JCC_TYPE_FLAGS_IS_VARIADIC_Msk) != 0;

		irType = jx_ir_typeGetFunction(irctx, shouldAddRetPtr ? jx_ir_typeGetPrimitive(irctx, JIR_TYPE_VOID) : retType, numArgs, args, isVarArg);

		jx_array_free(args);
	} break;
	case JCC_TYPE_ARRAY: {
		irType = jx_ir_typeGetArray(irctx, jccTypeToIRType(ctx, ccType->m_BaseType), ccType->m_ArrayLen);
	} break;
	case JCC_TYPE_STRUCT: {
		const uint64_t structUniqueID = (uint64_t)ccType;
		irType = jx_ir_typeGetStruct(irctx, structUniqueID);
		if (!irType) {
			jx_ir_type_struct_t* structType = jx_ir_typeStructBegin(irctx, structUniqueID, 0);
			if (structType) {
				jx_ir_type_t** members = (jx_ir_type_t**)jx_array_create(ctx->m_Allocator);

				jx_cc_struct_member_t* ccMember = ccType->m_StructMembers;
				while (ccMember) {
					if (ccMember->m_BitOffset == 0) {
						jx_array_push_back(members, jccTypeToIRType(ctx, ccMember->m_Type));
					}
					ccMember = ccMember->m_Next;
				}

				const uint32_t numMembers = (uint32_t)jx_array_sizeu(members);
				jx_ir_typeStructSetMembers(irctx, structType, numMembers, members);
				irType = jx_ir_typeStructEnd(irctx, structType);

				jx_array_free(members);

				const char* jccStructName = jccTypeGetStructName(ccType);
				if (jccStructName) {
					jx_ir_valueSetName(irctx, jx_ir_typeToValue(irType), jccStructName);
				} else {
					char structName[256];
					jx_snprintf(structName, JX_COUNTOF(structName), "struct_%p", ccType);
					jx_ir_valueSetName(irctx, jx_ir_typeToValue(irType), structName);
				}
			} else {
				JX_CHECK(false, "Failed to begin new struct");
			}
		}
	} break;
	case JCC_TYPE_UNION: {
		const uint64_t structUniqueID = (uint64_t)ccType;
		irType = jx_ir_typeGetStruct(irctx, structUniqueID);
		if (!irType) {
			// If I understand clang's output correctly, unions are modelled as structs with 1 or 2 members.
			// The first member is always the type with the largest (stricter?) alignment. The second member,
			// if present is an i8 array big enough so, with the addition of the first member, allocates enough
			// space for the whole union.
			// 
			// All accesses to union's members happen via bitcasts to its pointer.
			//
			// If two or more members have the same (max) alignment then Clang seems to set the type of the first 
			// struct member to the first such union member.

			// Find the member with the largest alignment and the largest size.
			jx_ir_type_struct_t* structType = jx_ir_typeStructBegin(irctx, structUniqueID, JIR_TYPE_STRUCT_FLAGS_IS_UNION_Msk);
			if (structType) {
				uint32_t maxMemberAlignment = 0;
				uint32_t maxMemberSize = 0;
				jx_ir_type_t* firstMemberType = NULL;

				jx_cc_struct_member_t* ccMember = ccType->m_StructMembers;
				while (ccMember) {
					jx_ir_type_t* memberType = jccTypeToIRType(ctx, ccMember->m_Type);
					const uint32_t memberSize = (uint32_t)jx_ir_typeGetSize(memberType);
					const uint32_t memberAlignment = (uint32_t)jx_ir_typeGetAlignment(memberType);

					if (memberSize > maxMemberSize) {
						maxMemberSize = memberSize;
					}
					if (memberAlignment > maxMemberAlignment) {
						maxMemberAlignment = memberAlignment;
						firstMemberType = memberType;
					}

					ccMember = ccMember->m_Next;
				}
				JX_CHECK(firstMemberType, "Union expected to have at least 1 member.");

				const uint32_t firstMemberSize = (uint32_t)jx_ir_typeGetSize(firstMemberType);
				JX_CHECK(maxMemberSize >= firstMemberSize, "WTF?");

				const uint32_t secondMemberSize = maxMemberSize - firstMemberSize;
				jx_ir_type_t* secondMemberType = secondMemberSize != 0
					? jx_ir_typeGetArray(irctx, jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I8), secondMemberSize)
					: NULL
					;

				jx_ir_type_t* members[] = {
					firstMemberType,
					secondMemberType
				};

				const uint32_t numMembers = secondMemberSize != 0 ? 2 : 1;
				jx_ir_typeStructSetMembers(irctx, structType, numMembers, members);
				irType = jx_ir_typeStructEnd(irctx, structType);

				const char* jccStructName = jccTypeGetStructName(ccType);
				if (jccStructName) {
					jx_ir_valueSetName(irctx, jx_ir_typeToValue(irType), jccStructName);
				} else {
					char structName[256];
					jx_snprintf(structName, JX_COUNTOF(structName), "struct_%p", ccType);
					jx_ir_valueSetName(irctx, jx_ir_typeToValue(irType), structName);
				}
			} else {
				JX_CHECK(false, "Failed to begin new struct");
			}
		}
	} break;
	default: {
		JX_CHECK(false, "Unknown kind of jcc type");
	} break;
	}

	return irType;
}

// https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170#calling-convention-defaults
// 
// Any argument that doesn't fit in 8 bytes, or isn't 1, 2, 4, or 8 bytes, must be passed 
// by reference. A single argument is never spread across multiple registers.
static jx_ir_type_t* jccFuncArgGetType(jx_irgen_context_t* ctx, jx_cc_type_t* ccType)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_type_t* argType = jccTypeToIRType(ctx, ccType);

	if (ccType->m_Kind == JCC_TYPE_STRUCT || ccType->m_Kind == JCC_TYPE_UNION) {
		const bool shouldConvertToPtr = false
			|| ccType->m_Size > 8
			|| !jx_isPow2_u32(ccType->m_Size)
			;
		if (shouldConvertToPtr) {
			return jx_ir_typeGetPointer(irctx, argType);
		}
	}

	return argType;
}

// https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170#return-values
static jx_ir_type_t* jccFuncRetGetType(jx_irgen_context_t* ctx, jx_cc_type_t* ccType, bool* addAsArg)
{
	jx_ir_context_t* irctx = ctx->m_IRCtx;

	jx_ir_type_t* argType = jccTypeToIRType(ctx, ccType);

	if (ccType->m_Kind == JCC_TYPE_STRUCT || ccType->m_Kind == JCC_TYPE_UNION) {
		const bool shouldConvertToPtr = false
			|| ccType->m_Size > 8
			|| !jx_isPow2_u32(ccType->m_Size)
			;
		if (shouldConvertToPtr) {
			*addAsArg = true;
			return jx_ir_typeGetPointer(irctx, argType);
		} else {
			switch (ccType->m_Size) {
			case 1: return jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I8);
			case 2: return jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I16);
			case 4: return jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I32);
			case 8: return jx_ir_typeGetPrimitive(irctx, JIR_TYPE_I64);
			default:
				JX_CHECK(false, "Should not land here!");
				break;
			}
		}
	}

	*addAsArg = false;
	return argType;
}

static const char* jccTypeGetStructName(const jx_cc_type_t* type)
{
	if (type->m_OriginType) {
		return jccTypeGetStructName(type->m_OriginType);
	}

	return type->m_DeclName
		? type->m_DeclName->m_String
		: NULL
		;
}

static uint64_t jccObjHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jccObj_to_irVal_item_t* hashItem = (const jccObj_to_irVal_item_t*)item;
	return (uint64_t)(uintptr_t)hashItem->m_ccObj;
}

static int32_t jccObjCompareCallback(const void* a, const void* b, void* udata)
{
	const jccObj_to_irVal_item_t* hashItemA = (const jccObj_to_irVal_item_t*)a;
	const jccObj_to_irVal_item_t* hashItemB = (const jccObj_to_irVal_item_t*)b;
	return (uintptr_t)hashItemA->m_ccObj < (uintptr_t)hashItemB->m_ccObj
		? -1
		: (uintptr_t)hashItemA->m_ccObj >(uintptr_t)hashItemB->m_ccObj ? 1 : 0
		;
}

static uint64_t jccLabelHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jccLabel_to_irBB_item_t* hashItem = (const jccLabel_to_irBB_item_t*)item;
	return jx_hashFNV1a(&hashItem->m_Label, sizeof(jx_cc_label_t), seed0, seed1);
}

static int32_t jccLabelCompareCallback(const void* a, const void* b, void* udata)
{
	const jccLabel_to_irBB_item_t* hashItemA = (const jccLabel_to_irBB_item_t*)a;
	const jccLabel_to_irBB_item_t* hashItemB = (const jccLabel_to_irBB_item_t*)b;
	return hashItemA->m_Label.m_ID < hashItemB->m_Label.m_ID
		? -1
		: hashItemA->m_Label.m_ID > hashItemB->m_Label.m_ID ? 1 : 0
		;
}
