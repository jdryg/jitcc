#include "jmir.h"
#include "jmir_pass.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/bitset.h>
#include <jlib/dbg.h>
#include <jlib/hashmap.h>
#include <jlib/logger.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>
#include <tracy/tracy/TracyC.h>

static const char* kMIROpcodeMnemonic[] = {
	[JMIR_OP_RET] = "ret",
	[JMIR_OP_CMP] = "cmp",
	[JMIR_OP_TEST] = "test",
	[JMIR_OP_JMP] = "jmp",
	[JMIR_OP_MOV] = "mov",
	[JMIR_OP_MOVSX] = "movsx",
	[JMIR_OP_MOVZX] = "movzx",
	[JMIR_OP_IMUL] = "imul",
	[JMIR_OP_IMUL3] = "imul",
	[JMIR_OP_IDIV] = "idiv",
	[JMIR_OP_DIV] = "div",
	[JMIR_OP_ADD] = "add",
	[JMIR_OP_SUB] = "sub",
	[JMIR_OP_LEA] = "lea",
	[JMIR_OP_XOR] = "xor",
	[JMIR_OP_AND] = "and",
	[JMIR_OP_OR] = "or",
	[JMIR_OP_SAR] = "sar",
	[JMIR_OP_SHR] = "shr",
	[JMIR_OP_SHL] = "shl",
	[JMIR_OP_CALL] = "call",
	[JMIR_OP_PUSH] = "push",
	[JMIR_OP_POP] = "pop",
	[JMIR_OP_CDQ] = "cdq",
	[JMIR_OP_CQO] = "cqo",
	[JMIR_OP_SETO] = "seto",
	[JMIR_OP_SETNO] = "setno",
	[JMIR_OP_SETB] = "setb",
	[JMIR_OP_SETNB] = "setnb",
	[JMIR_OP_SETE] = "sete",
	[JMIR_OP_SETNE] = "setne",
	[JMIR_OP_SETBE] = "setbe",
	[JMIR_OP_SETNBE] = "setnbe",
	[JMIR_OP_SETS] = "sets",
	[JMIR_OP_SETNS] = "setns",
	[JMIR_OP_SETP] = "setp",
	[JMIR_OP_SETNP] = "setnp",
	[JMIR_OP_SETL] = "setl",
	[JMIR_OP_SETNL] = "setnl",
	[JMIR_OP_SETLE] = "setle",
	[JMIR_OP_SETNLE] = "setnle",
	[JMIR_OP_JO] = "jo",
	[JMIR_OP_JNO] = "jno",
	[JMIR_OP_JB] = "jb",
	[JMIR_OP_JNB] = "jnb",
	[JMIR_OP_JE] = "je",
	[JMIR_OP_JNE] = "jne",
	[JMIR_OP_JBE] = "jbe",
	[JMIR_OP_JNBE] = "ja",
	[JMIR_OP_JS] = "js",
	[JMIR_OP_JNS] = "jns",
	[JMIR_OP_JP] = "jp",
	[JMIR_OP_JNP] = "jnp",
	[JMIR_OP_JL] = "jl",
	[JMIR_OP_JNL] = "jge",
	[JMIR_OP_JLE] = "jle",
	[JMIR_OP_JNLE] = "jg",
	[JMIR_OP_MOVSS] = "movss",
	[JMIR_OP_MOVSD] = "movsd",
	[JMIR_OP_MOVAPS] = "movaps",
	[JMIR_OP_MOVAPD] = "movapd",
	[JMIR_OP_MOVD] = "movd",
	[JMIR_OP_MOVQ] = "movq",
	[JMIR_OP_ADDPS] = "addps",
	[JMIR_OP_ADDSS] = "addss",
	[JMIR_OP_ADDPD] = "addpd",
	[JMIR_OP_ADDSD] = "addsd",
	[JMIR_OP_ANDNPS] = "andnps",
	[JMIR_OP_ANDNPD] = "andnpd",
	[JMIR_OP_ANDPS] = "andps",
	[JMIR_OP_ANDPD] = "andpd",
	[JMIR_OP_COMISS] = "comiss",
	[JMIR_OP_COMISD] = "comisd",
	[JMIR_OP_CVTSI2SS] = "cvtsi2ss",
	[JMIR_OP_CVTSI2SD] = "cvtsi2sd",
	[JMIR_OP_CVTSS2SI] = "cvtss2si",
	[JMIR_OP_CVTSD2SI] = "cvtsd2si",
	[JMIR_OP_CVTTSS2SI] = "cvttss2si",
	[JMIR_OP_CVTTSD2SI] = "cvttsd2si",
	[JMIR_OP_CVTSD2SS] = "cvtsd2ss",
	[JMIR_OP_CVTSS2SD] = "cvtss2sd",
	[JMIR_OP_DIVPS] = "divps",
	[JMIR_OP_DIVSS] = "divss",
	[JMIR_OP_DIVPD] = "divpd",
	[JMIR_OP_DIVSD] = "divsd",
	[JMIR_OP_MAXPS] = "maxps",
	[JMIR_OP_MAXSS] = "maxss",
	[JMIR_OP_MAXPD] = "maxpd",
	[JMIR_OP_MAXSD] = "maxsd",
	[JMIR_OP_MINPS] = "minps",
	[JMIR_OP_MINSS] = "minss",
	[JMIR_OP_MINPD] = "minpd",
	[JMIR_OP_MINSD] = "minsd",
	[JMIR_OP_MULPS] = "mulps",
	[JMIR_OP_MULSS] = "mulss",
	[JMIR_OP_MULPD] = "mulpd",
	[JMIR_OP_MULSD] = "mulsd",
	[JMIR_OP_ORPS] = "orps",
	[JMIR_OP_ORPD] = "orps",
	[JMIR_OP_RCPPS] = "rcpps",
	[JMIR_OP_RCPSS] = "rcpss",
	[JMIR_OP_RSQRTPS] = "rsqrtps",
	[JMIR_OP_RSQRTSS] = "rsqrtss",
	[JMIR_OP_SQRTPS] = "sqrtps",
	[JMIR_OP_SQRTSS] = "sqrtss",
	[JMIR_OP_SQRTPD] = "sqrtpd",
	[JMIR_OP_SQRTSD] = "sqrtsd",
	[JMIR_OP_SUBPS] = "subps",
	[JMIR_OP_SUBSS] = "subss",
	[JMIR_OP_SUBPD] = "subpd",
	[JMIR_OP_SUBSD] = "subsd",
	[JMIR_OP_UCOMISS] = "ucomiss",
	[JMIR_OP_UCOMISD] = "ucomisd",
	[JMIR_OP_UNPCKHPS] = "unpckhps",
	[JMIR_OP_UNPCKHPD] = "unpckhpd",
	[JMIR_OP_UNPCKLPS] = "unpcklps",
	[JMIR_OP_UNPCKLPD] = "unpcklpd",
	[JMIR_OP_XORPS] = "xorps",
	[JMIR_OP_XORPD] = "xorpd",
	[JMIR_OP_PUNPCKLBW] = "punpcklbw",
	[JMIR_OP_PUNPCKLWD] = "punpcklwd",
	[JMIR_OP_PUNPCKLDQ] = "punpckldq",
	[JMIR_OP_PUNPCKLQDQ] = "punpcklqdq",
	[JMIR_OP_PUNPCKHBW] = "punpckhbw",
	[JMIR_OP_PUNPCKHWD] = "punpckhwd",
	[JMIR_OP_PUNPCKHDQ] = "punpckhdq",
	[JMIR_OP_PUNPCKHQDQ] = "punpckhqdq",
};

typedef struct jx_mir_frame_info_t
{
	jx_mir_memory_ref_t** m_StackObjArr;
	uint32_t m_MaxCallArgs;
	uint32_t m_Size;
} jx_mir_frame_info_t;

typedef struct jx_mir_context_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_LinearAllocator;
	jx_mir_function_t** m_FuncArr;
	jx_mir_global_variable_t** m_GlobalVarArr;
	jx_mir_function_pass_t* m_FuncPass_removeFallthroughJmp;
	jx_mir_function_pass_t* m_FuncPass_simplifyCondJmp;
	jx_mir_function_pass_t* m_FuncPass_deadCodeElimination;
	jx_mir_function_pass_t* m_FuncPass_peephole;
	jx_mir_function_pass_t* m_FuncPass_regAlloc;
	jx_mir_function_pass_t* m_FuncPass_removeRedundantMoves;
	jx_mir_function_pass_t* m_FuncPass_redundantConstElimination;
	jx_mir_function_pass_t* m_FuncPass_instrCombine;
	jx_mir_function_pass_t* m_FuncPass_simplifyCFG;
	jx_hashmap_t* m_FuncProtoMap;
} jx_mir_context_t;

static jx_mir_operand_t* jmir_operandAlloc(jx_mir_context_t* ctx, jx_mir_operand_kind kind, jx_mir_type_kind type);
static jx_mir_instruction_t* jmir_instrAlloc(jx_mir_context_t* ctx, uint32_t opcode, uint32_t numOperands, jx_mir_operand_t** operands);
static jx_mir_instruction_t* jmir_instrAlloc1(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1);
static jx_mir_instruction_t* jmir_instrAlloc2(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1, jx_mir_operand_t* op2);
static jx_mir_instruction_t* jmir_instrAlloc3(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1, jx_mir_operand_t* op2, jx_mir_operand_t* op3);
static bool jmir_instrUpdateUseDefInfo(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_instruction_t* instr);
static void jmir_regPrint(jx_mir_context_t* ctx, jx_mir_reg_t reg, jx_mir_type_kind type, jx_string_buffer_t* sb);
static jx_mir_operand_t* jmir_funcCreateArgument(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb, uint32_t argID, jx_mir_type_kind argType);
static void jmir_funcFree(jx_mir_context_t* ctx, jx_mir_function_t* func);
static jx_mir_function_pass_t* jmir_funcPassCreate(jx_mir_context_t* ctx, jmirFuncPassCtorFunc ctorFunc, void* passConfig);
static void jmir_funcPassDestroy(jx_mir_context_t* ctx, jx_mir_function_pass_t* pass);
static bool jmir_funcPassApply(jx_mir_context_t* ctx, jx_mir_function_pass_t* pass, jx_mir_function_t* func);
static void jmir_globalVarFree(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv);
static jx_mir_memory_ref_t* jmir_memRefAlloc(jx_mir_context_t* ctx, jx_mir_reg_t baseReg, jx_mir_reg_t indexReg, uint32_t scale, int32_t displacement);
static jx_mir_frame_info_t* jmir_frameCreate(jx_mir_context_t* ctx);
static void jmir_frameDestroy(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo);
static jx_mir_memory_ref_t* jmir_frameAllocObj(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t sz, uint32_t alignment);
static jx_mir_memory_ref_t* jmir_frameObjRel(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, jx_mir_memory_ref_t* baseObj, int32_t offset);
static void jmir_frameMakeRoomForCall(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t numArguments);
static void jmir_frameFinalize(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo);
static uint64_t jmir_funcProtoHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jmir_funcProtoCompareCallback(const void* a, const void* b, void* udata);
static jx_mir_scc_t* jmir_sccAlloc(jx_mir_context_t* ctx);
static void jmir_sccFree(jx_mir_context_t* ctx, jx_mir_scc_t* scc);

jx_mir_context_t* jx_mir_createContext(jx_allocator_i* allocator)
{
	jx_mir_context_t* ctx = (jx_mir_context_t*)JX_ALLOC(allocator, sizeof(jx_mir_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_mir_context_t));
	ctx->m_Allocator = allocator;

	ctx->m_LinearAllocator = allocator_api->createLinearAllocator(256 << 10, allocator);
	if (!ctx->m_LinearAllocator) {
		jx_mir_destroyContext(ctx);
		return NULL;
	}

	ctx->m_FuncArr = (jx_mir_function_t**)jx_array_create(allocator);
	if (!ctx->m_FuncArr) {
		jx_mir_destroyContext(ctx);
		return NULL;
	}

	ctx->m_GlobalVarArr = (jx_mir_global_variable_t**)jx_array_create(allocator);
	if (!ctx->m_GlobalVarArr) {
		jx_mir_destroyContext(ctx);
		return NULL;
	}

	ctx->m_FuncProtoMap = jx_hashmapCreate(ctx->m_Allocator, sizeof(jx_mir_function_proto_t*), 64, 0, 0, jmir_funcProtoHashCallback, jmir_funcProtoCompareCallback, NULL, ctx);
	if (!ctx->m_FuncProtoMap) {
		jx_mir_destroyContext(ctx);
		return NULL;
	}

	// Initialize function passes to be executed when funcEnd is called
	{
		ctx->m_FuncPass_removeFallthroughJmp = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_removeFallthroughJmp, NULL);
		ctx->m_FuncPass_simplifyCondJmp = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_simplifyCondJmp, NULL);
		ctx->m_FuncPass_deadCodeElimination = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_deadCodeElimination, NULL);
		ctx->m_FuncPass_peephole = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_peephole, NULL);
		ctx->m_FuncPass_regAlloc = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_regAlloc, NULL);
		ctx->m_FuncPass_removeRedundantMoves = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_removeRedundantMoves, NULL);
		ctx->m_FuncPass_redundantConstElimination = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_redundantConstElimination, NULL);
		ctx->m_FuncPass_instrCombine = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_instrCombine, NULL);
		ctx->m_FuncPass_simplifyCFG = jmir_funcPassCreate(ctx, jx_mir_funcPassCreate_simplifyCFG, NULL);
	}

	return ctx;
}

void jx_mir_destroyContext(jx_mir_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	// Free function passes
	{
		if (ctx->m_FuncPass_removeFallthroughJmp) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_removeFallthroughJmp);
			ctx->m_FuncPass_removeFallthroughJmp = NULL;
		}

		if (ctx->m_FuncPass_simplifyCondJmp) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_simplifyCondJmp);
			ctx->m_FuncPass_simplifyCondJmp = NULL;
		}
		
		if (ctx->m_FuncPass_deadCodeElimination) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_deadCodeElimination);
			ctx->m_FuncPass_deadCodeElimination = NULL;
		}

		if (ctx->m_FuncPass_peephole) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_peephole);
			ctx->m_FuncPass_peephole = NULL;
		}

		if (ctx->m_FuncPass_regAlloc) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_regAlloc);
			ctx->m_FuncPass_regAlloc = NULL;
		}

		if (ctx->m_FuncPass_removeRedundantMoves) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_removeRedundantMoves);
			ctx->m_FuncPass_removeRedundantMoves = NULL;
		}

		if (ctx->m_FuncPass_redundantConstElimination) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_redundantConstElimination);
			ctx->m_FuncPass_redundantConstElimination = NULL;
		}

		if (ctx->m_FuncPass_instrCombine) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_instrCombine);
			ctx->m_FuncPass_instrCombine = NULL;
		}

		if (ctx->m_FuncPass_simplifyCFG) {
			jmir_funcPassDestroy(ctx, ctx->m_FuncPass_simplifyCFG);
			ctx->m_FuncPass_simplifyCFG = NULL;
		}
	}

	const uint32_t numGlobalVars = (uint32_t)jx_array_sizeu(ctx->m_GlobalVarArr);
	for (uint32_t iGV = 0; iGV < numGlobalVars; ++iGV) {
		jmir_globalVarFree(ctx, ctx->m_GlobalVarArr[iGV]);
	}
	jx_array_free(ctx->m_GlobalVarArr);

	const uint32_t numFuncs = (uint32_t)jx_array_sizeu(ctx->m_FuncArr);
	for (uint32_t iFunc = 0; iFunc < numFuncs; ++iFunc) {
		jmir_funcFree(ctx, ctx->m_FuncArr[iFunc]);
	}
	jx_array_free(ctx->m_FuncArr);

	if (ctx->m_FuncProtoMap) {
		jx_hashmapDestroy(ctx->m_FuncProtoMap);
		ctx->m_FuncProtoMap = NULL;
	}

	if (ctx->m_LinearAllocator) {
		allocator_api->destroyLinearAllocator(ctx->m_LinearAllocator);
		ctx->m_LinearAllocator = NULL;
	}

	JX_FREE(allocator, ctx);
}

