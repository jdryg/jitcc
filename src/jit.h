#ifndef JIT_H
#define JIT_H

#include <stdint.h>
#include <stdbool.h>
#include <jlib/macros.h>

typedef struct jx_allocator_i jx_allocator_i;

typedef struct jx_x64_label_t jx_x64_label_t;

typedef enum jx_x64_size
{
	JX64_SIZE_8  = 0,
	JX64_SIZE_16 = 1,
	JX64_SIZE_32 = 2,
	JX64_SIZE_64 = 3,
} jx_x64_size;

typedef enum jx_x64_scale
{
	JX64_SCALE_1 = 0,
	JX64_SCALE_2 = 1,
	JX64_SCALE_4 = 2,
	JX64_SCALE_8 = 3,
} jx_x64_scale;

typedef enum jx_x64_operand_type
{
	JX64_OPERAND_REG,
	JX64_OPERAND_IMM,
	JX64_OPERAND_MEM,
	JX64_OPERAND_LBL,
} jx_x64_operand_type;

typedef enum jx_x64_reg_id
{
	JX64_REG_ID_RAX = 0,
	JX64_REG_ID_RCX = 1, 
	JX64_REG_ID_RDX = 2, 
	JX64_REG_ID_RBX = 3, 
	JX64_REG_ID_RSP = 4, 
	JX64_REG_ID_RBP = 5, 
	JX64_REG_ID_RSI = 6, 
	JX64_REG_ID_RDI = 7, 
	JX64_REG_ID_R8  = 8, 
	JX64_REG_ID_R9  = 9, 
	JX64_REG_ID_R10 = 10, 
	JX64_REG_ID_R11 = 11, 
	JX64_REG_ID_R12 = 12, 
	JX64_REG_ID_R13 = 13, 
	JX64_REG_ID_R14 = 14, 
	JX64_REG_ID_R15 = 15, 
} jx_x64_reg_id;

typedef enum jx_x64_condition_code
{
	JX64_CC_O   = 0,
	JX64_CC_NO  = 1,
	JX64_CC_B   = 2,
	JX64_CC_NB  = 3,
	JX64_CC_E   = 4,
	JX64_CC_NE  = 5,
	JX64_CC_BE  = 6,
	JX64_CC_NBE = 7,
	JX64_CC_S   = 8,
	JX64_CC_NS  = 9,
	JX64_CC_P   = 10,
	JX64_CC_NP  = 11,
	JX64_CC_L   = 12,
	JX64_CC_NL  = 13,
	JX64_CC_LE  = 14,
	JX64_CC_NLE = 15,

	JX64_CC_NAE = JX64_CC_B,
	JX64_CC_AE  = JX64_CC_NB,
	JX64_CC_Z   = JX64_CC_E,
	JX64_CC_NZ  = JX64_CC_NE,
	JX64_CC_NA  = JX64_CC_BE,
	JX64_CC_A   = JX64_CC_NBE,
	JX64_CC_PE  = JX64_CC_P,
	JX64_CC_PO  = JX64_CC_NP,
	JX64_CC_NGE = JX64_CC_L,
	JX64_CC_GE  = JX64_CC_NL,
	JX64_CC_NG  = JX64_CC_LE,
	JX64_CC_G   = JX64_CC_NLE
} jx_x64_condition_code;

#define JX64_REG_ID_Pos          0
#define JX64_REG_ID_Msk          (0x0F << JX64_REG_ID_Pos)
#define JX64_REG_FLAG_Pos        4
#define JX64_REG_FLAG_Msk        (0x01 << JX64_REG_FLAG_Pos)
#define JX64_REG_SIZE_Pos        5
#define JX64_REG_SIZE_Msk        (0x03 << JX64_REG_SIZE_Pos)
#define JX64_REG(id, flag, size) ((((id) << JX64_REG_ID_Pos) & JX64_REG_ID_Msk) | (((flag) << JX64_REG_FLAG_Pos) & JX64_REG_FLAG_Msk) | (((size) << JX64_REG_SIZE_Pos) & JX64_REG_SIZE_Msk))

