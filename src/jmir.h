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

typedef enum jx_mir_hw_reg
{
	JMIR_HWREG_R0 = 0,
	JMIR_HWREG_R1 = 1,
	JMIR_HWREG_R2 = 2,
	JMIR_HWREG_R3 = 3,
	JMIR_HWREG_R4 = 4,
	JMIR_HWREG_R5 = 5,
	JMIR_HWREG_R6 = 6,
	JMIR_HWREG_R7 = 7,
	JMIR_HWREG_R8 = 8,
	JMIR_HWREG_R9 = 9,
	JMIR_HWREG_R10 = 10,
	JMIR_HWREG_R11 = 11,
	JMIR_HWREG_R12 = 12,
	JMIR_HWREG_R13 = 13,
	JMIR_HWREG_R14 = 14,
	JMIR_HWREG_R15 = 15,

	JMIR_HWREG_A = JMIR_HWREG_R0,
	JMIR_HWREG_C = JMIR_HWREG_R1,
	JMIR_HWREG_D = JMIR_HWREG_R2,
	JMIR_HWREG_B = JMIR_HWREG_R3,
	JMIR_HWREG_SP = JMIR_HWREG_R4,
	JMIR_HWREG_BP = JMIR_HWREG_R5,
	JMIR_HWREG_SI = JMIR_HWREG_R6,
	JMIR_HWREG_DI = JMIR_HWREG_R7,

	JMIR_HWREG_RET = JMIR_HWREG_A,

	JMIR_FIRST_VIRTUAL_REGISTER = 1000,

	JMIR_MEMORY_REG_NONE = UINT32_MAX
} jx_mir_hw_reg;

static const jx_mir_hw_reg kMIRFuncArgIReg[] = {
	JMIR_HWREG_C,
	JMIR_HWREG_D,
	JMIR_HWREG_R8,
	JMIR_HWREG_R9,
};

static const jx_mir_hw_reg kMIRFuncCallerSavedIReg[] = {
	JMIR_HWREG_A,
	JMIR_HWREG_C,
	JMIR_HWREG_D,
	JMIR_HWREG_R8,
	JMIR_HWREG_R9,
	JMIR_HWREG_R10,
	JMIR_HWREG_R11,
};

typedef enum jx_mir_operand_kind
{
	JMIR_OPERAND_REGISTER = 0,
	JMIR_OPERAND_CONST,
	JMIR_OPERAND_BASIC_BLOCK,
	JMIR_OPERAND_STACK_OBJECT,
	JMIR_OPERAND_GLOBAL_VARIABLE,
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
	uint32_t m_BaseRegID;
	uint32_t m_IndexRegID;
	uint32_t m_Scale;
	int32_t m_Displacement;
} jx_mir_memory_ref_t;

typedef struct jx_mir_operand_t
{
	jx_mir_operand_kind m_Kind;
	jx_mir_type_kind m_Type;
	union
	{
		uint32_t m_RegID;                  // JMIR_OPERAND_REGISTER
		int64_t m_ConstI64;                // JMIR_OPERAND_CONST + integer m_Type
		double m_ConstF64;                 // JMIR_OPERAND_CONST + float m_Type
		jx_mir_basic_block_t* m_BB;        // JMIR_OPERAND_BASIC_BLOCK
		jx_mir_stack_object_t* m_StackObj; // JMIR_OPERAND_STACK_OBJECT
		jx_mir_memory_ref_t m_MemRef;      // JMIR_OPERAND_MEMORY_REF
		const char* m_ExternalSymbolName;  // JMIR_OPERAND_EXTERNAL_SYMBOL
		// TODO: Global Variables
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
	uint32_t m_NextVirtualRegID;
	uint32_t m_Flags;
	JX_PAD(4);
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
	jx_mir_relocation_t* m_Relocations;
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
jx_mir_basic_block_t* jx_mir_funcGetExitBlock(jx_mir_context_t* ctx, jx_mir_function_t* func);
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
jx_mir_operand_t* jx_mir_opHWReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_hw_reg reg);
jx_mir_operand_t* jx_mir_opIConst(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, int64_t val);
jx_mir_operand_t* jx_mir_opBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb);
jx_mir_operand_t* jx_mir_opMemoryRef(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, uint32_t baseRegID, uint32_t indexRefID, uint32_t scale, int32_t displacement);
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

static inline bool jx_mir_regIsArg(uint32_t regID)
{
	return false
		|| kMIRFuncArgIReg[0] == regID
		|| kMIRFuncArgIReg[1] == regID
		|| kMIRFuncArgIReg[2] == regID
		|| kMIRFuncArgIReg[3] == regID
		;
}

static inline uint32_t jx_mir_regGetArgID(uint32_t regID)
{
	if (regID == kMIRFuncArgIReg[0]) {
		return 0;
	} else if (regID == kMIRFuncArgIReg[1]) {
		return 1;
	} else if (regID == kMIRFuncArgIReg[2]) {
		return 2;
	} else if (regID == kMIRFuncArgIReg[3]) {
		return 3;
	}

	return UINT32_MAX;
}

static inline bool jx_mir_opIsHWReg(jx_mir_operand_t* op, jx_mir_hw_reg regID)
{
	return op->m_Kind == JMIR_OPERAND_REGISTER && op->u.m_RegID == regID;
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
		[JMIR_TYPE_PTR]  = 8,
	};
	uint32_t sz = kTypeAlignment[type];
	JX_CHECK(sz != 0, "Type is unsized!");
	return sz;
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
