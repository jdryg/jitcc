#ifndef JX_MACHINE_IR_H
#define JX_MACHINE_IR_H

#include <stdint.h>
#include <stdbool.h>
#include <jlib/dbg.h>
#include <jlib/macros.h> // JX_PAD

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_string_buffer_t jx_string_buffer_t;

typedef struct jx_mir_operand_t jx_mir_operand_t;
typedef struct jx_mir_instruction_t jx_mir_instruction_t;
typedef struct jx_mir_basic_block_t jx_mir_basic_block_t;
typedef struct jx_mir_function_t jx_mir_function_t;
typedef struct jx_mir_frame_info_t jx_mir_frame_info_t;
typedef struct jx_mir_function_pass_t jx_mir_function_pass_t;

typedef enum jx_mir_type_kind
{
	JMIR_TYPE_VOID = 0,
	JMIR_TYPE_I8,
	JMIR_TYPE_I16,
	JMIR_TYPE_I32,
	JMIR_TYPE_I64,
	JMIR_TYPE_F32,
	JMIR_TYPE_F64,
	JMIR_TYPE_F128,
	JMIR_TYPE_PTR,

	JMIR_TYPE_COUNT
} jx_mir_type_kind;

typedef enum jx_mir_opcode
{
	JMIR_OP_RET,
	JMIR_OP_CMP,
	JMIR_OP_TEST,
	JMIR_OP_JMP,
	JMIR_OP_PHI,
	JMIR_OP_MOV,
	JMIR_OP_MOVSX,
	JMIR_OP_MOVZX,
	JMIR_OP_IMUL,
	JMIR_OP_IDIV,
	JMIR_OP_DIV,
	JMIR_OP_ADD,
	JMIR_OP_SUB,
	JMIR_OP_LEA,
	JMIR_OP_XOR,
	JMIR_OP_AND,
	JMIR_OP_OR,
	JMIR_OP_SAR,
	JMIR_OP_SHR,
	JMIR_OP_SHL,
	JMIR_OP_CALL,
	JMIR_OP_PUSH,
	JMIR_OP_POP,
	JMIR_OP_CDQ,
	JMIR_OP_CQO,

	JMIR_OP_SETO,
	JMIR_OP_SETNO,
	JMIR_OP_SETB,
	JMIR_OP_SETNB,
	JMIR_OP_SETE,
	JMIR_OP_SETNE,
	JMIR_OP_SETBE,
	JMIR_OP_SETNBE,
	JMIR_OP_SETS,
	JMIR_OP_SETNS,
	JMIR_OP_SETP,
	JMIR_OP_SETNP,
	JMIR_OP_SETL,
	JMIR_OP_SETNL,
	JMIR_OP_SETLE,
	JMIR_OP_SETNLE,

	JMIR_OP_JO,
	JMIR_OP_JNO,
	JMIR_OP_JB,
	JMIR_OP_JNB,
	JMIR_OP_JE,
	JMIR_OP_JNE,
	JMIR_OP_JBE,
	JMIR_OP_JNBE,
	JMIR_OP_JS,
	JMIR_OP_JNS,
	JMIR_OP_JP,
	JMIR_OP_JNP,
	JMIR_OP_JL,
	JMIR_OP_JNL,
	JMIR_OP_JLE,
	JMIR_OP_JNLE,

	JMIR_OP_MOVSS,
	JMIR_OP_MOVSD,
	JMIR_OP_MOVAPS,
	JMIR_OP_MOVAPD,
	JMIR_OP_MOVD,
	JMIR_OP_MOVQ,
	JMIR_OP_ADDPS,
	JMIR_OP_ADDSS,
	JMIR_OP_ADDPD,
	JMIR_OP_ADDSD,
	JMIR_OP_ANDNPS,
	JMIR_OP_ANDNPD,
	JMIR_OP_ANDPS,
	JMIR_OP_ANDPD,
	JMIR_OP_COMISS,
	JMIR_OP_COMISD,
	JMIR_OP_CVTSI2SS,
	JMIR_OP_CVTSI2SD,
	JMIR_OP_CVTSS2SI,
	JMIR_OP_CVTSD2SI,
	JMIR_OP_CVTTSS2SI,
	JMIR_OP_CVTTSD2SI,
	JMIR_OP_CVTSD2SS,
	JMIR_OP_CVTSS2SD,
	JMIR_OP_DIVPS,
	JMIR_OP_DIVSS,
	JMIR_OP_DIVPD,
	JMIR_OP_DIVSD,
	JMIR_OP_MAXPS,
	JMIR_OP_MAXSS,
	JMIR_OP_MAXPD,
	JMIR_OP_MAXSD,
	JMIR_OP_MINPS,
	JMIR_OP_MINSS,
	JMIR_OP_MINPD,
	JMIR_OP_MINSD,
	JMIR_OP_MULPS,
	JMIR_OP_MULSS,
	JMIR_OP_MULPD,
	JMIR_OP_MULSD,
	JMIR_OP_ORPS,
	JMIR_OP_ORPD,
	JMIR_OP_RCPPS,
	JMIR_OP_RCPSS,
	JMIR_OP_RSQRTPS,
	JMIR_OP_RSQRTSS,
	JMIR_OP_SQRTPS,
	JMIR_OP_SQRTSS,
	JMIR_OP_SQRTPD,
	JMIR_OP_SQRTSD,
	JMIR_OP_SUBPS,
	JMIR_OP_SUBSS,
	JMIR_OP_SUBPD,
	JMIR_OP_SUBSD,
	JMIR_OP_UCOMISS,
	JMIR_OP_UCOMISD,
	JMIR_OP_UNPCKHPS,
	JMIR_OP_UNPCKHPD,
	JMIR_OP_UNPCKLPS,
	JMIR_OP_UNPCKLPD,
	JMIR_OP_XORPS,
	JMIR_OP_XORPD,

	JMIR_OP_SETCC_BASE = JMIR_OP_SETO,
	JMIR_OP_JCC_BASE = JMIR_OP_JO,
} jx_mir_opcode;