#define JX64_REG_GET_ID(reg)     (((reg) & JX64_REG_ID_Msk) >> JX64_REG_ID_Pos)
#define JX64_REG_GET_FLAG(reg)   (((reg) & JX64_REG_FLAG_Msk) >> JX64_REG_FLAG_Pos)
#define JX64_REG_GET_SIZE(reg)   (((reg) & JX64_REG_SIZE_Msk) >> JX64_REG_SIZE_Pos)

typedef enum jx_x64_reg
{
	// 64-bit registers
	JX64_REG_RAX = JX64_REG(JX64_REG_ID_RAX, 0, JX64_SIZE_64),
	JX64_REG_RCX = JX64_REG(JX64_REG_ID_RCX, 0, JX64_SIZE_64),
	JX64_REG_RDX = JX64_REG(JX64_REG_ID_RDX, 0, JX64_SIZE_64),
	JX64_REG_RBX = JX64_REG(JX64_REG_ID_RBX, 0, JX64_SIZE_64),
	JX64_REG_RSP = JX64_REG(JX64_REG_ID_RSP, 0, JX64_SIZE_64),
	JX64_REG_RBP = JX64_REG(JX64_REG_ID_RBP, 0, JX64_SIZE_64),
	JX64_REG_RSI = JX64_REG(JX64_REG_ID_RSI, 0, JX64_SIZE_64),
	JX64_REG_RDI = JX64_REG(JX64_REG_ID_RDI, 0, JX64_SIZE_64),
	JX64_REG_R8  = JX64_REG(JX64_REG_ID_R8, 0, JX64_SIZE_64),
	JX64_REG_R9  = JX64_REG(JX64_REG_ID_R9, 0, JX64_SIZE_64),
	JX64_REG_R10 = JX64_REG(JX64_REG_ID_R10, 0, JX64_SIZE_64),
	JX64_REG_R11 = JX64_REG(JX64_REG_ID_R11, 0, JX64_SIZE_64),
	JX64_REG_R12 = JX64_REG(JX64_REG_ID_R12, 0, JX64_SIZE_64),
	JX64_REG_R13 = JX64_REG(JX64_REG_ID_R13, 0, JX64_SIZE_64),
	JX64_REG_R14 = JX64_REG(JX64_REG_ID_R14, 0, JX64_SIZE_64),
	JX64_REG_R15 = JX64_REG(JX64_REG_ID_R15, 0, JX64_SIZE_64),
	JX64_REG_RIP = JX64_REG(JX64_REG_ID_RBP, 1, JX64_SIZE_64), // NOTE: Don't use directly. Use label operands instead

	// 32-bit registers
	JX64_REG_EAX  = JX64_REG(JX64_REG_ID_RAX, 0, JX64_SIZE_32),
	JX64_REG_ECX  = JX64_REG(JX64_REG_ID_RCX, 0, JX64_SIZE_32), 
	JX64_REG_EDX  = JX64_REG(JX64_REG_ID_RDX, 0, JX64_SIZE_32), 
	JX64_REG_EBX  = JX64_REG(JX64_REG_ID_RBX, 0, JX64_SIZE_32), 
	JX64_REG_ESP  = JX64_REG(JX64_REG_ID_RSP, 0, JX64_SIZE_32), 
	JX64_REG_EBP  = JX64_REG(JX64_REG_ID_RBP, 0, JX64_SIZE_32), 
	JX64_REG_ESI  = JX64_REG(JX64_REG_ID_RSI, 0, JX64_SIZE_32), 
	JX64_REG_EDI  = JX64_REG(JX64_REG_ID_RDI, 0, JX64_SIZE_32), 
	JX64_REG_R8D  = JX64_REG(JX64_REG_ID_R8, 0, JX64_SIZE_32), 
	JX64_REG_R9D  = JX64_REG(JX64_REG_ID_R9, 0, JX64_SIZE_32), 
	JX64_REG_R10D = JX64_REG(JX64_REG_ID_R10, 0, JX64_SIZE_32), 
	JX64_REG_R11D = JX64_REG(JX64_REG_ID_R11, 0, JX64_SIZE_32), 
	JX64_REG_R12D = JX64_REG(JX64_REG_ID_R12, 0, JX64_SIZE_32), 
	JX64_REG_R13D = JX64_REG(JX64_REG_ID_R13, 0, JX64_SIZE_32), 
	JX64_REG_R14D = JX64_REG(JX64_REG_ID_R14, 0, JX64_SIZE_32), 
	JX64_REG_R15D = JX64_REG(JX64_REG_ID_R15, 0, JX64_SIZE_32), 
	JX64_REG_EIP  = JX64_REG(JX64_REG_ID_RBP, 1, JX64_SIZE_32), // NOTE: Don't use directly. Use label operands instead

	// 16-bit registers
	JX64_REG_AX   = JX64_REG(JX64_REG_ID_RAX, 0, JX64_SIZE_16),
	JX64_REG_CX   = JX64_REG(JX64_REG_ID_RCX, 0, JX64_SIZE_16), 
	JX64_REG_DX   = JX64_REG(JX64_REG_ID_RDX, 0, JX64_SIZE_16), 
	JX64_REG_BX   = JX64_REG(JX64_REG_ID_RBX, 0, JX64_SIZE_16), 
	JX64_REG_SP   = JX64_REG(JX64_REG_ID_RSP, 0, JX64_SIZE_16), 
	JX64_REG_BP   = JX64_REG(JX64_REG_ID_RBP, 0, JX64_SIZE_16), 
	JX64_REG_SI   = JX64_REG(JX64_REG_ID_RSI, 0, JX64_SIZE_16), 
	JX64_REG_DI   = JX64_REG(JX64_REG_ID_RDI, 0, JX64_SIZE_16), 
	JX64_REG_R8W  = JX64_REG(JX64_REG_ID_R8, 0, JX64_SIZE_16), 
	JX64_REG_R9W  = JX64_REG(JX64_REG_ID_R9, 0, JX64_SIZE_16), 
	JX64_REG_R10W = JX64_REG(JX64_REG_ID_R10, 0, JX64_SIZE_16), 
	JX64_REG_R11W = JX64_REG(JX64_REG_ID_R11, 0, JX64_SIZE_16), 
	JX64_REG_R12W = JX64_REG(JX64_REG_ID_R12, 0, JX64_SIZE_16), 
	JX64_REG_R13W = JX64_REG(JX64_REG_ID_R13, 0, JX64_SIZE_16), 
	JX64_REG_R14W = JX64_REG(JX64_REG_ID_R14, 0, JX64_SIZE_16), 
	JX64_REG_R15W = JX64_REG(JX64_REG_ID_R15, 0, JX64_SIZE_16), 

	// 8-bit registers
	JX64_REG_AL   = JX64_REG(JX64_REG_ID_RAX, 0, JX64_SIZE_8),
	JX64_REG_CL   = JX64_REG(JX64_REG_ID_RCX, 0, JX64_SIZE_8), 
	JX64_REG_DL   = JX64_REG(JX64_REG_ID_RDX, 0, JX64_SIZE_8), 
	JX64_REG_BL   = JX64_REG(JX64_REG_ID_RBX, 0, JX64_SIZE_8), 
	JX64_REG_SPL  = JX64_REG(JX64_REG_ID_RSP, 0, JX64_SIZE_8), 
	JX64_REG_BPL  = JX64_REG(JX64_REG_ID_RBP, 0, JX64_SIZE_8), 
	JX64_REG_SIL  = JX64_REG(JX64_REG_ID_RSI, 0, JX64_SIZE_8), 
	JX64_REG_DIL  = JX64_REG(JX64_REG_ID_RDI, 0, JX64_SIZE_8), 
	JX64_REG_R8B  = JX64_REG(JX64_REG_ID_R8, 0, JX64_SIZE_8), 
	JX64_REG_R9B  = JX64_REG(JX64_REG_ID_R9, 0, JX64_SIZE_8), 
	JX64_REG_R10B = JX64_REG(JX64_REG_ID_R10, 0, JX64_SIZE_8), 
	JX64_REG_R11B = JX64_REG(JX64_REG_ID_R11, 0, JX64_SIZE_8), 
	JX64_REG_R12B = JX64_REG(JX64_REG_ID_R12, 0, JX64_SIZE_8), 
	JX64_REG_R13B = JX64_REG(JX64_REG_ID_R13, 0, JX64_SIZE_8), 
	JX64_REG_R14B = JX64_REG(JX64_REG_ID_R14, 0, JX64_SIZE_8), 
	JX64_REG_R15B = JX64_REG(JX64_REG_ID_R15, 0, JX64_SIZE_8),

	JX64_REG_NONE = 0xFF,
} jx_x64_reg;