void jx_mir_print(jx_mir_context_t* ctx, jx_string_buffer_t* sb)
{
	const uint32_t numFunctions = (uint32_t)jx_array_sizeu(ctx->m_FuncArr);
	for (uint32_t iFunc = 0; iFunc < numFunctions; ++iFunc) {
		jx_mir_funcPrint(ctx, ctx->m_FuncArr[iFunc], sb);
		jx_strbuf_pushCStr(sb, "\n");
	}
}

uint32_t jx_mir_getNumGlobalVars(jx_mir_context_t* ctx)
{
	return (uint32_t)jx_array_sizeu(ctx->m_GlobalVarArr);
}

jx_mir_global_variable_t* jx_mir_getGlobalVarByID(jx_mir_context_t* ctx, uint32_t id)
{
	return ctx->m_GlobalVarArr[id];
}

jx_mir_global_variable_t* jx_mir_getGlobalVarByName(jx_mir_context_t* ctx, const char* name)
{
	const uint32_t numGlobalVariables = (uint32_t)jx_array_sizeu(ctx->m_GlobalVarArr);
	for (uint32_t iGV = 0; iGV < numGlobalVariables; ++iGV) {
		jx_mir_global_variable_t* gv = ctx->m_GlobalVarArr[iGV];
		if (!jx_strcmp(gv->m_Name, name)) {
			return gv;
		}
	}

	return NULL;
}

uint32_t jx_mir_getNumFunctions(jx_mir_context_t* ctx)
{
	return (uint32_t)jx_array_sizeu(ctx->m_FuncArr);
}

jx_mir_function_t* jx_mir_getFunctionByID(jx_mir_context_t* ctx, uint32_t id)
{
	return ctx->m_FuncArr[id];
}

jx_mir_function_t* jx_mir_getFunctionByName(jx_mir_context_t* ctx, const char* name)
{
	const uint32_t numFunctions = (uint32_t)jx_array_sizeu(ctx->m_FuncArr);
	for (uint32_t iFunc = 0; iFunc < numFunctions; ++iFunc) {
		jx_mir_function_t* func = ctx->m_FuncArr[iFunc];
		if (!jx_strcmp(func->m_Name, name)) {
			return func;
		}
	}

	return NULL;
}

jx_mir_global_variable_t* jx_mir_globalVarBegin(jx_mir_context_t* ctx, const char* name, uint32_t alignment)
{
	jx_mir_global_variable_t* gv = (jx_mir_global_variable_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_global_variable_t));
	if (!gv) {
		return NULL;
	}

	jx_memset(gv, 0, sizeof(jx_mir_global_variable_t));
	gv->m_Alignment = alignment;
	gv->m_Name = jx_strdup(name, ctx->m_LinearAllocator);
	if (!gv->m_Name) {
		jmir_globalVarFree(ctx, gv);
		return NULL;
	}

	gv->m_DataArr = (uint8_t*)jx_array_create(ctx->m_Allocator);
	if (!gv->m_DataArr) {
		jmir_globalVarFree(ctx, gv);
		return NULL;
	}

	gv->m_RelocationsArr = (jx_mir_relocation_t*)jx_array_create(ctx->m_Allocator);
	if (!gv->m_RelocationsArr) {
		jmir_globalVarFree(ctx, gv);
		return NULL;
	}

	jx_array_push_back(ctx->m_GlobalVarArr, gv);

	return gv;
}

void jx_mir_globalVarEnd(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv)
{
	JX_CHECK(jx_array_sizeu(gv->m_DataArr) > 0, "Empty global variable?");
}

uint32_t jx_mir_globalVarAppendData(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv, const uint8_t* data, uint32_t sz)
{
	const uint32_t offset = (uint32_t)jx_array_sizeu(gv->m_DataArr);
	uint8_t* dst = jx_array_addnptr(gv->m_DataArr, sz);
	if (!dst) {
		return UINT32_MAX;
	}

	jx_memcpy(dst, data, sz);

	return offset;
}

void jx_mir_globalVarAddRelocation(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv, uint32_t dataOffset, const char* symbolName)
{
	jx_array_push_back(gv->m_RelocationsArr, (jx_mir_relocation_t){
		.m_SymbolName = jx_strdup(symbolName, ctx->m_LinearAllocator),
		.m_Offset = dataOffset
	});
}

jx_mir_function_proto_t* jx_mir_funcProto(jx_mir_context_t* ctx, jx_mir_type_kind retType, uint32_t numArgs, jx_mir_type_kind* args, uint32_t flags)
{
	jx_mir_function_proto_t* key = &(jx_mir_function_proto_t){
		.m_RetType = retType,
		.m_NumArgs = numArgs,
		.m_Args = args,
		.m_Flags = flags
	};

	jx_mir_function_proto_t** cachedProto = (jx_mir_function_proto_t**)jx_hashmapGet(ctx->m_FuncProtoMap, &key);
	if (cachedProto) {
		return *cachedProto;
	}

	jx_mir_function_proto_t* proto = (jx_mir_function_proto_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_function_proto_t));
	if (!proto) {
		return NULL;
	}

	jx_memset(proto, 0, sizeof(jx_mir_function_proto_t));
	proto->m_RetType = retType;
	proto->m_NumArgs = numArgs;
	proto->m_Flags = flags;

	if (numArgs) {
		proto->m_Args = (jx_mir_type_kind*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_type_kind) * numArgs);
		if (!proto->m_Args) {
			return NULL;
		}

		jx_memcpy(proto->m_Args, args, sizeof(jx_mir_type_kind) * numArgs);
	}

	jx_hashmapSet(ctx->m_FuncProtoMap, &proto);

	return proto;

}

jx_mir_function_t* jx_mir_funcBegin(jx_mir_context_t* ctx, const char* name, jx_mir_function_proto_t* proto)
{
	jx_mir_function_t* func = (jx_mir_function_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_function_t));
	if (!func) {
		return NULL;
	}

	jx_memset(func, 0, sizeof(jx_mir_function_t));
	func->m_Name = jx_strdup(name, ctx->m_LinearAllocator);
	func->m_Prototype = proto;

	jx_mir_basic_block_t* entryBlock = jx_mir_bbAlloc(ctx);

	const uint32_t numArgs = proto->m_NumArgs;
	if (numArgs) {
		func->m_Args = (jx_mir_operand_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_operand_t*) * numArgs);
		if (!func->m_Args) {
			return NULL;
		}

		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			func->m_Args[iArg] = jmir_funcCreateArgument(ctx, func, entryBlock, iArg, proto->m_Args[iArg]);
			if (!func->m_Args[iArg]) {
				return NULL;
			}
		}
	}

	if ((proto->m_Flags & JMIR_FUNC_PROTO_FLAGS_EXTERNAL_Msk) == 0) {
		jx_mir_funcAppendBasicBlock(ctx, func, entryBlock);

		func->m_FrameInfo = jmir_frameCreate(ctx);
		if (!func->m_FrameInfo) {
			return NULL;
		}
	} else {
		jx_mir_bbFree(ctx, entryBlock);
	}

	jx_array_push_back(ctx->m_FuncArr, func);

	return func;
}

void jx_mir_funcEnd(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	if (!func->m_BasicBlockListHead) {
		JX_CHECK((func->m_Prototype->m_Flags & JMIR_FUNC_PROTO_FLAGS_EXTERNAL_Msk) != 0, "Internal function without body?");
		return;
	}

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(ctx->m_Allocator);
		jx_mir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif
	
	jx_mir_funcUpdateCFG(ctx, func);

	{
		jmir_funcPassApply(ctx, ctx->m_FuncPass_removeFallthroughJmp, func);
		jmir_funcPassApply(ctx, ctx->m_FuncPass_simplifyCondJmp, func);
		jmir_funcPassApply(ctx, ctx->m_FuncPass_simplifyCFG, func);

#if 0
		{
			jx_string_buffer_t* sb = jx_strbuf_create(ctx->m_Allocator);
			jx_strbuf_printf(sb, "%s pre-instrCombine\n", func->m_Name);
			jx_mir_funcPrint(ctx, func, sb);
			jx_strbuf_nullTerminate(sb);
			JX_SYS_LOG_INFO(NULL, "\n%s", jx_strbuf_getString(sb, NULL));
			jx_strbuf_destroy(sb);
		}
#endif

		uint32_t numIter = 0;
		bool changed = true;
		while (changed && numIter < 5) {
			changed = jmir_funcPassApply(ctx, ctx->m_FuncPass_instrCombine, func);
			changed = jmir_funcPassApply(ctx, ctx->m_FuncPass_deadCodeElimination, func) || changed;
			changed = jmir_funcPassApply(ctx, ctx->m_FuncPass_peephole, func) || changed;
			changed = jmir_funcPassApply(ctx, ctx->m_FuncPass_deadCodeElimination, func) || changed;
			++numIter;
		}

#if 0
		{
			jx_mir_funcRenumberVirtualRegs(ctx, func);
			jx_string_buffer_t* sb = jx_strbuf_create(ctx->m_Allocator);
			jx_strbuf_printf(sb, "%s pre-RA\n", func->m_Name);
			jx_mir_funcPrint(ctx, func, sb);
			jx_strbuf_nullTerminate(sb);
			JX_SYS_LOG_INFO(NULL, "\n%s", jx_strbuf_getString(sb, NULL));
			jx_strbuf_destroy(sb);
		}
#endif

		jmir_funcPassApply(ctx, ctx->m_FuncPass_regAlloc, func);
		jmir_funcPassApply(ctx, ctx->m_FuncPass_removeRedundantMoves, func);
		jmir_funcPassApply(ctx, ctx->m_FuncPass_redundantConstElimination, func);

#if 0
		{
			jx_string_buffer_t* sb = jx_strbuf_create(ctx->m_Allocator);
			jx_strbuf_printf(sb, "%s post-RA\n", func->m_Name);
			jx_mir_funcPrint(ctx, func, sb);
			jx_strbuf_nullTerminate(sb);
			JX_SYS_LOG_INFO(NULL, "\n%s", jx_strbuf_getString(sb, NULL));
			jx_strbuf_destroy(sb);
		}
#endif

		jmir_funcPassApply(ctx, ctx->m_FuncPass_simplifyCondJmp, func);
		
		changed = true;
		while (changed) {
			changed = jmir_funcPassApply(ctx, ctx->m_FuncPass_peephole, func);
			changed = jmir_funcPassApply(ctx, ctx->m_FuncPass_deadCodeElimination, func) || changed;
		}
	}

	// Store all callee-saved registers used by the function on the stack.
	jx_mir_operand_t* gpRegStackSlot[JX_COUNTOF(kMIRFuncCalleeSavedIReg)] = { 0 };
	jx_mir_operand_t* xmmRegStackSlot[JX_COUNTOF(kMIRFuncCalleeSavedFReg)] = { 0 };
	for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedIReg); ++iReg) {
		jx_mir_reg_t reg = kMIRFuncCalleeSavedIReg[iReg];
		if ((func->m_UsedHWRegs[JMIR_REG_CLASS_GP] & (1u << reg.m_ID)) != 0) {
			jx_mir_operand_t* stackSlot = jx_mir_opStackObj(ctx, func, JMIR_TYPE_I64, 8, 8);
			jx_mir_bbPrependInstr(ctx, func->m_BasicBlockListHead, jx_mir_mov(ctx, stackSlot, jx_mir_opHWReg(ctx, func, JMIR_TYPE_I64, reg)));
			gpRegStackSlot[iReg] = stackSlot;
		}
	}
	for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedFReg); ++iReg) {
		jx_mir_reg_t reg = kMIRFuncCalleeSavedFReg[iReg];
		if ((func->m_UsedHWRegs[JMIR_REG_CLASS_XMM] & (1u << reg.m_ID)) != 0) {
			jx_mir_operand_t* stackSlot = jx_mir_opStackObj(ctx, func, JMIR_TYPE_F128, 16, 16);
			jx_mir_bbPrependInstr(ctx, func->m_BasicBlockListHead, jx_mir_movaps(ctx, stackSlot, jx_mir_opHWReg(ctx, func, JMIR_TYPE_F128, reg)));
			xmmRegStackSlot[iReg] = stackSlot;
		}
	}

	// Insert prologue/epilogue
	jx_mir_frame_info_t* frameInfo = func->m_FrameInfo;
	jmir_frameFinalize(ctx, frameInfo);

	{
		// NOTE: Prepend prologue instructions in reverse order.
		jx_mir_basic_block_t* entryBlock = func->m_BasicBlockListHead;
		if ((func->m_Prototype->m_Flags & JMIR_FUNC_PROTO_FLAGS_VARARG_Msk) != 0) {
			// Store register operands into their shadow space.
			for (uint32_t iArgReg = 0; iArgReg < JX_COUNTOF(kMIRFuncArgIReg); ++iArgReg) {
#if 1
				jx_mir_operand_t* shadowSpaceSlot = jx_mir_opMemoryRef(ctx, func, JMIR_TYPE_I64, kMIRRegGP_BP, kMIRRegGPNone, 1, 16 + iArgReg * 8);
#else
				// NOTE: Use RSP instead of RBP. There is no reason to keep this, but leave it here in case I need it in the future.
				jx_mir_operand_t* shadowSpaceSlot = jx_mir_opMemoryRef(ctx, func, JMIR_TYPE_I64, kMIRRegGP_SP, kMIRRegGPNone, 1, frameInfo->m_Size + 16 + iArgReg * 8);
#endif
				jx_mir_bbPrependInstr(ctx, entryBlock, jx_mir_mov(ctx, shadowSpaceSlot, jx_mir_opHWReg(ctx, func, JMIR_TYPE_I64, kMIRFuncArgIReg[iArgReg])));
			}
		}

		if (frameInfo->m_Size != 0) {
			jx_mir_bbPrependInstr(ctx, entryBlock, jx_mir_sub(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, kMIRRegGP_SP), jx_mir_opIConst(ctx, func, JMIR_TYPE_I32, (int64_t)frameInfo->m_Size)));
		}
		jx_mir_bbPrependInstr(ctx, entryBlock, jx_mir_mov(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, kMIRRegGP_BP), jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, kMIRRegGP_SP)));
		jx_mir_bbPrependInstr(ctx, entryBlock, jx_mir_push(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, kMIRRegGP_BP)));

		// Add epilogue code to each exit block.
		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_mir_instruction_t* firstTerminator = jx_mir_bbGetFirstTerminatorInstr(ctx, bb);
			if (firstTerminator && firstTerminator->m_OpCode == JMIR_OP_RET) {
				JX_CHECK(!firstTerminator->m_Next, "Unexpected instruction after ret!");

				// Restore all callee-saved GP registers from the stack.
				for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedIReg); ++iReg) {
					if (gpRegStackSlot[iReg]) {
						jx_mir_reg_t reg = kMIRFuncCalleeSavedIReg[iReg];
						jx_mir_bbInsertInstrBefore(ctx, bb, firstTerminator, jx_mir_mov(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_I64, reg), gpRegStackSlot[iReg]));
					}
				}

				// Restore all callee-saved FP registers from the stack.
				for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedFReg); ++iReg) {
					if (xmmRegStackSlot[iReg]) {
						jx_mir_reg_t reg = kMIRFuncCalleeSavedFReg[iReg];
						jx_mir_bbInsertInstrBefore(ctx, bb, firstTerminator, jx_mir_movaps(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_F128, reg), xmmRegStackSlot[iReg]));
					}
				}

				jx_mir_bbInsertInstrBefore(ctx, bb, firstTerminator, jx_mir_mov(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, kMIRRegGP_SP), jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, kMIRRegGP_BP)));
				jx_mir_bbInsertInstrBefore(ctx, bb, firstTerminator, jx_mir_pop(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, kMIRRegGP_BP)));
			}

			bb = bb->m_Next;
		}
	}

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(ctx->m_Allocator);
		jx_mir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif
}