typedef enum jx_mir_condition_code
{
	JMIR_CC_O = 0,
	JMIR_CC_NO = 1,
	JMIR_CC_B = 2,
	JMIR_CC_NB = 3,
	JMIR_CC_E = 4,
	JMIR_CC_NE = 5,
	JMIR_CC_BE = 6,
	JMIR_CC_NBE = 7,
	JMIR_CC_S = 8,
	JMIR_CC_NS = 9,
	JMIR_CC_P = 10,
	JMIR_CC_NP = 11,
	JMIR_CC_L = 12,
	JMIR_CC_NL = 13,
	JMIR_CC_LE = 14,
	JMIR_CC_NLE = 15,

	JMIR_CC_COUNT,

	JMIR_CC_NAE = JMIR_CC_B,
	JMIR_CC_AE = JMIR_CC_NB,
	JMIR_CC_Z = JMIR_CC_E,
	JMIR_CC_NZ = JMIR_CC_NE,
	JMIR_CC_NA = JMIR_CC_BE,
	JMIR_CC_A = JMIR_CC_NBE,
	JMIR_CC_PE = JMIR_CC_P,
	JMIR_CC_PO = JMIR_CC_NP,
	JMIR_CC_NGE = JMIR_CC_L,
	JMIR_CC_GE = JMIR_CC_NL,
	JMIR_CC_NG = JMIR_CC_LE,
	JMIR_CC_G = JMIR_CC_NLE
} jx_mir_condition_code;

typedef enum jx_mir_reg_class
{
	JMIR_REG_CLASS_GP = 0,
	JMIR_REG_CLASS_XMM = 1,

	JMIR_REG_CLASS_COUNT,
} jx_mir_reg_class;

typedef enum jx_mir_hw_reg_id
{
	JMIR_HWREGID_A = 0,
	JMIR_HWREGID_C = 1,
	JMIR_HWREGID_D = 2,
	JMIR_HWREGID_B = 3,
	JMIR_HWREGID_SP = 4,
	JMIR_HWREGID_BP = 5,
	JMIR_HWREGID_SI = 6,
	JMIR_HWREGID_DI = 7,
	JMIR_HWREGID_R8 = 8,
	JMIR_HWREGID_R9 = 9,
	JMIR_HWREGID_R10 = 10,
	JMIR_HWREGID_R11 = 11,
	JMIR_HWREGID_R12 = 12,
	JMIR_HWREGID_R13 = 13,
	JMIR_HWREGID_R14 = 14,
	JMIR_HWREGID_R15 = 15,

	JMIR_HWREGID_NONE = 0x1FFFFFFF, // UINT32_MAX truncated to the bitwidth of jx_mir_reg_t::m_ID
} jx_mir_hw_reg_id;

typedef struct jx_mir_reg_t
{
	uint32_t m_ID : 29;
	uint32_t m_Class : 2;
	uint32_t m_IsVirtual : 1;
} jx_mir_reg_t;

