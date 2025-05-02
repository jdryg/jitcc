#include "jit_gen.h"
#include "jit.h"
#include "jmir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>

#include <stdlib.h> // calloc
#include <stdio.h>  // printf
#include <memory.h> // memset/memcpy

typedef bool (*jx64VoidFunc)(jx_x64_context_t* ctx);
typedef bool (*jx64UnaryFunc)(jx_x64_context_t* ctx, jx_x64_operand_t op);
typedef bool (*jx64BinaryFunc)(jx_x64_context_t* ctx, jx_x64_operand_t op1, jx_x64_operand_t op2);
typedef bool (*jx64CondFunc)(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_operand_t op);

typedef enum jx64gen_instr_kind
{
	JX64GEN_INSTR_UNKNOWN = 0,
	JX64GEN_INSTR_VOID = 1,
	JX64GEN_INSTR_UNARY = 2,
	JX64GEN_INSTR_BINARY = 3,
	JX64GEN_INSTR_COND = 4,
} jx64gen_instr_kind;

typedef struct jx64gen_instr_desc_t
{
	jx64gen_instr_kind m_Kind;
	JX_PAD(4);
	union
	{
		jx64VoidFunc m_VoidFunc;
		jx64UnaryFunc m_UnaryFunc;
		jx64BinaryFunc m_BinaryFunc;
		struct
		{
			jx64CondFunc m_Func;
			uint32_t m_CCBase;
		} m_Cond;
	} u;
} jx64gen_instr_desc_t;

static const jx64gen_instr_desc_t kInstrDesc[] = {
	[JMIR_OP_RET]       = { .m_Kind = JX64GEN_INSTR_VOID,   .u.m_VoidFunc = jx64_retn },
	[JMIR_OP_CMP]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_cmp },
	[JMIR_OP_TEST]      = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_test },
	[JMIR_OP_JMP]       = { .m_Kind = JX64GEN_INSTR_UNARY,  .u.m_UnaryFunc = jx64_jmp },
	[JMIR_OP_PHI]       = { 0 },
	[JMIR_OP_MOV]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_mov },
	[JMIR_OP_MOVSX]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_movsx },
	[JMIR_OP_MOVZX]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_movzx },
	[JMIR_OP_IMUL]      = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_imul },
	[JMIR_OP_IDIV]      = { .m_Kind = JX64GEN_INSTR_UNARY,  .u.m_UnaryFunc = jx64_idiv },
	[JMIR_OP_DIV]       = { .m_Kind = JX64GEN_INSTR_UNARY,  .u.m_UnaryFunc = jx64_idiv },
	[JMIR_OP_ADD]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_add },
	[JMIR_OP_SUB]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_sub },
	[JMIR_OP_LEA]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_lea },
	[JMIR_OP_XOR]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_xor },
	[JMIR_OP_AND]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_and },
	[JMIR_OP_OR]        = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_or },
	[JMIR_OP_SAR]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_sar },
	[JMIR_OP_SHR]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_shr },
	[JMIR_OP_SHL]       = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_shl },
	[JMIR_OP_CALL]      = { .m_Kind = JX64GEN_INSTR_UNARY,  .u.m_UnaryFunc = jx64_call },
	[JMIR_OP_PUSH]      = { .m_Kind = JX64GEN_INSTR_UNARY,  .u.m_UnaryFunc = jx64_push },
	[JMIR_OP_POP]       = { .m_Kind = JX64GEN_INSTR_UNARY,  .u.m_UnaryFunc = jx64_pop },
	[JMIR_OP_CDQ]       = { .m_Kind = JX64GEN_INSTR_VOID,   .u.m_VoidFunc = jx64_cdq },
	[JMIR_OP_CQO]       = { .m_Kind = JX64GEN_INSTR_VOID,   .u.m_VoidFunc = jx64_cqo },
	[JMIR_OP_SETO]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNO]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETB]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNB]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETE]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNE]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETBE]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNBE]    = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETS]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNS]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETP]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNP]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETL]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNL]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETLE]     = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_SETNLE]    = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_CCBase = JMIR_OP_SETCC_BASE },
	[JMIR_OP_JO]        = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNO]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JB]        = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNB]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JE]        = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNE]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JBE]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNBE]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JS]        = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNS]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JP]        = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNP]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JL]        = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNL]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JLE]       = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_JNLE]      = { .m_Kind = JX64GEN_INSTR_COND,   .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_CCBase = JMIR_OP_JCC_BASE },
	[JMIR_OP_MOVSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_movss },
	[JMIR_OP_MOVSD]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_movsd },
	[JMIR_OP_MOVD]      = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_movd },
	[JMIR_OP_MOVQ]      = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_movq },
	[JMIR_OP_ADDPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_addps },
	[JMIR_OP_ADDSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_addss },
	[JMIR_OP_ANDNPS]    = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_andnps },
	[JMIR_OP_ANDPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_andps },
	[JMIR_OP_COMISS]    = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_comiss },
	[JMIR_OP_CVTSI2SS]  = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_cvtsi2ss },
	[JMIR_OP_CVTSS2SI]  = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_cvtss2si },
	[JMIR_OP_CVTTSS2SI] = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_cvttss2si },
	[JMIR_OP_CVTSD2SS]  = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_cvtsd2ss },
	[JMIR_OP_CVTSS2SD]  = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_cvtss2sd },
	[JMIR_OP_DIVPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_divps },
	[JMIR_OP_DIVSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_divss },
	[JMIR_OP_MAXPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_maxps },
	[JMIR_OP_MAXSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_maxss },
	[JMIR_OP_MINPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_minps },
	[JMIR_OP_MINSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_minss },
	[JMIR_OP_MULPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_mulps },
	[JMIR_OP_MULSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_mulss },
	[JMIR_OP_ORPS]      = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_orps },
	[JMIR_OP_RCPPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_rcpps },
	[JMIR_OP_RCPSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_rcpss },
	[JMIR_OP_RSQRTPS]   = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_rsqrtps },
	[JMIR_OP_RSQRTSS]   = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_rsqrtss },
	[JMIR_OP_SQRTPS]    = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_sqrtps },
	[JMIR_OP_SQRTSS]    = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_sqrtss },
	[JMIR_OP_SUBPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_subps },
	[JMIR_OP_SUBSS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_subss },
	[JMIR_OP_UCOMISS]   = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_ucomiss },
	[JMIR_OP_UNPCKHPS]  = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_unpckhps },
	[JMIR_OP_UNPCKLPS]  = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_unpcklps },
	[JMIR_OP_XORPS]     = { .m_Kind = JX64GEN_INSTR_BINARY, .u.m_BinaryFunc = jx64_xorps },
};

