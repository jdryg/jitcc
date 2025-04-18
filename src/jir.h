#ifndef JX_IR_H
#define JX_IR_H

#include <stdint.h>
#include <stdbool.h>

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
typedef struct jx_ir_global_variable_t jx_ir_global_variable_t;
typedef struct jx_ir_argument_t jx_ir_argument_t;
typedef struct jx_ir_function_t jx_ir_function_t;
typedef struct jx_ir_basic_block_t jx_ir_basic_block_t;
typedef struct jx_ir_instruction_t jx_ir_instruction_t;
typedef struct jx_ir_symbol_table_t jx_ir_symbol_table_t;
typedef struct jx_ir_vm_t jx_ir_vm_t;
typedef struct jx_ir_function_pass_t jx_ir_function_pass_t;

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
	JIR_LINKAGE_LINK_ONCE,
	JIR_LINKAGE_WEAK,
	JIR_LINKAGE_APPENDING,
	JIR_LINKAGE_INTERNAL,
} jx_ir_linkage_kind;

typedef enum jx_ir_opcode
{
	// Can execute ---------------*
	// Can create -------------*  |
	//                         |  |
	JIR_OP_RET,             // OK OK
	JIR_OP_BRANCH,          // OK OK
	JIR_OP_ADD,             // OK OK
	JIR_OP_SUB,             // OK OK
	JIR_OP_MUL,             // OK OK
	JIR_OP_DIV,             // OK OK
	JIR_OP_REM,             // OK OK
	JIR_OP_AND,             // OK OK
	JIR_OP_OR,              // OK OK
	JIR_OP_XOR,             // OK OK
	JIR_OP_SET_LE,          // OK OK
	JIR_OP_SET_GE,          // OK OK
	JIR_OP_SET_LT,          // OK OK
	JIR_OP_SET_GT,          // OK OK
	JIR_OP_SET_EQ,          // OK OK
	JIR_OP_SET_NE,          // OK OK
	JIR_OP_ALLOCA,          // OK OK
	JIR_OP_LOAD,            // OK OK
	JIR_OP_STORE,           // OK OK
	JIR_OP_GET_ELEMENT_PTR, // OK -
	JIR_OP_PHI,             // OK -
	JIR_OP_CALL,            // OK OK
	JIR_OP_SHL,             // OK OK
	JIR_OP_SHR,             // OK OK
	JIR_OP_TRUNC,           // OK -
	JIR_OP_ZEXT,            // OK -
	JIR_OP_SEXT,            // OK -
	JIR_OP_PTR_TO_INT,      // OK -
	JIR_OP_INT_TO_PTR,      // OK -
	JIR_OP_BITCAST,         // OK -

	JIR_OP_SET_CC_BASE = JIR_OP_SET_LE
} jx_ir_opcode;