static const jx_mir_reg_t kMIRRegGPNone  = { .m_ID = JMIR_HWREGID_NONE, .m_Class = JMIR_REG_CLASS_GP, .m_IsVirtual = true };
static const jx_mir_reg_t kMIRRegXMMNone = { .m_ID = JMIR_HWREGID_NONE, .m_Class = JMIR_REG_CLASS_XMM, .m_IsVirtual = true };

#define JMIR_REG_HW_GP(id) { .m_ID = (id), .m_Class = JMIR_REG_CLASS_GP, .m_IsVirtual = false }
#define JMIR_REG_HW_XMM(id) { .m_ID = (id), .m_Class = JMIR_REG_CLASS_XMM, .m_IsVirtual = false }

static const jx_mir_reg_t kMIRRegGP_A   = JMIR_REG_HW_GP(JMIR_HWREGID_A);
static const jx_mir_reg_t kMIRRegGP_C   = JMIR_REG_HW_GP(JMIR_HWREGID_C);
static const jx_mir_reg_t kMIRRegGP_D   = JMIR_REG_HW_GP(JMIR_HWREGID_D);
static const jx_mir_reg_t kMIRRegGP_B   = JMIR_REG_HW_GP(JMIR_HWREGID_B);
static const jx_mir_reg_t kMIRRegGP_SP  = JMIR_REG_HW_GP(JMIR_HWREGID_SP);
static const jx_mir_reg_t kMIRRegGP_BP  = JMIR_REG_HW_GP(JMIR_HWREGID_BP);
static const jx_mir_reg_t kMIRRegGP_SI  = JMIR_REG_HW_GP(JMIR_HWREGID_SI);
static const jx_mir_reg_t kMIRRegGP_DI  = JMIR_REG_HW_GP(JMIR_HWREGID_DI);
static const jx_mir_reg_t kMIRRegGP_R8  = JMIR_REG_HW_GP(JMIR_HWREGID_R8);
static const jx_mir_reg_t kMIRRegGP_R9  = JMIR_REG_HW_GP(JMIR_HWREGID_R9);
static const jx_mir_reg_t kMIRRegGP_R10 = JMIR_REG_HW_GP(JMIR_HWREGID_R10);
static const jx_mir_reg_t kMIRRegGP_R11 = JMIR_REG_HW_GP(JMIR_HWREGID_R11);
static const jx_mir_reg_t kMIRRegGP_R12 = JMIR_REG_HW_GP(JMIR_HWREGID_R12);
static const jx_mir_reg_t kMIRRegGP_R13 = JMIR_REG_HW_GP(JMIR_HWREGID_R13);
static const jx_mir_reg_t kMIRRegGP_R14 = JMIR_REG_HW_GP(JMIR_HWREGID_R14);
static const jx_mir_reg_t kMIRRegGP_R15 = JMIR_REG_HW_GP(JMIR_HWREGID_R15);
static const jx_mir_reg_t kMIRRegXMM_0  = JMIR_REG_HW_XMM(0);
static const jx_mir_reg_t kMIRRegXMM_1  = JMIR_REG_HW_XMM(1);
static const jx_mir_reg_t kMIRRegXMM_2  = JMIR_REG_HW_XMM(2);
static const jx_mir_reg_t kMIRRegXMM_3  = JMIR_REG_HW_XMM(3);
static const jx_mir_reg_t kMIRRegXMM_4  = JMIR_REG_HW_XMM(4);
static const jx_mir_reg_t kMIRRegXMM_5  = JMIR_REG_HW_XMM(5);
static const jx_mir_reg_t kMIRRegXMM_6  = JMIR_REG_HW_XMM(6);
static const jx_mir_reg_t kMIRRegXMM_7  = JMIR_REG_HW_XMM(7);
static const jx_mir_reg_t kMIRRegXMM_8  = JMIR_REG_HW_XMM(8);
static const jx_mir_reg_t kMIRRegXMM_9  = JMIR_REG_HW_XMM(9);
static const jx_mir_reg_t kMIRRegXMM_10 = JMIR_REG_HW_XMM(1);
static const jx_mir_reg_t kMIRRegXMM_11 = JMIR_REG_HW_XMM(1);
static const jx_mir_reg_t kMIRRegXMM_12 = JMIR_REG_HW_XMM(1);
static const jx_mir_reg_t kMIRRegXMM_13 = JMIR_REG_HW_XMM(1);
static const jx_mir_reg_t kMIRRegXMM_14 = JMIR_REG_HW_XMM(1);
static const jx_mir_reg_t kMIRRegXMM_15 = JMIR_REG_HW_XMM(1);

