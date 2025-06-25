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
#include <math.h>   // cosf/sinf
#include <memory.h> // memset/memcpy
#include <string.h> // strcpy
#include <Windows.h>

typedef bool (*jx64VoidFunc)(jx_x64_context_t* ctx);
typedef bool (*jx64UnaryFunc)(jx_x64_context_t* ctx, jx_x64_operand_t op);
typedef bool (*jx64BinaryFunc)(jx_x64_context_t* ctx, jx_x64_operand_t op1, jx_x64_operand_t op2);
typedef bool (*jx64TernaryFunc)(jx_x64_context_t* ctx, jx_x64_operand_t op1, jx_x64_operand_t op2, jx_x64_operand_t op3);
typedef bool (*jx64CondFunc)(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_operand_t op);

typedef enum jx64gen_instr_kind
{
	JX64GEN_INSTR_UNKNOWN = 0,
	JX64GEN_INSTR_VOID = 1,
	JX64GEN_INSTR_UNARY = 2,
	JX64GEN_INSTR_BINARY = 3,
	JX64GEN_INSTR_TERNARY = 4,
	JX64GEN_INSTR_COND = 5,
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
		jx64TernaryFunc m_TernaryFunc;
		struct
		{
			jx64CondFunc m_Func;
			uint32_t m_Code;
			JX_PAD(4);
		} m_Cond;
	} u;
} jx64gen_instr_desc_t;