jx_mir_operand_t* jx_mir_funcGetArgument(jx_mir_context_t* ctx, jx_mir_function_t* func, uint32_t argID)
{
	if (argID >= func->m_Prototype->m_NumArgs) {
		JX_CHECK(false, "Invalid argument ID");
		return NULL;
	}

	return func->m_Args[argID];
}

void jx_mir_funcAppendBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb)
{
	JX_UNUSED(ctx);
	JX_CHECK(!bb->m_ParentFunc && !bb->m_Next && !bb->m_Prev, "Basic block already part of a function?");

	bb->m_ParentFunc = func;
	bb->m_ID = func->m_NextBasicBlockID++;

	if (!func->m_BasicBlockListHead) {
		JX_CHECK(!func->m_BasicBlockListTail, "Invalid linked-list state");
		func->m_BasicBlockListHead = bb;
	} else {
		JX_CHECK(func->m_BasicBlockListTail, "Invalid linked-list state");
		func->m_BasicBlockListTail->m_Next = bb;
		bb->m_Prev = func->m_BasicBlockListTail;
	}

	func->m_BasicBlockListTail = bb;

	func->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);
}

void jx_mir_funcPrependBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb)
{
	JX_UNUSED(ctx);
	JX_CHECK(!bb->m_ParentFunc && !bb->m_Next && !bb->m_Prev, "Basic block already part of a function?");

	bb->m_ParentFunc = func;
	bb->m_ID = func->m_NextBasicBlockID++;

	bb->m_Next = func->m_BasicBlockListHead;

	if (func->m_BasicBlockListHead) {
		func->m_BasicBlockListHead->m_Prev = bb;
	} else {
		func->m_BasicBlockListTail = bb;
	}
	func->m_BasicBlockListHead = bb;

	func->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);
}

bool jx_mir_funcRemoveBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb)
{
	JX_UNUSED(ctx);

	if (bb->m_ParentFunc != func) {
		JX_CHECK(false, "Trying to remove basic block from another function.");
		return false;
	}

	if (func->m_BasicBlockListHead == bb) {
		func->m_BasicBlockListHead = bb->m_Next;
	}
	if (func->m_BasicBlockListTail == bb) {
		func->m_BasicBlockListTail = bb->m_Prev;
	}

	if (bb->m_Prev) {
		bb->m_Prev->m_Next = bb->m_Next;
	}
	if (bb->m_Next) {
		bb->m_Next->m_Prev = bb->m_Prev;
	}

	bb->m_ParentFunc = NULL;
	bb->m_Prev = NULL;
	bb->m_Next = NULL;
	bb->m_ID = UINT32_MAX;

	func->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);

	return true;
}

void jx_mir_funcAllocStackForCall(jx_mir_context_t* ctx, jx_mir_function_t* func, uint32_t numArguments)
{
	jmir_frameMakeRoomForCall(ctx, func->m_FrameInfo, numArguments);
}

bool jx_mir_funcUpdateCFG(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	if ((func->m_Flags & JMIR_FUNC_FLAGS_CFG_VALID_Msk) != 0) {
		return true;
	}

	TracyCZoneN(tracyCtx, "mir: Update CFG", 1);

	// Make sure every basic block has a CFG annotation and reset its pred/succ arrays
	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_array_resize(bb->m_PredArr, 0);
		jx_array_resize(bb->m_SuccArr, 0);

		bb = bb->m_Next;
	}

	// Build the CFG
	bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = jx_mir_bbGetFirstTerminatorInstr(ctx, bb);
		bool fallthroughToNextBlock = true;
		bool retFound = false;
		while (instr) {
			if (instr->m_OpCode == JMIR_OP_JMP || jx_mir_opcodeIsJcc(instr->m_OpCode)) {
				JX_CHECK(!retFound && fallthroughToNextBlock, "Already found ret or jmp instruction. Did not expect more instructions!");

				// Conditional or unconditional jump
				jx_mir_operand_t* targetOperand = instr->m_Operands[0];
				if (targetOperand->m_Kind == JMIR_OPERAND_BASIC_BLOCK) {
					jx_mir_basic_block_t* targetBB = targetOperand->u.m_BB;
					jx_array_push_back(bb->m_SuccArr, targetBB);
					jx_array_push_back(targetBB->m_PredArr, bb);
				} else {
					// TODO: What should I do in this case? I should probably add all func's basic blocks
					// as successors to this block, since it's hard (impossible?) to know all potential targets.
					// 
					// Alternatively, I can move the pred/succ arrays into jx_mir_basic_block_t and build the CFG
					// during IR-to-Asm translation (mir_gen).
					// 
					// Currently this cannot happen (afair) because mir_gen always uses basic blocks 
					// as jump targets.
					JX_NOT_IMPLEMENTED();
				}

				fallthroughToNextBlock = instr->m_OpCode != JMIR_OP_JMP;
			} else if (instr->m_OpCode == JMIR_OP_RET) {
				// Return.
				fallthroughToNextBlock = false;
				retFound = true;
			} else {
				JX_CHECK(false, "Expected only terminator instructions after first terminator!");
			}

			instr = instr->m_Next;
		}

		if (fallthroughToNextBlock) {
			JX_CHECK(bb->m_Next, "Trying to fallthrough to the next block but there is no next block!");
			jx_mir_basic_block_t* targetBB = bb->m_Next;
			jx_array_push_back(bb->m_SuccArr, targetBB);
			jx_array_push_back(targetBB->m_PredArr, bb);
		}

		bb = bb->m_Next;
	}

	func->m_Flags |= JMIR_FUNC_FLAGS_CFG_VALID_Msk;

	TracyCZoneEnd(tracyCtx);

	return true;
}

bool jx_mir_funcRenumberVirtualRegs(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	TracyCZoneN(tracyCtx, "mir: Renumber VRegs", 1);

	uint32_t numOldVRegs[JMIR_REG_CLASS_COUNT];
	jx_memcpy(numOldVRegs, func->m_NextVirtualRegID, sizeof(uint32_t) * JMIR_REG_CLASS_COUNT);

	uint32_t* oldToNewMap[JMIR_REG_CLASS_COUNT] = { 0 };
	for (uint32_t iRegClass = 0; iRegClass < JMIR_REG_CLASS_COUNT; ++iRegClass) {
		if (numOldVRegs[iRegClass]) {
			oldToNewMap[iRegClass] = (uint32_t*)JX_ALLOC(ctx->m_Allocator, sizeof(uint32_t) * numOldVRegs[iRegClass]);
			if (!oldToNewMap[iRegClass]) {
				TracyCZoneEnd(tracyCtx);
				return false;
			}

			jx_memset(oldToNewMap[iRegClass], 0xFF, sizeof(uint32_t) * numOldVRegs[iRegClass]);
		}
	}

	uint32_t numNewVRegs[JMIR_REG_CLASS_COUNT] = { 0 };

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jmir_instrUpdateUseDefInfo(ctx, func, instr);

			const uint32_t numDefs = instr->m_UseDef.m_NumDefs;
			for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
				jx_mir_reg_t def = instr->m_UseDef.m_Defs[iDef];
				if (jx_mir_regIsVirtual(def)) {
					JX_CHECK(def.m_ID < numOldVRegs[def.m_Class], "Invalid register ID!");
					if (oldToNewMap[def.m_Class][def.m_ID] == UINT32_MAX) {
						oldToNewMap[def.m_Class][def.m_ID] = numNewVRegs[def.m_Class]++;
					}
				}
			}

			instr = instr->m_Next;
		}

		bb = bb->m_Next;
	}

#if 0
	// DEBUG
	// Make sure all used regs have valid IDs
	bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			const uint32_t numUses = instr->m_UseDef.m_NumUses;
			for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
				jx_mir_reg_t use = instr->m_UseDef.m_Uses[iUse];
				if (jx_mir_regIsVirtual(use)) {
					JX_CHECK(use.m_ID < numOldVRegs[use.m_Class], "Invalid register ID!");
					JX_CHECK(oldToNewMap[use.m_Class][use.m_ID] != UINT32_MAX, "Instruction uses an undefined virtual register!");
				}
			}

			instr = instr->m_Next;
		}

		bb = bb->m_Next;
	}
#endif

	bool numberingChanged = false;
	for (uint32_t iRegClass = 0; iRegClass < JMIR_REG_CLASS_COUNT; ++iRegClass) {
		if (numOldVRegs[iRegClass] != numNewVRegs[iRegClass]) {
			numberingChanged = true;
			break;
		}
	}

	if (numberingChanged) {
		// Update all register/memRef operands to use the new vreg numbering
		// NOTE: Because operands might be shared between instructions, the initial
		// pass maps new vregs to IDs above the old number of vregs and the second
		// pass subtracts the old number of vregs from each such operand.
		bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				const uint32_t numOperands = instr->m_NumOperands;
				for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
					jx_mir_operand_t* operand = instr->m_Operands[iOperand];
					if (operand->m_Kind == JMIR_OPERAND_REGISTER) {
						{
							jx_mir_reg_t reg = operand->u.m_Reg;
							if (jx_mir_regIsVirtual(reg) && reg.m_ID < numOldVRegs[reg.m_Class]) {
								JX_CHECK(oldToNewMap[reg.m_Class][reg.m_ID] != UINT32_MAX, "Invalid mapping!");
								operand->u.m_Reg.m_ID = numOldVRegs[reg.m_Class] + oldToNewMap[reg.m_Class][reg.m_ID];
							}
						}
					} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
						jx_mir_memory_ref_t* memRef = operand->u.m_MemRef;
						if (jx_mir_regIsValid(memRef->m_BaseReg) && jx_mir_regIsVirtual(memRef->m_BaseReg)) {
							jx_mir_reg_t reg = memRef->m_BaseReg;
							if (jx_mir_regIsVirtual(reg) && reg.m_ID < numOldVRegs[reg.m_Class]) {
								JX_CHECK(oldToNewMap[reg.m_Class][reg.m_ID] != UINT32_MAX, "Invalid mapping!");
								memRef->m_BaseReg.m_ID = numOldVRegs[reg.m_Class] + oldToNewMap[reg.m_Class][reg.m_ID];
							}
						}
						if (jx_mir_regIsValid(memRef->m_IndexReg) && jx_mir_regIsVirtual(memRef->m_IndexReg)) {
							jx_mir_reg_t reg = memRef->m_IndexReg;
							if (jx_mir_regIsVirtual(reg) && reg.m_ID < numOldVRegs[reg.m_Class]) {
								JX_CHECK(oldToNewMap[reg.m_Class][reg.m_ID] != UINT32_MAX, "Invalid mapping!");
								memRef->m_IndexReg.m_ID = numOldVRegs[reg.m_Class] + oldToNewMap[reg.m_Class][reg.m_ID];
							}
						}
					}
				}

				instr = instr->m_Next;
			}

			bb = bb->m_Next;
		}

		// Subtract deltas from all vregs (final mapping)
		bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				const uint32_t numOperands = instr->m_NumOperands;
				for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
					jx_mir_operand_t* operand = instr->m_Operands[iOperand];
					if (operand->m_Kind == JMIR_OPERAND_REGISTER) {
						{
							jx_mir_reg_t reg = operand->u.m_Reg;
							if (jx_mir_regIsVirtual(reg) && reg.m_ID >= numOldVRegs[reg.m_Class]) {
								operand->u.m_Reg.m_ID -= numOldVRegs[reg.m_Class];
							}
						}
					} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
						jx_mir_memory_ref_t* memRef = operand->u.m_MemRef;
						if (jx_mir_regIsValid(memRef->m_BaseReg) && jx_mir_regIsVirtual(memRef->m_BaseReg)) {
							jx_mir_reg_t reg = memRef->m_BaseReg;
							if (jx_mir_regIsVirtual(reg) && reg.m_ID >= numOldVRegs[reg.m_Class]) {
								memRef->m_BaseReg.m_ID -= numOldVRegs[reg.m_Class];
							}
						}
						if (jx_mir_regIsValid(memRef->m_IndexReg) && jx_mir_regIsVirtual(memRef->m_IndexReg)) {
							jx_mir_reg_t reg = memRef->m_IndexReg;
							if (jx_mir_regIsVirtual(reg) && reg.m_ID >= numOldVRegs[reg.m_Class]) {
								memRef->m_IndexReg.m_ID -= numOldVRegs[reg.m_Class];
							}
						}
					}
				}

				instr = instr->m_Next;
			}

			bb = bb->m_Next;
		}

		jx_memcpy(func->m_NextVirtualRegID, numNewVRegs, sizeof(uint32_t)* JMIR_REG_CLASS_COUNT);
	}

	for (uint32_t iRegClass = 0; iRegClass < JMIR_REG_CLASS_COUNT; ++iRegClass) {
		JX_FREE(ctx->m_Allocator, oldToNewMap[iRegClass]);
	}

	TracyCZoneEnd(tracyCtx);

	return true;
}

bool jx_mir_funcUpdateLiveness(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
//	if ((func->m_Flags & JMIR_FUNC_FLAGS_LIVENESS_VALID_Msk) != 0) {
//		return true;
//	}

	TracyCZoneN(tracyCtx, "mir: Update Liveness", 1);

	if (!jx_mir_funcUpdateCFG(ctx, func)) {
		return false;
	}

	// Make sure every basic block has a liveness annotation and reset its bitsets
	const uint32_t numRegs = jx_mir_funcGetRegBitsetSize(ctx, func);
	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_bitsetResize(&bb->m_LiveInSet, numRegs, ctx->m_Allocator);
		jx_bitsetResize(&bb->m_LiveOutSet, numRegs, ctx->m_Allocator);

		jx_bitsetClear(&bb->m_LiveInSet);
		jx_bitsetClear(&bb->m_LiveOutSet);

		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_bitsetResize(&instr->m_LiveOutSet, numRegs, ctx->m_Allocator);

			// Recalculate use/def info
			jmir_instrUpdateUseDefInfo(ctx, func, instr);

			instr = instr->m_Next;
		}

		bb = bb->m_Next;
	}

	const uint64_t bitsetBufferSz = jx_bitsetCalcBufferSize(numRegs);
	uint8_t* tempBitsetBuffer = (uint8_t*)JX_ALLOC(ctx->m_Allocator, bitsetBufferSz * 2);

	jx_bitset_t prevLiveIn = {
		.m_Bits = (uint64_t*)tempBitsetBuffer,
		.m_NumBits = numRegs,
		.m_BitCapacity = 0
	};
	jx_bitset_t instrLive = {
		.m_Bits = (uint64_t*)(tempBitsetBuffer + bitsetBufferSz),
		.m_NumBits = numRegs,
		.m_BitCapacity = 0
	};

	uint32_t numIterations = 0;
	// Rebuild live in/out sets
	bool changed = true;
	while (changed) {
		changed = false;
		++numIterations;

		bb = func->m_BasicBlockListTail;
		while (bb) {
			// out'[v] = out[v]
			// in'[v] = in[v]
			jx_bitsetCopy(&prevLiveIn, &bb->m_LiveInSet);

			// out[v] = Union(w in succ, in[w])
			const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
			if (numSucc) {
				jx_bitsetCopy(&bb->m_LiveOutSet, &bb->m_SuccArr[0]->m_LiveInSet);
				for (uint32_t iSucc = 1; iSucc < numSucc; ++iSucc) {
					jx_bitsetUnion(&bb->m_LiveOutSet, &bb->m_SuccArr[iSucc]->m_LiveInSet);
				}
			}

			// Calculate live in by walking the basic block's instruction list backwards,
			// while storing live info for each instruction.
			{
				jx_bitsetCopy(&instrLive, &bb->m_LiveOutSet);

				jx_mir_instruction_t* instr = bb->m_InstrListTail;
				while (instr) {
					jx_mir_instr_usedef_t* instrUseDefAnnot = &instr->m_UseDef;
					jx_bitset_t* instrLiveOutSet = &instr->m_LiveOutSet;

#if 0
					if (jx_mir_instrIsMovRegReg(instr)) {
						JX_CHECK(instrUseDefAnnot->m_NumUses == 1, "Move instruction expected to have 1 use.");
						jx_bitsetResetBit(&instrLive, jx_mir_funcMapRegToBitsetID(ctx, func, instrUseDefAnnot->m_Uses[0]));
					}
#endif

					jx_bitsetCopy(instrLiveOutSet, &instrLive);

					const uint32_t numDefs = instrUseDefAnnot->m_NumDefs;
					for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
						jx_bitsetResetBit(&instrLive, jx_mir_funcMapRegToBitsetID(ctx, func, instrUseDefAnnot->m_Defs[iDef]));
					}

					const uint32_t numUses = instrUseDefAnnot->m_NumUses;
					for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
						jx_bitsetSetBit(&instrLive, jx_mir_funcMapRegToBitsetID(ctx, func, instrUseDefAnnot->m_Uses[iUse]));
					}

					instr = instr->m_Prev;
				}

				jx_bitsetCopy(&bb->m_LiveInSet, &instrLive);
			}

			// Check if something changed
			changed = changed
				|| !jx_bitsetEqual(&bb->m_LiveInSet, &prevLiveIn)
				;

			bb = bb->m_Prev;
		}
	}

