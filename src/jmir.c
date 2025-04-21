#include "jmir.h"
#include "jmir_pass.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>

static const char* kMIROpcodeMnemonic[] = {
	[JMIR_OP_RET] = "ret",
	[JMIR_OP_CMP] = "cmp",
	[JMIR_OP_TEST] = "test",
	[JMIR_OP_JMP] = "jmp",
	[JMIR_OP_PHI] = "phi",
	[JMIR_OP_MOV] = "mov",
	[JMIR_OP_MOVSX] = "movsx",
	[JMIR_OP_MOVZX] = "movzx",
	[JMIR_OP_IMUL] = "imul",
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
};

typedef struct jx_mir_frame_info_t
{
	jx_mir_stack_object_t** m_StackObjs;
	uint32_t m_MaxCallArgs;
	uint32_t m_Size;
} jx_mir_frame_info_t;

typedef struct jx_mir_context_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_OperandAllocator;
	jx_mir_function_t** m_FuncArr;
	jx_mir_global_variable_t** m_GlobalVarArr;
	jx_mir_function_pass_t* m_OnFuncEndPasses;
} jx_mir_context_t;

static bool jmir_opcodeIsTerminator(uint32_t opcode);
static jx_mir_operand_t* jmir_operandAlloc(jx_mir_context_t* ctx, jx_mir_operand_kind kind, jx_mir_type_kind type);
static jx_mir_instruction_t* jmir_instrAlloc(jx_mir_context_t* ctx, uint32_t opcode, uint32_t numOperands, jx_mir_operand_t** operands);
static jx_mir_instruction_t* jmir_instrAlloc1(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1);
static jx_mir_instruction_t* jmir_instrAlloc2(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1, jx_mir_operand_t* op2);
static jx_mir_instruction_t* jmir_instrAlloc3(jx_mir_context_t* ctx, uint32_t opcode, jx_mir_operand_t* op1, jx_mir_operand_t* op2, jx_mir_operand_t* op3);
static jx_mir_operand_t* jmir_funcCreateArgument(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb, uint32_t argID, jx_mir_type_kind argType);
static void jmir_funcFree(jx_mir_context_t* ctx, jx_mir_function_t* func);
static void jmir_globalVarFree(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv);
static jx_mir_frame_info_t* jmir_frameCreate(jx_mir_context_t* ctx);
static void jmir_frameDestroy(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo);
static jx_mir_stack_object_t* jmir_frameAllocObj(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t sz, uint32_t alignment);
static void jmir_frameMakeRoomForCall(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t numArguments);
static void jmir_frameFinalize(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo);