static const jx64gen_instr_desc_t kInstrDesc[] = {
	[JMIR_OP_RET]        = { .m_Kind = JX64GEN_INSTR_VOID,    .u.m_VoidFunc = jx64_retn },
	[JMIR_OP_CMP]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cmp },
	[JMIR_OP_TEST]       = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_test },
	[JMIR_OP_JMP]        = { .m_Kind = JX64GEN_INSTR_UNARY,   .u.m_UnaryFunc = jx64_jmp },
	[JMIR_OP_MOV]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_mov },
	[JMIR_OP_MOVSX]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movsx },
	[JMIR_OP_MOVZX]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movzx },
	[JMIR_OP_IMUL]       = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_imul },
	[JMIR_OP_IMUL3]      = { .m_Kind = JX64GEN_INSTR_TERNARY, .u.m_TernaryFunc = jx64_imul3 },
	[JMIR_OP_IDIV]       = { .m_Kind = JX64GEN_INSTR_UNARY,   .u.m_UnaryFunc = jx64_idiv },
	[JMIR_OP_DIV]        = { .m_Kind = JX64GEN_INSTR_UNARY,   .u.m_UnaryFunc = jx64_idiv },
	[JMIR_OP_ADD]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_add },
	[JMIR_OP_SUB]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_sub },
	[JMIR_OP_LEA]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_lea },
	[JMIR_OP_XOR]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_xor },
	[JMIR_OP_AND]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_and },
	[JMIR_OP_OR]         = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_or },
	[JMIR_OP_SAR]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_sar },
	[JMIR_OP_SHR]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_shr },
	[JMIR_OP_SHL]        = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_shl },
	[JMIR_OP_CALL]       = { .m_Kind = JX64GEN_INSTR_UNARY,   .u.m_UnaryFunc = jx64_call },
	[JMIR_OP_PUSH]       = { .m_Kind = JX64GEN_INSTR_UNARY,   .u.m_UnaryFunc = jx64_push },
	[JMIR_OP_POP]        = { .m_Kind = JX64GEN_INSTR_UNARY,   .u.m_UnaryFunc = jx64_pop },
	[JMIR_OP_CDQ]        = { .m_Kind = JX64GEN_INSTR_VOID,    .u.m_VoidFunc = jx64_cdq },
	[JMIR_OP_CQO]        = { .m_Kind = JX64GEN_INSTR_VOID,    .u.m_VoidFunc = jx64_cqo },
	[JMIR_OP_SETO]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_O },
	[JMIR_OP_SETNO]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NO },
	[JMIR_OP_SETB]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_B },
	[JMIR_OP_SETNB]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NB },
	[JMIR_OP_SETE]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_E },
	[JMIR_OP_SETNE]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NE },
	[JMIR_OP_SETBE]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_BE },
	[JMIR_OP_SETNBE]     = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NBE },
	[JMIR_OP_SETS]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_S },
	[JMIR_OP_SETNS]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NS },
	[JMIR_OP_SETP]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_P },
	[JMIR_OP_SETNP]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NP },
	[JMIR_OP_SETL]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_L },
	[JMIR_OP_SETNL]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NL },
	[JMIR_OP_SETLE]      = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_LE },
	[JMIR_OP_SETNLE]     = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_setcc, .u.m_Cond.m_Code = JMIR_CC_NLE },
	[JMIR_OP_JO]         = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_O },
	[JMIR_OP_JNO]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NO },
	[JMIR_OP_JB]         = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_B },
	[JMIR_OP_JNB]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NB },
	[JMIR_OP_JE]         = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_E },
	[JMIR_OP_JNE]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NE },
	[JMIR_OP_JBE]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_BE },
	[JMIR_OP_JNBE]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NBE },
	[JMIR_OP_JS]         = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_S },
	[JMIR_OP_JNS]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NS },
	[JMIR_OP_JP]         = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_P },
	[JMIR_OP_JNP]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NP },
	[JMIR_OP_JL]         = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_L },
	[JMIR_OP_JNL]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NL },
	[JMIR_OP_JLE]        = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_LE },
	[JMIR_OP_JNLE]       = { .m_Kind = JX64GEN_INSTR_COND,    .u.m_Cond.m_Func = jx64_jcc, .u.m_Cond.m_Code = JMIR_CC_NLE },
	[JMIR_OP_MOVSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movss },
	[JMIR_OP_MOVSD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movsd },
	[JMIR_OP_MOVAPS]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movaps },
	[JMIR_OP_MOVAPD]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movapd },
	[JMIR_OP_MOVD]       = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movd },
	[JMIR_OP_MOVQ]       = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_movq },
	[JMIR_OP_ADDPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_addps },
	[JMIR_OP_ADDSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_addss },
	[JMIR_OP_ADDPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_addpd },
	[JMIR_OP_ADDSD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_addsd },
	[JMIR_OP_ANDNPS]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_andnps },
	[JMIR_OP_ANDNPD]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_andnpd },
	[JMIR_OP_ANDPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_andps },
	[JMIR_OP_ANDPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_andpd },
	[JMIR_OP_COMISS]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_comiss },
	[JMIR_OP_COMISD]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_comisd },
	[JMIR_OP_CVTSI2SS]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvtsi2ss },
	[JMIR_OP_CVTSI2SD]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvtsi2sd },
	[JMIR_OP_CVTSS2SI]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvtss2si },
	[JMIR_OP_CVTSD2SI]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvtsd2si },
	[JMIR_OP_CVTTSS2SI]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvttss2si },
	[JMIR_OP_CVTTSD2SI]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvttsd2si },
	[JMIR_OP_CVTSD2SS]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvtsd2ss },
	[JMIR_OP_CVTSS2SD]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_cvtss2sd },
	[JMIR_OP_DIVPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_divps },
	[JMIR_OP_DIVSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_divss },
	[JMIR_OP_DIVPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_divpd },
	[JMIR_OP_DIVSD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_divsd },
	[JMIR_OP_MAXPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_maxps },
	[JMIR_OP_MAXSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_maxss },
	[JMIR_OP_MAXPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_maxpd },
	[JMIR_OP_MAXSD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_maxsd },
	[JMIR_OP_MINPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_minps },
	[JMIR_OP_MINSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_minss },
	[JMIR_OP_MINPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_minpd },
	[JMIR_OP_MINSD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_minsd },
	[JMIR_OP_MULPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_mulps },
	[JMIR_OP_MULSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_mulss },
	[JMIR_OP_MULPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_mulpd },
	[JMIR_OP_MULSD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_mulsd },
	[JMIR_OP_ORPS]       = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_orps },
	[JMIR_OP_ORPD]       = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_orpd },
	[JMIR_OP_RCPPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_rcpps },
	[JMIR_OP_RCPSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_rcpss },
	[JMIR_OP_RSQRTPS]    = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_rsqrtps },
	[JMIR_OP_RSQRTSS]    = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_rsqrtss },
	[JMIR_OP_SQRTPS]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_sqrtps },
	[JMIR_OP_SQRTSS]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_sqrtss },
	[JMIR_OP_SQRTPD]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_sqrtpd },
	[JMIR_OP_SQRTSD]     = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_sqrtsd },
	[JMIR_OP_SUBPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_subps },
	[JMIR_OP_SUBSS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_subss },
	[JMIR_OP_SUBPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_subpd },
	[JMIR_OP_SUBSD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_subsd },
	[JMIR_OP_UCOMISS]    = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_ucomiss },
	[JMIR_OP_UCOMISD]    = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_ucomisd },
	[JMIR_OP_UNPCKHPS]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_unpckhps },
	[JMIR_OP_UNPCKHPD]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_unpckhpd },
	[JMIR_OP_UNPCKLPS]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_unpcklps },
	[JMIR_OP_UNPCKLPD]   = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_unpcklpd },
	[JMIR_OP_XORPS]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_xorps },
	[JMIR_OP_XORPD]      = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_xorpd },
	[JMIR_OP_PUNPCKLBW]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpcklbw },
	[JMIR_OP_PUNPCKLWD]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpcklwd },
	[JMIR_OP_PUNPCKLDQ]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpckldq },
	[JMIR_OP_PUNPCKLQDQ] = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpcklqdq },
	[JMIR_OP_PUNPCKHBW]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpckhbw },
	[JMIR_OP_PUNPCKHWD]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpckhwd },
	[JMIR_OP_PUNPCKHDQ]  = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpckhdq },
	[JMIR_OP_PUNPCKHQDQ] = { .m_Kind = JX64GEN_INSTR_BINARY,  .u.m_BinaryFunc = jx64_punpckhqdq },

};