static const char* kLinkageName[] = {
	[JIR_LINKAGE_EXTERNAL]  = "external",
	[JIR_LINKAGE_LINK_ONCE] = "link_once",
	[JIR_LINKAGE_WEAK]      = "weak",
	[JIR_LINKAGE_APPENDING] = "appending",
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
	jx_ir_symbol_table_t* m_SymbolTable;
	const char* m_Name;
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

typedef struct jx_ir_type_struct_t
{
	JX_INHERITS(jx_ir_type_t);
	uint64_t m_UniqueID;
	jx_ir_type_t** m_Members;
	uint32_t m_NumMembers;
	uint32_t m_Flags; // JIR_TYPE_STRUCT_FLAGS_xxx
} jx_ir_type_struct_t;

typedef struct jx_ir_constant_t
{
	JX_INHERITS(jx_ir_user_t);
	union
	{
		bool m_Bool;
		int64_t m_I64;
		uint64_t m_U64;
		float m_F32;
		double m_F64;
		uintptr_t m_Ptr;
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
} jx_ir_basic_block_t;

typedef struct jx_ir_function_t
{
	JX_INHERITS(jx_ir_global_value_t);
	jx_ir_function_t* m_Prev;
	jx_ir_function_t* m_Next;
	jx_ir_basic_block_t* m_BasicBlockListHead;
	jx_ir_argument_t* m_ArgListHead;
	jx_ir_symbol_table_t* m_SymbolTable;
	uint32_t m_NextTempID;
	JX_PAD(4);
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

typedef union jx_ir_generic_value_t
{
	bool m_Bool;
	uint8_t m_U8;
	int8_t m_I8;
	uint16_t m_U16;
	int16_t m_I16;
	uint32_t m_U32;
	int32_t m_I32;
	uint64_t m_U64;
	int64_t m_I64;
	float m_F32;
	double m_F64;
	uintptr_t m_Ptr;
	uint8_t m_Untyped[8];
} jx_ir_generic_value_t;

typedef struct jx_ir_function_pass_o jx_ir_function_pass_o;
typedef struct jx_ir_function_pass_t
{
	jx_ir_function_pass_o* m_Inst;
	jx_ir_function_pass_t* m_Next;

	bool (*run)(jx_ir_function_pass_o* pass, jx_ir_context_t* ctx, jx_ir_function_t* func);
	void (*destroy)(jx_ir_function_pass_o* pass, jx_allocator_i* allocator);
} jx_ir_function_pass_t;

jx_ir_context_t* jx_ir_createContext(jx_allocator_i* allocator);
void jx_ir_destroyContext(jx_ir_context_t* ctx);
void jx_ir_print(jx_ir_context_t* ctx, jx_string_buffer_t* sb);
jx_ir_module_t* jx_ir_getModule(jx_ir_context_t* ctx, uint32_t id);

jx_ir_vm_t* jx_ir_vmAlloc(jx_ir_context_t* ctx, jx_ir_module_t* mod);
void jx_ir_vmFree(jx_ir_context_t* ctx, jx_ir_vm_t* vm);
jx_ir_generic_value_t jx_ir_vmExecFunc(jx_ir_context_t* ctx, jx_ir_vm_t* vm, jx_ir_function_t* func, uint32_t numArgs, jx_ir_generic_value_t* args);

jx_ir_module_t* jx_ir_moduleBegin(jx_ir_context_t* ctx, const char* name);
void jx_ir_moduleEnd(jx_ir_context_t* ctx, jx_ir_module_t* mod);
bool jx_ir_moduleAddFunc(jx_ir_context_t* ctx, jx_ir_module_t* mod, jx_ir_function_t* func);
bool jx_ir_moduleAddGlobalVar(jx_ir_context_t* ctx, jx_ir_module_t* mod, jx_ir_global_variable_t* gv);
jx_ir_function_t* jx_ir_moduleGetFunc(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name);
jx_ir_global_variable_t* jx_ir_moduleGetGlobalVar(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name);
jx_ir_global_value_t* jx_ir_moduleGetGlobalVal(jx_ir_context_t* ctx, jx_ir_module_t* mod, const char* name);
void jx_ir_modulePrint(jx_ir_context_t* ctx, jx_ir_module_t* mod, jx_string_buffer_t* sb);

jx_ir_function_t* jx_ir_funcBegin(jx_ir_context_t* ctx, jx_ir_type_t* type, jx_ir_linkage_kind linkageKind, const char* name);
void jx_ir_funcEnd(jx_ir_context_t* ctx, jx_ir_function_t* func);
jx_ir_argument_t* jx_ir_funcGetArgument(jx_ir_context_t* ctx, jx_ir_function_t* func, uint32_t argID);
void jx_ir_funcAppendBasicBlock(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_basic_block_t* bb);
bool jx_ir_funcRemoveBasicBlock(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_ir_basic_block_t* bb);
jx_ir_type_function_t* jx_ir_funcGetType(jx_ir_context_t* ctx, jx_ir_function_t* func);
void jx_ir_funcPrint(jx_ir_context_t* ctx, jx_ir_function_t* func, jx_string_buffer_t* sb);
bool jx_ir_funcCheck(jx_ir_context_t* ctx, jx_ir_function_t* func);

jx_ir_global_variable_t* jx_ir_globalVarDeclare(jx_ir_context_t* ctx, jx_ir_type_t* type, bool isConstant, jx_ir_linkage_kind linkageKind, jx_ir_constant_t* initializer, const char* name);
jx_ir_global_variable_t* jx_ir_globalVarDeclareCStr(jx_ir_context_t* ctx, jx_ir_linkage_kind linkageKind, const char* str, const char* name);
void jx_ir_globalVarPrint(jx_ir_context_t* ctx, jx_ir_global_variable_t* gv, jx_string_buffer_t* sb);

jx_ir_basic_block_t* jx_ir_bbAlloc(jx_ir_context_t* ctx, const char* name);
void jx_ir_bbFree(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb);
jx_ir_instruction_t* jx_ir_bbGetLastInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb);
bool jx_ir_bbAppendInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
bool jx_ir_bbPrependInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
void jx_ir_bbRemoveInstr(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr);
bool jx_ir_bbConvertCondBranch(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, bool condVal);
void jx_ir_bbPrint(jx_ir_context_t* ctx, jx_ir_basic_block_t* bb, jx_string_buffer_t* sb);

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
jx_ir_type_t* jx_ir_typeGetOpaque(jx_ir_context_t* ctx);
jx_ir_type_t* jx_ir_typeGetStruct(jx_ir_context_t* ctx, uint64_t uniqueID);
jx_ir_type_struct_t* jx_ir_typeStructBegin(jx_ir_context_t* ctx, uint64_t uniqueID, uint32_t structFlags);
jx_ir_type_t* jx_ir_typeStructEnd(jx_ir_context_t* ctx, jx_ir_type_struct_t* structType);
bool jx_ir_typeStructSetMembers(jx_ir_context_t* ctx, jx_ir_type_struct_t* structType, uint32_t numMembers, jx_ir_type_t** members);
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
jx_ir_constant_t* jx_ir_constArray(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t numValues, jx_ir_constant_t** values);
jx_ir_constant_t* jx_ir_constStruct(jx_ir_context_t* ctx, jx_ir_type_t* type, uint32_t numMembers, jx_ir_constant_t** memberValues);
jx_ir_constant_t* jx_ir_constPointer(jx_ir_context_t* ctx, jx_ir_type_t* type, void* ptr);
jx_ir_constant_t* jx_ir_constPointerNull(jx_ir_context_t* ctx, jx_ir_type_t* type);
jx_ir_constant_t* jx_ir_constPointerToGlobalVal(jx_ir_context_t* ctx, jx_ir_global_value_t* gv);
jx_ir_constant_t* jx_ir_constGetZero(jx_ir_context_t* ctx, jx_ir_type_t* type);
jx_ir_constant_t* jx_ir_constGetOnes(jx_ir_context_t* ctx, jx_ir_type_t* type);
void jx_ir_constPrint(jx_ir_context_t* ctx, jx_ir_constant_t* c, jx_string_buffer_t* sb);

#endif // JX_IR_H