typedef struct jx64gen_context_t
{
	jx_allocator_i* m_Allocator;
	jx_x64_context_t* m_JITCtx;
	jx_mir_context_t* m_MIRCtx;
	jx_x64_symbol_t** m_GlobalVars;
	jx_x64_symbol_t** m_Funcs;
	jx_x64_label_t** m_BasicBlocks;
	uint32_t m_NumGlobalVars;
	uint32_t m_NumFuncs;
	uint32_t m_NumBasicBlocks;
	JX_PAD(4);
} jx64gen_context_t;

static jx_x64_operand_t jx_x64gen_convertMIROperand(jx64gen_context_t* ctx, const jx_mir_operand_t* mirOp);
static jx_x64_size jx_x64gen_convertMIRTypeToSize(jx_mir_type_kind type);
static jx_x64_reg jx_x64gen_convertMIRReg(jx_mir_reg_t mirReg, jx_x64_size sz);
static jx_x64_scale jx_x64gen_convertMIRScale(uint32_t mirScale);

bool jx_x64_emitCode(jx_x64_context_t* jitCtx, jx_mir_context_t* mirCtx, jx_allocator_i* allocator)
{
	jx64gen_context_t* ctx = &(jx64gen_context_t) { 0 };

	ctx->m_Allocator = allocator;
	ctx->m_JITCtx = jitCtx;
	ctx->m_MIRCtx = mirCtx;

	// Declare global variables.
	ctx->m_NumGlobalVars = jx_mir_getNumGlobalVars(mirCtx);
	ctx->m_GlobalVars = (jx_x64_symbol_t**)JX_ALLOC(allocator, sizeof(jx_x64_symbol_t*) * ctx->m_NumGlobalVars);
	if (!ctx->m_GlobalVars) {
		return false;
	}

	jx_memset(ctx->m_GlobalVars, 0, sizeof(jx_x64_symbol_t*) * ctx->m_NumGlobalVars);
	for (uint32_t iGV = 0; iGV < ctx->m_NumGlobalVars; ++iGV) {
		jx_mir_global_variable_t* mirGV = jx_mir_getGlobalVarByID(mirCtx, iGV);

		const char* gvName = jx_strrchr(mirGV->m_Name, ':');
		if (gvName) {
			++gvName;
		} else {
			gvName = mirGV->m_Name;
		}

		ctx->m_GlobalVars[iGV] = jx64_globalVarDeclare(jitCtx, gvName);
	}

	// Declare functions
	ctx->m_NumFuncs = jx_mir_getNumFunctions(mirCtx);
	ctx->m_Funcs = (jx_x64_symbol_t**)JX_ALLOC(allocator, sizeof(jx_x64_symbol_t*) * ctx->m_NumFuncs);
	if (!ctx->m_Funcs) {
		return false;
	}

	jx_memset(ctx->m_Funcs, 0, sizeof(jx_x64_symbol_t*) * ctx->m_NumFuncs);
	for (uint32_t iFunc = 0; iFunc < ctx->m_NumFuncs; ++iFunc) {
		jx_mir_function_t* mirFunc = jx_mir_getFunctionByID(mirCtx, iFunc);

		const char* funcName = jx_strrchr(mirFunc->m_Name, ':');
		if (funcName) {
			++funcName;
		} else {
			funcName = mirFunc->m_Name;
		}

		ctx->m_Funcs[iFunc] = jx64_funcDeclare(jitCtx, funcName);
	}

	// Emit functions
	for (uint32_t iFunc = 0; iFunc < ctx->m_NumFuncs; ++iFunc) {
		jx_mir_function_t* mirFunc = jx_mir_getFunctionByID(mirCtx, iFunc);

		if (mirFunc->m_BasicBlockListHead) {
			ctx->m_NumBasicBlocks = mirFunc->m_NextBasicBlockID;
			ctx->m_BasicBlocks = (jx_x64_label_t**)JX_ALLOC(allocator, sizeof(jx_x64_label_t*) * ctx->m_NumBasicBlocks);
			if (!ctx->m_BasicBlocks) {
				return false;
			}

			jx_memset(ctx->m_BasicBlocks, 0, sizeof(jx_x64_label_t*) * ctx->m_NumBasicBlocks);
			for (uint32_t iBB = 0; iBB < ctx->m_NumBasicBlocks; ++iBB) {
				ctx->m_BasicBlocks[iBB] = jx64_labelAlloc(jitCtx, JX64_SECTION_TEXT);
			}

			jx64_funcBegin(jitCtx, ctx->m_Funcs[iFunc]);

			jx_mir_basic_block_t* mirBB = mirFunc->m_BasicBlockListHead;
			while (mirBB) {
				jx64_labelBind(jitCtx, ctx->m_BasicBlocks[mirBB->m_ID]);

				jx_mir_instruction_t* mirInstr = mirBB->m_InstrListHead;
				while (mirInstr) {
					JX_CHECK(mirInstr->m_OpCode < JX_COUNTOF(kInstrDesc), "Unknown opcode!");
					const jx64gen_instr_desc_t* desc = &kInstrDesc[mirInstr->m_OpCode];
					switch (desc->m_Kind) {
					case JX64GEN_INSTR_VOID: {
						desc->u.m_VoidFunc(jitCtx);
					} break;
					case JX64GEN_INSTR_UNARY: {
						jx_x64_operand_t op = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[0]);
						desc->u.m_UnaryFunc(jitCtx, op);
					} break;
					case JX64GEN_INSTR_BINARY: {
						jx_x64_operand_t op1 = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[0]);
						jx_x64_operand_t op2 = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[1]);
						desc->u.m_BinaryFunc(jitCtx, op1, op2);
					} break;
					case JX64GEN_INSTR_COND: {
						jx_x64_operand_t op = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[0]);
						jx_x64_condition_code cc = (jx_x64_condition_code)(mirInstr->m_OpCode - desc->u.m_Cond.m_CCBase);
						desc->u.m_Cond.m_Func(jitCtx, cc, op);
					} break;
					default:
						JX_NOT_IMPLEMENTED();
						break;
					}

					mirInstr = mirInstr->m_Next;
				}

				mirBB = mirBB->m_Next;
			}

			jx64_funcEnd(jitCtx);

			for (uint32_t iBB = 0; iBB < ctx->m_NumBasicBlocks; ++iBB) {
				jx64_labelFree(jitCtx, ctx->m_BasicBlocks[iBB]);
			}
			JX_FREE(allocator, ctx->m_BasicBlocks);
			ctx->m_BasicBlocks = NULL;
		} else {
			// TODO: Emit external function stub here instead of jx64_finalize()?
		}
	}

	// Emit global variables
	for (uint32_t iGV = 0; iGV < ctx->m_NumGlobalVars; ++iGV) {
		jx_mir_global_variable_t* mirGV = jx_mir_getGlobalVarByID(mirCtx, iGV);

		const uint32_t dataSize = (uint32_t)jx_array_sizeu(mirGV->m_DataArr);
		jx64_globalVarDefine(jitCtx, ctx->m_GlobalVars[iGV], mirGV->m_DataArr, dataSize, mirGV->m_Alignment);

		const uint32_t numRelocations = (uint32_t)jx_array_sizeu(mirGV->m_RelocationsArr);
		for (uint32_t iReloc = 0; iReloc < numRelocations; ++iReloc) {
			jx_mir_relocation_t* mirReloc = &mirGV->m_RelocationsArr[iReloc];
			jx64_symbolAddRelocation(jitCtx, ctx->m_GlobalVars[iGV], JX64_RELOC_ADDR64, mirReloc->m_Offset, mirReloc->m_SymbolName);
		}
	}

	// DEBUG/TEST
	{
		jx_x64_symbol_t* strlenSymbol = jx64_symbolGetByName(jitCtx, "strlen");
		if (strlenSymbol) {
			jx64_symbolSetExternalAddress(jitCtx, strlenSymbol, (void*)jx_strlen);
		}

		jx_x64_symbol_t* callocSymbol = jx64_symbolGetByName(jitCtx, "calloc");
		if (callocSymbol) {
			jx64_symbolSetExternalAddress(jitCtx, callocSymbol, (void*)calloc);
		}

		jx_x64_symbol_t* printfSymbol = jx64_symbolGetByName(jitCtx, "printf");
		if (printfSymbol) {
			jx64_symbolSetExternalAddress(jitCtx, printfSymbol, (void*)printf);
		}

		jx_x64_symbol_t* memsetSymbol = jx64_symbolGetByName(jitCtx, "memset");
		if (memsetSymbol) {
			jx64_symbolSetExternalAddress(jitCtx, memsetSymbol, (void*)memset);
		}

		jx_x64_symbol_t* memcpySymbol = jx64_symbolGetByName(jitCtx, "memcpy");
		if (memcpySymbol) {
			jx64_symbolSetExternalAddress(jitCtx, memcpySymbol, (void*)memcpy);
		}

		jx_x64_symbol_t* sprintfSymbol = jx64_symbolGetByName(jitCtx, "sprintf");
		if (sprintfSymbol) {
			jx64_symbolSetExternalAddress(jitCtx, sprintfSymbol, (void*)sprintf);
		}
	}

	jx64_finalize(jitCtx);

	JX_FREE(allocator, ctx->m_Funcs);
	JX_FREE(allocator, ctx->m_GlobalVars);

	return true;
}