jx_mir_context_t* jx_mir_createContext(jx_allocator_i* allocator)
{
	jx_mir_context_t* ctx = (jx_mir_context_t*)JX_ALLOC(allocator, sizeof(jx_mir_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_mir_context_t));
	ctx->m_Allocator = allocator;

	ctx->m_OperandAllocator = allocator_api->createPoolAllocator(sizeof(jx_mir_operand_t), 512, allocator);
	if (!ctx->m_OperandAllocator) {
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

	// Initialize function passes to be executed when funcEnd is called
	{
		jx_mir_function_pass_t head = { 0 };
		jx_mir_function_pass_t* cur = &head;

		// Remove fallthrough jumps
		{
			jx_mir_function_pass_t* pass = (jx_mir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_function_pass_t));
			if (!pass) {
				jx_mir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_mir_function_pass_t));
			if (!jx_mir_funcPassCreate_removeFallthroughJmp(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		// Simplify condition jumps
		{
			jx_mir_function_pass_t* pass = (jx_mir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_function_pass_t));
			if (!pass) {
				jx_mir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_mir_function_pass_t));
			if (!jx_mir_funcPassCreate_simplifyCondJmp(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		// Fix mem/mem operations
		{
			jx_mir_function_pass_t* pass = (jx_mir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_function_pass_t));
			if (!pass) {
				jx_mir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_mir_function_pass_t));
			if (!jx_mir_funcPassCreate_fixMemMemOps(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

#if 1
		// Register allocator
		{
			jx_mir_function_pass_t* pass = (jx_mir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_function_pass_t));
			if (!pass) {
				jx_mir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_mir_function_pass_t));
			if (!jx_mir_funcPassCreate_regAlloc(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}
#endif

		// Remove redundant moves
		{
			jx_mir_function_pass_t* pass = (jx_mir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_function_pass_t));
			if (!pass) {
				jx_mir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_mir_function_pass_t));
			if (!jx_mir_funcPassCreate_removeRedundantMoves(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		// Simplify condition jumps
		// NOTE: Rerun this pass because the register allocator + the removal of redundant moves might have
		// created more opportunities for conditional jump simplifications.
		// E.g. c-testsuite/00008.c
		{
			jx_mir_function_pass_t* pass = (jx_mir_function_pass_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_function_pass_t));
			if (!pass) {
				jx_mir_destroyContext(ctx);
				return NULL;
			}

			jx_memset(pass, 0, sizeof(jx_mir_function_pass_t));
			if (!jx_mir_funcPassCreate_simplifyCondJmp(pass, ctx->m_Allocator)) {
				JX_CHECK(false, "Failed to initialize function pass!");
				JX_FREE(ctx->m_Allocator, pass);
			} else {
				cur->m_Next = pass;
				cur = cur->m_Next;
			}
		}

		ctx->m_OnFuncEndPasses = head.m_Next;
	}

	return ctx;
}

void jx_mir_destroyContext(jx_mir_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	// Free function passes
	{
		jx_mir_function_pass_t* pass = ctx->m_OnFuncEndPasses;
		while (pass) {
			jx_mir_function_pass_t* next = pass->m_Next;
			pass->destroy(pass->m_Inst, allocator);
			JX_FREE(allocator, pass);

			pass = next;
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

	if (ctx->m_OperandAllocator) {
		allocator_api->destroyPoolAllocator(ctx->m_OperandAllocator);
		ctx->m_OperandAllocator = NULL;
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
	jx_mir_global_variable_t* gv = (jx_mir_global_variable_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_global_variable_t));
	if (!gv) {
		return NULL;
	}

	jx_memset(gv, 0, sizeof(jx_mir_global_variable_t));
	gv->m_Alignment = alignment;
	gv->m_Name = jx_strdup(name, ctx->m_Allocator);
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
		.m_SymbolName = jx_strdup(symbolName, ctx->m_Allocator),
		.m_Offset = dataOffset
	});
}

jx_mir_function_t* jx_mir_funcBegin(jx_mir_context_t* ctx, jx_mir_type_kind retType, uint32_t numArgs, jx_mir_type_kind* args, uint32_t flags, const char* name)
{
	jx_mir_function_t* func = (jx_mir_function_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_function_t));
	if (!func) {
		return NULL;
	}

	jx_memset(func, 0, sizeof(jx_mir_function_t));
	func->m_Name = jx_strdup(name, ctx->m_Allocator);
	func->m_RetType = retType;
	func->m_Flags = flags;
	func->m_NumArgs = numArgs;

	jx_mir_basic_block_t* entryBlock = jx_mir_bbAlloc(ctx);

	if (numArgs) {
		func->m_Args = (jx_mir_operand_t**)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_operand_t*) * numArgs);
		if (!func->m_Args) {
			JX_FREE(ctx->m_Allocator, func);
			return NULL;
		}

		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			func->m_Args[iArg] = jmir_funcCreateArgument(ctx, func, entryBlock, iArg, args[iArg]);
			if (!func->m_Args[iArg]) {
				JX_FREE(ctx->m_Allocator, func->m_Args);
				JX_FREE(ctx->m_Allocator, func);
				return NULL;
			}
		}
	}

	if ((func->m_Flags & JMIR_FUNC_FLAGS_EXTERNAL_Msk) == 0) {
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
		JX_CHECK((func->m_Flags & JMIR_FUNC_FLAGS_EXTERNAL_Msk) != 0, "Internal function without body?");
		return;
	}
	
	jx_mir_function_pass_t* pass = ctx->m_OnFuncEndPasses;
	while (pass) {
		bool funcModified = pass->run(pass->m_Inst, ctx, func);
		JX_UNUSED(funcModified);
		pass = pass->m_Next;
	}

	// Insert prologue/epilogue
	jx_mir_frame_info_t* frameInfo = func->m_FrameInfo;
	jmir_frameFinalize(ctx, frameInfo);
	if (frameInfo->m_Size != 0) {
		// NOTE: Prepend prologue instructions in reverse order.
		jx_mir_basic_block_t* entryBlock = func->m_BasicBlockListHead;
		jx_mir_bbPrependInstr(ctx, entryBlock, jx_mir_sub(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, JMIR_HWREG_SP), jx_mir_opIConst(ctx, func, JMIR_TYPE_I32, (int64_t)frameInfo->m_Size)));
		jx_mir_bbPrependInstr(ctx, entryBlock, jx_mir_mov(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, JMIR_HWREG_BP), jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, JMIR_HWREG_SP)));
		jx_mir_bbPrependInstr(ctx, entryBlock, jx_mir_push(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, JMIR_HWREG_BP)));

		// Add epilogue code to each exit block.
		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_mir_instruction_t* firstTerminator = jx_mir_bbGetFirstTerminatorInstr(ctx, bb);
			if (firstTerminator && firstTerminator->m_OpCode == JMIR_OP_RET) {
				JX_CHECK(!firstTerminator->m_Next, "Unexpected instruction after ret!");
				jx_mir_bbInsertInstrBefore(ctx, bb, firstTerminator, jx_mir_mov(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, JMIR_HWREG_SP), jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, JMIR_HWREG_BP)));
				jx_mir_bbInsertInstrBefore(ctx, bb, firstTerminator, jx_mir_pop(ctx, jx_mir_opHWReg(ctx, func, JMIR_TYPE_PTR, JMIR_HWREG_BP)));
			}

			bb = bb->m_Next;
		}
	}
}