static const jx_mir_reg_t kMIRFuncArgIReg[] = {
	JMIR_REG_HW_GP(JMIR_HWREGID_C),
	JMIR_REG_HW_GP(JMIR_HWREGID_D),
	JMIR_REG_HW_GP(JMIR_HWREGID_R8),
	JMIR_REG_HW_GP(JMIR_HWREGID_R9),
};

static const jx_mir_reg_t kMIRFuncArgFReg[] = {
	JMIR_REG_HW_XMM(0),
	JMIR_REG_HW_XMM(1),
	JMIR_REG_HW_XMM(2),
	JMIR_REG_HW_XMM(3),
};

static const jx_mir_reg_t kMIRFuncCallerSavedIReg[] = {
	JMIR_REG_HW_GP(JMIR_HWREGID_A),
	JMIR_REG_HW_GP(JMIR_HWREGID_C),
	JMIR_REG_HW_GP(JMIR_HWREGID_D),
	JMIR_REG_HW_GP(JMIR_HWREGID_R8),
	JMIR_REG_HW_GP(JMIR_HWREGID_R9),
	JMIR_REG_HW_GP(JMIR_HWREGID_R10),
	JMIR_REG_HW_GP(JMIR_HWREGID_R11),
};

static const jx_mir_reg_t kMIRFuncCallerSavedFReg[] = {
	JMIR_REG_HW_XMM(0),
	JMIR_REG_HW_XMM(1),
	JMIR_REG_HW_XMM(2),
	JMIR_REG_HW_XMM(3),
	JMIR_REG_HW_XMM(4),
	JMIR_REG_HW_XMM(5),
};

static const jx_mir_reg_t kMIRFuncCalleeSavedIReg[] = {
	JMIR_REG_HW_GP(JMIR_HWREGID_B),
//	JMIR_REG_HW_GP(JMIR_HWREGID_BP), // Always saved by the function if needed; never used by the register allocator.
	JMIR_REG_HW_GP(JMIR_HWREGID_SI),
	JMIR_REG_HW_GP(JMIR_HWREGID_DI),
	JMIR_REG_HW_GP(JMIR_HWREGID_R12),
	JMIR_REG_HW_GP(JMIR_HWREGID_R13),
	JMIR_REG_HW_GP(JMIR_HWREGID_R14),
	JMIR_REG_HW_GP(JMIR_HWREGID_R15),
};

static const jx_mir_reg_t kMIRFuncCalleeSavedFReg[] = {
	JMIR_REG_HW_XMM(6),
	JMIR_REG_HW_XMM(7),
	JMIR_REG_HW_XMM(8),
	JMIR_REG_HW_XMM(9),
	JMIR_REG_HW_XMM(10),
	JMIR_REG_HW_XMM(11),
	JMIR_REG_HW_XMM(12),
	JMIR_REG_HW_XMM(13),
	JMIR_REG_HW_XMM(14),
	JMIR_REG_HW_XMM(15),
};

typedef enum jx_mir_operand_kind
{
	JMIR_OPERAND_REGISTER = 0,
	JMIR_OPERAND_CONST,
	JMIR_OPERAND_BASIC_BLOCK,
	JMIR_OPERAND_STACK_OBJECT,
	JMIR_OPERAND_EXTERNAL_SYMBOL,
	JMIR_OPERAND_MEMORY_REF,
} jx_mir_operand_kind;

typedef struct jx_mir_stack_object_t
{
	int32_t m_SPOffset;
	uint32_t m_Size;
} jx_mir_stack_object_t;

typedef struct jx_mir_memory_ref_t
{
	jx_mir_reg_t m_BaseReg;
	jx_mir_reg_t m_IndexReg;
	uint32_t m_Scale;
	int32_t m_Displacement;
} jx_mir_memory_ref_t;

typedef struct jx_mir_operand_t
{
	jx_mir_operand_kind m_Kind;
	jx_mir_type_kind m_Type;
	union
	{
		jx_mir_reg_t m_Reg;                // JMIR_OPERAND_REGISTER
		int64_t m_ConstI64;                // JMIR_OPERAND_CONST + integer m_Type
		double m_ConstF64;                 // JMIR_OPERAND_CONST + float m_Type
		jx_mir_basic_block_t* m_BB;        // JMIR_OPERAND_BASIC_BLOCK
		jx_mir_stack_object_t* m_StackObj; // JMIR_OPERAND_STACK_OBJECT
		jx_mir_memory_ref_t m_MemRef;      // JMIR_OPERAND_MEMORY_REF
		const char* m_ExternalSymbolName;  // JMIR_OPERAND_EXTERNAL_SYMBOL
	} u;
} jx_mir_operand_t;