static jx_x64_operand_t jx_x64gen_convertMIROperand(jx64gen_context_t* ctx, const jx_mir_operand_t* mirOp)
{
	jx_x64_operand_t op = jx64_opReg(JX64_REG_NONE);

	switch (mirOp->m_Kind) {
	case JMIR_OPERAND_REGISTER: {
		JX_CHECK(!mirOp->u.m_Reg.m_IsVirtual, "Expected hardware register!");
		switch (mirOp->m_Type) {
		case JMIR_TYPE_VOID: {
			JX_CHECK(false, "Unexpected void register!");
		} break;
		case JMIR_TYPE_I8: {
			JX_CHECK(mirOp->u.m_Reg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register.");
			op = jx64_opReg((jx_x64_reg)(JX64_REG_AL + mirOp->u.m_Reg.m_ID));
		} break;
		case JMIR_TYPE_I16: {
			JX_CHECK(mirOp->u.m_Reg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register.");
			op = jx64_opReg((jx_x64_reg)(JX64_REG_AX + mirOp->u.m_Reg.m_ID));
		} break;
		case JMIR_TYPE_I32: {
			JX_CHECK(mirOp->u.m_Reg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register.");
			op = jx64_opReg((jx_x64_reg)(JX64_REG_EAX + mirOp->u.m_Reg.m_ID));
		} break;
		case JMIR_TYPE_I64:
		case JMIR_TYPE_PTR: {
			JX_CHECK(mirOp->u.m_Reg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register.");
			op = jx64_opReg((jx_x64_reg)(JX64_REG_RAX + mirOp->u.m_Reg.m_ID));
		} break;
		case JMIR_TYPE_F32: {
			JX_CHECK(mirOp->u.m_Reg.m_Class == JMIR_REG_CLASS_XMM, "Expected XMM register.");
			op = jx64_opReg((jx_x64_reg)(JX64_REG_XMM0 + mirOp->u.m_Reg.m_ID));
		} break;
		case JMIR_TYPE_F64: {
			JX_CHECK(mirOp->u.m_Reg.m_Class == JMIR_REG_CLASS_XMM, "Expected XMM register.");
			op = jx64_opReg((jx_x64_reg)(JX64_REG_XMM0 + mirOp->u.m_Reg.m_ID));
		} break;
		default:
			JX_CHECK(false, "Unknown mir type");
			break;
		}
	} break;
	case JMIR_OPERAND_CONST: {
		switch (mirOp->m_Type) {
		case JMIR_TYPE_VOID: {
			JX_CHECK(false, "Unexpected void constant!");
		} break;
		case JMIR_TYPE_I8: {
			op = jx64_opImmI8((int8_t)mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I16: {
			op = jx64_opImmI16((int16_t)mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I32: {
			op = jx64_opImmI32((int32_t)mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I64:
		case JMIR_TYPE_PTR: {
			op = jx64_opImmI64(mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_F32: {
			const float fconst = (float)mirOp->u.m_ConstF64;
			char globalName[256];
			jx_snprintf(globalName, JX_COUNTOF(globalName), "f32c_%08X", (uint32_t)jx_bitcast_f_i32(fconst));
			jx_x64_symbol_t* sym = jx64_symbolGetByName(ctx->m_JITCtx, globalName);
			if (!sym) {
				sym = jx64_globalVarDeclare(ctx->m_JITCtx, globalName);
				jx64_globalVarDefine(ctx->m_JITCtx, sym, (const uint8_t*)&fconst, sizeof(float), 4);
			}

			op = jx64_opSymbol(JX64_SIZE_32, sym);
		} break;
		case JMIR_TYPE_F64: {
			const double dconst = mirOp->u.m_ConstF64;
			char globalName[256];
			jx_snprintf(globalName, JX_COUNTOF(globalName), "f64c_%16X", (uint64_t)jx_bitcast_d_i64(dconst));
			jx_x64_symbol_t* sym = jx64_symbolGetByName(ctx->m_JITCtx, globalName);
			if (!sym) {
				sym = jx64_globalVarDeclare(ctx->m_JITCtx, globalName);
				jx64_globalVarDefine(ctx->m_JITCtx, sym, (const uint8_t*)&dconst, sizeof(double), 8);
			}

			op = jx64_opSymbol(JX64_SIZE_64, sym);
		} break;
		default:
			JX_CHECK(false, "Unknown mir type");
			break;
		}
	} break;
	case JMIR_OPERAND_BASIC_BLOCK: {
		jx_mir_basic_block_t* bb = mirOp->u.m_BB;
		op = jx64_opLbl(JX64_SIZE_64, ctx->m_BasicBlocks[bb->m_ID]);
	} break;
	case JMIR_OPERAND_STACK_OBJECT: {
		jx_x64_size size = jx_x64gen_convertMIRTypeToSize(mirOp->m_Type);
		int32_t disp = mirOp->u.m_StackObj->m_SPOffset;
		op = jx64_opMem(size, JX64_REG_RSP, JX64_REG_NONE, JX64_SCALE_1, disp);
	} break;
	case JMIR_OPERAND_EXTERNAL_SYMBOL: {
		const char* name = mirOp->u.m_ExternalSymbolName;
		jx_x64_symbol_t* symbol = jx64_symbolGetByName(ctx->m_JITCtx, name);
		if (!symbol) {
			// Check if this is an intrinsic function and add a new symbol now.
			const bool isIntrinsic = false
				|| !jx_strcmp(name, "memset")
				|| !jx_strcmp(name, "memcpy")
				;
			if (isIntrinsic) {
				symbol = jx64_funcDeclare(ctx->m_JITCtx, name);
			}
		}
		JX_CHECK(symbol, "Symbol not found!");
		op = jx64_opSymbol(JX64_SIZE_64, symbol);
	} break;
	case JMIR_OPERAND_MEMORY_REF: {
		jx_x64_size size = jx_x64gen_convertMIRTypeToSize(mirOp->m_Type);
		jx_x64_reg baseReg = jx_x64gen_convertMIRReg(mirOp->u.m_MemRef.m_BaseReg, JX64_SIZE_64);
		jx_x64_reg indexReg = jx_x64gen_convertMIRReg(mirOp->u.m_MemRef.m_IndexReg, JX64_SIZE_64);
		jx_x64_scale scale = jx_x64gen_convertMIRScale(mirOp->u.m_MemRef.m_Scale);
		int32_t disp = mirOp->u.m_MemRef.m_Displacement;
		op = jx64_opMem(size, baseReg, indexReg, scale, disp);
	} break;
	default:
		JX_CHECK(false, "Unknown kind of mir operand");
		break;
	}

	return op;
}

static jx_x64_size jx_x64gen_convertMIRTypeToSize(jx_mir_type_kind type)
{
	jx_x64_size sz = JX64_SIZE_8;

	switch (type) {
	case JMIR_TYPE_VOID : {
		JX_CHECK(false, "void does not have a size!");
	} break;
	case JMIR_TYPE_I8: {
		sz = JX64_SIZE_8;
	} break;
	case JMIR_TYPE_I16: {
		sz = JX64_SIZE_16;
	} break;
	case JMIR_TYPE_I32: 
	case JMIR_TYPE_F32: {
		sz = JX64_SIZE_32;
	} break;
	case JMIR_TYPE_I64:
	case JMIR_TYPE_PTR: 
	case JMIR_TYPE_F64: {
		sz = JX64_SIZE_64;
	} break;
	default:
		JX_CHECK(false, "Unknown mir type");
		break;
	}

	return sz;
}

static jx_x64_reg jx_x64gen_convertMIRReg(jx_mir_reg_t mirReg, jx_x64_size sz)
{
	if (mirReg.m_ID == JMIR_HWREGID_NONE) {
		return JX64_REG_NONE;
	}

	JX_CHECK(!mirReg.m_IsVirtual, "Expected hardware register!");

	jx_x64_reg reg = JX64_REG_NONE;
	switch (sz) {
	case JX64_SIZE_8: {
		JX_CHECK(mirReg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register");
		reg = (jx_x64_reg)(JX64_REG_AL + mirReg.m_ID);
	} break;
	case JX64_SIZE_16: {
		JX_CHECK(mirReg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register");
		reg = (jx_x64_reg)(JX64_REG_AX + mirReg.m_ID);
	} break;
	case JX64_SIZE_32: {
		JX_CHECK(mirReg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register");
		reg = (jx_x64_reg)(JX64_REG_EAX + mirReg.m_ID);
	} break;
	case JX64_SIZE_64: {
		JX_CHECK(mirReg.m_Class == JMIR_REG_CLASS_GP, "Expected GP register");
		reg = (jx_x64_reg)(JX64_REG_RAX + mirReg.m_ID);
	} break;
	case JX64_SIZE_128: {
		JX_CHECK(mirReg.m_Class == JMIR_REG_CLASS_XMM, "Expected XMM register");
		reg = (jx_x64_reg)(JX64_REG_XMM0 + mirReg.m_ID);
	} break;
	default:
		JX_CHECK(false, "Unknown size!");
		break;
	}

	return reg;
}

static jx_x64_scale jx_x64gen_convertMIRScale(uint32_t mirScale)
{
	jx_x64_scale scale = JX64_SCALE_1;

	switch (mirScale) {
	case 1:
		scale = JX64_SCALE_1;
		break;
	case 2:
		scale = JX64_SCALE_2;
		break;
	case 4:
		scale = JX64_SCALE_4;
		break;
	case 8:
		scale = JX64_SCALE_8;
		break;
	default:
		JX_CHECK(false, "Invalid scale value");
		break;
	}

	return scale;
}