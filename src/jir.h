#ifndef JX_IR_H
#define JX_IR_H

#include <stdint.h>
#include <stdbool.h>

#include <jlib/array.h> // jx_array_sizeu
#include <jlib/dbg.h>   // JX_CHECK
#include <jlib/macros.h>

typedef struct jx_allocator_i jx_allocator_i;
typedef struct jx_string_buffer_t jx_string_buffer_t;
typedef struct jx_hashmap_t jx_hashmap_t;

typedef struct jx_ir_context_t jx_ir_context_t;
typedef struct jx_ir_module_t jx_ir_module_t;
typedef struct jx_ir_use_t jx_ir_use_t;
typedef struct jx_ir_value_t jx_ir_value_t;
typedef struct jx_ir_user_t jx_ir_user_t;
typedef struct jx_ir_type_t jx_ir_type_t;
typedef struct jx_ir_global_value_t jx_ir_global_value_t;
typedef struct jx_ir_global_variable_t jx_ir_global_variable_t;
typedef struct jx_ir_argument_t jx_ir_argument_t;
typedef struct jx_ir_function_t jx_ir_function_t;
typedef struct jx_ir_basic_block_t jx_ir_basic_block_t;
typedef struct jx_ir_instruction_t jx_ir_instruction_t;
typedef struct jx_ir_function_pass_t jx_ir_function_pass_t;
typedef struct jx_ir_module_pass_t jx_ir_module_pass_t;

typedef enum jx_ir_value_kind
{
	JIR_VALUE_TYPE,
	JIR_VALUE_CONSTANT,
	JIR_VALUE_ARGUMENT,
	JIR_VALUE_INSTRUCTION,
	JIR_VALUE_BASIC_BLOCK,
	JIR_VALUE_FUNCTION,
	JIR_VALUE_GLOBAL_VARIABLE,
} jx_ir_value_kind;

typedef enum jx_ir_type_kind
{
	// Primitive types
	JIR_TYPE_VOID = 0,
	JIR_TYPE_BOOL,
	JIR_TYPE_U8,
	JIR_TYPE_I8,
	JIR_TYPE_U16,
	JIR_TYPE_I16,
	JIR_TYPE_U32,
	JIR_TYPE_I32,
	JIR_TYPE_U64,
	JIR_TYPE_I64,
	JIR_TYPE_F32,
	JIR_TYPE_F64,

	JIR_TYPE_TYPE,
	JIR_TYPE_LABEL,

	// Derived types
	JIR_TYPE_FUNCTION,
	JIR_TYPE_STRUCT,
	JIR_TYPE_ARRAY,
	JIR_TYPE_POINTER,

	JIR_TYPE_FIRST_DERIVED = JIR_TYPE_FUNCTION,
	JIR_TYPE_NUM_PRIMITIVE_TYPES = JIR_TYPE_FIRST_DERIVED,
	JIR_TYPE_COUNT,
} jx_ir_type_kind;

typedef enum jx_ir_linkage_kind
{
	JIR_LINKAGE_EXTERNAL,
	JIR_LINKAGE_INTERNAL,
} jx_ir_linkage_kind;

typedef enum jx_ir_opcode
{
	// Can create -------------*
	//                         |
	JIR_OP_RET,             // OK
	JIR_OP_BRANCH,          // OK
	JIR_OP_ADD,             // OK
	JIR_OP_SUB,             // OK
	JIR_OP_MUL,             // OK
	JIR_OP_DIV,             // OK
	JIR_OP_REM,             // OK
	JIR_OP_AND,             // OK
	JIR_OP_OR,              // OK
	JIR_OP_XOR,             // OK
	JIR_OP_SET_LE,          // OK
	JIR_OP_SET_GE,          // OK
	JIR_OP_SET_LT,          // OK
	JIR_OP_SET_GT,          // OK
	JIR_OP_SET_EQ,          // OK
	JIR_OP_SET_NE,          // OK
	JIR_OP_ALLOCA,          // OK
	JIR_OP_LOAD,            // OK
	JIR_OP_STORE,           // OK
	JIR_OP_GET_ELEMENT_PTR, // OK
	JIR_OP_PHI,             // OK
	JIR_OP_CALL,            // OK
	JIR_OP_SHL,             // OK
	JIR_OP_SHR,             // OK
	JIR_OP_TRUNC,           // OK
	JIR_OP_ZEXT,            // OK
	JIR_OP_SEXT,            // OK
	JIR_OP_PTR_TO_INT,      // OK
	JIR_OP_INT_TO_PTR,      // OK
	JIR_OP_BITCAST,         // OK
	JIR_OP_FPEXT,           // OK
	JIR_OP_FPTRUNC,         // OK
	JIR_OP_FP2UI,           // OK
	JIR_OP_FP2SI,           // OK
	JIR_OP_UI2FP,           // OK
	JIR_OP_SI2FP,           // OK

	JIR_OP_SET_CC_BASE = JIR_OP_SET_LE
} jx_ir_opcode;