typedef struct jx_x64gen_context_t
{
	jx_allocator_i* m_Allocator;
	jx_x64_context_t* m_JITCtx;
	jx_mir_context_t* m_MIRCtx;
	jx_x64_symbol_t** m_GlobalVars;
	jx_x64_symbol_t** m_Funcs;
	jx_x64_label_t** m_BasicBlocks;
} jx_x64gen_context_t;

static jx_x64_operand_t jx_x64gen_convertMIROperand(jx_x64gen_context_t* ctx, const jx_mir_operand_t* mirOp);
static jx_x64_size jx_x64gen_convertMIRTypeToSize(jx_mir_type_kind type);
static jx_x64_reg jx_x64gen_convertMIRReg(jx_mir_reg_t mirReg, jx_x64_size sz);
static jx_x64_scale jx_x64gen_convertMIRScale(uint32_t mirScale);
static void jx_x64gen_setExternalSymbol(jx_x64_context_t* ctx, const char* name, void* addr);

// TEST/DEBUG
static int func1(int a, int b, int c, int d, int e, int f)
{
	return a == 1 && b == 2 && c == 3 && d == 4 && e == 5 && f == 6
		? 0
		: 1
		;
}

static int func2(float a, double b, float c, double d, float e, float f)
{
	return a == 1.0f && b == 2.0 && c == 3.0f && d == 4.0 && e == 5.0f && f == 6.0f
		? 0
		: 1
		;
}

static int func3(int a, double b, int c, float d, int e, float f)
{
	return a == 1 && b == 2.0 && c == 3 && d == 4.0f && e == 5 && f == 6.0f
		? 0
		: 1
		;
}

static int64_t func5(int a, float b, int c, int d, int e)
{
	return a == 1 && b == 2.0f && c == 3 && d == 4 && e == 5
		? 0
		: 1
		;
}