jx_mir_operand_t* jx_mir_funcGetArgument(jx_mir_context_t* ctx, jx_mir_function_t* func, uint32_t argID)
{
	if (argID >= func->m_NumArgs) {
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
		func->m_BasicBlockListHead = bb;
	} else {
		jx_mir_basic_block_t* tail = func->m_BasicBlockListHead;
		while (tail->m_Next) {
			tail = tail->m_Next;
		}

		tail->m_Next = bb;
		bb->m_Prev = tail;
	}
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
	}
	func->m_BasicBlockListHead = bb;
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

	return true;
}

void jx_mir_funcAllocStackForCall(jx_mir_context_t* ctx, jx_mir_function_t* func, uint32_t numArguments)
{
	jmir_frameMakeRoomForCall(ctx, func->m_FrameInfo, numArguments);
}

void jx_mir_funcPrint(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_string_buffer_t* sb)
{
	jx_strbuf_printf(sb, "global %s:\n", func->m_Name);

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_strbuf_printf(sb, "bb.%u:\n", bb->m_ID);
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
	jx_mir_basic_block_t* bb = (jx_mir_basic_block_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_basic_block_t));
	if (!bb) {
		return NULL;
	}

	jx_memset(bb, 0, sizeof(jx_mir_basic_block_t));
	bb->m_ID = UINT32_MAX;

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

	JX_FREE(ctx->m_Allocator, bb);
}