//	JX_TRACE("liveness: Func %s iterations %u", func->m_Name, numIterations);

	JX_FREE(ctx->m_Allocator, tempBitsetBuffer);

	func->m_Flags |= JMIR_FUNC_FLAGS_LIVENESS_VALID_Msk;

	TracyCZoneEnd(tracyCtx);

	return true;
}

typedef struct jmir_scc_list_t
{
	jx_mir_scc_t* m_Head;
	jx_mir_scc_t* m_Tail;
} jmir_scc_list_t;

typedef struct jmir_scc_tarjan_state_t
{
	jx_mir_basic_block_t** m_Stack;
	uint32_t m_NextIndex;
	JX_PAD(4);
} jmir_scc_tarjan_state_t;

static bool jmir_funcStrongConnect(jx_mir_context_t* ctx, jmir_scc_list_t* sccList, jmir_scc_tarjan_state_t* sccState, jx_mir_basic_block_t* bb)
{
	const uint32_t id = sccState->m_NextIndex++;
	bb->m_SCCInfo.m_ID = id;
	bb->m_SCCInfo.m_LowLink = id;

	jx_array_push_back(sccState->m_Stack, bb);
	bb->m_SCCInfo.m_OnStack = true;

	const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
	for (uint32_t iSucc = 0; iSucc < numSucc; ++iSucc) {
		jx_mir_basic_block_t* succ = bb->m_SuccArr[iSucc];
		if (succ->m_SCCInfo.m_ID == UINT32_MAX) {
			// Successor w has not yet been visited; recurse on it
			jmir_funcStrongConnect(ctx, sccList, sccState, succ);
			bb->m_SCCInfo.m_LowLink = jx_min_u32(bb->m_SCCInfo.m_LowLink, succ->m_SCCInfo.m_LowLink);
		} else if (succ->m_SCCInfo.m_OnStack) {
			// Successor w is in stack S and hence in the current SCC
			bb->m_SCCInfo.m_LowLink = jx_min_u32(bb->m_SCCInfo.m_LowLink, succ->m_SCCInfo.m_ID);
		} else {
			// If w is not on stack, then (v, w) is an edge pointing to 
			// an SCC already found and must be ignored
		}
	}

	// If v is a root node, pop the stack and generate an SCC
	if (bb->m_SCCInfo.m_LowLink == bb->m_SCCInfo.m_ID) {
		jx_mir_scc_t* scc = jmir_sccAlloc(ctx);
		if (!scc) {
			return false;
		}

		jx_mir_basic_block_t* stackBB = NULL;
		do {
			stackBB = jx_array_pop_back(sccState->m_Stack);
			stackBB->m_SCCInfo.m_OnStack = false;
			stackBB->m_SCCInfo.m_SCC = scc;
			jx_array_push_back(scc->m_BasicBlockArr, stackBB);
		} while (stackBB != bb);

		if (!sccList->m_Head) {
			sccList->m_Head = scc;
			sccList->m_Tail = scc;
		} else {
			scc->m_Prev = sccList->m_Tail;
			sccList->m_Tail->m_Next = scc;
			sccList->m_Tail = scc;
		}
	}

	return true;
}

static void jmir_funcFindSCCs(jx_mir_context_t* ctx, jmir_scc_list_t* sccList, uint32_t depth, jx_mir_basic_block_t** bbList, uint32_t numBasicBlocks)
{
	// Reset SCC state of all basic blocks
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jx_mir_bb_scc_info_t* sccInfo = &bbList[iBB]->m_SCCInfo;
		sccInfo->m_ID = UINT32_MAX;
		sccInfo->m_LowLink = UINT32_MAX;
		sccInfo->m_OnStack = false;
		sccInfo->m_SCC = NULL;
	}

	// https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
	jmir_scc_tarjan_state_t* sccState = &(jmir_scc_tarjan_state_t){ 0 };
	sccState->m_Stack = (jx_mir_basic_block_t**)jx_array_create(ctx->m_Allocator);
	jx_array_reserve(sccState->m_Stack, numBasicBlocks);

	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jx_mir_bb_scc_info_t* sccInfo = &bbList[iBB]->m_SCCInfo;
		if (sccInfo->m_ID == UINT32_MAX) {
			jmir_funcStrongConnect(ctx, sccList, sccState, bbList[iBB]);
		}
	}

	jx_array_free(sccState->m_Stack);

	jx_mir_scc_t* scc = sccList->m_Head;
	while (scc) {
		scc->m_Depth = depth;

		const uint32_t numSCCNodes = (uint32_t)jx_array_sizeu(scc->m_BasicBlockArr);
		if (numSCCNodes != 1) {
			// Check if this a natural loop (single entry into the SCC).
			// If it is "remove" the entry node from the node list and recursively
			// find all sub-SCCs.
			// 
			// For a node to be the entry node it must be the only node with a predecessor
			// from another SCC.
			uint32_t entryNodeID = UINT32_MAX;
			uint32_t numEntries = 0;
			for (uint32_t iNode = 0; iNode < numSCCNodes && numEntries <= 1; ++iNode) {
				jx_mir_basic_block_t* bb = scc->m_BasicBlockArr[iNode];
				const uint32_t numPreds = (uint32_t)jx_array_sizeu(bb->m_PredArr);
				for (uint32_t iPred = 0; iPred < numPreds; ++iPred) {
					jx_mir_basic_block_t* pred = bb->m_PredArr[iPred];
					if (pred->m_SCCInfo.m_SCC != scc) {
						entryNodeID = iNode;
						++numEntries;
					}
				}
			}

			JX_CHECK(numEntries != 0 && entryNodeID != UINT32_MAX, "Unconnected SCC?");
			if (numEntries == 1) {
				// This is a single-entry SCC.
				scc->m_EntryNode = scc->m_BasicBlockArr[entryNodeID];
				jx_array_del(scc->m_BasicBlockArr, entryNodeID);

				jmir_scc_list_t subList = { 0 };
				jmir_funcFindSCCs(ctx, &subList, depth + 1, scc->m_BasicBlockArr, (uint32_t)jx_array_sizeu(scc->m_BasicBlockArr));
				scc->m_FirstChild = subList.m_Head;
			} else {
				// This is an irreducible SCC. Cannot dive deeper.
			}
		}

		scc = scc->m_Next;
	}
}

bool jx_mir_funcUpdateSCCs(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	if ((func->m_Flags & JMIR_FUNC_FLAGS_SCC_VALID_Msk) != 0) {
		return true;
	}

	TracyCZoneN(tracyCtx, "mir: Update SCCs", 1);

	jx_mir_scc_t* scc = func->m_SCCListHead;
	while (scc) {
		jx_mir_scc_t* sccNext = scc->m_Next;
		jmir_sccFree(ctx, scc);
		scc = sccNext;
	}
	func->m_SCCListHead = NULL;
	func->m_SCCListTail = NULL;

	if (!jx_mir_funcUpdateCFG(ctx, func)) {
		TracyCZoneEnd(tracyCtx);
		return false;
	}

	jx_mir_basic_block_t** bbArr = (jx_mir_basic_block_t**)jx_array_create(ctx->m_Allocator);

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_array_push_back(bbArr, bb);
		bb = bb->m_Next;
	}

	jmir_scc_list_t sccList = { 0 };
	jmir_funcFindSCCs(ctx, &sccList, 0, bbArr, (uint32_t)jx_array_sizeu(bbArr));
	func->m_SCCListHead = sccList.m_Head;
	func->m_SCCListTail = sccList.m_Tail;
	
	jx_array_free(bbArr);

	func->m_Flags |= JMIR_FUNC_FLAGS_SCC_VALID_Msk;

	TracyCZoneEnd(tracyCtx);

	return true;
}

uint32_t jx_mir_funcGetRegBitsetSize(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	return 0
		+ 16 // Max GP regs
		+ 16 // Max XMM regs
		+ func->m_NextVirtualRegID[JMIR_REG_CLASS_GP]
		+ func->m_NextVirtualRegID[JMIR_REG_CLASS_XMM]
		;
}

jx_mir_reg_t jx_mir_funcMapBitsetIDToReg(jx_mir_context_t* ctx, jx_mir_function_t* func, uint32_t id)
{
	if (id < 16) {
		return (jx_mir_reg_t){
			.m_IsVirtual = 0,
			.m_ID = id,
			.m_Class = JMIR_REG_CLASS_GP
		};
	} else if (id < 32) {
		return (jx_mir_reg_t){
			.m_IsVirtual = 0,
			.m_ID = id - 16,
			.m_Class = JMIR_REG_CLASS_XMM
		};
	}

	id -= 32;
	if (id < func->m_NextVirtualRegID[JMIR_REG_CLASS_GP]) {
		return (jx_mir_reg_t){
			.m_IsVirtual = 1,
			.m_ID = id,
			.m_Class = JMIR_REG_CLASS_GP
		};
	}

	id -= func->m_NextVirtualRegID[JMIR_REG_CLASS_GP];
	return (jx_mir_reg_t){
		.m_IsVirtual = 1,
		.m_ID = id,
		.m_Class = JMIR_REG_CLASS_XMM
	};
}

uint32_t jx_mir_funcMapRegToBitsetID(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_reg_t reg)
{
	// Registers are laid out as:
	//  hw_gp, hw_gp, hw_gp, ..., hw_gp, 
	//  hw_xmm, hw_xmm, ..., hw_xmm, 
	//  v_gp, v_gp, ..., v_gp, 
	//  v_xmm, v_xmm, ..., v_xmm
	uint32_t id = 0;
	if (reg.m_IsVirtual) {
		id = (16 + 16); // 16 GP regs + 16 XMM regs

		for (uint32_t iClass = 0; iClass < reg.m_Class; ++iClass) {
			id += func->m_NextVirtualRegID[iClass];
		}

		id += reg.m_ID;
	} else {
		for (uint32_t iClass = 0; iClass < reg.m_Class; ++iClass) {
			id += 16;
		}

		id += reg.m_ID;
	}

	return id;
}

static void jmir_funcReplaceInstrVirtualReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_instruction_t* instr, jx_mir_reg_t oldReg, jx_mir_reg_t newReg)
{
	const uint32_t numOperands = instr->m_NumOperands;
	for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
		jx_mir_operand_t* operand = instr->m_Operands[iOperand];

		if (operand->m_Kind == JMIR_OPERAND_REGISTER) {
			if (jx_mir_regEqual(operand->u.m_Reg, oldReg)) {
				instr->m_Operands[iOperand] = jx_mir_opRegAlias(ctx, func, operand->m_Type, newReg);
			}
		} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jx_mir_memory_ref_t memRef = *operand->u.m_MemRef;

			if (jx_mir_regEqual(memRef.m_BaseReg, oldReg)) {
				memRef.m_BaseReg = newReg;
			}
			if (jx_mir_regEqual(memRef.m_IndexReg, oldReg)) {
				memRef.m_IndexReg = newReg;
			}

			if (!jx_mir_memRefEqual(&memRef, operand->u.m_MemRef)) {
				instr->m_Operands[iOperand] = jx_mir_opMemoryRef(ctx, func, operand->m_Type, memRef.m_BaseReg, memRef.m_IndexReg, memRef.m_Scale, memRef.m_Displacement);
			}
		}
	}
}

bool jx_mir_funcSpillVirtualReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_reg_t reg)
{
	JX_CHECK(jx_mir_regIsValid(reg) && jx_mir_regIsVirtual(reg), "Trying to spill an invalid or a hw register!");

	jx_mir_type_kind regType = reg.m_Class == JMIR_REG_CLASS_GP ? JMIR_TYPE_I64 : JMIR_TYPE_F128;
	jx_mir_operand_t* stackSlot = jx_mir_opStackObj(ctx, func, regType, jx_mir_typeGetSize(regType), jx_mir_typeGetAlignment(regType));

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_mir_instruction_t* instrNext = instr->m_Next;

			// Recalculate use/def info
			jmir_instrUpdateUseDefInfo(ctx, func, instr);

			bool use = false;
			bool def = false;

			jx_mir_instr_usedef_t* instrUseDefAnnot = &instr->m_UseDef;
			const uint32_t numUses = instrUseDefAnnot->m_NumUses;
			for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
				if (jx_mir_regEqual(instrUseDefAnnot->m_Uses[iUse], reg)) {
					use = true;
					break;
				}
			}

			const uint32_t numDefs = instrUseDefAnnot->m_NumDefs;
			for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
				if (jx_mir_regEqual(instrUseDefAnnot->m_Defs[iDef], reg)) {
					def = true;
					break;
				}
			}

			if (use || def) {
				// instr uses this register. Find the register in the operand list and figure out its type
				// TODO: Find a better way to figure out the register type
				jx_mir_type_kind regType = JMIR_TYPE_VOID;
				const uint32_t numOperands = instr->m_NumOperands;
				for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
					jx_mir_operand_t* operand = instr->m_Operands[iOperand];
					if (operand->m_Kind == JMIR_OPERAND_REGISTER) {
						if (jx_mir_regEqual(operand->u.m_Reg, reg)) {
							regType = operand->m_Type;
							break;
						}
					} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
						if (jx_mir_regEqual(operand->u.m_MemRef->m_BaseReg, reg) || jx_mir_regEqual(operand->u.m_MemRef->m_IndexReg, reg)) {
							regType = JMIR_TYPE_I64;
							break;
						}
					}
				}

				JX_CHECK(regType != JMIR_TYPE_VOID, "Use/def info says the register is referenced by the instruction but it was not found in its operands!");

				jx_mir_operand_t* temp = jx_mir_opVirtualReg(ctx, func, regType);
				jx_mir_operand_t* stackSlotTyped = jx_mir_opStackObjRel(ctx, func, regType, stackSlot->u.m_MemRef, 0);

				if (use && def) {
					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movss(ctx, temp, stackSlotTyped));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movsd(ctx, temp, stackSlotTyped));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movaps(ctx, temp, stackSlotTyped));
					} else {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, temp, stackSlotTyped));
					}

					jmir_funcReplaceInstrVirtualReg(ctx, func, instr, reg, temp->u.m_Reg);

					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movss(ctx, stackSlotTyped, temp));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movsd(ctx, stackSlotTyped, temp));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movaps(ctx, stackSlotTyped, temp));
					} else {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_mov(ctx, stackSlotTyped, temp));
					}
				} else if (use) {
					// Load from stack into new temporary
					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movss(ctx, temp, stackSlotTyped));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movsd(ctx, temp, stackSlotTyped));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movaps(ctx, temp, stackSlotTyped));
					} else {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, temp, stackSlotTyped));
					}

					jmir_funcReplaceInstrVirtualReg(ctx, func, instr, reg, temp->u.m_Reg);
				} else if (def) {
					jmir_funcReplaceInstrVirtualReg(ctx, func, instr, reg, temp->u.m_Reg);

					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movss(ctx, stackSlotTyped, temp));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movsd(ctx, stackSlotTyped, temp));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movaps(ctx, stackSlotTyped, temp));
					} else {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_mov(ctx, stackSlotTyped, temp));
					}
				}
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

	return true;
}