typedef struct jx_mir_instruction_t
{
	jx_mir_instruction_t* m_Next;
	jx_mir_instruction_t* m_Prev;
	jx_mir_basic_block_t* m_ParentBB;
	jx_mir_operand_t** m_Operands;
	uint32_t m_NumOperands;
	uint32_t m_OpCode;
} jx_mir_instruction_t;

typedef struct jx_mir_basic_block_t
{
	jx_mir_basic_block_t* m_Next;
	jx_mir_basic_block_t* m_Prev;
	jx_mir_function_t* m_ParentFunc;
	jx_mir_instruction_t* m_InstrListHead;
	uint32_t m_ID;
	JX_PAD(4);
} jx_mir_basic_block_t;

#define JMIR_FUNC_FLAGS_VARARG_Pos   0
#define JMIR_FUNC_FLAGS_VARARG_Msk   (1u << JMIR_FUNC_FLAGS_VARARG_Pos)
#define JMIR_FUNC_FLAGS_EXTERNAL_Pos 1
#define JMIR_FUNC_FLAGS_EXTERNAL_Msk (1u << JMIR_FUNC_FLAGS_EXTERNAL_Pos)

typedef struct jx_mir_function_t
{
	char* m_Name;
	jx_mir_basic_block_t* m_BasicBlockListHead;
	jx_mir_frame_info_t* m_FrameInfo;
	jx_mir_operand_t** m_Args;
	uint32_t m_NumArgs;
	jx_mir_type_kind m_RetType;
	uint32_t m_NextBasicBlockID;
	uint32_t m_NextVirtualRegID[JMIR_REG_CLASS_COUNT];
	uint32_t m_Flags;
	uint32_t m_UsedHWRegs[JMIR_REG_CLASS_COUNT];
} jx_mir_function_t;

typedef struct jx_mir_relocation_t
{
	char* m_SymbolName;
	uint32_t m_Offset;
	JX_PAD(4);
} jx_mir_relocation_t;

typedef struct jx_mir_global_variable_t
{
	char* m_Name;
	uint8_t* m_DataArr;
	jx_mir_relocation_t* m_RelocationsArr;
	uint32_t m_Alignment;
	JX_PAD(4);
} jx_mir_global_variable_t;

typedef struct jx_mir_context_t jx_mir_context_t;

typedef struct jx_mir_function_pass_o jx_mir_function_pass_o;
typedef struct jx_mir_function_pass_t
{
	jx_mir_function_pass_o* m_Inst;
	jx_mir_function_pass_t* m_Next;

	bool (*run)(jx_mir_function_pass_o* pass, jx_mir_context_t* ctx, jx_mir_function_t* func);
	void (*destroy)(jx_mir_function_pass_o* pass, jx_allocator_i* allocator);
} jx_mir_function_pass_t;

jx_mir_context_t* jx_mir_createContext(jx_allocator_i* allocator);
void jx_mir_destroyContext(jx_mir_context_t* ctx);
void jx_mir_print(jx_mir_context_t* ctx, jx_string_buffer_t* sb);
uint32_t jx_mir_getNumGlobalVars(jx_mir_context_t* ctx);
jx_mir_global_variable_t* jx_mir_getGlobalVarByID(jx_mir_context_t* ctx, uint32_t id);
jx_mir_global_variable_t* jx_mir_getGlobalVarByName(jx_mir_context_t* ctx, const char* name);
uint32_t jx_mir_getNumFunctions(jx_mir_context_t* ctx);
jx_mir_function_t* jx_mir_getFunctionByID(jx_mir_context_t* ctx, uint32_t id);
jx_mir_function_t* jx_mir_getFunctionByName(jx_mir_context_t* ctx, const char* name);

jx_mir_global_variable_t* jx_mir_globalVarBegin(jx_mir_context_t* ctx, const char* name, uint32_t alignment);
void jx_mir_globalVarEnd(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv);
uint32_t jx_mir_globalVarAppendData(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv, const uint8_t* data, uint32_t sz);
void jx_mir_globalVarAddRelocation(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv, uint32_t dataOffset, const char* symbolName);

jx_mir_function_t* jx_mir_funcBegin(jx_mir_context_t* ctx, jx_mir_type_kind retType, uint32_t numArgs, jx_mir_type_kind* args, uint32_t flags, const char* name);
void jx_mir_funcEnd(jx_mir_context_t* ctx, jx_mir_function_t* func);
jx_mir_operand_t* jx_mir_funcGetArgument(jx_mir_context_t* ctx, jx_mir_function_t* func, uint32_t argID);
void jx_mir_funcAppendBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb);
void jx_mir_funcPrependBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb);
bool jx_mir_funcRemoveBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb);
void jx_mir_funcAllocStackForCall(jx_mir_context_t* ctx, jx_mir_function_t* func, uint32_t numArguments);
void jx_mir_funcPrint(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_string_buffer_t* sb);