bool jx_mir_bbAppendInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb, jx_mir_instruction_t* instr)
{
	JX_UNUSED(ctx);
	JX_CHECK(!instr->m_ParentBB && !instr->m_Prev && !instr->m_Next, "Instruction already part of a basic block?");

	instr->m_ParentBB = bb;

	if (!bb->m_InstrListHead) {
		bb->m_InstrListHead = instr;
	} else {
		jx_mir_instruction_t* tail = bb->m_InstrListHead;
		while (tail->m_Next) {
			tail = tail->m_Next;
		}

		tail->m_Next = instr;
		instr->m_Prev = tail;
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
	}

	bb->m_InstrListHead = instr;

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

	if (instr->m_Prev) {
		instr->m_Prev->m_Next = instr->m_Next;
	}
	if (instr->m_Next) {
		instr->m_Next->m_Prev = instr->m_Prev;
	}

	instr->m_ParentBB = NULL;
	instr->m_Prev = NULL;
	instr->m_Next = NULL;

	return true;

}

jx_mir_instruction_t* jx_mir_bbGetFirstTerminatorInstr(jx_mir_context_t* ctx, jx_mir_basic_block_t* bb)
{
	JX_UNUSED(ctx);

	jx_mir_instruction_t* instr = bb->m_InstrListHead;
	while (instr) {
		if (jmir_opcodeIsTerminator(instr->m_OpCode)) {
			return instr;
		}

		instr = instr->m_Next;
	}

	return NULL;
}

jx_mir_operand_t* jx_mir_opVirtualReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_REGISTER, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_RegID = JMIR_FIRST_VIRTUAL_REGISTER + func->m_NextVirtualRegID;
	func->m_NextVirtualRegID++;

	return operand;
}

jx_mir_operand_t* jx_mir_opHWReg(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, jx_mir_hw_reg reg)
{
	JX_CHECK(reg < JMIR_FIRST_VIRTUAL_REGISTER, "Invalid hardware register");
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_REGISTER, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_RegID = (uint32_t)reg;

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

jx_mir_operand_t* jx_mir_opBasicBlock(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_BASIC_BLOCK, JMIR_TYPE_VOID);
	if (!operand) {
		return NULL;
	}

	operand->u.m_BB = bb;

	return operand;
}

jx_mir_operand_t* jx_mir_opMemoryRef(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, uint32_t baseRegID, uint32_t indexRegID, uint32_t scale, int32_t displacement)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_MEMORY_REF, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_MemRef.m_BaseRegID = baseRegID;
	operand->u.m_MemRef.m_IndexRegID = indexRegID;
	operand->u.m_MemRef.m_Scale = scale;
	operand->u.m_MemRef.m_Displacement = displacement;

	return operand;
}

jx_mir_operand_t* jx_mir_opStackObj(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, uint32_t sz, uint32_t alignment)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_STACK_OBJECT, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_StackObj = jmir_frameAllocObj(ctx, func->m_FrameInfo, sz, alignment);

	return operand;
}

jx_mir_operand_t* jx_mir_opExternalSymbol(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_type_kind type, const char* name)
{
	jx_mir_operand_t* operand = jmir_operandAlloc(ctx, JMIR_OPERAND_EXTERNAL_SYMBOL, type);
	if (!operand) {
		return NULL;
	}

	operand->u.m_ExternalSymbolName = name;

	return operand;
}