typedef struct Struct1
{
	int j, k, l;    // Struct1 exceeds 64 bits.
} Struct1;
static Struct1 func7(int a, double b, int c, float d)
{
	return a == 1 && b == 2.0 && c == 3 && d == 4.0f
		? (Struct1) { 1, -1, 0 }
		: (Struct1) { 1, 2, 3 }
		;
}

typedef struct Struct2
{
	int j, k;    // Struct2 fits in 64 bits, and meets requirements for return by value.
} Struct2;
static Struct2 func8(int a, double b, int c, float d)
{
	return a == 1 && b == 2.0 && c == 3 && d == 4.0f
		? (Struct2) { 2, -2 }
		: (Struct2) { 1, 2 }
		;
}

jx_x64gen_context_t* jx_x64gen_createContext(jx_x64_context_t* jitCtx, jx_mir_context_t* mirCtx, jx_allocator_i* allocator)
{
	jx_x64gen_context_t* ctx = (jx_x64gen_context_t*)JX_ALLOC(allocator, sizeof(jx_x64gen_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_x64gen_context_t));
	ctx->m_Allocator = allocator;
	ctx->m_JITCtx = jitCtx;
	ctx->m_MIRCtx = mirCtx;
	ctx->m_GlobalVars = (jx_x64_symbol_t**)jx_array_create(allocator);
	if (!ctx->m_GlobalVars) {
		jx_x64gen_destroyContext(ctx);
		return NULL;
	}

	ctx->m_Funcs = (jx_x64_symbol_t**)jx_array_create(allocator);
	if (!ctx->m_Funcs) {
		jx_x64gen_destroyContext(ctx);
		return NULL;
	}

	ctx->m_BasicBlocks = (jx_x64_label_t**)jx_array_create(allocator);
	if (!ctx->m_BasicBlocks) {
		jx_x64gen_destroyContext(ctx);
		return NULL;
	}

	return ctx;
}

void jx_x64gen_destroyContext(jx_x64gen_context_t* ctx)
{
	if (ctx->m_BasicBlocks) {
		jx_array_free(ctx->m_BasicBlocks);
		ctx->m_BasicBlocks = NULL;
	}

	if (ctx->m_Funcs) {
		jx_array_free(ctx->m_Funcs);
		ctx->m_Funcs = NULL;
	}

	if (ctx->m_GlobalVars) {
		jx_array_free(ctx->m_GlobalVars);
		ctx->m_GlobalVars = NULL;
	}

	JX_FREE(ctx->m_Allocator, ctx);
}