void jx_mir_funcPrint(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_string_buffer_t* sb)
{
	if (func->m_BasicBlockListHead) {
		// TODO: Only update CFG if it's already created by some other code.
		jx_mir_funcUpdateCFG(ctx, func);
		jx_mir_funcUpdateSCCs(ctx, func);
		jx_mir_funcUpdateLiveness(ctx, func);
	}

	jx_strbuf_printf(sb, "global %s:\n", func->m_Name);

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_strbuf_printf(sb, "bb.%u:\n", bb->m_ID);

		// Loop depth
		{
			jx_strbuf_printf(sb, "  ; loopDepth = %u\n", bb->m_SCCInfo.m_SCC->m_Depth);
		}

		// CFG
		{
			const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
			jx_strbuf_printf(sb, "  ; pred[%u] = { ", numPred);
			for (uint32_t iPred = 0; iPred < numPred; ++iPred) {
				jx_mir_basic_block_t* pred = bb->m_PredArr[iPred];
				if (iPred != 0) {
					jx_strbuf_pushCStr(sb, ", ");
				}
				jx_strbuf_printf(sb, "bb.%u", pred->m_ID);
			}
			jx_strbuf_pushCStr(sb, " }\n");

			const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
			jx_strbuf_printf(sb, "  ; succ[%u] = { ", numSucc);
			for (uint32_t iSucc = 0; iSucc < numSucc; ++iSucc) {
				jx_mir_basic_block_t* succ = bb->m_SuccArr[iSucc];
				if (iSucc != 0) {
					jx_strbuf_pushCStr(sb, ", ");
				}
				jx_strbuf_printf(sb, "bb.%u", succ->m_ID);
			}
			jx_strbuf_pushCStr(sb, " }\n");
		}

		// Liveness
		{
			// Print live-in and live-out sets
			jx_strbuf_pushCStr(sb, "  ; liveIn = { ");
			{
				const jx_bitset_t* bs = &bb->m_LiveInSet;

				jx_bitset_iterator_t iter;
				jx_bitsetIterBegin(bs, &iter, 0);
				
				bool first = true;
				uint32_t id = jx_bitsetIterNext(bs, &iter);
				while (id != UINT32_MAX) {
					if (!first) {
						jx_strbuf_pushCStr(sb, ", ");
					}
					first = false;

					jx_mir_reg_t reg = jx_mir_funcMapBitsetIDToReg(ctx, func, id);
					jmir_regPrint(ctx, reg, reg.m_Class == JMIR_REG_CLASS_GP ? JMIR_TYPE_I64 : JMIR_TYPE_F128, sb);
					id = jx_bitsetIterNext(bs, &iter);
				}
			}
			jx_strbuf_pushCStr(sb, " }\n");

			jx_strbuf_pushCStr(sb, "  ; liveOut = { ");
			{
				const jx_bitset_t* bs = &bb->m_LiveOutSet;

				jx_bitset_iterator_t iter;
				jx_bitsetIterBegin(bs, &iter, 0);

				bool first = true;
				uint32_t id = jx_bitsetIterNext(bs, &iter);
				while (id != UINT32_MAX) {
					if (!first) {
						jx_strbuf_pushCStr(sb, ", ");
					}
					first = false;

					jx_mir_reg_t reg = jx_mir_funcMapBitsetIDToReg(ctx, func, id);
					jmir_regPrint(ctx, reg, reg.m_Class == JMIR_REG_CLASS_GP ? JMIR_TYPE_I64 : JMIR_TYPE_F128, sb);
					id = jx_bitsetIterNext(bs, &iter);
				}
			}
			jx_strbuf_pushCStr(sb, " }\n");
		}

		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_mir_instrPrint(ctx, instr, sb);

			instr = instr->m_Next;
		}

		bb = bb->m_Next;
	}
}

jx_mir_basic_block_t* jx_mir_bbAlloc(jx_mir_context_t* ctx)
{
	jx_mir_basic_block_t* bb = (jx_mir_basic_block_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_basic_block_t));
	if (!bb) {
		return NULL;
	}

	jx_memset(bb, 0, sizeof(jx_mir_basic_block_t));
	bb->m_ID = UINT32_MAX;

	bb->m_PredArr = (jx_mir_basic_block_t**)jx_array_create(ctx->m_Allocator);
	if (!bb->m_PredArr) {
		jx_mir_bbFree(ctx, bb);
		return false;
	}

	bb->m_SuccArr = (jx_mir_basic_block_t**)jx_array_create(ctx->m_Allocator);
	if (!bb->m_SuccArr) {
		jx_mir_bbFree(ctx, bb);
		return false;
	}
	jx_array_reserve(bb->m_SuccArr, 2);

	return bb;
}

void jx_mir_bbFree(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb)
{
	jx_mir_instruction_t* instr = bb->m_InstrListHead;
	while (instr) {
		jx_mir_instruction_t* nextInstr = instr->m_Next;
		jx_mir_instrFree(ctx, instr);
		instr = nextInstr;
	}

	jx_array_free(bb->m_PredArr);
	jx_array_free(bb->m_SuccArr);

	jx_bitsetFree(&bb->m_LiveInSet, ctx->m_Allocator);
	jx_bitsetFree(&bb->m_LiveOutSet, ctx->m_Allocator);
}

bool jx_mir_bbAppendInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* instr)
{
	JX_UNUSED(ctx);
	JX_CHECK(!instr->m_ParentBB && !instr->m_Prev && !instr->m_Next, "Instruction already part of a basic block?");

	instr->m_ParentBB = bb;

	if (!bb->m_InstrListHead) {
		JX_CHECK(!bb->m_InstrListTail, "Invalid linked-list state");
		bb->m_InstrListHead = instr;
	} else {
		JX_CHECK(bb->m_InstrListTail, "Invalid linked-list state");
		bb->m_InstrListTail->m_Next = instr;
		instr->m_Prev = bb->m_InstrListTail;
	}

	bb->m_InstrListTail = instr;

	if (bb->m_ParentFunc) {
		bb->m_ParentFunc->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);
	}

	return true;
}

bool jx_mir_bbPrependInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* instr)
{
	JX_UNUSED(ctx);
	JX_CHECK(!instr->m_ParentBB && !instr->m_Prev && !instr->m_Next, "Instruction already part of a basic block?");

	instr->m_ParentBB = bb;

	instr->m_Next = bb->m_InstrListHead;

	if (bb->m_InstrListHead) {
		bb->m_InstrListHead->m_Prev = instr;
	} else {
		bb->m_InstrListTail = instr;
	}

	bb->m_InstrListHead = instr;

	if (bb->m_ParentFunc) {
		bb->m_ParentFunc->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);
	}

	return true;
}

bool jx_mir_bbInsertInstrBefore(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* anchor, jx_mir_instruction_t* instr)
{
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

	if (bb->m_ParentFunc) {
		bb->m_ParentFunc->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);
	}

	return true;
}

bool jx_mir_bbInsertInstrAfter(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* anchor, jx_mir_instruction_t* instr)
{
	JX_UNUSED(ctx);
	JX_CHECK(!instr->m_ParentBB && !instr->m_Prev && !instr->m_Next, "Instruction already part of a basic block?");
	JX_CHECK(anchor->m_ParentBB == bb, "Anchor instruction not part of this basic block");

	instr->m_ParentBB = bb;

	if (anchor->m_Next) {
		instr->m_Next = anchor->m_Next;
		anchor->m_Next->m_Prev = instr;
	} else {
		instr->m_Next = NULL;
	}

	anchor->m_Next = instr;
	instr->m_Prev = anchor;

	if (bb->m_InstrListTail == anchor) {
		bb->m_InstrListTail = instr;
	}

	if (bb->m_ParentFunc) {
		bb->m_ParentFunc->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);
	}

	return true;
}

bool jx_mir_bbRemoveInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* instr)
{
	JX_UNUSED(ctx);

	if (instr->m_ParentBB != bb) {
		JX_CHECK(false, "Trying to remove instruction from another basic block.");
		return false;
	}

	if (bb->m_InstrListHead == instr) {
		bb->m_InstrListHead = instr->m_Next;
	}
	if (bb->m_InstrListTail == instr) {
		bb->m_InstrListTail = instr->m_Prev;
	}

	if (instr->m_Prev) {
		instr->m_Prev->m_Next = instr->m_Next;
	}
	if (instr->m_Next) {
		instr->m_Next->m_Prev = instr->m_Prev;
	}

	instr->m_ParentBB = NULL;
	instr->m_Prev = NULL;
	instr->m_Next = NULL;

	if (bb->m_ParentFunc) {
		bb->m_ParentFunc->m_Flags &= ~(JMIR_FUNC_FLAGS_CFG_VALID_Msk | JMIR_FUNC_FLAGS_SCC_VALID_Msk);
	}

	return true;
}

jx_mir_instruction_t* jx_mir_bbGetFirstTerminatorInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb)
{
	JX_UNUSED(ctx);

	jx_mir_instruction_t* instr = bb->m_InstrListTail;
	jx_mir_instruction_t* lastTerminatorInstr = instr && jx_mir_opcodeIsTerminator(instr->m_OpCode)
		? instr
		: NULL
		;
	while (instr) {
		if (!jx_mir_opcodeIsTerminator(instr->m_OpCode)) {
			return lastTerminatorInstr;
		}

		lastTerminatorInstr = instr;
		instr = instr->m_Prev;
	}

	return lastTerminatorInstr;
}

jx_mir_operand_t* jx_mir_opVirtualReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_REGISTER, type);
	if (!operand) {
		return NULL;
	}

	const jx_mir_reg_class regClass = jx_mir_typeGetClass(type);

	const uint32_t id = func->m_NextVirtualRegID[regClass];
	func->m_NextVirtualRegID[regClass]++;

	operand->u.m_Reg = (jx_mir_reg_t){
		.m_ID = id,
		.m_Class = regClass,
		.m_IsVirtual = true
	};

	return operand;
}

jx_mir_operand_t* jx_mir_opHWReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_reg_t reg)
{
	JX_CHECK(!reg.m_IsVirtual, "Expected hardware register");
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_REGISTER, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_Reg = reg;

	func->m_UsedHWRegs[reg.m_Class] |= 1u << reg.m_ID;

	return operand;
}

jx_mir_operand_t* jx_mir_opRegAlias(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_reg_t reg)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_REGISTER, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_Reg = reg;

	JX_CHECK(reg.m_IsVirtual || (func->m_UsedHWRegs[reg.m_Class] & (1u << reg.m_ID)) != 0, "Trying to alias a hw register which hasn't been used yet.");

	return operand;
}

jx_mir_operand_t* jx_mir_opIConst(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, int64_t val)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_CONST, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_ConstI64 = val;

	return operand;
}

jx_mir_operand_t* jx_mir_opFConst(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, double val)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_CONST, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_ConstF64 = val;

	return operand;
}

jx_mir_operand_t* jx_mir_opBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_BASIC_BLOCK, JMIR_TYPE_VOID);
	if (!operand) {
		return NULL;
	}

	operand->u.m_BB = bb;

	return operand;
}

jx_mir_operand_t* jx_mir_opMemoryRef(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_reg_t baseReg, jx_mir_reg_t indexReg, uint32_t scale, int32_t displacement)
{
	JX_CHECK(!jx_mir_regIsValid(baseReg) || jx_mir_regIsClass(baseReg, JMIR_REG_CLASS_GP), "Invalid base register");
	JX_CHECK(!jx_mir_regIsValid(indexReg) || jx_mir_regIsClass(indexReg, JMIR_REG_CLASS_GP), "Invalid base register");

	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_MEMORY_REF, type);
	if (!operand) {
		return NULL;
	}

	jx_mir_memory_ref_t* memRef = jmir_memRefAlloc(ctx, baseReg, indexReg, scale, displacement);
	if (!memRef) {
		return NULL;
	}

	operand->u.m_MemRef = memRef;

	if (jx_mir_regIsValid(baseReg) && jx_mir_regIsHW(baseReg)) {
		func->m_UsedHWRegs[baseReg.m_Class] |= 1u << baseReg.m_ID;
	}
	if (jx_mir_regIsValid(indexReg) && jx_mir_regIsHW(indexReg)) {
		func->m_UsedHWRegs[indexReg.m_Class] |= 1u << indexReg.m_ID;
	}

	return operand;
}

jx_mir_operand_t* jx_mir_opStackObj(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, uint32_t sz, uint32_t alignment)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_MEMORY_REF, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_MemRef = jmir_frameAllocObj(ctx, func->m_FrameInfo, sz, alignment);

	return operand;
}

jx_mir_operand_t* jx_mir_opStackObjRel(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_memory_ref_t* baseObj, int32_t offset)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_MEMORY_REF, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_MemRef = jmir_frameObjRel(ctx, func->m_FrameInfo, baseObj, offset);

	return operand;
}

jx_mir_operand_t* jx_mir_opExternalSymbol(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, const char* name, int32_t offset)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_EXTERNAL_SYMBOL, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_ExternalSymbol.m_Name = name;
	operand->u.m_ExternalSymbol.m_Offset = offset;

	return operand;
}

static void jmir_regPrint(jx_mir_context_t* ctx, jx_mir_reg_t reg, jx_mir_type_kind type, jx_string_buffer_t* sb)
{
	JX_CHECK(type != JMIR_TYPE_VOID, "Unexpected void register!");
	static const char* kRegPostfix[] = {
		[JMIR_TYPE_I8] = ".b",
		[JMIR_TYPE_I16] = ".w",
		[JMIR_TYPE_I32] = ".d",
		[JMIR_TYPE_I64] = "",
		[JMIR_TYPE_F32] = "f.32",
		[JMIR_TYPE_F64] = "f.64",
		[JMIR_TYPE_F128] = "f",
		[JMIR_TYPE_PTR] = "",
	};

	if (reg.m_IsVirtual) {
		jx_strbuf_printf(sb, "%%vr%u%s", reg.m_ID, kRegPostfix[type]);
	} else {
		// Special handling of important hw registers
		if (reg.m_Class == JMIR_REG_CLASS_GP) {
			if (reg.m_ID == JMIR_HWREGID_A) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$al");  break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$ax");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$eax"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rax"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else if (reg.m_ID == JMIR_HWREGID_C) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$cl");  break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$cx");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$ecx"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rcx"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else if (reg.m_ID == JMIR_HWREGID_D) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$dl");  break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$dx");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$edx"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rdx"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else if (reg.m_ID == JMIR_HWREGID_B) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$bl");  break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$bx");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$ebx"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rbx"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else if (reg.m_ID == JMIR_HWREGID_SP) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$spl"); break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$sp");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$esp"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rsp"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else if (reg.m_ID == JMIR_HWREGID_BP) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$bpl"); break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$bp");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$ebp"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rbp"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else if (reg.m_ID == JMIR_HWREGID_SI) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$sil"); break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$si");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$esi"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rsi"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else if (reg.m_ID == JMIR_HWREGID_DI) {
				switch (type) {
				case JMIR_TYPE_I8:  jx_strbuf_pushCStr(sb, "$dil"); break;
				case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$di");  break;
				case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$edi"); break;
				case JMIR_TYPE_I64:
				case JMIR_TYPE_PTR: jx_strbuf_pushCStr(sb, "$rdi"); break;
				default:
					JX_CHECK(false, "Invalid GP register type");
					break;
				}
			} else {
				jx_strbuf_printf(sb, "$r%u%s", reg.m_ID, kRegPostfix[type]);
			}
		} else if (reg.m_Class == JMIR_REG_CLASS_XMM) {
			jx_strbuf_printf(sb, "$xmm%u", reg.m_ID);
		} else {
			JX_CHECK(false, "Unknown register class.");
		}
	}
}