static const char* kLinkageName[] = {
	[JIR_LINKAGE_EXTERNAL]  = "external",
	[JIR_LINKAGE_INTERNAL]  = "internal",
};

static const char* kOpcodeMnemonic[] = {
	[JIR_OP_RET]             = "ret",
	[JIR_OP_BRANCH]          = "br",
	[JIR_OP_ADD]             = "add",
	[JIR_OP_SUB]             = "sub",
	[JIR_OP_MUL]             = "mul",
	[JIR_OP_DIV]             = "div",
	[JIR_OP_REM]             = "rem",
	[JIR_OP_AND]             = "and",
	[JIR_OP_OR]              = "or",
	[JIR_OP_XOR]             = "xor",
	[JIR_OP_SET_LE]          = "setle",
	[JIR_OP_SET_GE]          = "setge",
	[JIR_OP_SET_LT]          = "setlt",
	[JIR_OP_SET_GT]          = "setgt",
	[JIR_OP_SET_EQ]          = "seteq",
	[JIR_OP_SET_NE]          = "setne",
	[JIR_OP_ALLOCA]          = "alloca",
	[JIR_OP_LOAD]            = "load",
	[JIR_OP_STORE]           = "store",
	[JIR_OP_GET_ELEMENT_PTR] = "getelementptr",
	[JIR_OP_PHI]             = "phi",
	[JIR_OP_CALL]            = "call",
	[JIR_OP_SHL]             = "shl",
	[JIR_OP_SHR]             = "shr",
	[JIR_OP_TRUNC]           = "trunc",
	[JIR_OP_ZEXT]            = "zext",
	[JIR_OP_SEXT]            = "sext",
	[JIR_OP_PTR_TO_INT]      = "ptrtoint",
	[JIR_OP_INT_TO_PTR]      = "inttoptr",
	[JIR_OP_BITCAST]         = "bitcast",
	[JIR_OP_FPEXT]           = "fpext",
	[JIR_OP_FPTRUNC]         = "fptrunc",
	[JIR_OP_FP2UI]           = "fp2ui",
	[JIR_OP_FP2SI]           = "fp2si",
	[JIR_OP_UI2FP]           = "ui2fp",
	[JIR_OP_SI2FP]           = "si2fp",
};

// NOTE: Order must match the order of JIR_OP_SET_cc opcodes above
typedef enum jx_ir_condition_code
{
	JIR_CC_LE = 0,
	JIR_CC_GE,
	JIR_CC_LT,
	JIR_CC_GT,
	JIR_CC_EQ,
	JIR_CC_NE,
} jx_ir_condition_code;

typedef enum jx_ir_intrinsic_func
{
	JIR_INTRINSIC_MEMCPY_P0_P0_I32,
	JIR_INTRINSIC_MEMCPY_P0_P0_I64,
	JIR_INTRINSIC_MEMSET_P0_I32,
	JIR_INTRINSIC_MEMSET_P0_I64,

	JIR_INTRINSIC_COUNT
} jx_ir_intrinsic_func;

#define JIR_TYPE_FLAGS_IS_SIGNED_Pos     1
#define JIR_TYPE_FLAGS_IS_SIGNED_Msk     (1u << JIR_TYPE_FLAGS_IS_SIGNED_Pos)
#define JIR_TYPE_FLAGS_IS_UNSIGNED_Pos   2
#define JIR_TYPE_FLAGS_IS_UNSIGNED_Msk   (1u << JIR_TYPE_FLAGS_IS_UNSIGNED_Pos)

#define JIR_TYPE_STRUCT_FLAGS_IS_PACKED_Pos     0
#define JIR_TYPE_STRUCT_FLAGS_IS_PACKED_Msk     (1u << JIR_TYPE_STRUCT_FLAGS_IS_PACKED_Pos)
#define JIR_TYPE_STRUCT_FLAGS_IS_UNION_Pos      1
#define JIR_TYPE_STRUCT_FLAGS_IS_UNION_Msk      (1u << JIR_TYPE_STRUCT_FLAGS_IS_UNION_Pos)
#define JIR_TYPE_STRUCT_FLAGS_IS_INCOMPLETE_Pos 2
#define JIR_TYPE_STRUCT_FLAGS_IS_INCOMPLETE_Msk (1u << JIR_TYPE_STRUCT_FLAGS_IS_INCOMPLETE_Pos)

#define JIR_VALUE_FLAGS_CONST_GLOBAL_VAL_PTR_Pos 0
#define JIR_VALUE_FLAGS_CONST_GLOBAL_VAL_PTR_Msk (1u << JIR_VALUE_FLAGS_CONST_GLOBAL_VAL_PTR_Pos)