bool jx_x64gen_codeGen(jx_x64gen_context_t* ctx)
{
	jx_mir_context_t* mirCtx = ctx->m_MIRCtx;
	jx_x64_context_t* jitCtx = ctx->m_JITCtx;

	jx_array_resize(ctx->m_GlobalVars, 0);
	jx_array_resize(ctx->m_Funcs, 0);
	jx_array_resize(ctx->m_BasicBlocks, 0);

	// Declare global variables.
	const uint32_t numGlobalVars = jx_mir_getNumGlobalVars(mirCtx);
	for (uint32_t iGV = 0; iGV < numGlobalVars; ++iGV) {
		jx_mir_global_variable_t* mirGV = jx_mir_getGlobalVarByID(mirCtx, iGV);

		const char* gvName = jx_strrchr(mirGV->m_Name, ':');
		if (gvName) {
			++gvName;
		} else {
			gvName = mirGV->m_Name;
		}

		jx_x64_symbol_t* gv = jx64_globalVarDeclare(jitCtx, gvName);
		if (!gv) {
			return false;
		}

		jx_array_push_back(ctx->m_GlobalVars, gv);
	}

	// Declare functions
	const uint32_t numFuncs = jx_mir_getNumFunctions(mirCtx);
	for (uint32_t iFunc = 0; iFunc < numFuncs; ++iFunc) {
		jx_mir_function_t* mirFunc = jx_mir_getFunctionByID(mirCtx, iFunc);

		const char* funcName = jx_strrchr(mirFunc->m_Name, ':');
		if (funcName) {
			++funcName;
		} else {
			funcName = mirFunc->m_Name;
		}

		jx_x64_symbol_t* func = jx64_funcDeclare(jitCtx, funcName);
		if (!func) {
			return false;
		}

		jx_array_push_back(ctx->m_Funcs, func);
	}

	// Emit functions
	for (uint32_t iFunc = 0; iFunc < numFuncs; ++iFunc) {
		jx_mir_function_t* mirFunc = jx_mir_getFunctionByID(mirCtx, iFunc);

		if (mirFunc->m_BasicBlockListHead) {
			const uint32_t numBasicBlocks = mirFunc->m_NextBasicBlockID;
			for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
				jx_x64_label_t* lbl = jx64_labelAlloc(jitCtx, JX64_SECTION_TEXT);
				if (!lbl) {
					return false;
				}

				jx_array_push_back(ctx->m_BasicBlocks, lbl);
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
						if (!desc->u.m_VoidFunc(jitCtx)) {
							JX_CHECK(false, "Failed to emit instruction.");
						}
					} break;
					case JX64GEN_INSTR_UNARY: {
						jx_x64_operand_t op = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[0]);
						if (!desc->u.m_UnaryFunc(jitCtx, op)) {
							JX_CHECK(false, "Failed to emit instruction.");
						}
					} break;
					case JX64GEN_INSTR_BINARY: {
						jx_x64_operand_t op1 = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[0]);
						jx_x64_operand_t op2 = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[1]);
						if (!desc->u.m_BinaryFunc(jitCtx, op1, op2)) {
							JX_CHECK(false, "Failed to emit instruction.");
						}
					} break;
					case JX64GEN_INSTR_TERNARY: {
						jx_x64_operand_t op1 = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[0]);
						jx_x64_operand_t op2 = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[1]);
						jx_x64_operand_t op3 = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[2]);
						if (!desc->u.m_TernaryFunc(jitCtx, op1, op2, op3)) {
							JX_CHECK(false, "Failed to emit instruction.");
						}
					} break;
					case JX64GEN_INSTR_COND: {
						jx_x64_operand_t op = jx_x64gen_convertMIROperand(ctx, mirInstr->m_Operands[0]);
						if (!desc->u.m_Cond.m_Func(jitCtx, desc->u.m_Cond.m_Code, op)) {
							JX_CHECK(false, "Failed to emit instruction.");
						}
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

			for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
				jx64_labelFree(jitCtx, ctx->m_BasicBlocks[iBB]);
			}
			jx_array_resize(ctx->m_BasicBlocks, 0);
		} else {
			// TODO: Emit external function stub here instead of jx64_finalize()?
		}
	}

	// Emit global variables
	for (uint32_t iGV = 0; iGV < numGlobalVars; ++iGV) {
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
		jx_x64gen_setExternalSymbol(jitCtx, "abs", (void*)abs);
		jx_x64gen_setExternalSymbol(jitCtx, "abort", (void*)abort);
		jx_x64gen_setExternalSymbol(jitCtx, "acos", (void*)acos);
		jx_x64gen_setExternalSymbol(jitCtx, "atoi", (void*)atoi);

		jx_x64gen_setExternalSymbol(jitCtx, "calloc", (void*)calloc);
		jx_x64gen_setExternalSymbol(jitCtx, "ceil", (void*)ceil);
		jx_x64gen_setExternalSymbol(jitCtx, "cos", (void*)cos);
		jx_x64gen_setExternalSymbol(jitCtx, "cosf", (void*)cosf);

		jx_x64gen_setExternalSymbol(jitCtx, "fabs", (void*)fabs);
		jx_x64gen_setExternalSymbol(jitCtx, "fclose", (void*)fclose);
		jx_x64gen_setExternalSymbol(jitCtx, "fgetc", (void*)fgetc);
		jx_x64gen_setExternalSymbol(jitCtx, "fgets", (void*)fgets);
		jx_x64gen_setExternalSymbol(jitCtx, "floor", (void*)floor);
		jx_x64gen_setExternalSymbol(jitCtx, "fmod", (void*)fmod);
		jx_x64gen_setExternalSymbol(jitCtx, "fopen", (void*)fopen);
		jx_x64gen_setExternalSymbol(jitCtx, "fprintf", (void*)fprintf);
		jx_x64gen_setExternalSymbol(jitCtx, "fread", (void*)fread);
		jx_x64gen_setExternalSymbol(jitCtx, "free", (void*)free);
		jx_x64gen_setExternalSymbol(jitCtx, "frexp", (void*)frexp);
		jx_x64gen_setExternalSymbol(jitCtx, "fseek", (void*)fseek);
		jx_x64gen_setExternalSymbol(jitCtx, "ftell", (void*)ftell);
		jx_x64gen_setExternalSymbol(jitCtx, "fwrite", (void*)fwrite);

		jx_x64gen_setExternalSymbol(jitCtx, "func1", (void*)func1);
		jx_x64gen_setExternalSymbol(jitCtx, "func2", (void*)func2);
		jx_x64gen_setExternalSymbol(jitCtx, "func3", (void*)func3);
		jx_x64gen_setExternalSymbol(jitCtx, "func5", (void*)func5);
		jx_x64gen_setExternalSymbol(jitCtx, "func7", (void*)func7);
		jx_x64gen_setExternalSymbol(jitCtx, "func8", (void*)func8);

		jx_x64gen_setExternalSymbol(jitCtx, "getc", (void*)getc);

		jx_x64gen_setExternalSymbol(jitCtx, "malloc", (void*)malloc);
		jx_x64gen_setExternalSymbol(jitCtx, "memcmp", (void*)memcmp);
		jx_x64gen_setExternalSymbol(jitCtx, "memcpy", (void*)memcpy);
		jx_x64gen_setExternalSymbol(jitCtx, "memmove", (void*)memmove);
		jx_x64gen_setExternalSymbol(jitCtx, "memset", (void*)memset);

		jx_x64gen_setExternalSymbol(jitCtx, "pow", (void*)pow);
		jx_x64gen_setExternalSymbol(jitCtx, "printf", (void*)printf);
		jx_x64gen_setExternalSymbol(jitCtx, "putchar", (void*)putchar);

		jx_x64gen_setExternalSymbol(jitCtx, "realloc", (void*)realloc);
		jx_x64gen_setExternalSymbol(jitCtx, "roundf", (void*)roundf);

		jx_x64gen_setExternalSymbol(jitCtx, "sin", (void*)sin);
		jx_x64gen_setExternalSymbol(jitCtx, "sinf", (void*)sinf);
		jx_x64gen_setExternalSymbol(jitCtx, "sprintf", (void*)sprintf);
		jx_x64gen_setExternalSymbol(jitCtx, "sqrt", (void*)sqrt);
		jx_x64gen_setExternalSymbol(jitCtx, "strcat", (void*)strcat);
		jx_x64gen_setExternalSymbol(jitCtx, "strchr", (void*)strchr);
		jx_x64gen_setExternalSymbol(jitCtx, "strcmp", (void*)strcmp);
		jx_x64gen_setExternalSymbol(jitCtx, "strcpy", (void*)strcpy);
		jx_x64gen_setExternalSymbol(jitCtx, "strlen", (void*)strlen);
		jx_x64gen_setExternalSymbol(jitCtx, "strncmp", (void*)strncmp);
		jx_x64gen_setExternalSymbol(jitCtx, "strncpy", (void*)strncpy);
		jx_x64gen_setExternalSymbol(jitCtx, "strrchr", (void*)strrchr);

		jx_x64gen_setExternalSymbol(jitCtx, "GetStockObject", (void*)GetStockObject);
		jx_x64gen_setExternalSymbol(jitCtx, "LoadIconA", (void*)LoadIconA);
		jx_x64gen_setExternalSymbol(jitCtx, "LoadCursorA", (void*)LoadCursorA);
		jx_x64gen_setExternalSymbol(jitCtx, "RegisterClassA", (void*)RegisterClassA);
		jx_x64gen_setExternalSymbol(jitCtx, "CreateWindowExA", (void*)CreateWindowExA);
		jx_x64gen_setExternalSymbol(jitCtx, "GetMessageA", (void*)GetMessageA);
		jx_x64gen_setExternalSymbol(jitCtx, "TranslateMessage", (void*)TranslateMessage);
		jx_x64gen_setExternalSymbol(jitCtx, "DispatchMessageA", (void*)DispatchMessageA);
		jx_x64gen_setExternalSymbol(jitCtx, "DefWindowProcA", (void*)DefWindowProcA);
		jx_x64gen_setExternalSymbol(jitCtx, "PostQuitMessage", (void*)PostQuitMessage);
		jx_x64gen_setExternalSymbol(jitCtx, "DestroyWindow", (void*)DestroyWindow);
		jx_x64gen_setExternalSymbol(jitCtx, "EndPaint", (void*)EndPaint);
		jx_x64gen_setExternalSymbol(jitCtx, "DrawTextA", (void*)DrawTextA);
		jx_x64gen_setExternalSymbol(jitCtx, "SetBkMode", (void*)SetBkMode);
		jx_x64gen_setExternalSymbol(jitCtx, "SetTextColor", (void*)SetTextColor);
		jx_x64gen_setExternalSymbol(jitCtx, "GetClientRect", (void*)GetClientRect);
		jx_x64gen_setExternalSymbol(jitCtx, "BeginPaint", (void*)BeginPaint);
		jx_x64gen_setExternalSymbol(jitCtx, "SetWindowPos", (void*)SetWindowPos);
		jx_x64gen_setExternalSymbol(jitCtx, "GetWindowRect", (void*)GetWindowRect);
		jx_x64gen_setExternalSymbol(jitCtx, "GetClientRect", (void*)GetClientRect);
		jx_x64gen_setExternalSymbol(jitCtx, "GetDesktopWindow", (void*)GetDesktopWindow);
		jx_x64gen_setExternalSymbol(jitCtx, "GetParent", (void*)GetParent);
	}

	jx64_finalize(jitCtx);

	return true;
}