void jx_mir_opPrint(jx_mir_context_t* ctx, jx_mir_operand_t* op, bool memRefSkipSize, jx_string_buffer_t* sb)
{
	switch (op->m_Kind) {
	case JMIR_OPERAND_REGISTER: {
		jmir_regPrint(ctx, op->u.m_Reg, op->m_Type, sb);
	} break;
	case JMIR_OPERAND_CONST: {
		switch (op->m_Type) {
		case JMIR_TYPE_VOID: {
			JX_CHECK(false, "void constant?");
		} break;
		case JMIR_TYPE_I8: {
			jx_strbuf_printf(sb, "0x%02X", (int8_t)op->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I16: {
			jx_strbuf_printf(sb, "0x%04X", (int16_t)op->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I32: {
			jx_strbuf_printf(sb, "0x%08X", (int32_t)op->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I64: {
			jx_strbuf_printf(sb, "0x%016llX", op->u.m_ConstI64);
		} break;
		case JMIR_TYPE_F32:
		case JMIR_TYPE_F64: {
			jx_strbuf_printf(sb, "%f", op->u.m_ConstF64);
		} break;
		case JMIR_TYPE_PTR: {
			jx_strbuf_printf(sb, "0x%p", (uintptr_t)op->u.m_ConstI64);
		} break;
		default:
			JX_CHECK(false, "Unknown type!");
			break;
		}
	} break;
	case JMIR_OPERAND_BASIC_BLOCK: {
		jx_strbuf_printf(sb, "bb.%u", op->u.m_BB->m_ID);
	} break;
	case JMIR_OPERAND_MEMORY_REF: {
		if (!memRefSkipSize) {
			switch (op->m_Type) {
			case JMIR_TYPE_I8:
				jx_strbuf_pushCStr(sb, "byte ptr");
				break;
			case JMIR_TYPE_I16:
				jx_strbuf_pushCStr(sb, "word ptr");
				break;
			case JMIR_TYPE_I32:
			case JMIR_TYPE_F32:
				jx_strbuf_pushCStr(sb, "dword ptr");
				break;
			case JMIR_TYPE_I64:
			case JMIR_TYPE_PTR:
			case JMIR_TYPE_F64:
				jx_strbuf_pushCStr(sb, "qword ptr");
				break;
			case JMIR_TYPE_F128:
				jx_strbuf_pushCStr(sb, "xmmword ptr");
				break;
			case JMIR_TYPE_VOID:
				JX_NOT_IMPLEMENTED();
				break;
			default:
				JX_CHECK(false, "Unknown kind of type!");
				break;
			}
			jx_strbuf_push(sb, " ", 1);
		}

		jx_strbuf_pushCStr(sb, "[");
		bool insertOp = false;
		if (op->u.m_MemRef->m_BaseReg.m_ID != JMIR_HWREGID_NONE) {
			jmir_regPrint(ctx, op->u.m_MemRef->m_BaseReg, JMIR_TYPE_PTR, sb);
			insertOp = true;
		}

		if (op->u.m_MemRef->m_IndexReg.m_ID != JMIR_HWREGID_NONE) {
			if (insertOp) {
				jx_strbuf_pushCStr(sb, " + ");
			}

			jmir_regPrint(ctx, op->u.m_MemRef->m_IndexReg, JMIR_TYPE_PTR, sb);

			if (op->u.m_MemRef->m_Scale != 1) {
				jx_strbuf_printf(sb, " * %u", op->u.m_MemRef->m_Scale);
			}
			insertOp = true;
		}

		if (op->u.m_MemRef->m_Displacement != 0) {
			if (insertOp && op->u.m_MemRef->m_Displacement > 0) {
				jx_strbuf_pushCStr(sb, " + ");
			}

			if (op->u.m_MemRef->m_Displacement < 0) {
				jx_strbuf_printf(sb, " - %u", (uint32_t)(-op->u.m_MemRef->m_Displacement));
			} else {
				jx_strbuf_printf(sb, "%u", (uint32_t)op->u.m_MemRef->m_Displacement);
			}
		}
		jx_strbuf_pushCStr(sb, "]");
	} break;
	case JMIR_OPERAND_EXTERNAL_SYMBOL: {
		jx_strbuf_printf(sb, "%s", op->u.m_ExternalSymbol.m_Name);
	} break;
	default:
		JX_CHECK(false, "Unknown operand type");
		break;
	}
}

void jx_mir_instrFree(jx_mir_context_t* ctx, jx_mir_instruction_t* instr)
{
	jx_bitsetFree(&instr->m_LiveOutSet, ctx->m_Allocator);
}

void jx_mir_instrPrint(jx_mir_context_t* ctx, jx_mir_instruction_t* instr, jx_string_buffer_t* sb)
{
	jx_strbuf_printf(sb, "  %s ", kMIROpcodeMnemonic[instr->m_OpCode]);
	
	const uint32_t numOperands = instr->m_NumOperands;
	for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
		if (iOperand != 0) {
			jx_strbuf_pushCStr(sb, ", ");
		}
		jx_mir_opPrint(ctx, instr->m_Operands[iOperand], instr->m_OpCode == JMIR_OP_LEA, sb);
	}

#if 1
	jx_strbuf_pushCStr(sb, ";\n");
#else
	jx_strbuf_pushCStr(sb, "; liveOut = { ");
	{
		const jx_bitset_t* bs = &instr->m_LiveOutSet;

		jx_bitset_iterator_t iter;
		jx_bitsetIterBegin(bs, &iter, 0);

		bool first = true;
		uint32_t id = jx_bitsetIterNext(bs, &iter);
		while (id != UINT32_MAX) {
			if (!first) {
				jx_strbuf_pushCStr(sb, ", ");
			}
			first = false;

			jx_mir_reg_t reg = jx_mir_funcMapBitsetIDToReg(ctx, instr->m_ParentBB->m_ParentFunc, id);
			jmir_regPrint(ctx, reg, reg.m_Class == JMIR_REG_CLASS_GP ? JMIR_TYPE_I64 : JMIR_TYPE_F128, sb);
			id = jx_bitsetIterNext(bs, &iter);
		}
	}
	jx_strbuf_pushCStr(sb, " }\n");
#endif
}

jx_mir_instruction_t* jx_mir_mov(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	JX_CHECK(!jx_mir_typeIsFloatingPoint(dst->m_Type) && !jx_mir_typeIsFloatingPoint(src->m_Type), "Floating point types not allowed!");
	JX_CHECK(jx_mir_typeGetSize(dst->m_Type) == jx_mir_typeGetSize(src->m_Type), "mov expected same size operands.");
	if (dst->m_Kind != JMIR_OPERAND_REGISTER) {
		JX_CHECK(src->m_Kind != JMIR_OPERAND_CONST || src->m_Type != JMIR_TYPE_I64 || (src->u.m_ConstI64 >= INT32_MIN && src->u.m_ConstI64 <= INT32_MAX), "Wrong immediate!");
	}
	return jmir_instrAlloc2(ctx, JMIR_OP_MOV, dst, src);
}

jx_mir_instruction_t* jx_mir_movsx(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	JX_CHECK(!jx_mir_typeIsFloatingPoint(dst->m_Type) && !jx_mir_typeIsFloatingPoint(src->m_Type), "Floating point types not allowed!");
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVSX, dst, src);
}

jx_mir_instruction_t* jx_mir_movzx(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	JX_CHECK(!jx_mir_typeIsFloatingPoint(dst->m_Type) && !jx_mir_typeIsFloatingPoint(src->m_Type), "Floating point types not allowed!");
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVZX, dst, src);
}

jx_mir_instruction_t* jx_mir_add(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ADD, dst, src);
}

jx_mir_instruction_t* jx_mir_sub(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SUB, dst, src);
}

jx_mir_instruction_t* jx_mir_adc(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_sbb(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_and(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_AND, dst, src);
}

jx_mir_instruction_t* jx_mir_or(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_OR, dst, src);
}

jx_mir_instruction_t* jx_mir_xor(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_XOR, dst, src);
}

jx_mir_instruction_t* jx_mir_cmp(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CMP, dst, src);
}

jx_mir_instruction_t* jx_mir_ret(jx_mir_context_t* ctx, jx_mir_operand_t* val)
{
	jx_mir_instruction_t* instr = jmir_instrAlloc(ctx, JMIR_OP_RET, val ? 1 : 0, NULL);
	if (!instr) {
		return NULL;
	}

	if (val) {
		instr->m_Operands[0] = val;
	}

	return instr;
}

jx_mir_instruction_t* jx_mir_not(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_neg(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_mul(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_div(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return jmir_instrAlloc1(ctx, JMIR_OP_DIV, op);
}

jx_mir_instruction_t* jx_mir_idiv(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	JX_CHECK(op->m_Kind != JMIR_OPERAND_CONST, "TODO: Make sure operand is not constant");
	return jmir_instrAlloc1(ctx, JMIR_OP_IDIV, op);
}

jx_mir_instruction_t* jx_mir_inc(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_dec(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_imul(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_IMUL, dst, src);
}

jx_mir_instruction_t* jx_mir_imul3(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, jx_mir_operand_t* imm)
{
	JX_CHECK(imm->m_Kind == JMIR_OPERAND_CONST, "imul with 3 operands expected 2nd source operand to be an immediate.");
	return jmir_instrAlloc3(ctx, JMIR_OP_IMUL3, dst, src, imm);
}

jx_mir_instruction_t* jx_mir_lea(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_LEA, dst, src);;
}

jx_mir_instruction_t* jx_mir_test(jx_mir_context_t* ctx, jx_mir_operand_t* op1, jx_mir_operand_t* op2)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_TEST, op1, op2);
}

jx_mir_instruction_t* jx_mir_setcc(jx_mir_context_t* ctx, jx_mir_condition_code cc, jx_mir_operand_t* dst)
{
	return jmir_instrAlloc1(ctx, JMIR_OP_SETCC_BASE + cc, dst);
}

jx_mir_instruction_t* jx_mir_sar(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SAR, op, shift);
}

jx_mir_instruction_t* jx_mir_sal(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SHL, op, shift);
}

jx_mir_instruction_t* jx_mir_shr(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SHR, op, shift);
}

jx_mir_instruction_t* jx_mir_shl(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SHL, op, shift);
}

jx_mir_instruction_t* jx_mir_rcr(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_rcl(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_ror(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_rol(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_cmovcc(jx_mir_context_t* ctx, jx_mir_condition_code cc, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return NULL;
}

jx_mir_instruction_t* jx_mir_jcc(jx_mir_context_t* ctx, jx_mir_condition_code cc, jx_mir_operand_t* op)
{
	return jmir_instrAlloc1(ctx, JMIR_OP_JCC_BASE + cc, op);
}

jx_mir_instruction_t* jx_mir_jmp(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return jmir_instrAlloc1(ctx, JMIR_OP_JMP, op);
}

jx_mir_instruction_t* jx_mir_call(jx_mir_context_t* ctx, jx_mir_operand_t* func, jx_mir_function_proto_t* proto)
{
	jx_mir_instruction_t* instr = jmir_instrAlloc1(ctx, JMIR_OP_CALL, func);
	if (instr) {
		instr->m_FuncProto = proto;
	}

	return instr;
}

jx_mir_instruction_t* jx_mir_push(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return jmir_instrAlloc1(ctx, JMIR_OP_PUSH, op);
}

jx_mir_instruction_t* jx_mir_pop(jx_mir_context_t* ctx, jx_mir_operand_t* op)
{
	return jmir_instrAlloc1(ctx, JMIR_OP_POP, op);
}

jx_mir_instruction_t* jx_mir_cdq(jx_mir_context_t* ctx)
{
	return jmir_instrAlloc(ctx, JMIR_OP_CDQ, 0, NULL);
}

jx_mir_instruction_t* jx_mir_cqo(jx_mir_context_t* ctx)
{
	return jmir_instrAlloc(ctx, JMIR_OP_CQO, 0, NULL);
}

jx_mir_instruction_t* jx_mir_movss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVSS, dst, src);
}

jx_mir_instruction_t* jx_mir_movsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVSD, dst, src);
}

jx_mir_instruction_t* jx_mir_movaps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVAPS, dst, src);
}

jx_mir_instruction_t* jx_mir_movapd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVAPD, dst, src);
}

jx_mir_instruction_t* jx_mir_movd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVD, dst, src);
}

jx_mir_instruction_t* jx_mir_movq(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVQ, dst, src);
}

jx_mir_instruction_t* jx_mir_addps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ADDPS, dst, src);
}

jx_mir_instruction_t* jx_mir_addss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ADDSS, dst, src);
}

jx_mir_instruction_t* jx_mir_addpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ADDPD, dst, src);
}

jx_mir_instruction_t* jx_mir_addsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ADDSD, dst, src);
}

jx_mir_instruction_t* jx_mir_andnps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ANDNPS, dst, src);
}

jx_mir_instruction_t* jx_mir_andnpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ANDNPD, dst, src);
}

jx_mir_instruction_t* jx_mir_andps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ANDPS, dst, src);
}

jx_mir_instruction_t* jx_mir_andpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ANDPD, dst, src);
}

jx_mir_instruction_t* jx_mir_cmpps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8)
{
	JX_NOT_IMPLEMENTED();
	return NULL;
}

jx_mir_instruction_t* jx_mir_cmpss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8)
{
	JX_NOT_IMPLEMENTED();
	return NULL;
}

jx_mir_instruction_t* jx_mir_cmppd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8)
{
	JX_NOT_IMPLEMENTED();
	return NULL;
}

jx_mir_instruction_t* jx_mir_cmpsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8)
{
	JX_NOT_IMPLEMENTED();
	return NULL;
}

jx_mir_instruction_t* jx_mir_comiss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_COMISS, dst, src);
}

jx_mir_instruction_t* jx_mir_comisd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_COMISD, dst, src);
}

jx_mir_instruction_t* jx_mir_cvtsi2ss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTSI2SS, dst, src);
}

jx_mir_instruction_t* jx_mir_cvtsi2sd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTSI2SD, dst, src);
}

jx_mir_instruction_t* jx_mir_cvtss2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTSS2SI, dst, src);
}

jx_mir_instruction_t* jx_mir_cvtsd2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTSD2SI, dst, src);
}

jx_mir_instruction_t* jx_mir_cvttss2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTTSS2SI, dst, src);
}

jx_mir_instruction_t* jx_mir_cvttsd2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTTSD2SI, dst, src);
}

jx_mir_instruction_t* jx_mir_cvtsd2ss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTSD2SS, dst, src);
}

jx_mir_instruction_t* jx_mir_cvtss2sd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_CVTSS2SD, dst, src);
}

jx_mir_instruction_t* jx_mir_divps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_DIVPS, dst, src);
}