static void jmir_regPrint(jx_mir_context_t* ctx, uint32_t regID, jx_mir_type_kind type, jx_string_buffer_t* sb)
{
	JX_CHECK(type != JMIR_TYPE_VOID, "Unexpected void register!");
	static const char* kRegPostfix[] = {
		[JMIR_TYPE_I8]  = "b",
		[JMIR_TYPE_I16] = "w",
		[JMIR_TYPE_I32] = "d",
		[JMIR_TYPE_I64] = "",
		[JMIR_TYPE_F32] = "f32",
		[JMIR_TYPE_F64] = "f64",
		[JMIR_TYPE_PTR] = "",
	};

	if (regID >= JMIR_FIRST_VIRTUAL_REGISTER) {
		jx_strbuf_printf(sb, "%%vr%u%s", regID - JMIR_FIRST_VIRTUAL_REGISTER, kRegPostfix[type]);
	} else {
		// Special handling of important hw registers
		if (regID == JMIR_HWREG_A) {
			switch (type) {
			case JMIR_TYPE_I8: jx_strbuf_pushCStr(sb, "$al"); break;
			case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$ax"); break;
			case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$eax"); break;
			case JMIR_TYPE_I64:
			case JMIR_TYPE_PTR:
				jx_strbuf_pushCStr(sb, "$rax");
				break;
			default:
				jx_strbuf_printf(sb, "$r%u%s", regID, kRegPostfix[type]);
				break;
			}
		} else if (regID == JMIR_HWREG_C) {
			switch (type) {
			case JMIR_TYPE_I8: jx_strbuf_pushCStr(sb, "$cl"); break;
			case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$cx"); break;
			case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$ecx"); break;
			case JMIR_TYPE_I64:
			case JMIR_TYPE_PTR:
				jx_strbuf_pushCStr(sb, "$rcx");
				break;
			default:
				jx_strbuf_printf(sb, "$r%u%s", regID, kRegPostfix[type]);
				break;
			}
		} else if (regID == JMIR_HWREG_D) {
			switch (type) {
			case JMIR_TYPE_I8: jx_strbuf_pushCStr(sb, "$dl"); break;
			case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$dx"); break;
			case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$edx"); break;
			case JMIR_TYPE_I64:
			case JMIR_TYPE_PTR:
				jx_strbuf_pushCStr(sb, "$rdx");
				break;
			default:
				jx_strbuf_printf(sb, "$r%u%s", regID, kRegPostfix[type]);
				break;
			}
		} else if (regID == JMIR_HWREG_B) {
			switch (type) {
			case JMIR_TYPE_I8: jx_strbuf_pushCStr(sb, "$bl"); break;
			case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$bx"); break;
			case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$ebx"); break;
			case JMIR_TYPE_I64:
			case JMIR_TYPE_PTR:
				jx_strbuf_pushCStr(sb, "$rbx");
				break;
			default:
				jx_strbuf_printf(sb, "$r%u%s", regID, kRegPostfix[type]);
				break;
			}
		} else if (regID == JMIR_HWREG_SP) {
			switch (type) {
			case JMIR_TYPE_I8: jx_strbuf_pushCStr(sb, "$spl"); break;
			case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$sp"); break;
			case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$esp"); break;
			case JMIR_TYPE_I64:
			case JMIR_TYPE_PTR:
				jx_strbuf_pushCStr(sb, "$rsp");
				break;
			default:
				jx_strbuf_printf(sb, "$r%u%s", regID, kRegPostfix[type]);
				break;
			}
		} else if (regID == JMIR_HWREG_BP) {
			switch (type) {
			case JMIR_TYPE_I8: jx_strbuf_pushCStr(sb, "$bpl"); break;
			case JMIR_TYPE_I16: jx_strbuf_pushCStr(sb, "$bp"); break;
			case JMIR_TYPE_I32: jx_strbuf_pushCStr(sb, "$ebp"); break;
			case JMIR_TYPE_I64:
			case JMIR_TYPE_PTR:
				jx_strbuf_pushCStr(sb, "$rbp");
				break;
			default:
				jx_strbuf_printf(sb, "$r%u%s", regID, kRegPostfix[type]);
				break;
			}
		} else {
			jx_strbuf_printf(sb, "$r%u%s", regID, kRegPostfix[type]);
		}
	}
}