typedef struct jx_ir_module_t
{
	jx_ir_module_t* m_Prev;
	jx_ir_module_t* m_Next;
	jx_ir_global_variable_t* m_GlobalVarListHead;
	jx_ir_function_t* m_FunctionListHead;
	const char* m_Name;
	jx_ir_value_t* m_IntrinsicFuncs[JIR_INTRINSIC_COUNT];
} jx_ir_module_t;

typedef struct jx_ir_use_t
{
	jx_ir_use_t* m_Prev;
	jx_ir_use_t* m_Next;
	jx_ir_value_t* m_Value;
	jx_ir_user_t* m_User;
} jx_ir_use_t;

typedef struct jx_ir_value_t
{
	jx_ir_value_kind m_Kind;
	uint32_t m_Flags; // JIR_VALUE_FLAGS_xxx
	jx_ir_type_t* m_Type;
	const char* m_Name;
	jx_ir_use_t* m_UsesListHead;
	jx_ir_use_t* m_UsesListTail;
} jx_ir_value_t;

typedef struct jx_ir_user_t
{
	JX_INHERITS(jx_ir_value_t);
	jx_ir_use_t** m_OperandArr; // TODO: Why does this have to be a dynamic array? Keep it for now.
} jx_ir_user_t;

typedef struct jx_ir_type_t
{
	JX_INHERITS(jx_ir_value_t);
	jx_ir_type_kind m_Kind;
	uint32_t m_Flags; // JIR_TYPE_FLAGS_xxx
} jx_ir_type_t;

typedef struct jx_ir_type_function_t
{
	JX_INHERITS(jx_ir_type_t);
	jx_ir_type_t* m_RetType;
	jx_ir_type_t** m_Args;
	uint32_t m_NumArgs;
	bool m_IsVarArg;
	JX_PAD(3);
} jx_ir_type_function_t;

typedef struct jx_ir_type_pointer_t
{
	JX_INHERITS(jx_ir_type_t);
	jx_ir_type_t* m_BaseType;
} jx_ir_type_pointer_t;

// TODO: Inherit from jx_ir_type_pointer_t?
typedef struct jx_ir_type_array_t
{
	JX_INHERITS(jx_ir_type_t);
	jx_ir_type_t* m_BaseType;
	uint32_t m_Size;
	JX_PAD(4);
} jx_ir_type_array_t;

typedef struct jx_ir_struct_member_t
{
	jx_ir_type_t* m_Type;
	uint32_t m_Offset;
	uint32_t m_Alignment;
} jx_ir_struct_member_t;

typedef struct jx_ir_type_struct_t
{
	JX_INHERITS(jx_ir_type_t);
	uint64_t m_UniqueID;
	jx_ir_struct_member_t* m_Members;
	uint32_t m_NumMembers;
	uint32_t m_Flags; // JIR_TYPE_STRUCT_FLAGS_xxx
	uint32_t m_Size;
	uint32_t m_Alignment;
} jx_ir_type_struct_t;

typedef struct jx_ir_constant_t
{
	JX_INHERITS(jx_ir_user_t);
	union
	{
		bool m_Bool;
		int64_t m_I64;
		uint64_t m_U64;
		double m_F64;
		uintptr_t m_Ptr;
		struct
		{
			jx_ir_global_value_t* m_GlobalVal;
			int64_t m_Offset;
		} m_GlobalVal;
	} u;
} jx_ir_constant_t;

typedef struct jx_ir_global_value_t
{
	JX_INHERITS(jx_ir_user_t);
	jx_ir_module_t* m_ParentModule;
	jx_ir_linkage_kind m_LinkageKind;
	JX_PAD(4);
} jx_ir_global_value_t;

typedef struct jx_ir_global_variable_t
{
	JX_INHERITS(jx_ir_global_value_t);
	jx_ir_global_variable_t* m_Prev;
	jx_ir_global_variable_t* m_Next;
	bool m_IsConstantGlobal;
} jx_ir_global_variable_t;

typedef struct jx_ir_argument_t
{
	JX_INHERITS(jx_ir_value_t);
	jx_ir_argument_t* m_Prev;
	jx_ir_argument_t* m_Next;
	jx_ir_function_t* m_ParentFunc;
	uint32_t m_ID;
	JX_PAD(4);
} jx_ir_argument_t;

typedef struct jx_ir_basic_block_t
{
	JX_INHERITS(jx_ir_value_t);
	jx_ir_basic_block_t* m_Prev;
	jx_ir_basic_block_t* m_Next;
	jx_ir_instruction_t* m_InstrListHead;
	jx_ir_function_t* m_ParentFunc;
	jx_ir_basic_block_t** m_PredArr; // Predecessors
	jx_ir_basic_block_t** m_SuccArr; // Successors, max 2 but keep it as a dynamic array for consistency.

	jx_ir_basic_block_t* m_ImmDom;   // Immediate Dominator
	uint32_t m_RevPostOrderID;
	JX_PAD(4);
} jx_ir_basic_block_t;