jx_mir_instruction_t* jx_mir_divss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_DIVSS, dst, src);
}

jx_mir_instruction_t* jx_mir_divpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_DIVPD, dst, src);
}

jx_mir_instruction_t* jx_mir_divsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_DIVSD, dst, src);
}

jx_mir_instruction_t* jx_mir_maxps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MAXPS, dst, src);
}

jx_mir_instruction_t* jx_mir_maxss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MAXSS, dst, src);
}

jx_mir_instruction_t* jx_mir_maxpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MAXPD, dst, src);
}

jx_mir_instruction_t* jx_mir_maxsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MAXSD, dst, src);
}

jx_mir_instruction_t* jx_mir_minps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MINPS, dst, src);
}

jx_mir_instruction_t* jx_mir_minss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MINSS, dst, src);
}

jx_mir_instruction_t* jx_mir_minpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MINPD, dst, src);
}

jx_mir_instruction_t* jx_mir_minsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MINSD, dst, src);
}

jx_mir_instruction_t* jx_mir_mulps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MULPS, dst, src);
}

jx_mir_instruction_t* jx_mir_mulss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MULSS, dst, src);
}

jx_mir_instruction_t* jx_mir_mulpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MULPD, dst, src);
}

jx_mir_instruction_t* jx_mir_mulsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MULSD, dst, src);
}

jx_mir_instruction_t* jx_mir_orps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ORPS, dst, src);
}

jx_mir_instruction_t* jx_mir_orpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_ORPD, dst, src);
}

jx_mir_instruction_t* jx_mir_rcpps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_RCPPS, dst, src);
}

jx_mir_instruction_t* jx_mir_rcpss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_RCPSS, dst, src);
}

jx_mir_instruction_t* jx_mir_rsqrtps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_RSQRTPS, dst, src);
}

jx_mir_instruction_t* jx_mir_rsqrtss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_RSQRTSS, dst, src);
}

jx_mir_instruction_t* jx_mir_shufps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8)
{
	JX_NOT_IMPLEMENTED();
	return NULL;
}

jx_mir_instruction_t* jx_mir_shufpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8)
{
	JX_NOT_IMPLEMENTED();
	return NULL;
}

jx_mir_instruction_t* jx_mir_sqrtps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SQRTPS, dst, src);
}

jx_mir_instruction_t* jx_mir_sqrtss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SQRTSS, dst, src);
}

jx_mir_instruction_t* jx_mir_sqrtpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SQRTPD, dst, src);
}

jx_mir_instruction_t* jx_mir_sqrtsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SQRTSD, dst, src);
}

jx_mir_instruction_t* jx_mir_subps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SUBPS, dst, src);
}

jx_mir_instruction_t* jx_mir_subss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SUBSS, dst, src);
}

jx_mir_instruction_t* jx_mir_subpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SUBPD, dst, src);
}

jx_mir_instruction_t* jx_mir_subsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_SUBSD, dst, src);
}

jx_mir_instruction_t* jx_mir_ucomiss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_UCOMISS, dst, src);
}

jx_mir_instruction_t* jx_mir_ucomisd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_UCOMISD, dst, src);
}

jx_mir_instruction_t* jx_mir_unpckhps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_UNPCKHPS, dst, src);
}

jx_mir_instruction_t* jx_mir_unpckhpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_UNPCKHPD, dst, src);
}

jx_mir_instruction_t* jx_mir_unpcklps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_UNPCKLPS, dst, src);
}

jx_mir_instruction_t* jx_mir_unpcklpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_UNPCKLPD, dst, src);
}

jx_mir_instruction_t* jx_mir_xorps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_XORPS, dst, src);
}

jx_mir_instruction_t* jx_mir_xorpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_XORPD, dst, src);
}

jx_mir_instruction_t* jx_mir_punpcklbw(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKLBW, dst, src);
}

jx_mir_instruction_t* jx_mir_punpcklwd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKLWD, dst, src);
}

jx_mir_instruction_t* jx_mir_punpckldq(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKLDQ, dst, src);
}

jx_mir_instruction_t* jx_mir_punpcklqdq(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKLQDQ, dst, src);
}

jx_mir_instruction_t* jx_mir_punpckhbw(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKHBW, dst, src);
}

jx_mir_instruction_t* jx_mir_punpckhwd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKHWD, dst, src);
}

jx_mir_instruction_t* jx_mir_punpckhdq(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKHDQ, dst, src);
}

jx_mir_instruction_t* jx_mir_punpckhqdq(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_PUNPCKHQDQ, dst, src);
}

static bool jmir_opcodeIsTerminator(uint32_t opcode)
{
	return false
		|| opcode == JMIR_OP_RET
		|| opcode == JMIR_OP_JMP
		|| jx_mir_opcodeIsJcc(opcode)
		;
}

static jx_mir_operand_t* jmir_operandAlloc(jx_mir_context_t* ctx, jx_mir_operand_kind kind, jx_mir_type_kind type)
{
	jx_mir_operand_t* op = (jx_mir_operand_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_operand_t));
	if (!op) {
		return NULL;
	}

	jx_memset(op, 0, sizeof(jx_mir_operand_t));
	op->m_Kind = kind;
	op->m_Type = type;

	return op;
}

static jx_mir_instruction_t* jmir_instrAlloc(jx_mir_context_t* ctx, uint32_t opcode, uint32_t numOperands, jx_mir_operand_t** operands)
{
	jx_mir_instruction_t* instr = (jx_mir_instruction_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_instruction_t));
	if (!instr) {
		return NULL;
	}

	jx_memset(instr, 0, sizeof(jx_mir_instruction_t));
	instr->m_OpCode = opcode;
	instr->m_NumOperands = numOperands;
	if (numOperands) {
		instr->m_Operands = (jx_mir_operand_t**)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_operand_t*) * numOperands);
		if (!instr->m_Operands) {
			jx_mir_instrFree(ctx, instr);
			return NULL;
		}

		if (operands) {
			jx_memcpy(instr->m_Operands, operands, sizeof(jx_mir_operand_t*) * numOperands);
		} else {
			jx_memset(instr->m_Operands, 0, sizeof(jx_mir_operand_t*) * numOperands);
		}
	}

	return instr;
}

static jx_mir_instruction_t* jmir_instrAlloc1(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1)
{
	jx_mir_operand_t* operands[] = { op1 };
	return jmir_instrAlloc(ctx, opcode, 1, operands);
}

static jx_mir_instruction_t* jmir_instrAlloc2(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1, jx_mir_operand_t* op2)
{
	jx_mir_operand_t* operands[] = { op1, op2 };
	return jmir_instrAlloc(ctx, opcode, 2, operands);
}

static jx_mir_instruction_t* jmir_instrAlloc3(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1, jx_mir_operand_t* op2, jx_mir_operand_t* op3)
{
	jx_mir_operand_t* operands[] = { op1, op2, op3 };
	return jmir_instrAlloc(ctx, opcode, 3, operands);
}

static inline void jmir_instrAddUse(jx_mir_instr_usedef_t* useDef, jx_mir_reg_t reg)
{
	if (jx_mir_regIsValid(reg)) {
		JX_CHECK(useDef->m_NumUses + 1 <= JMIR_MAX_INSTR_USES, "Too many instruction uses");
		useDef->m_Uses[useDef->m_NumUses++] = reg;
	}
}

static inline void jmir_instrAddDef(jx_mir_instr_usedef_t* useDef, jx_mir_reg_t reg)
{
	JX_CHECK(jx_mir_regIsValid(reg), "Invalid register ID");
	JX_CHECK(useDef->m_NumDefs + 1 <= JMIR_MAX_INSTR_DEFS, "Too many instruction defs");
	useDef->m_Defs[useDef->m_NumDefs++] = reg;
}