jx_mir_basic_block_t* jx_mir_bbAlloc(jx_mir_context_t* ctx);
void jx_mir_bbFree(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb);
bool jx_mir_bbAppendInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* instr);
bool jx_mir_bbPrependInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* instr);
bool jx_mir_bbInsertInstrBefore(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* anchor, jx_mir_instruction_t* instr);
bool jx_mir_bbInsertInstrAfter(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* anchor, jx_mir_instruction_t* instr);
bool jx_mir_bbRemoveInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* instr);
jx_mir_instruction_t* jx_mir_bbGetFirstTerminatorInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb);

jx_mir_operand_t* jx_mir_opVirtualReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type);
jx_mir_operand_t* jx_mir_opHWReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_reg_t reg);
jx_mir_operand_t* jx_mir_opIConst(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, int64_t val);
jx_mir_operand_t* jx_mir_opFConst(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, double val);
jx_mir_operand_t* jx_mir_opBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb);
jx_mir_operand_t* jx_mir_opMemoryRef(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_reg_t baseReg, jx_mir_reg_t indexReg, uint32_t scale, int32_t displacement);
jx_mir_operand_t* jx_mir_opStackObj(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, uint32_t sz, uint32_t alignment);
jx_mir_operand_t* jx_mir_opExternalSymbol(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, const char* name);
void jx_mir_opPrint(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_string_buffer_t* sb);