#define JIR_FUNC_FLAGS_INLINE_Pos          0
#define JIR_FUNC_FLAGS_INLINE_Msk          (1u << JIR_FUNC_FLAGS_INLINE_Pos)
#define JIR_FUNC_FLAGS_DOM_TREE_VALID_Pos  1
#define JIR_FUNC_FLAGS_DOM_TREE_VALID_Msk  (1u << JIR_FUNC_FLAGS_DOM_TREE_VALID_Pos)

typedef struct jx_ir_function_t
{
	JX_INHERITS(jx_ir_global_value_t);
	jx_ir_function_t* m_Prev;
	jx_ir_function_t* m_Next;
	jx_ir_basic_block_t* m_BasicBlockListHead;
	jx_ir_argument_t* m_ArgListHead;
	uint32_t m_NextTempID;
	uint32_t m_Flags; // JIR_FUNC_FLAGS_xxx
} jx_ir_function_t;

typedef struct jx_ir_instruction_t
{
	JX_INHERITS(jx_ir_user_t);
	jx_ir_instruction_t* m_Prev;
	jx_ir_instruction_t* m_Next;
	jx_ir_basic_block_t* m_ParentBB;
	jx_ir_opcode m_OpCode;
	JX_PAD(4);
} jx_ir_instruction_t;

typedef struct jx_ir_function_pass_o jx_ir_function_pass_o;
typedef struct jx_ir_function_pass_t
{
	jx_ir_function_pass_o* m_Inst;
	jx_ir_function_pass_t* m_Next;

	bool (*run)(jx_ir_function_pass_o* pass, jx_ir_context_t* ctx, jx_ir_function_t* func);
	void (*destroy)(jx_ir_function_pass_o* pass, jx_allocator_i* allocator);
} jx_ir_function_pass_t;

typedef struct jx_ir_module_pass_o jx_ir_module_pass_o;
typedef struct jx_ir_module_pass_t
{
	jx_ir_module_pass_o* m_Inst;
	jx_ir_module_pass_t* m_Next;

	bool (*run)(jx_ir_module_pass_o* pass, jx_ir_context_t* ctx, jx_ir_module_t* mod);
	void (*destroy)(jx_ir_module_pass_o* pass, jx_allocator_i* allocator);
} jx_ir_module_pass_t;

jx_ir_context_t* jx_ir_createContext(jx_allocator_i* allocator);
void jx_ir_destroyContext(jx_ir_context_t* ctx);
void jx_ir_print(jx_ir_context_t* ctx, jx_string_buffer_t* sb);
jx_ir_module_t* jx_ir_getModule(jx_ir_context_t* ctx, uint32_t id);

jx_ir_module_t* jx_ir_moduleBegin(jx_ir_context_t* ctx, const char* name);
void jx_ir_moduleEnd(jx_ir_context_t* ctx, jx_ir_module_t* mod);
const char* jx_ir_moduleGetName(jx_ir_context_t* ctx, jx_ir_module_t* mod);
jx_ir_global_value_t* jx_ir_moduleDeclareGlobalVal(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name, jx_ir_type_t* type, jx_ir_linkage_kind linkage);
jx_ir_global_value_t* jx_ir_moduleGetGlobalVal(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name);
jx_ir_function_t* jx_ir_moduleGetFunc(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name);
jx_ir_global_variable_t* jx_ir_moduleGetGlobalVar(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name);
void jx_ir_modulePrint(jx_ir_context_t* ctx, jx_ir_module_t* mod, jx_string_buffer_t* sb);

bool jx_ir_funcBegin(jx_ir_context_t* ctx, jx_ir_function_t* func, uint32_t flags);
void jx_ir_funcEnd(jx_ir_context_t* ctx, jx_ir_function_t* func);
jx_ir_argument_t* jx_ir_funcGetArgument(jx_ir_context_t* ctx, jx_ir_function_t* func, uint32_t argID);
void jx_ir_funcAppendBasicBlock(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_basic_block_t* bb);
bool jx_ir_funcRemoveBasicBlock(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_basic_block_t* bb);
jx_ir_type_function_t* jx_ir_funcGetType(jx_ir_context_t* ctx, jx_ir_function_t* func);
uint32_t jx_ir_funcCountBasicBlocks(jx_ir_context_t* ctx, jx_ir_function_t* func);
bool jx_ir_funcUpdateDomTree(jx_ir_context_t* ctx, jx_ir_function_t* func);
void jx_ir_funcPrint(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_string_buffer_t* sb);
bool jx_ir_funcCheck(jx_ir_context_t* ctx, jx_ir_function_t* func);

bool jx_ir_globalVarDefine(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv, bool isConst, jx_ir_constant_t* initializer);
void jx_ir_globalVarPrint(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv, jx_string_buffer_t* sb);