static jx_x64_operand_t jx_x64gen_convertMIROperand(jx_x64gen_context_t* ctx, const jx_mir_operand_t* mirOp)
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
		case JMIR_TYPE_F32:
		case JMIR_TYPE_F64:
		case JMIR_TYPE_F128: {
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
			jx_snprintf(globalName, JX_COUNTOF(globalName), "f64c_%016llX", (uint64_t)jx_bitcast_d_i64(dconst));
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
	case JMIR_OPERAND_EXTERNAL_SYMBOL: {
		const char* name = mirOp->u.m_ExternalSymbol.m_Name;
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
		jx_x64_reg baseReg = jx_x64gen_convertMIRReg(mirOp->u.m_MemRef->m_BaseReg, JX64_SIZE_64);
		jx_x64_reg indexReg = jx_x64gen_convertMIRReg(mirOp->u.m_MemRef->m_IndexReg, JX64_SIZE_64);
		jx_x64_scale scale = jx_x64gen_convertMIRScale(mirOp->u.m_MemRef->m_Scale);
		int32_t disp = mirOp->u.m_MemRef->m_Displacement;
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
	case JMIR_TYPE_F128: {
		sz = JX64_SIZE_128;
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

static void jx_x64gen_setExternalSymbol(jx_x64_context_t* ctx, const char* name, void* addr)
{
	jx_x64_symbol_t* sym = jx64_symbolGetByName(ctx, name);
	if (sym) {
		jx64_symbolSetExternalAddress(ctx, sym, addr);
	}
}