void jx_mir_opPrint(jx_mir_context_t* ctx, jx_mir_operand_t* op, jx_string_buffer_t* sb)
{
	switch (op->m_Kind) {
	case JMIR_OPERAND_REGISTER: {
		jmir_regPrint(ctx, op->u.m_RegID, op->m_Type, sb);
	} break;
	case JMIR_OPERAND_CONST: {
		switch (op->m_Type) {
		case JMIR_TYPE_VOID: {
			JX_CHECK(false, "void constant?");
		} break;
		case JMIR_TYPE_I8:
		case JMIR_TYPE_I16:
		case JMIR_TYPE_I32:
		case JMIR_TYPE_I64: {
			jx_strbuf_printf(sb, "%lld", op->u.m_ConstI64);
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
	case JMIR_OPERAND_STACK_OBJECT: {
		switch (op->m_Type) {
		case JMIR_TYPE_I8:
			jx_strbuf_pushCStr(sb, "byte ptr");
			break;
		case JMIR_TYPE_I16:
			jx_strbuf_pushCStr(sb, "word ptr");
			break;
		case JMIR_TYPE_I32:
			jx_strbuf_pushCStr(sb, "dword ptr");
			break;
		case JMIR_TYPE_I64:
		case JMIR_TYPE_PTR:
			jx_strbuf_pushCStr(sb, "qword ptr");
			break;
		case JMIR_TYPE_F32:
		case JMIR_TYPE_F64:
		case JMIR_TYPE_VOID:
			JX_NOT_IMPLEMENTED();
			break;
		default:
			JX_CHECK(false, "Unknown kind of type!");
			break;
		}
		jx_strbuf_pushCStr(sb, " [");

		jmir_regPrint(ctx, JMIR_HWREG_SP, JMIR_TYPE_PTR, sb);

		if (op->u.m_StackObj->m_SPOffset < 0) {
			jx_strbuf_printf(sb, " - %u", (uint32_t)(-op->u.m_StackObj->m_SPOffset));
		} else if (op->u.m_StackObj->m_SPOffset > 0) {
			jx_strbuf_printf(sb, " + %u", (uint32_t)op->u.m_StackObj->m_SPOffset);
		}
		
		jx_strbuf_pushCStr(sb, "]");
	} break;
	case JMIR_OPERAND_GLOBAL_VARIABLE: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OPERAND_MEMORY_REF: {
		switch (op->m_Type) {
		case JMIR_TYPE_I8:
			jx_strbuf_pushCStr(sb, "byte ptr");
			break;
		case JMIR_TYPE_I16:
			jx_strbuf_pushCStr(sb, "word ptr");
			break;
		case JMIR_TYPE_I32:
			jx_strbuf_pushCStr(sb, "dword ptr");
			break;
		case JMIR_TYPE_I64:
		case JMIR_TYPE_PTR:
			jx_strbuf_pushCStr(sb, "qword ptr");
			break;
		case JMIR_TYPE_F32:
		case JMIR_TYPE_F64:
		case JMIR_TYPE_VOID:
			JX_NOT_IMPLEMENTED();
			break;
		default:
			JX_CHECK(false, "Unknown kind of type!");
			break;
		}
		jx_strbuf_pushCStr(sb, " [");
		bool insertOp = false;
		if (op->u.m_MemRef.m_BaseRegID != JMIR_MEMORY_REG_NONE) {
			jmir_regPrint(ctx, op->u.m_MemRef.m_BaseRegID, JMIR_TYPE_PTR, sb);
			insertOp = true;
		}

		if (op->u.m_MemRef.m_IndexRegID != JMIR_MEMORY_REG_NONE) {
			if (insertOp) {
				jx_strbuf_pushCStr(sb, " + ");
			}

			jmir_regPrint(ctx, op->u.m_MemRef.m_IndexRegID, JMIR_TYPE_PTR, sb);

			if (op->u.m_MemRef.m_Scale != 1) {
				jx_strbuf_printf(sb, " * %u", op->u.m_MemRef.m_Scale);
			}
			insertOp = true;
		}

		if (op->u.m_MemRef.m_Displacement != 0) {
			if (insertOp && op->u.m_MemRef.m_Displacement > 0) {
				jx_strbuf_pushCStr(sb, " + ");
			}

			if (op->u.m_MemRef.m_Displacement < 0) {
				jx_strbuf_printf(sb, " - %u", (uint32_t)(-op->u.m_MemRef.m_Displacement));
			} else {
				jx_strbuf_printf(sb, "%u", (uint32_t)op->u.m_MemRef.m_Displacement);
			}
		}
		jx_strbuf_pushCStr(sb, "]");
	} break;
	case JMIR_OPERAND_EXTERNAL_SYMBOL: {
		jx_strbuf_printf(sb, "%s", op->u.m_ExternalSymbolName);
	} break;
	default:
		JX_CHECK(false, "Unknown operand type");
		break;
	}
}

void jx_mir_instrFree(jx_mir_context_t* ctx, jx_mir_instruction_t* instr)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	JX_FREE(allocator, instr->m_Operands);
	JX_FREE(allocator, instr);
}

void jx_mir_instrPrint(jx_mir_context_t* ctx, jx_mir_instruction_t* instr, jx_string_buffer_t* sb)
{
	// TODO: lea does not require size prefixes
	jx_strbuf_printf(sb, "  %s ", kMIROpcodeMnemonic[instr->m_OpCode]);
	
	const uint32_t numOperands = instr->m_NumOperands;
	for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
		if (iOperand != 0) {
			jx_strbuf_pushCStr(sb, ", ");
		}
		jx_mir_opPrint(ctx, instr->m_Operands[iOperand], sb);
	}

	jx_strbuf_pushCStr(sb, ";\n");
}

jx_mir_instruction_t* jx_mir_mov(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOV, dst, src);
}