typedef struct jx_x64_mem_t
{
	jx_x64_reg m_Base;
	jx_x64_reg m_Index;
	jx_x64_scale m_Scale;
	int32_t m_Displacement;
} jx_x64_mem_t;

typedef struct jx_x64_operand_t
{
	jx_x64_operand_type m_Type;
	jx_x64_size m_Size;
	union
	{
		jx_x64_reg m_Reg;
		int64_t m_ImmI64;
		jx_x64_mem_t m_Mem;
		jx_x64_label_t* m_Lbl;
	} u;
} jx_x64_operand_t;

typedef enum jx_x64_symbol_kind
{
	JX64_SYMBOL_GLOBAL_VARIABLE,
	JX64_SYMOBL_FUNCTION
} jx_x64_symbol_kind;

typedef struct jx_x64_symbol_t
{
	jx_x64_symbol_kind m_Kind;
	uint32_t m_Size;
	jx_x64_label_t* m_Label;
	char* m_Name;
} jx_x64_symbol_t;

typedef struct jx_x64_context_t jx_x64_context_t;

jx_x64_context_t* jx_x64_createContext(jx_allocator_i* allocator);
void jx_x64_destroyContext(jx_x64_context_t* ctx);

void jx64_resetBuffer(jx_x64_context_t* ctx);
const uint8_t* jx64_getBuffer(jx_x64_context_t* ctx, uint32_t* sz);