void jx_mir_instrFree(jx_mir_context_t* ctx, jx_mir_instruction_t* instr);
void jx_mir_instrPrint(jx_mir_context_t* ctx, jx_mir_instruction_t* instr, jx_string_buffer_t* sb);
jx_mir_instruction_t* jx_mir_mov(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_movsx(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_movzx(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_add(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_sub(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_adc(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_sbb(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_and(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_or(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_xor(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cmp(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_ret(jx_mir_context_t* ctx, jx_mir_operand_t* val);
jx_mir_instruction_t* jx_mir_not(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_neg(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_mul(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_div(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_idiv(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_inc(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_dec(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_imul(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_lea(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_test(jx_mir_context_t* ctx, jx_mir_operand_t* op1, jx_mir_operand_t* op2);
jx_mir_instruction_t* jx_mir_setcc(jx_mir_context_t* ctx, jx_mir_condition_code cc, jx_mir_operand_t* dst);
jx_mir_instruction_t* jx_mir_sar(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_sal(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_shr(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_shl(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_rcr(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_rcl(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_ror(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_rol(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_mir_operand_t* shift);
jx_mir_instruction_t* jx_mir_cmovcc(jx_mir_context_t* ctx, jx_mir_condition_code cc, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_jcc(jx_mir_context_t* ctx, jx_mir_condition_code cc, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_jmp(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_call(jx_mir_context_t* ctx, jx_mir_operand_t* func);
jx_mir_instruction_t* jx_mir_phi(jx_mir_context_t* ctx, jx_mir_operand_t* dst, uint32_t numPredecessors);
jx_mir_instruction_t* jx_mir_push(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_pop(jx_mir_context_t* ctx, jx_mir_operand_t* op);
jx_mir_instruction_t* jx_mir_cdq(jx_mir_context_t* ctx);
jx_mir_instruction_t* jx_mir_cqo(jx_mir_context_t* ctx);

jx_mir_instruction_t* jx_mir_movss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_movsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_movaps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_movapd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_movd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_movq(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_addps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_addss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_addpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_addsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_andnps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_andnpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_andps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_andpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cmpps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8);
jx_mir_instruction_t* jx_mir_cmpss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8);
jx_mir_instruction_t* jx_mir_cmppd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8);
jx_mir_instruction_t* jx_mir_cmpsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8);
jx_mir_instruction_t* jx_mir_comiss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_comisd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvtsi2ss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvtsi2sd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvtss2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvtsd2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvttss2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvttsd2si(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvtsd2ss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_cvtss2sd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_divps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_divss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_divpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_divsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_maxps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_maxss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_maxpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_maxsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_minps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_minss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_minpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_minsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_mulps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_mulss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_mulpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_mulsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_orps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_orpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_rcpps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_rcpss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_rsqrtps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_rsqrtss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_shufps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8);
jx_mir_instruction_t* jx_mir_shufpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src, uint8_t imm8);
jx_mir_instruction_t* jx_mir_sqrtps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_sqrtss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_sqrtpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_sqrtsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_subps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_subss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_subpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_subsd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_ucomiss(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_ucomisd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_unpckhps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_unpckhpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_unpcklps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_unpcklpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_xorps(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);
jx_mir_instruction_t* jx_mir_xorpd(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src);

static inline bool jx_mir_regIsValid(jx_mir_reg_t reg)
{
	return reg.m_ID != JMIR_HWREGID_NONE;
}

static inline bool jx_mir_regIsGP(jx_mir_reg_t reg)
{
	return reg.m_Class == JMIR_REG_CLASS_GP;
}

static inline bool jx_mir_regIsXMM(jx_mir_reg_t reg)
{
	return reg.m_Class == JMIR_REG_CLASS_XMM;
}

static inline bool jx_mir_regIsVirtual(jx_mir_reg_t reg)
{
	return reg.m_IsVirtual;
}

static inline bool jx_mir_regIsHW(jx_mir_reg_t reg)
{
	return !reg.m_IsVirtual;
}

static inline bool jx_mir_regEqual(jx_mir_reg_t a, jx_mir_reg_t b)
{
	return true
		&& a.m_ID == b.m_ID
		&& a.m_Class == b.m_Class
		&& a.m_IsVirtual == b.m_IsVirtual
		;
}

static inline bool jx_mir_regIsSameClass(jx_mir_reg_t a, jx_mir_reg_t b)
{
	return a.m_Class == b.m_Class;
}

static inline bool jx_mir_regIsClass(jx_mir_reg_t a, jx_mir_reg_class regClass)
{
	return a.m_Class == regClass;
}

static inline bool jx_mir_regIsArg(jx_mir_reg_t reg)
{
	if (!reg.m_IsVirtual) {
		if (reg.m_Class == JMIR_REG_CLASS_GP) {
			// Hopefully this will be unrolled
			for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncArgIReg); ++iReg) {
				if (kMIRFuncArgIReg[iReg].m_ID == reg.m_ID) {
					return true;
				}
			}
		} else if (reg.m_Class == JMIR_REG_CLASS_XMM) {
			// Hopefully this will be unrolled
			for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncArgFReg); ++iReg) {
				if (kMIRFuncArgFReg[iReg].m_ID == reg.m_ID) {
					return true;
				}
			}
		} else {
			JX_CHECK(false, "Unknown register class");
		}
	}

	return false;
}

static inline uint32_t jx_mir_regGetArgID(jx_mir_reg_t reg)
{
	if (!reg.m_IsVirtual) {
		if (reg.m_Class == JMIR_REG_CLASS_GP) {
			for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncArgIReg); ++iReg) {
				if (kMIRFuncArgIReg[iReg].m_ID == reg.m_ID) {
					return iReg;
				}
			}
		} else if (reg.m_Class == JMIR_REG_CLASS_XMM) {
			for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncArgFReg); ++iReg) {
				if (kMIRFuncArgFReg[iReg].m_ID == reg.m_ID) {
					return iReg;
				}
			}
		} else {
			JX_CHECK(false, "Unknown register class");
		}
	}

	return UINT32_MAX;
}

static inline bool jx_mir_opIsReg(jx_mir_operand_t* op, jx_mir_reg_t reg)
{
	return true
		&& op->m_Kind == JMIR_OPERAND_REGISTER 
		&& op->u.m_Reg.m_ID == reg.m_ID 
		&& op->u.m_Reg.m_Class == reg.m_Class
		&& op->u.m_Reg.m_IsVirtual == reg.m_IsVirtual
		;
}

static inline uint32_t jx_mir_typeGetSize(jx_mir_type_kind type)
{
	static uint32_t kTypeSize[] = {
		[JMIR_TYPE_VOID] = 0,
		[JMIR_TYPE_I8]   = 1,
		[JMIR_TYPE_I16]  = 2,
		[JMIR_TYPE_I32]  = 4,
		[JMIR_TYPE_I64]  = 8,
		[JMIR_TYPE_F32]  = 4,
		[JMIR_TYPE_F64]  = 8,
		[JMIR_TYPE_F128] = 16,
		[JMIR_TYPE_PTR]  = 8,
	};
	uint32_t sz = kTypeSize[type];
	JX_CHECK(sz != 0, "Type is unsized!");
	return sz;
}

static inline uint32_t jx_mir_typeGetAlignment(jx_mir_type_kind type)
{
	static uint32_t kTypeAlignment[] = {
		[JMIR_TYPE_VOID] = 0,
		[JMIR_TYPE_I8]   = 1,
		[JMIR_TYPE_I16]  = 2,
		[JMIR_TYPE_I32]  = 4,
		[JMIR_TYPE_I64]  = 8,
		[JMIR_TYPE_F32]  = 4,
		[JMIR_TYPE_F64]  = 8,
		[JMIR_TYPE_F128] = 16,
		[JMIR_TYPE_PTR]  = 8,
	};
	uint32_t sz = kTypeAlignment[type];
	JX_CHECK(sz != 0, "Type is unsized!");
	return sz;
}

static inline bool jx_mir_typeIsFloatingPoint(jx_mir_type_kind type)
{
	return false
		|| type == JMIR_TYPE_F32 
		|| type == JMIR_TYPE_F64
		|| type == JMIR_TYPE_F128
		;
}

static inline bool jx_mir_opcodeIsJcc(uint32_t opcode)
{
	return true
		&& (opcode >= JMIR_OP_JCC_BASE) 
		&& (opcode < JMIR_OP_JCC_BASE + JMIR_CC_COUNT)
		;
}

static inline bool jx_mir_opcodeIsSetcc(uint32_t opcode)
{
	return true
		&& (opcode >= JMIR_OP_SETCC_BASE)
		&& (opcode < JMIR_OP_SETCC_BASE + JMIR_CC_COUNT)
		;
}

static inline jx_mir_condition_code jx_mir_ccInvert(jx_mir_condition_code cc)
{
	static jx_mir_condition_code kInvertedCC[] = {
		[JMIR_CC_O]   = JMIR_CC_NO,
		[JMIR_CC_NO]  = JMIR_CC_O,
		[JMIR_CC_B]   = JMIR_CC_NB,
		[JMIR_CC_NB]  = JMIR_CC_B,
		[JMIR_CC_E]   = JMIR_CC_NE,
		[JMIR_CC_NE]  = JMIR_CC_E,
		[JMIR_CC_BE]  = JMIR_CC_NBE,
		[JMIR_CC_NBE] = JMIR_CC_BE,
		[JMIR_CC_S]   = JMIR_CC_NS,
		[JMIR_CC_NS]  = JMIR_CC_S,
		[JMIR_CC_P]   = JMIR_CC_NP,
		[JMIR_CC_NP]  = JMIR_CC_P,
		[JMIR_CC_L]   = JMIR_CC_NL,
		[JMIR_CC_NL]  = JMIR_CC_L,
		[JMIR_CC_LE]  = JMIR_CC_NLE,
		[JMIR_CC_NLE] = JMIR_CC_LE,
	};

	return kInvertedCC[cc];
}

static inline jx_mir_condition_code jx_mir_ccSwapOperands(jx_mir_condition_code cc)
{
	JX_CHECK(cc != JMIR_CC_O && cc != JMIR_CC_NO, "Don't know how to handle overflow when swapping operands. Is it symmetric or not? The table below swaps the condition code.");

	static jx_mir_condition_code kSwappedCC[] = {
		[JMIR_CC_O]   = JMIR_CC_NO, // ???
		[JMIR_CC_NO]  = JMIR_CC_O,  // ???
		[JMIR_CC_B]   = JMIR_CC_A,  // A < B  -> B > A
		[JMIR_CC_NB]  = JMIR_CC_NA, // A >= b -> B <= A
		[JMIR_CC_E]   = JMIR_CC_E,  // Symmetric
		[JMIR_CC_NE]  = JMIR_CC_NE, // Symmetric
		[JMIR_CC_BE]  = JMIR_CC_AE, // A <= B -> B >= A
		[JMIR_CC_NBE] = JMIR_CC_B,  // A > B  -> B < A
		[JMIR_CC_S]   = JMIR_CC_NS, //
		[JMIR_CC_NS]  = JMIR_CC_S,  //
		[JMIR_CC_P]   = JMIR_CC_P,  // Symmetric
		[JMIR_CC_NP]  = JMIR_CC_NP, // Symmetric
		[JMIR_CC_L]   = JMIR_CC_G,  // A < B  -> B > A
		[JMIR_CC_NL]  = JMIR_CC_LE, // A >= B -> B <= A
		[JMIR_CC_LE]  = JMIR_CC_GE, // A <= B -> B >= A
		[JMIR_CC_NLE] = JMIR_CC_L,  // A > B  -> B < A
	};

	return kSwappedCC[cc];
}

#endif // JX_MACHINE_IR_H