jx_mir_instruction_t* jx_mir_movsx(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
	return jmir_instrAlloc2(ctx, JMIR_OP_MOVSX, dst, src);
}

jx_mir_instruction_t* jx_mir_movzx(jx_mir_context_t* ctx, jx_mir_operand_t* dst, jx_mir_operand_t* src)
{
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

jx_mir_instruction_t* jx_mir_call(jx_mir_context_t* ctx, jx_mir_operand_t* func)
{
	return jmir_instrAlloc1(ctx, JMIR_OP_CALL, func);;
}

jx_mir_instruction_t* jx_mir_phi(jx_mir_context_t* ctx, jx_mir_operand_t* dst, uint32_t numPredecessors)
{
	jx_mir_instruction_t* instr = jmir_instrAlloc(ctx, JMIR_OP_PHI, 1 + numPredecessors * 2, NULL);
	if (!instr) {
		return NULL;
	}

	instr->m_Operands[0] = dst;

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
	jx_mir_operand_t* op = (jx_mir_operand_t*)JX_ALLOC(ctx->m_OperandAllocator, sizeof(jx_mir_operand_t));
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
	jx_mir_instruction_t* instr = (jx_mir_instruction_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_instruction_t));
	if (!instr) {
		return NULL;
	}

	jx_memset(instr, 0, sizeof(jx_mir_instruction_t));
	instr->m_OpCode = opcode;
	instr->m_NumOperands = numOperands;
	if (numOperands) {
		instr->m_Operands = (jx_mir_operand_t**)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_operand_t*) * numOperands);
		if (!instr->m_Operands) {
			JX_FREE(ctx->m_Allocator, instr);
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

static jx_mir_operand_t* jmir_funcCreateArgument(jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_basic_block_t* bb, uint32_t argID, jx_mir_type_kind argType)
{
	jx_mir_operand_t* vReg = jx_mir_opVirtualReg(ctx, func, argType);

	jx_mir_operand_t* src = (argID < JX_COUNTOF(kMIRFuncArgIReg))
		? jx_mir_opHWReg(ctx, func, argType, kMIRFuncArgIReg[argID])
		: jx_mir_opMemoryRef(ctx, func, argType, JMIR_HWREG_BP, JMIR_MEMORY_REG_NONE, 1, 16 + argID * 8)
		;

	jx_mir_bbAppendInstr(ctx, bb, jx_mir_mov(ctx, vReg, src));

	return vReg;
}

static void jmir_funcFree(jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

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

	JX_FREE(allocator, func->m_Args);
	JX_FREE(allocator, func->m_Name);
	JX_FREE(allocator, func);
}

static void jmir_globalVarFree(jx_mir_context_t* ctx, jx_mir_global_variable_t* gv)
{
	jx_allocator_i* allocator = ctx->m_Allocator;
	const uint32_t numRelocs = (uint32_t)jx_array_sizeu(gv->m_RelocationsArr);
	for (uint32_t iReloc = 0; iReloc < numRelocs; ++iReloc) {
		jx_mir_relocation_t* reloc = &gv->m_RelocationsArr[iReloc];
		JX_FREE(allocator, reloc->m_SymbolName);
	}
	jx_array_free(gv->m_RelocationsArr);
	JX_FREE(allocator, gv->m_Name);
	jx_array_free(gv->m_DataArr);
	JX_FREE(allocator, gv);
}

static jx_mir_frame_info_t* jmir_frameCreate(jx_mir_context_t* ctx)
{
	jx_mir_frame_info_t* fi = (jx_mir_frame_info_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_frame_info_t));
	if (!fi) {
		return NULL;
	}

	jx_memset(fi, 0, sizeof(jx_mir_frame_info_t));
	fi->m_StackObjs = (jx_mir_stack_object_t**)jx_array_create(ctx->m_Allocator);
	if (!fi->m_StackObjs) {
		jmir_frameDestroy(ctx, fi);
		return NULL;
	}

	return fi;
}