jx_x64_label_t* jx64_labelAlloc(jx_x64_context_t* ctx);
void jx64_labelFree(jx_x64_context_t* ctx, jx_x64_label_t* lbl);
void jx64_labelBind(jx_x64_context_t* ctx, jx_x64_label_t* lbl);
uint32_t jx64_labelGetOffset(jx_x64_context_t* ctx, jx_x64_label_t* lbl);

jx_x64_symbol_t* jx64_globalVarDeclare(jx_x64_context_t* ctx, const char* name);
bool jx64_globalVarDefine(jx_x64_context_t* ctx, jx_x64_symbol_t* gv, const uint8_t* data, uint32_t sz, uint32_t alignment);

jx_x64_symbol_t* jx64_funcDeclare(jx_x64_context_t* ctx, const char* name);
bool jx64_funcBegin(jx_x64_context_t* ctx, jx_x64_symbol_t* func);
void jx64_funcEnd(jx_x64_context_t* ctx);

jx_x64_symbol_t* jx64_symbolGetByName(jx_x64_context_t* ctx, const char* name);

bool jx64_emitBytes(jx_x64_context_t* ctx, const uint8_t* bytes, uint32_t n);
bool jx64_nop(jx_x64_context_t* ctx, uint32_t n);
bool jx64_push(jx_x64_context_t* ctx, jx_x64_operand_t op);
bool jx64_pop(jx_x64_context_t* ctx, jx_x64_operand_t op);
bool jx64_mov(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_movsx(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_movzx(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_add(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_sub(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_adc(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_sbb(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_and(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_or(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_xor(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_cmp(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_retn(jx_x64_context_t* ctx);
bool jx64_not(jx_x64_context_t* ctx, jx_x64_operand_t op);   // One's Complement Negation
bool jx64_neg(jx_x64_context_t* ctx, jx_x64_operand_t op);   // Two's Complement Negation
bool jx64_mul(jx_x64_context_t* ctx, jx_x64_operand_t op);   // Unsigned Multiply AL, AX or EAX by register or memory
bool jx64_div(jx_x64_context_t* ctx, jx_x64_operand_t op);   // Unsigned Divide AL, AX or EAX by register or memory
bool jx64_idiv(jx_x64_context_t* ctx, jx_x64_operand_t op);  // Signed Devide AL, AX or EAX by register or memory
bool jx64_inc(jx_x64_context_t* ctx, jx_x64_operand_t op);
bool jx64_dec(jx_x64_context_t* ctx, jx_x64_operand_t op);
bool jx64_imul(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_lea(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_test(jx_x64_context_t* ctx, jx_x64_operand_t op1, jx_x64_operand_t op2);
bool jx64_std(jx_x64_context_t* ctx); // Set Direction Flag
bool jx64_cld(jx_x64_context_t* ctx); // Clear Direction Flag
bool jx64_stc(jx_x64_context_t* ctx); // Set Carry Flag
bool jx64_cmc(jx_x64_context_t* ctx); // Complement Carry Flag
bool jx64_clc(jx_x64_context_t* ctx); // Clear Carry Flag
bool jx64_setcc(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_operand_t dst);
bool jx64_sar(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_sal(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_shr(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_shl(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_rcr(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_rcl(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_ror(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_rol(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift);
bool jx64_cmovcc(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_operand_t dst, jx_x64_operand_t src);
bool jx64_bt(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src);  // Bit Test
bool jx64_btr(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src); // Bit Test and Reset
bool jx64_bts(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src); // Bit Test and Set
bool jx64_btc(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src); // Bit Test and Complement
bool jx64_bsr(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src); // Bit Scan Reverse
bool jx64_bsf(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src); // Bit Scan Forward
bool jx64_jcc(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_label_t* lbl);
bool jx64_jmp(jx_x64_context_t* ctx, jx_x64_operand_t op);
bool jx64_call(jx_x64_context_t* ctx, jx_x64_operand_t op);
bool jx64_cdq(jx_x64_context_t* ctx);
bool jx64_cwd(jx_x64_context_t* ctx);
bool jx64_cqo(jx_x64_context_t* ctx);
bool jx64_cbw(jx_x64_context_t* ctx);  // AX := sign-extend of AL
bool jx64_cwde(jx_x64_context_t* ctx); // EAX := sign-extend of AX
bool jx64_cdqe(jx_x64_context_t* ctx); // RAX := sign-extend of EAX

static inline jx_x64_operand_t jx64_opReg(jx_x64_reg reg)
{
	return (jx_x64_operand_t){ .m_Type = JX64_OPERAND_REG, .m_Size = JX64_REG_GET_SIZE(reg), .u.m_Reg = reg };
}

static inline jx_x64_operand_t jx64_opImmI8(int8_t val)
{
	return (jx_x64_operand_t){ .m_Type = JX64_OPERAND_IMM, .m_Size = JX64_SIZE_8, .u.m_ImmI64 = (int64_t)val };
}

static inline jx_x64_operand_t jx64_opImmI16(int16_t val)
{
	return (jx_x64_operand_t){ .m_Type = JX64_OPERAND_IMM, .m_Size = JX64_SIZE_16, .u.m_ImmI64 = (int64_t)val };
}

static inline jx_x64_operand_t jx64_opImmI32(int32_t val)
{
	return (jx_x64_operand_t){ .m_Type = JX64_OPERAND_IMM, .m_Size = JX64_SIZE_32, .u.m_ImmI64 = (int64_t)val };
}

static inline jx_x64_operand_t jx64_opImmI64(int64_t val)
{
	return (jx_x64_operand_t){ .m_Type = JX64_OPERAND_IMM, .m_Size = JX64_SIZE_64, .u.m_ImmI64 = val };
}

static inline jx_x64_operand_t jx64_opMem(jx_x64_size size, jx_x64_reg base, jx_x64_reg index, jx_x64_scale scale, int32_t disp)
{
	return (jx_x64_operand_t){ .m_Type = JX64_OPERAND_MEM, .m_Size = size, .u.m_Mem = { .m_Base = base, .m_Index = index, .m_Scale = scale, .m_Displacement = disp } };
}

static inline jx_x64_operand_t jx64_opLbl(jx_x64_size size, jx_x64_label_t* lbl)
{
	return (jx_x64_operand_t){ .m_Type = JX64_OPERAND_LBL, .m_Size = size, .u.m_Lbl = lbl };
}

#endif // JIT_H