jx_ir_basic_block_t* jx_ir_bbAlloc(jx_ir_context_t* ctx, const char* name);
void jx_ir_bbFree(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb);
jx_ir_instruction_t* jx_ir_bbGetLastInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb);
bool jx_ir_bbAppendInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
bool jx_ir_bbPrependInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
bool jx_ir_bbInsertInstrBefore(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* anchor, jx_ir_instruction_t* instr);
void jx_ir_bbRemoveInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
bool jx_ir_bbConvertCondBranch(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, bool condVal);
jx_ir_basic_block_t* jx_ir_bbSplitAt(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
void jx_ir_bbPrint(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_string_buffer_t* sb);

jx_ir_instruction_t* jx_ir_instrClone(jx_ir_context_t* ctx, jx_ir_instruction_t* instr);
jx_ir_value_t* jx_ir_instrReplaceOperand(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, uint32_t id, jx_ir_value_t* val);
void jx_ir_instrFree(jx_ir_context_t* ctx, jx_ir_instruction_t* instr);
jx_ir_instruction_t* jx_ir_instrRet(jx_ir_context_t* ctx, jx_ir_value_t* val);
jx_ir_instruction_t* jx_ir_instrBranch(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb);
jx_ir_instruction_t* jx_ir_instrBranchIf(jx_ir_context_t* ctx, jx_ir_value_t* cond, jx_ir_basic_block_t* trueBB, jx_ir_basic_block_t* falseBB);
jx_ir_instruction_t* jx_ir_instrAdd(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrSub(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrMul(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrDiv(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrRem(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrAnd(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrOr(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrXor(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrShl(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_value_t* shiftAmount);
jx_ir_instruction_t* jx_ir_instrShr(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_value_t* shiftAmount);
jx_ir_instruction_t* jx_ir_instrNeg(jx_ir_context_t* ctx, jx_ir_value_t* val);
jx_ir_instruction_t* jx_ir_instrNot(jx_ir_context_t* ctx, jx_ir_value_t* val);
jx_ir_instruction_t* jx_ir_instrSetCC(jx_ir_context_t* ctx, jx_ir_condition_code cc, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrSetEQ(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrSetNE(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrSetLT(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrSetLE(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrSetGT(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrSetGE(jx_ir_context_t* ctx, jx_ir_value_t* val1, jx_ir_value_t* val2);
jx_ir_instruction_t* jx_ir_instrTrunc(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrZeroExt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrSignExt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrPtrToInt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrIntToPtr(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrBitcast(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrFPExt(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrFPTrunc(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrFP2UI(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrFP2SI(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrUI2FP(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrSI2FP(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_type_t* targetType);
jx_ir_instruction_t* jx_ir_instrCall(jx_ir_context_t* ctx, jx_ir_value_t* func, uint32_t numParams, jx_ir_value_t** params);
jx_ir_instruction_t* jx_ir_instrAlloca(jx_ir_context_t* ctx, jx_ir_type_t* type, jx_ir_value_t* arraySize);
jx_ir_instruction_t* jx_ir_instrLoad(jx_ir_context_t* ctx, jx_ir_type_t* type, jx_ir_value_t* ptr);
jx_ir_instruction_t* jx_ir_instrStore(jx_ir_context_t* ctx, jx_ir_value_t* ptr, jx_ir_value_t* val);
jx_ir_instruction_t* jx_ir_instrGetElementPtr(jx_ir_context_t* ctx, jx_ir_value_t* ptr, uint32_t numIndices, jx_ir_value_t** indices);
jx_ir_instruction_t* jx_ir_instrPhi(jx_ir_context_t* ctx, jx_ir_type_t* type);
jx_ir_instruction_t* jx_ir_instrMemCopy(jx_ir_context_t* ctx, jx_ir_value_t* dstPtr, jx_ir_value_t* srcPtr, jx_ir_value_t* size);
jx_ir_instruction_t* jx_ir_instrMemSet(jx_ir_context_t* ctx, jx_ir_value_t* dstPtr, jx_ir_value_t* i8Val, jx_ir_value_t* size);
bool jx_ir_instrPhiAddValue(jx_ir_context_t* ctx, jx_ir_instruction_t* phiInstr, jx_ir_basic_block_t* bb, jx_ir_value_t* val);
jx_ir_value_t* jx_ir_instrPhiRemoveValue(jx_ir_context_t* ctx, jx_ir_instruction_t* phiInstr, jx_ir_basic_block_t* bb);
jx_ir_value_t* jx_ir_instrPhiHasValue(jx_ir_context_t* ctx, jx_ir_instruction_t* phiInstr, jx_ir_basic_block_t* bb);
void jx_ir_instrPrint(jx_ir_context_t* ctx, jx_ir_instruction_t* instr, jx_string_buffer_t* sb);
bool jx_ir_instrCheck(jx_ir_context_t* ctx, jx_ir_instruction_t* instr);
jx_ir_value_t* jx_ir_instrGetOperandVal(jx_ir_instruction_t* instr, uint32_t operandID);
void jx_ir_instrSwapOperands(jx_ir_instruction_t* instr, uint32_t op1, uint32_t op2);
bool jx_ir_instrIsDead(jx_ir_instruction_t* instr);

jx_ir_value_t* jx_ir_userToValue(jx_ir_user_t* user);
jx_ir_value_t* jx_ir_typeToValue(jx_ir_type_t* type);
jx_ir_value_t* jx_ir_constToValue(jx_ir_constant_t* c);
jx_ir_value_t* jx_ir_globalValToValue(jx_ir_global_value_t* gv);
jx_ir_value_t* jx_ir_globalVarToValue(jx_ir_global_variable_t* gv);
jx_ir_value_t* jx_ir_funcToValue(jx_ir_function_t* func);
jx_ir_value_t* jx_ir_argToValue(jx_ir_argument_t* arg);
jx_ir_value_t* jx_ir_bbToValue(jx_ir_basic_block_t* bb);
jx_ir_value_t* jx_ir_instrToValue(jx_ir_instruction_t* instr);

jx_ir_user_t* jx_ir_constToUser(jx_ir_constant_t* c);
jx_ir_user_t* jx_ir_globalValToUser(jx_ir_global_value_t* gv);
jx_ir_user_t* jx_ir_globalVarToUser(jx_ir_global_variable_t* gv);
jx_ir_user_t* jx_ir_funcToUser(jx_ir_function_t* func);
jx_ir_user_t* jx_ir_instrToUser(jx_ir_instruction_t* instr);

bool jx_ir_opcodeIsAssociative(jx_ir_opcode opcode, jx_ir_type_t* type);
bool jx_ir_opcodeIsCommutative(jx_ir_opcode opcode);
bool jx_ir_opcodeIsTerminator(jx_ir_opcode opcode);
bool jx_ir_opcodeIsSetcc(jx_ir_opcode opcode);

bool jx_ir_valueSetName(jx_ir_context_t* ctx, jx_ir_value_t* val, const char* name);
void jx_ir_valueAddUse(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_use_t* use);
void jx_ir_valueKillUse(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_use_t* use);
void jx_ir_valueReplaceAllUsesWith(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_ir_value_t* newVal);
void jx_ir_valuePrint(jx_ir_context_t* ctx, jx_ir_value_t* val, jx_string_buffer_t* sb);
jx_ir_constant_t* jx_ir_valueToConst(jx_ir_value_t* val);
jx_ir_function_t* jx_ir_valueToFunc(jx_ir_value_t* val);
jx_ir_instruction_t* jx_ir_valueToInstr(jx_ir_value_t* val);
jx_ir_basic_block_t* jx_ir_valueToBasicBlock(jx_ir_value_t* val);
jx_ir_argument_t* jx_ir_valueToArgument(jx_ir_value_t* val);

jx_ir_type_t* jx_ir_typeGetPrimitive(jx_ir_context_t* ctx, jx_ir_type_kind kind);
jx_ir_type_t* jx_ir_typeGetFunction(jx_ir_context_t* ctx, jx_ir_type_t* retType, uint32_t numArgs, jx_ir_type_t** args, bool isVarArg);
jx_ir_type_t* jx_ir_typeGetPointer(jx_ir_context_t* ctx, jx_ir_type_t* baseType);
jx_ir_type_t* jx_ir_typeGetArray(jx_ir_context_t* ctx, jx_ir_type_t* baseType, uint32_t sz);
jx_ir_type_t* jx_ir_typeGetStruct(jx_ir_context_t* ctx, uint64_t uniqueID);
jx_ir_type_struct_t* jx_ir_typeStructBegin(jx_ir_context_t* ctx, uint64_t uniqueID, uint32_t structFlags, uint32_t sz, uint32_t alignment);
jx_ir_type_t* jx_ir_typeStructEnd(jx_ir_context_t* ctx, jx_ir_type_struct_t* structType);
bool jx_ir_typeStructSetMembers(jx_ir_context_t* ctx, jx_ir_type_struct_t* structType, uint32_t numMembers, const jx_ir_struct_member_t* members);
size_t jx_ir_typeStructGetMemberOffset(jx_ir_type_struct_t* structType, uint32_t memberID);
void jx_ir_typePrint(jx_ir_context_t* ctx, jx_ir_type_t* type, jx_string_buffer_t* sb);
bool jx_ir_typeIsSigned(jx_ir_type_t* type);
bool jx_ir_typeIsUnsigned(jx_ir_type_t* type);
bool jx_ir_typeIsInteger(jx_ir_type_t* type);
bool jx_ir_typeIsIntegral(jx_ir_type_t* type);
bool jx_ir_typeIsFloatingPoint(jx_ir_type_t* type);
bool jx_ir_typeIsPrimitive(jx_ir_type_t* type);
bool jx_ir_typeIsDerived(jx_ir_type_t* type);
bool jx_ir_typeIsFirstClass(jx_ir_type_t* type);
bool jx_ir_typeIsSized(jx_ir_type_t* type);
bool jx_ir_typeIsComposite(jx_ir_type_t* type);
bool jx_ir_typeIsFuncPtr(jx_ir_type_t* type);
bool jx_ir_typeIsSmallPow2Struct(jx_ir_type_t* type);
size_t jx_ir_typeGetAlignment(jx_ir_type_t* type);
size_t jx_ir_typeGetSize(jx_ir_type_t* type);
uint32_t jx_ir_typeGetIntegerConversionRank(jx_ir_type_t* type);
bool jx_ir_typeCanRepresent(jx_ir_type_t* type, jx_ir_type_t* other);
jx_ir_type_kind jx_ir_typeToUnsigned(jx_ir_type_kind type);
jx_ir_type_pointer_t* jx_ir_typeToPointer(jx_ir_type_t* type);
jx_ir_type_function_t* jx_ir_typeToFunction(jx_ir_type_t* type);
jx_ir_type_array_t* jx_ir_typeToArray(jx_ir_type_t* type);
jx_ir_type_struct_t* jx_ir_typeToStruct(jx_ir_type_t* type);

jx_ir_constant_t* jx_ir_constGetBool(jx_ir_context_t* ctx, bool val);
jx_ir_constant_t* jx_ir_constGetI8(jx_ir_context_t* ctx, int8_t val);
jx_ir_constant_t* jx_ir_constGetU8(jx_ir_context_t* ctx, uint8_t val);
jx_ir_constant_t* jx_ir_constGetI16(jx_ir_context_t* ctx, int16_t val);
jx_ir_constant_t* jx_ir_constGetU16(jx_ir_context_t* ctx, uint16_t val);
jx_ir_constant_t* jx_ir_constGetI32(jx_ir_context_t* ctx, int32_t val);
jx_ir_constant_t* jx_ir_constGetU32(jx_ir_context_t* ctx, uint32_t val);
jx_ir_constant_t* jx_ir_constGetI64(jx_ir_context_t* ctx, int64_t val);
jx_ir_constant_t* jx_ir_constGetU64(jx_ir_context_t* ctx, uint64_t val);
jx_ir_constant_t* jx_ir_constGetInteger(jx_ir_context_t* ctx, jx_ir_type_kind type, int64_t val);
jx_ir_constant_t* jx_ir_constGetF32(jx_ir_context_t* ctx, float val);
jx_ir_constant_t* jx_ir_constGetF64(jx_ir_context_t* ctx, double val);
jx_ir_constant_t* jx_ir_constGetFloat(jx_ir_context_t* ctx, jx_ir_type_kind type, double val);
jx_ir_constant_t* jx_ir_constArray(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t numValues, jx_ir_constant_t** values);
jx_ir_constant_t* jx_ir_constStruct(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t numMembers, jx_ir_constant_t** memberValues);
jx_ir_constant_t* jx_ir_constPointer(jx_ir_context_t* ctx, jx_ir_type_t* type, void* ptr);
jx_ir_constant_t* jx_ir_constPointerNull(jx_ir_context_t* ctx, jx_ir_type_t* type);
jx_ir_constant_t* jx_ir_constPointerToGlobalVal(jx_ir_context_t* ctx, jx_ir_global_value_t* gv, int64_t offset);
jx_ir_constant_t* jx_ir_constGetZero(jx_ir_context_t* ctx, jx_ir_type_t* type);
jx_ir_constant_t* jx_ir_constGetOnes(jx_ir_context_t* ctx, jx_ir_type_t* type);
void jx_ir_constPrint(jx_ir_context_t* ctx, jx_ir_constant_t* c, jx_string_buffer_t* sb);

static inline bool jx_ir_opcodeIsAssociative(jx_ir_opcode opcode, jx_ir_type_t* type)
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

static inline bool jx_ir_opcodeIsCommutative(jx_ir_opcode opcode)
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

static inline bool jx_ir_opcodeIsTerminator(jx_ir_opcode opcode)
{
	return false
		|| opcode == JIR_OP_RET
		|| opcode == JIR_OP_BRANCH
		;
}

static inline bool jx_ir_opcodeIsSetcc(jx_ir_opcode opcode)
{
	return false
		|| opcode == JIR_OP_SET_LE
		|| opcode == JIR_OP_SET_GE
		|| opcode == JIR_OP_SET_LT
		|| opcode == JIR_OP_SET_GT
		|| opcode == JIR_OP_SET_EQ
		|| opcode == JIR_OP_SET_NE
		;
}

static inline jx_ir_condition_code jx_ir_ccSwapOperands(jx_ir_condition_code cc)
{
	static jx_ir_condition_code kSwappedCC[] = {
		[JIR_CC_LE] = JIR_CC_GE, // A <= B => B >= A
		[JIR_CC_GE] = JIR_CC_LE, // A >= B => B <= A
		[JIR_CC_LT] = JIR_CC_GT, // A < B => B > A
		[JIR_CC_GT] = JIR_CC_LT, // A > B => B < A
		[JIR_CC_EQ] = JIR_CC_EQ, // A == B => B == A
		[JIR_CC_NE] = JIR_CC_NE, // A != B => B != A
	};

	return kSwappedCC[cc];
}

static inline jx_ir_condition_code jx_ir_ccInvert(jx_ir_condition_code cc)
{
	static jx_ir_condition_code kInvertedCC[] = {
		[JIR_CC_LE] = JIR_CC_GT,
		[JIR_CC_GE] = JIR_CC_LT,
		[JIR_CC_LT] = JIR_CC_GE,
		[JIR_CC_GT] = JIR_CC_LE,
		[JIR_CC_EQ] = JIR_CC_NE,
		[JIR_CC_NE] = JIR_CC_EQ,
	};

	return kInvertedCC[cc];
}

static inline bool jx_ir_constIsZero(jx_ir_constant_t* c)
{
	jx_ir_type_t* type = jx_ir_constToValue(c)->m_Type;
	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		return !c->u.m_Bool;
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_I8:
	case JIR_TYPE_U16:
	case JIR_TYPE_I16:
	case JIR_TYPE_U32:
	case JIR_TYPE_I32:
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		return c->u.m_I64 == 0;
	} break;
	case JIR_TYPE_F32:
	case JIR_TYPE_F64: {
		return c->u.m_F64 == 0.0;
	} break;
	case JIR_TYPE_POINTER: {
		return c->u.m_Ptr == 0;
	} break;
	default:
		break;
	}

	return false;
}

static inline bool jx_ir_constIsOnes(jx_ir_constant_t* c)
{
	jx_ir_type_t* type = jx_ir_constToValue(c)->m_Type;
	switch (type->m_Kind) {
	case JIR_TYPE_BOOL: {
		return c->u.m_Bool;
	} break;
	case JIR_TYPE_U8:
	case JIR_TYPE_I8: {
		return (c->u.m_U64 & 0x00000000000000FFull) == 0x00000000000000FFull;
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		return (c->u.m_U64 & 0x000000000000FFFFull) == 0x000000000000FFFFull;
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		return (c->u.m_U64 & 0x00000000FFFFFFFFull) == 0x00000000FFFFFFFFull;
	} break;
	case JIR_TYPE_U64:
	case JIR_TYPE_I64: {
		return c->u.m_U64 == 0xFFFFFFFFFFFFFFFFull;
	} break;
	case JIR_TYPE_POINTER: {
		return c->u.m_Ptr == 0xFFFFFFFFFFFFFFFFull;
	} break;
	default:
		break;
	}

	return false;
}

static inline jx_ir_value_t* jx_ir_instrGetOperandVal(jx_ir_instruction_t* instr, uint32_t operandID)
{
	jx_ir_user_t* user = jx_ir_instrToUser(instr);
	JX_CHECK(operandID < jx_array_sizeu(user->m_OperandArr), "Invalid operand ID");
	return user->m_OperandArr[operandID]->m_Value;
}

static inline void jx_ir_instrSwapOperands(jx_ir_instruction_t* instr, uint32_t op1, uint32_t op2)
{
	jx_ir_use_t* tmp = instr->super.m_OperandArr[0];
	instr->super.m_OperandArr[0] = instr->super.m_OperandArr[1];
	instr->super.m_OperandArr[1] = tmp;
}

static inline bool jx_ir_instrIsDead(jx_ir_instruction_t* instr)
{
	return true
		&& !instr->super.super.m_UsesListHead
		&& instr->m_OpCode != JIR_OP_BRANCH
		&& instr->m_OpCode != JIR_OP_CALL
		&& instr->m_OpCode != JIR_OP_RET
		&& instr->m_OpCode != JIR_OP_STORE
		;
}

static inline bool jx_ir_instrIsUncondBranch(jx_ir_instruction_t* instr)
{
	return true
		&& instr->m_OpCode == JIR_OP_BRANCH
		&& jx_array_sizeu(instr->super.m_OperandArr) == 1
		;
}

static inline bool jx_ir_instrIsCondBranch(jx_ir_instruction_t* instr)
{
	return true
		&& instr->m_OpCode == JIR_OP_BRANCH
		&& jx_array_sizeu(instr->super.m_OperandArr) == 3
		;
}

#endif // JX_IR_H