static void jmir_frameDestroy(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo)
{
	const uint32_t numStackObjects = jx_array_sizeu(frameInfo->m_StackObjs);
	for (uint32_t iStackObj = 0; iStackObj < numStackObjects; ++iStackObj) {
		JX_FREE(ctx->m_Allocator, frameInfo->m_StackObjs[iStackObj]);
	}
	jx_array_free(frameInfo->m_StackObjs);
	JX_FREE(ctx->m_Allocator, frameInfo);
}

static jx_mir_stack_object_t* jmir_frameAllocObj(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t sz, uint32_t alignment)
{
	jx_mir_stack_object_t* obj = (jx_mir_stack_object_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_mir_stack_object_t));
	if (!obj) {
		return NULL;
	}

	jx_memset(obj, 0, sizeof(jx_mir_stack_object_t));
	obj->m_Size = sz;
	obj->m_SPOffset = jx_roundup_u32(frameInfo->m_Size, alignment);
	frameInfo->m_Size = obj->m_SPOffset + sz;

	jx_array_push_back(frameInfo->m_StackObjs, obj);

	return obj;
}

static void jmir_frameMakeRoomForCall(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo, uint32_t numArguments)
{
	if (numArguments <= frameInfo->m_MaxCallArgs) {
		// Already have enough space for that many arguments.
		return;
	}

	// This might be the first call for the current frame. Make sure there is a 
	// shadow space for at least 4 arguments. This is needed even if the called
	// function has less than 4 arguments.
	const uint32_t maxCallArgs = jx_max_u32(numArguments, 4);
	const uint32_t delta = (maxCallArgs - frameInfo->m_MaxCallArgs) * 8;

	// Move all local variables.
	const uint32_t numStackObjects = (uint32_t)jx_array_sizeu(frameInfo->m_StackObjs);
	for (uint32_t iObj = 0; iObj < numStackObjects; ++iObj) {
		jx_mir_stack_object_t* obj = frameInfo->m_StackObjs[iObj];
		obj->m_SPOffset += delta;
	}

	frameInfo->m_MaxCallArgs = maxCallArgs;
	frameInfo->m_Size += delta;
}

static void jmir_frameFinalize(jx_mir_context_t* ctx, jx_mir_frame_info_t* frameInfo)
{
	frameInfo->m_Size = jx_roundup_u32(frameInfo->m_Size, 16);
}