static bool jmir_instrUpdateUseDefInfo(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_instruction_t* instr)
{
	jx_mir_instr_usedef_t* annot = &instr->m_UseDef;

	annot->m_NumDefs = 0;
	annot->m_NumUses = 0;
	switch (instr->m_OpCode) {
	case JMIR_OP_RET: {
		if (func->m_Prototype->m_RetType != JMIR_TYPE_VOID) {
			jmir_instrAddUse(annot, kMIRRegGP_A);
		}
	} break;
	case JMIR_OP_CMP:
	case JMIR_OP_TEST: 
	case JMIR_OP_COMISS:
	case JMIR_OP_COMISD:
	case JMIR_OP_UCOMISS:
	case JMIR_OP_UCOMISD: {
		for (uint32_t iOperand = 0; iOperand < 2; ++iOperand) {
			jx_mir_operand_t* src = instr->m_Operands[iOperand];
			if (src->m_Kind == JMIR_OPERAND_REGISTER) {
				jmir_instrAddUse(annot, src->u.m_Reg);
			} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
				jmir_instrAddUse(annot, src->u.m_MemRef->m_BaseReg);
				jmir_instrAddUse(annot, src->u.m_MemRef->m_IndexReg);
			}
		}
	} break;
	case JMIR_OP_MOV:
	case JMIR_OP_MOVSX:
	case JMIR_OP_MOVZX: 
	case JMIR_OP_MOVSS: 
	case JMIR_OP_MOVSD: 
	case JMIR_OP_MOVD:
	case JMIR_OP_MOVQ:
	case JMIR_OP_MOVAPS:
	case JMIR_OP_MOVAPD:
	case JMIR_OP_CVTSI2SS:
	case JMIR_OP_CVTSI2SD:
	case JMIR_OP_CVTSS2SI:
	case JMIR_OP_CVTSD2SI:
	case JMIR_OP_CVTTSS2SI:
	case JMIR_OP_CVTTSD2SI:
	case JMIR_OP_CVTSD2SS:
	case JMIR_OP_CVTSS2SD: 
	case JMIR_OP_RCPPS:
	case JMIR_OP_RCPSS:
	case JMIR_OP_RSQRTPS:
	case JMIR_OP_RSQRTSS:
	case JMIR_OP_SQRTPS:
	case JMIR_OP_SQRTSS:
	case JMIR_OP_SQRTPD:
	case JMIR_OP_SQRTSD: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddUse(annot, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, src->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, src->u.m_MemRef->m_IndexReg);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddDef(annot, dst->u.m_Reg);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, dst->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, dst->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_IDIV:
	case JMIR_OP_DIV: {
		jx_mir_operand_t* op = instr->m_Operands[0];

		jmir_instrAddUse(annot, kMIRRegGP_A);
		jmir_instrAddUse(annot, kMIRRegGP_D);

		if (op->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddUse(annot, op->u.m_Reg);
		} else if (op->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, op->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, op->u.m_MemRef->m_IndexReg);
		}

		jmir_instrAddDef(annot, kMIRRegGP_A);
		jmir_instrAddDef(annot, kMIRRegGP_D);
	} break;
	case JMIR_OP_IMUL3: {
		jx_mir_operand_t* dst = instr->m_Operands[0];
		JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "Expected register operand.");
		jmir_instrAddDef(annot, dst->u.m_Reg);

		jx_mir_operand_t* src1 = instr->m_Operands[1];
		if (src1->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddUse(annot, src1->u.m_Reg);
		} else if (src1->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, src1->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, src1->u.m_MemRef->m_IndexReg);
		}

		JX_CHECK(instr->m_Operands[2]->m_Kind == JMIR_OPERAND_CONST, "Expected constant operand.");
	} break;
	case JMIR_OP_ADD:
	case JMIR_OP_SUB:
	case JMIR_OP_IMUL:
	case JMIR_OP_AND:
	case JMIR_OP_OR:
	case JMIR_OP_SAR:
	case JMIR_OP_SHR:
	case JMIR_OP_SHL: 
	case JMIR_OP_ADDPS:
	case JMIR_OP_ADDSS:
	case JMIR_OP_ADDPD:
	case JMIR_OP_ADDSD:
	case JMIR_OP_ANDNPS:
	case JMIR_OP_ANDNPD:
	case JMIR_OP_ANDPS:
	case JMIR_OP_ANDPD:
	case JMIR_OP_DIVPS:
	case JMIR_OP_DIVSS:
	case JMIR_OP_DIVPD:
	case JMIR_OP_DIVSD:
	case JMIR_OP_MAXPS:
	case JMIR_OP_MAXSS:
	case JMIR_OP_MAXPD:
	case JMIR_OP_MAXSD:
	case JMIR_OP_MINPS:
	case JMIR_OP_MINSS:
	case JMIR_OP_MINPD:
	case JMIR_OP_MINSD:
	case JMIR_OP_MULPS:
	case JMIR_OP_MULSS:
	case JMIR_OP_MULPD:
	case JMIR_OP_MULSD:
	case JMIR_OP_ORPS:
	case JMIR_OP_ORPD:
	case JMIR_OP_SUBPS:
	case JMIR_OP_SUBSS:
	case JMIR_OP_SUBPD:
	case JMIR_OP_SUBSD:
	case JMIR_OP_UNPCKHPS:
	case JMIR_OP_UNPCKHPD:
	case JMIR_OP_UNPCKLPS:
	case JMIR_OP_UNPCKLPD:
	case JMIR_OP_PUNPCKLBW:
	case JMIR_OP_PUNPCKLWD:
	case JMIR_OP_PUNPCKLDQ:
	case JMIR_OP_PUNPCKLQDQ:
	case JMIR_OP_PUNPCKHBW:
	case JMIR_OP_PUNPCKHWD:
	case JMIR_OP_PUNPCKHDQ:
	case JMIR_OP_PUNPCKHQDQ: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddUse(annot, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, src->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, src->u.m_MemRef->m_IndexReg);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddUse(annot, dst->u.m_Reg); // binary operators use both src and dst operands.
			jmir_instrAddDef(annot, dst->u.m_Reg);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, dst->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, dst->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_XOR: 
	case JMIR_OP_XORPS: 
	case JMIR_OP_XORPD: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (src->m_Kind == JMIR_OPERAND_REGISTER && dst->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regEqual(src->u.m_Reg, dst->u.m_Reg)) {
			// Special case: 
			// xor reg, reg only defines reg and has no uses
			jmir_instrAddDef(annot, src->u.m_Reg);
		} else {
			if (src->m_Kind == JMIR_OPERAND_REGISTER) {
				jmir_instrAddUse(annot, src->u.m_Reg);
			} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
				jmir_instrAddUse(annot, src->u.m_MemRef->m_BaseReg);
				jmir_instrAddUse(annot, src->u.m_MemRef->m_IndexReg);
			}

			if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
				jmir_instrAddUse(annot, dst->u.m_Reg); // binary operators use both src and dst operands.
				jmir_instrAddDef(annot, dst->u.m_Reg);
			} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
				jmir_instrAddUse(annot, dst->u.m_MemRef->m_BaseReg);
				jmir_instrAddUse(annot, dst->u.m_MemRef->m_IndexReg);
			}
		}
	} break;
	case JMIR_OP_LEA: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, src->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, src->u.m_MemRef->m_IndexReg);
		} else if (src->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			// NOTE: External symbols are RIP based so there is no register to use.
		} else {
			JX_CHECK(false, "lea source operand expected to be a memory ref or a stack object.");
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "lea destination operand expected to be a register.");
		jmir_instrAddDef(annot, dst->u.m_Reg);
	} break;
	case JMIR_OP_CALL: {
		jx_mir_operand_t* funcOp = instr->m_Operands[0];
		if (funcOp->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddUse(annot, funcOp->u.m_Reg);
		} else {
			JX_CHECK(funcOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL, "TODO: Handle call [memRef]/[stack object]?");
		}

		jx_mir_function_proto_t* funcProto = instr->m_FuncProto;
		if (!funcProto) {
			for (uint32_t iRegArg = 0; iRegArg < JX_COUNTOF(kMIRFuncArgIReg); ++iRegArg) {
				jmir_instrAddUse(annot, kMIRFuncArgIReg[iRegArg]);
			}
			for (uint32_t iRegArg = 0; iRegArg < JX_COUNTOF(kMIRFuncArgFReg); ++iRegArg) {
				jmir_instrAddUse(annot, kMIRFuncArgFReg[iRegArg]);
			}
		} else {
			const uint32_t numArgs = jx_min_u32(funcProto->m_NumArgs, JX_COUNTOF(kMIRFuncArgIReg));
			for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
				jx_mir_reg_class argClass = jx_mir_typeGetClass(funcProto->m_Args[iArg]);
				if (argClass == JMIR_REG_CLASS_GP) {
					jmir_instrAddUse(annot, kMIRFuncArgIReg[iArg]);
				} else if (argClass == JMIR_REG_CLASS_XMM) {
					jmir_instrAddUse(annot, kMIRFuncArgFReg[iArg]);
				} else {
					JX_CHECK(false, "Unknown register class");
				}
			}

			if ((funcProto->m_Flags & JMIR_FUNC_PROTO_FLAGS_VARARG_Msk) != 0) {
				for (uint32_t iArg = numArgs; iArg < JX_COUNTOF(kMIRFuncArgIReg); ++iArg) {
					jmir_instrAddUse(annot, kMIRFuncArgIReg[iArg]);
				}
				for (uint32_t iArg = numArgs; iArg < JX_COUNTOF(kMIRFuncArgFReg); ++iArg) {
					jmir_instrAddUse(annot, kMIRFuncArgFReg[iArg]);
				}
			}
		}

		// TODO: If the called function is not an external function we might be able 
		// to def only the caller-saved regs touched by the function.
		{
			const uint32_t numCallerSavedIRegs = JX_COUNTOF(kMIRFuncCallerSavedIReg);
			for (uint32_t iReg = 0; iReg < numCallerSavedIRegs; ++iReg) {
				jmir_instrAddDef(annot, kMIRFuncCallerSavedIReg[iReg]);
			}

			const uint32_t numCallerSavedFRegs = JX_COUNTOF(kMIRFuncCallerSavedFReg);
			for (uint32_t iReg = 0; iReg < numCallerSavedFRegs; ++iReg) {
				jmir_instrAddDef(annot, kMIRFuncCallerSavedFReg[iReg]);
			}
		}
	} break;
	case JMIR_OP_PUSH: {
		jx_mir_operand_t* op = instr->m_Operands[0];
		if (op->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddUse(annot, op->u.m_Reg);
		} else if (op->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, op->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, op->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_POP: {
		jx_mir_operand_t* op = instr->m_Operands[0];
		if (op->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddDef(annot, op->u.m_Reg);
		} else if (op->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, op->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, op->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_CDQ:
	case JMIR_OP_CQO: {
		jmir_instrAddUse(annot, kMIRRegGP_A);
		jmir_instrAddDef(annot, kMIRRegGP_D);
	} break;
	case JMIR_OP_SETO:
	case JMIR_OP_SETNO:
	case JMIR_OP_SETB:
	case JMIR_OP_SETNB:
	case JMIR_OP_SETE:
	case JMIR_OP_SETNE:
	case JMIR_OP_SETBE:
	case JMIR_OP_SETNBE:
	case JMIR_OP_SETS:
	case JMIR_OP_SETNS:
	case JMIR_OP_SETP:
	case JMIR_OP_SETNP:
	case JMIR_OP_SETL:
	case JMIR_OP_SETNL:
	case JMIR_OP_SETLE:
	case JMIR_OP_SETNLE: {
		jx_mir_operand_t* src = instr->m_Operands[0];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_instrAddDef(annot, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_instrAddUse(annot, src->u.m_MemRef->m_BaseReg);
			jmir_instrAddUse(annot, src->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_JO:
	case JMIR_OP_JNO:
	case JMIR_OP_JB:
	case JMIR_OP_JNB:
	case JMIR_OP_JE:
	case JMIR_OP_JNE:
	case JMIR_OP_JBE:
	case JMIR_OP_JNBE:
	case JMIR_OP_JS:
	case JMIR_OP_JNS:
	case JMIR_OP_JP:
	case JMIR_OP_JNP:
	case JMIR_OP_JL:
	case JMIR_OP_JNL:
	case JMIR_OP_JLE:
	case JMIR_OP_JNLE:
	case JMIR_OP_JMP: {
		jx_mir_operand_t* src = instr->m_Operands[0];
		JX_CHECK(src->m_Kind == JMIR_OPERAND_BASIC_BLOCK, "I don't know how to handle non-basic block jump targets atm!");
	} break;
	default:
		JX_CHECK(false, "Unknown mir opcode!");
		break;
	}

	for (uint32_t iDef = annot->m_NumDefs; iDef < JMIR_MAX_INSTR_DEFS; ++iDef) {
		annot->m_Defs[iDef] = kMIRRegGPNone;
	}
	for (uint32_t iUse = annot->m_NumUses; iUse < JMIR_MAX_INSTR_USES; ++iUse) {
		annot->m_Uses[iUse] = kMIRRegGPNone;
	}

	return true;
}

bool jx_mir_instrIsMovRegReg(jx_mir_instruction_t* instr)
{
	const bool isMov = false
		|| instr->m_OpCode == JMIR_OP_MOV
		|| instr->m_OpCode == JMIR_OP_MOVSS
		|| instr->m_OpCode == JMIR_OP_MOVSD
		|| instr->m_OpCode == JMIR_OP_MOVAPS
		|| instr->m_OpCode == JMIR_OP_MOVAPD
		;
	if (!isMov) {
		return false;
	}

	jx_mir_operand_t* dst = instr->m_Operands[0];
	jx_mir_operand_t* src = instr->m_Operands[1];
	return dst->m_Kind == JMIR_OPERAND_REGISTER
		&& src->m_Kind == JMIR_OPERAND_REGISTER
		&& jx_mir_regIsSameClass(dst->u.m_Reg, src->u.m_Reg)
		;
}

static jx_mir_operand_t* jmir_funcCreateArgument(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb, uint32_t argID, jx_mir_type_kind argType)
{
	jx_mir_operand_t* vReg = jx_mir_opVirtualReg(ctx, func, argType);

	if (jx_mir_typeIsFloatingPoint(argType)) {
		jx_mir_operand_t* src = (argID < JX_COUNTOF(kMIRFuncArgIReg))
			? jx_mir_opHWReg(ctx, func, argType, kMIRFuncArgFReg[argID])
			: jx_mir_opMemoryRef(ctx, func, argType, kMIRRegGP_BP, kMIRRegGPNone, 1, 16 + argID * 8) // TODO: Is this correct?
			;

		if (argType == JMIR_TYPE_F32) {
			jx_mir_bbAppendInstr(ctx, bb, jx_mir_movss(ctx, vReg, src));
		} else if (argType == JMIR_TYPE_F64) {
			jx_mir_bbAppendInstr(ctx, bb, jx_mir_movsd(ctx, vReg, src));
		} else if (argType == JMIR_TYPE_F128) {
			jx_mir_bbAppendInstr(ctx, bb, jx_mir_movaps(ctx, vReg, src));
		} else {
			JX_CHECK(false, "Unknown floating point type");
		}
	} else {
		jx_mir_operand_t* src = (argID < JX_COUNTOF(kMIRFuncArgIReg))
			? jx_mir_opHWReg(ctx, func, argType, kMIRFuncArgIReg[argID])
			: jx_mir_opMemoryRef(ctx, func, argType, kMIRRegGP_BP, kMIRRegGPNone, 1, 16 + argID * 8)
			;

		jx_mir_bbAppendInstr(ctx, bb, jx_mir_mov(ctx, vReg, src));
	}

	return vReg;
}

static void jmir_funcFree(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jx_mir_scc_t* scc = func->m_SCCListHead;
	while (scc) {
		jx_mir_scc_t* sccNext = scc->m_Next;
		jmir_sccFree(ctx, scc);
		scc = sccNext;
	}

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_basic_block_t* nextBB = bb->m_Next;
		jx_mir_bbFree(ctx, bb);
		bb = nextBB;
	}

	if (func->m_FrameInfo) {
		jmir_frameDestroy(ctx, func->m_FrameInfo);
		func->m_FrameInfo = NULL;
	}
}

static jx_mir_function_pass_t* jmir_funcPassCreate(jx_mir_context_t* ctx, jmirFuncPassCtorFunc ctorFunc, void* passConfig)
{
	jx_mir_function_pass_t* pass = (jx_mir_function_pass_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_function_pass_t));
	if (!pass) {
		return NULL;
	}

	jx_memset(pass, 0, sizeof(jx_mir_function_pass_t));
	if (!ctorFunc(pass, ctx->m_Allocator)) {
		return NULL;
	}

	return pass;
}

static void jmir_funcPassDestroy(jx_mir_context_t* ctx, jx_mir_function_pass_t* pass)
{
	pass->destroy(pass->m_Inst, ctx->m_Allocator);
}

static bool jmir_funcPassApply(jx_mir_context_t* ctx, jx_mir_function_pass_t* pass, jx_mir_function_t* func)
{
	return pass->run(pass->m_Inst, ctx, func);
}

static void jmir_globalVarFree(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv)
{
	jx_array_free(gv->m_RelocationsArr);
	jx_array_free(gv->m_DataArr);
}

static jx_mir_memory_ref_t* jmir_memRefAlloc(jx_mir_context_t* ctx, jx_mir_reg_t baseReg, jx_mir_reg_t indexReg, uint32_t scale, int32_t displacement)
{
	jx_mir_memory_ref_t* memRef = (jx_mir_memory_ref_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_memory_ref_t));
	if (!memRef) {
		return NULL;
	}

	jx_memset(memRef, 0, sizeof(jx_mir_memory_ref_t));
	memRef->m_BaseReg = baseReg;
	memRef->m_IndexReg = indexReg;
	memRef->m_Scale = scale;
	memRef->m_Displacement = displacement;

	return memRef;
}

static jx_mir_frame_info_t* jmir_frameCreate(jx_mir_context_t* ctx)
{
	jx_mir_frame_info_t* fi = (jx_mir_frame_info_t*)JX_ALLOC(ctx->m_LinearAllocator, sizeof(jx_mir_frame_info_t));
	if (!fi) {
		return NULL;
	}

	jx_memset(fi, 0, sizeof(jx_mir_frame_info_t));
	fi->m_StackObjArr = (jx_mir_memory_ref_t**)jx_array_create(ctx->m_Allocator);
	if (!fi->m_StackObjArr) {
		jmir_frameDestroy(ctx, fi);
		return NULL;
	}

	return fi;
}

static void jmir_frameDestroy(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo)
{
	jx_array_free(frameInfo->m_StackObjArr);
}

static jx_mir_memory_ref_t* jmir_frameAllocObj(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t sz, uint32_t alignment)
{
	jx_mir_memory_ref_t* obj = jmir_memRefAlloc(ctx, kMIRRegGP_SP, kMIRRegGPNone, 1, jx_roundup_u32(frameInfo->m_Size, alignment));
	if (!obj) {
		return NULL;
	}

	frameInfo->m_Size = obj->m_Displacement + sz;

	jx_array_push_back(frameInfo->m_StackObjArr, obj);

	return obj;
}

static jx_mir_memory_ref_t* jmir_frameObjRel(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, jx_mir_memory_ref_t* baseObj, int32_t offset)
{
	jx_mir_memory_ref_t* obj = jmir_memRefAlloc(ctx, baseObj->m_BaseReg, baseObj->m_IndexReg, baseObj->m_Scale, baseObj->m_Displacement + offset);
	if (!obj) {
		return NULL;
	}

	jx_array_push_back(frameInfo->m_StackObjArr, obj);

	return obj;
}

static void jmir_frameMakeRoomForCall(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t numArguments)
{
	// This might be the first call for the current frame. Make sure there is a 
	// shadow space for at least 4 arguments. This is needed even if the called
	// function has less than 4 arguments.
	const uint32_t maxCallArgs = jx_max_u32(numArguments, 4);
	if (maxCallArgs <= frameInfo->m_MaxCallArgs) {
		// Already have enough space for that many arguments.
		return;
	}

	const uint32_t delta = (maxCallArgs - frameInfo->m_MaxCallArgs) * 8;

	// Move all local variables.
	const uint32_t numStackObjects = (uint32_t)jx_array_sizeu(frameInfo->m_StackObjArr);
	for (uint32_t iObj = 0; iObj < numStackObjects; ++iObj) {
		jx_mir_memory_ref_t* obj = frameInfo->m_StackObjArr[iObj];
		obj->m_Displacement += delta;
	}

	frameInfo->m_MaxCallArgs = maxCallArgs;
	frameInfo->m_Size += delta;
}

static void jmir_frameFinalize(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo)
{
	frameInfo->m_Size = jx_roundup_u32(frameInfo->m_Size, 16);
}

static uint64_t jmir_funcProtoHashCallback(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jx_mir_function_proto_t* proto = *(const jx_mir_function_proto_t**)item;
	
	uint64_t hash = jx_hashFNV1a(&proto->m_RetType, sizeof(jx_mir_type_kind), seed0, seed1);
	hash = jx_hashFNV1a(&proto->m_Flags, sizeof(uint32_t), hash, seed1);
	hash = jx_hashFNV1a(&proto->m_NumArgs, sizeof(uint32_t), hash, seed1);

	if (proto->m_Args) {
		hash = jx_hashFNV1a(proto->m_Args, sizeof(jx_mir_type_kind) * proto->m_NumArgs, hash, seed1);
	}

	return hash;
}

static int32_t jmir_funcProtoCompareCallback(const void* a, const void* b, void* udata)
{
	const jx_mir_function_proto_t* cA = *(const jx_mir_function_proto_t**)a;
	const jx_mir_function_proto_t* cB = *(const jx_mir_function_proto_t**)b;

	int32_t res = cA->m_RetType < cB->m_RetType
		? -1
		: (cA->m_RetType > cB->m_RetType ? 1 : 0)
		;
	if (res != 0) {
		return res;
	}

	res = cA->m_Flags < cB->m_Flags
		? -1
		: (cA->m_Flags > cB->m_Flags ? 1 : 0)
		;
	if (res != 0) {
		return res;
	}

	res = cA->m_NumArgs < cB->m_NumArgs
		? -1
		: (cA->m_NumArgs > cB->m_NumArgs ? 1 : 0)
		;
	if (res != 0) {
		return res;
	}

	const uint32_t numArgs = cA->m_NumArgs;
	for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
		jx_mir_type_kind argA = cA->m_Args[iArg];
		jx_mir_type_kind argB = cB->m_Args[iArg];
		res = argA < argB
			? -1
			: (argA > argB ? 1 : 0)
			;
		if (res != 0) {
			return res;
		}
	}

	return 0;
}

static jx_mir_scc_t* jmir_sccAlloc(jx_mir_context_t* ctx)
{
	jx_mir_scc_t* scc = (jx_mir_scc_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_scc_t));
	if (!scc) {
		return NULL;
	}

	jx_memset(scc, 0, sizeof(jx_mir_scc_t));
	scc->m_BasicBlockArr = (jx_mir_basic_block_t**)jx_array_create(ctx->m_Allocator);
	if (!scc->m_BasicBlockArr) {
		jmir_sccFree(ctx, scc);
		return NULL;
	}

	return scc;
}

static void jmir_sccFree(jx_mir_context_t* ctx, jx_mir_scc_t* scc)
{
	jx_array_free(scc->m_BasicBlockArr);
	
	jx_mir_scc_t* child = scc->m_FirstChild;
	while (child) {
		jx_mir_scc_t* childNext = child->m_Next;
		jmir_sccFree(ctx, child);
		child = childNext;
	}

	JX_FREE(ctx->m_Allocator, scc);
}