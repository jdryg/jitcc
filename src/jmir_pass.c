#include "jmir_pass.h"
#include "jmir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/bitset.h>
#include <jlib/logger.h>
#include <jlib/hashmap.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>

//////////////////////////////////////////////////////////////////////////
// Remove fallthrough jumps
//
static void jmir_funcPass_removeFallthroughJmpDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_removeFallthroughJmpRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

bool jx_mir_funcPassCreate_removeFallthroughJmp(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	pass->m_Inst = NULL;
	pass->run = jmir_funcPass_removeFallthroughJmpRun;
	pass->destroy = jmir_funcPass_removeFallthroughJmpDestroy;

	return true;
}

static void jmir_funcPass_removeFallthroughJmpDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
}

static bool jmir_funcPass_removeFallthroughJmpRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = jx_mir_bbGetFirstTerminatorInstr(ctx, bb);
		if (instr) {
			while (instr->m_Next) {
				instr = instr->m_Next;
			}

			if (instr->m_OpCode == JMIR_OP_JMP) {
				jx_mir_operand_t* targetBB = instr->m_Operands[0];
				if (targetBB->m_Kind == JMIR_OPERAND_BASIC_BLOCK && targetBB->u.m_BB == bb->m_Next) {
					jx_mir_bbRemoveInstr(ctx, bb, instr);
					jx_mir_instrFree(ctx, instr);
				}
			}
		}

		bb = bb->m_Next;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Remove redundant moves
// 
// Eliminates mov/movss/movsd rX, rX even if one of them is a subset of the other
// E.g. mov cx, ecx is considered redundant.
// 
// Note that this instruction is actually invalid (there is no valid encoding)
// and it's not generated during codegen. It might appear when the variable is spilled 
// during register allocation.
// 
// TODO: Check if this is wrong and if it should be fixed in the regalloc code.
//
static void jmir_funcPass_removeRedundantMovesDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_removeRedundantMovesRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

bool jx_mir_funcPassCreate_removeRedundantMoves(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	pass->m_Inst = NULL;
	pass->run = jmir_funcPass_removeRedundantMovesRun;
	pass->destroy = jmir_funcPass_removeRedundantMovesDestroy;

	return true;
}

static void jmir_funcPass_removeRedundantMovesDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
}

static bool jmir_funcPass_removeRedundantMovesRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_mir_instruction_t* instrNext = instr->m_Next;

			const bool isMov = false
				|| instr->m_OpCode == JMIR_OP_MOV
				|| instr->m_OpCode == JMIR_OP_MOVSS
				|| instr->m_OpCode == JMIR_OP_MOVSD
				;
			if (isMov) {
				jx_mir_operand_t* dst = instr->m_Operands[0];
				jx_mir_operand_t* src = instr->m_Operands[1];

				if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regEqual(dst->u.m_Reg, src->u.m_Reg)) {
					jx_mir_bbRemoveInstr(ctx, bb, instr);
					jx_mir_instrFree(ctx, instr);
				}
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Simplify conditional jumps
//
static void jmir_funcPass_simplifyCondJmpDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_simplifyCondJmpRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

bool jx_mir_funcPassCreate_simplifyCondJmp(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	pass->m_Inst = NULL;
	pass->run = jmir_funcPass_simplifyCondJmpRun;
	pass->destroy = jmir_funcPass_simplifyCondJmpDestroy;

	return true;
}

static void jmir_funcPass_simplifyCondJmpDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
}

static bool jmir_funcPass_simplifyCondJmpRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = jx_mir_bbGetFirstTerminatorInstr(ctx, bb);
		while (instr) {
			jx_mir_instruction_t* instrNext = instr->m_Next;

			if (instr->m_OpCode == JMIR_OP_JE || instr->m_OpCode == JMIR_OP_JNE) {
				// Turn the following sequence
				//
				// cmp/ucomiss/comiss ...
				// setcc %vrb
				// test %vrb, %vrb
				// je/jne bb.target
				//
				// into
				//
				// cmp ...
				// jcc bb.target
				jx_mir_instruction_t* testInstr = instr->m_Prev;
				if (testInstr && testInstr->m_OpCode == JMIR_OP_TEST) {
					jx_mir_operand_t* op0 = testInstr->m_Operands[0];
					jx_mir_operand_t* op1 = testInstr->m_Operands[1];
					const bool isValidTest = true
						&& op0->m_Kind == JMIR_OPERAND_REGISTER
						&& op1->m_Kind == JMIR_OPERAND_REGISTER
						&& op0->m_Type == JMIR_TYPE_I8
						&& op1->m_Type == JMIR_TYPE_I8
						&& jx_mir_regEqual(op0->u.m_Reg, op1->u.m_Reg)
						;
					if (isValidTest) {
						jx_mir_instruction_t* setccInstr = testInstr->m_Prev;
						if (setccInstr && jx_mir_opcodeIsSetcc(setccInstr->m_OpCode)) {
							jx_mir_operand_t* op = setccInstr->m_Operands[0];
							const bool isValidSetcc = true
								&& op->m_Kind == JMIR_OPERAND_REGISTER
								&& op->m_Type == JMIR_TYPE_I8
								&& jx_mir_regEqual(op->u.m_Reg, op0->u.m_Reg)
								;
							if (isValidSetcc) {
								jx_mir_instruction_t* cmpInstr = setccInstr->m_Prev;
								const bool isCmpInstr = cmpInstr && (false
									|| cmpInstr->m_OpCode == JMIR_OP_CMP
									|| cmpInstr->m_OpCode == JMIR_OP_COMISS
									|| cmpInstr->m_OpCode == JMIR_OP_COMISD
									|| cmpInstr->m_OpCode == JMIR_OP_UCOMISS
									|| cmpInstr->m_OpCode == JMIR_OP_UCOMISD)
									;
								if (isCmpInstr) {
									if (cmpInstr->m_Operands[1]->m_Kind == JMIR_OPERAND_REGISTER) {
										jx_mir_instruction_t* movInstr = cmpInstr->m_Prev;
										const bool isMovRegConst = true
											&& movInstr
											&& movInstr->m_OpCode == JMIR_OP_MOV
											&& movInstr->m_Operands[0]->m_Kind == JMIR_OPERAND_REGISTER
											&& jx_mir_regEqual(movInstr->m_Operands[0]->u.m_Reg, cmpInstr->m_Operands[1]->u.m_Reg)
											&& movInstr->m_Operands[1]->m_Kind == JMIR_OPERAND_CONST
											;
										if (isMovRegConst) {
											jx_mir_bbInsertInstrBefore(ctx, bb, cmpInstr, jx_mir_cmp(ctx, cmpInstr->m_Operands[0], movInstr->m_Operands[1]));

											jx_mir_bbRemoveInstr(ctx, bb, movInstr);
											jx_mir_bbRemoveInstr(ctx, bb, cmpInstr);
											jx_mir_instrFree(ctx, movInstr);
											jx_mir_instrFree(ctx, cmpInstr);
										}
									}

									const jx_mir_condition_code jumpCC = (jx_mir_condition_code)(instr->m_OpCode - JMIR_OP_JCC_BASE);
									jx_mir_condition_code setCC = (jx_mir_condition_code)(setccInstr->m_OpCode - JMIR_OP_SETCC_BASE);
									if (jumpCC == JMIR_CC_E) {
										// Invert setCC condition code
										setCC = jx_mir_ccInvert(setCC);
									}

									jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_jcc(ctx, setCC, instr->m_Operands[0]));

									jx_mir_bbRemoveInstr(ctx, bb, testInstr);
									jx_mir_bbRemoveInstr(ctx, bb, setccInstr);
									jx_mir_bbRemoveInstr(ctx, bb, instr);
									jx_mir_instrFree(ctx, testInstr);
									jx_mir_instrFree(ctx, setccInstr);
									jx_mir_instrFree(ctx, instr);
								}
							}
						}
					}
				}
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Fix 
// - mov memDst, memSrc => mov vr, memSrc; mov memDst, vr;
//
static void jmir_funcPass_fixMemMemOpsDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_fixMemMemOpsRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

bool jx_mir_funcPassCreate_fixMemMemOps(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	pass->m_Inst = NULL;
	pass->run = jmir_funcPass_fixMemMemOpsRun;
	pass->destroy = jmir_funcPass_fixMemMemOpsDestroy;

	return true;
}

static void jmir_funcPass_fixMemMemOpsDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
}

static bool jmir_funcPass_fixMemMemOpsRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_mir_instruction_t* instrNext = instr->m_Next;

			switch (instr->m_OpCode) {
			case JMIR_OP_MOV:
			case JMIR_OP_MOVSX:
			case JMIR_OP_MOVZX:
			case JMIR_OP_CMP: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				jx_mir_operand_t* rhs = instr->m_Operands[1];
				const bool isLhsMem = lhs->m_Kind == JMIR_OPERAND_MEMORY_REF;
				const bool isRhsMem = rhs->m_Kind == JMIR_OPERAND_MEMORY_REF;
				if (isLhsMem && isRhsMem) {
					jx_mir_operand_t* vreg = jx_mir_opVirtualReg(ctx, func, rhs->m_Type);
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, vreg, rhs));
					instr->m_Operands[1] = vreg;
				}
			} break;
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Register allocator using Iterated Register Coalescing algorithm from
// https://www.cse.iitm.ac.in/~krishna/cs6013/george.pdf
// 
#define JMIR_REGALLOC_MAX_ITERATIONS    10

typedef struct jmir_graph_node_t jmir_graph_node_t;
typedef struct jmir_mov_instr_t jmir_mov_instr_t;

typedef enum jmir_graph_node_state
{
	JMIR_NODE_STATE_ALLOCATED = 0,
	JMIR_NODE_STATE_INITIAL,
	JMIR_NODE_STATE_PRECOLORED,
	JMIR_NODE_STATE_SIMPLIFY,
	JMIR_NODE_STATE_FREEZE,
	JMIR_NODE_STATE_SPILL,
	JMIR_NODE_STATE_SPILLED,
	JMIR_NODE_STATE_COALESCED,
	JMIR_NODE_STATE_COLORED,
	JMIR_NODE_STATE_SELECT,

	JMIR_NODE_STATE_COUNT
} jmir_graph_node_state;

typedef enum jmir_mov_instr_state
{
	JMIR_MOV_STATE_ALLOCATED = 0,
	JMIR_MOV_STATE_COALESCED,
	JMIR_MOV_STATE_CONSTRAINED,
	JMIR_MOV_STATE_FROZEN,
	JMIR_MOV_STATE_WORKLIST,
	JMIR_MOV_STATE_ACTIVE,

	JMIR_MOV_STATE_COUNT
} jmir_mov_instr_state;

typedef struct jmir_graph_node_t
{
	jmir_graph_node_t* m_Next;
	jmir_graph_node_t* m_Prev;
	jx_bitset_t* m_AdjacentSet;
	jmir_graph_node_t** m_AdjacentArr;
	jmir_mov_instr_t** m_MoveArr;
	jmir_graph_node_t* m_Alias;
	jmir_graph_node_state m_State;
	jx_mir_reg_class m_RegClass;
	uint32_t m_Degree;
	uint32_t m_ID; // ID of the node; used as the index in all bitsets; must be unique
	uint32_t m_Color;
	uint32_t m_SpillCost;
} jmir_graph_node_t;

typedef struct jmir_mov_instr_t
{
	jmir_mov_instr_t* m_Next;
	jmir_mov_instr_t* m_Prev;
	jmir_graph_node_t* m_Dst;
	jmir_graph_node_t* m_Src;
	jmir_mov_instr_state m_State;
	JX_PAD(4);
} jmir_mov_instr_t;

typedef struct jmir_hw_reg_desc_t
{
	uint32_t m_Color;
	bool m_Available;
	JX_PAD(3);
} jmir_hw_reg_desc_t;

typedef struct jmir_func_pass_regalloc_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_LinearAllocator;
	jx_mir_context_t* m_Ctx;
	jx_mir_function_t* m_Func;

	jmir_graph_node_t* m_NodeList[JMIR_NODE_STATE_COUNT];
	jmir_mov_instr_t* m_MoveList[JMIR_MOV_STATE_COUNT];

	jmir_graph_node_t* m_Nodes;
	uint32_t m_NumNodes;
	JX_PAD(4);

	uint32_t m_TotalHWRegs[JMIR_REG_CLASS_COUNT];
	uint32_t m_NumAvailHWRegs[JMIR_REG_CLASS_COUNT];
	uint64_t m_AvailHWRegs[JMIR_REG_CLASS_COUNT];
} jmir_func_pass_regalloc_t;

static void jmir_funcPass_regAllocDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_regAllocRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static bool jmir_regAlloc_init(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func, const jmir_hw_reg_desc_t* gpRegDesc, const jmir_hw_reg_desc_t* xmmRegDesc);
static void jmir_regAlloc_shutdown(jmir_func_pass_regalloc_t* pass);

static void jmir_regAlloc_makeWorklist(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_simplify(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_coalesce(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_freeze(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_selectSpill(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_assignColors(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_replaceRegs(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_spill(jmir_func_pass_regalloc_t* pass);

static void jmir_regAlloc_addEdge(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v);
static bool jmir_regAlloc_areNodesAdjacent(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v);
static bool jmir_regAlloc_canCombineNodes(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v);
static void jmir_regAlloc_combineNodes(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v);
static bool jmir_regAlloc_OK(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* t, jmir_graph_node_t* r);
static jmir_graph_node_t* jmir_regAlloc_nodeGetAlias(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node);
static void jmir_regAlloc_nodeAddMovesFrom(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* dst, jmir_graph_node_t* src);
static void jmir_regAlloc_nodeFreezeMoves(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node);
static bool jmir_regAlloc_nodeMoveRelated(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node);
static void jmir_regAlloc_nodeDecrementDegree(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node);
static void jmir_regAlloc_nodeEnableMove(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node);
static void jmir_regAlloc_nodeAddWorklist(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node);
static jmir_graph_node_t* jmir_regAlloc_nodeAdjacentIterNext(jmir_graph_node_t* node, uint32_t* iter);
static bool jmir_regAlloc_nodeInit(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, uint32_t regID, jx_mir_reg_class regClass, uint32_t color);
static void jmir_regAlloc_nodeSetState(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, jmir_graph_node_state state);
static jmir_graph_node_t* jmir_regAlloc_getNode(jmir_func_pass_regalloc_t* pass, uint32_t nodeID);
static bool jmir_nodeIs(const jmir_graph_node_t* node, jmir_graph_node_state state);

static jmir_mov_instr_t* jmir_regAlloc_movAlloc(jmir_func_pass_regalloc_t* pass, jx_mir_instruction_t* instr);
static void jmir_regAlloc_movSetState(jmir_func_pass_regalloc_t* pass, jmir_mov_instr_t* mov, jmir_mov_instr_state state);
static bool jmir_movIs(const jmir_mov_instr_t* mov, jmir_mov_instr_state state);

bool jx_mir_funcPassCreate_regAlloc(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jmir_func_pass_regalloc_t* inst = (jmir_func_pass_regalloc_t*)JX_ALLOC(allocator, sizeof(jmir_func_pass_regalloc_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jmir_func_pass_regalloc_t));
	inst->m_Allocator = allocator;

	inst->m_LinearAllocator = allocator_api->createLinearAllocator(4u << 20, allocator);
	if (!inst->m_LinearAllocator) {
		return false;
	}

	pass->m_Inst = (jx_mir_function_pass_o*)inst;
	pass->run = jmir_funcPass_regAllocRun;
	pass->destroy = jmir_funcPass_regAllocDestroy;

	return true;
}

static void jmir_funcPass_regAllocDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jmir_func_pass_regalloc_t* pass = (jmir_func_pass_regalloc_t*)inst;

	if (pass->m_LinearAllocator) {
		allocator_api->destroyLinearAllocator(pass->m_LinearAllocator);
		pass->m_LinearAllocator = NULL;
	}

	JX_FREE(allocator, pass);
}

static bool jmir_funcPass_regAllocRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jmir_func_pass_regalloc_t* pass = (jmir_func_pass_regalloc_t*)inst;

	pass->m_Ctx = ctx;
	pass->m_Func = func;

#if 0
	// Color == hw reg ID
	static const jmir_hw_reg_desc_t gpRegDesc[] = {
		[JMIR_HWREGID_A]   = { .m_Color = 0,  .m_Available = true },
		[JMIR_HWREGID_C]   = { .m_Color = 1,  .m_Available = true },
		[JMIR_HWREGID_D]   = { .m_Color = 2,  .m_Available = true },
		[JMIR_HWREGID_B]   = { .m_Color = 3,  .m_Available = true },
		[JMIR_HWREGID_SP]  = { .m_Color = 4,  .m_Available = false },
		[JMIR_HWREGID_BP]  = { .m_Color = 5,  .m_Available = false },
		[JMIR_HWREGID_SI]  = { .m_Color = 6,  .m_Available = true },
		[JMIR_HWREGID_DI]  = { .m_Color = 7,  .m_Available = true },
		[JMIR_HWREGID_R8]  = { .m_Color = 8,  .m_Available = true },
		[JMIR_HWREGID_R9]  = { .m_Color = 9,  .m_Available = true },
		[JMIR_HWREGID_R10] = { .m_Color = 10, .m_Available = true },
		[JMIR_HWREGID_R11] = { .m_Color = 11, .m_Available = true },
		[JMIR_HWREGID_R12] = { .m_Color = 12, .m_Available = true },
		[JMIR_HWREGID_R13] = { .m_Color = 13, .m_Available = true },
		[JMIR_HWREGID_R14] = { .m_Color = 14, .m_Available = true },
		[JMIR_HWREGID_R15] = { .m_Color = 15, .m_Available = true },
	};
#elif 0
	// Prioritize calle-saved regs
	static const jmir_hw_reg_desc_t gpRegDesc[] = {
		[JMIR_HWREGID_A]   = { .m_Color = 7,  .m_Available = true },
		[JMIR_HWREGID_C]   = { .m_Color = 8,  .m_Available = true },
		[JMIR_HWREGID_D]   = { .m_Color = 9,  .m_Available = true },
		[JMIR_HWREGID_B]   = { .m_Color = 0,  .m_Available = true },
		[JMIR_HWREGID_SP]  = { .m_Color = 14, .m_Available = false },
		[JMIR_HWREGID_BP]  = { .m_Color = 15, .m_Available = false },
		[JMIR_HWREGID_SI]  = { .m_Color = 1,  .m_Available = true },
		[JMIR_HWREGID_DI]  = { .m_Color = 2,  .m_Available = true },
		[JMIR_HWREGID_R8]  = { .m_Color = 10, .m_Available = true },
		[JMIR_HWREGID_R9]  = { .m_Color = 11, .m_Available = true },
		[JMIR_HWREGID_R10] = { .m_Color = 12, .m_Available = true },
		[JMIR_HWREGID_R11] = { .m_Color = 13, .m_Available = true },
		[JMIR_HWREGID_R12] = { .m_Color = 3,  .m_Available = true },
		[JMIR_HWREGID_R13] = { .m_Color = 4,  .m_Available = true },
		[JMIR_HWREGID_R14] = { .m_Color = 5,  .m_Available = true },
		[JMIR_HWREGID_R15] = { .m_Color = 6,  .m_Available = true },
	};
#else
	// Prioritize caller-saved regs except RAX is last
	static const jmir_hw_reg_desc_t gpRegDesc[] = {
		[JMIR_HWREGID_C]   = { .m_Color = 0,   .m_Available = true },
		[JMIR_HWREGID_D]   = { .m_Color = 1,   .m_Available = true },
		[JMIR_HWREGID_R8]  = { .m_Color = 2,   .m_Available = true },
		[JMIR_HWREGID_R9]  = { .m_Color = 3,   .m_Available = true },
		[JMIR_HWREGID_R10] = { .m_Color = 4,   .m_Available = true },
		[JMIR_HWREGID_R11] = { .m_Color = 5,   .m_Available = true },
		[JMIR_HWREGID_A]   = { .m_Color = 6,   .m_Available = true },
		[JMIR_HWREGID_B]   = { .m_Color = 7,   .m_Available = true },
		[JMIR_HWREGID_SI]  = { .m_Color = 8,   .m_Available = true },
		[JMIR_HWREGID_DI]  = { .m_Color = 9,   .m_Available = true },
		[JMIR_HWREGID_R12] = { .m_Color = 10,  .m_Available = true },
		[JMIR_HWREGID_R13] = { .m_Color = 11,  .m_Available = true },
		[JMIR_HWREGID_R14] = { .m_Color = 12,  .m_Available = true },
		[JMIR_HWREGID_R15] = { .m_Color = 13,  .m_Available = true },
		[JMIR_HWREGID_SP]  = { .m_Color = 14,  .m_Available = false },
		[JMIR_HWREGID_BP]  = { .m_Color = 15,  .m_Available = false },
	};
#endif

#if 0
	static const jmir_hw_reg_desc_t xmmRegDesc[] = {
		[0]  = { .m_Color = 0,  .m_Available = true },
		[1]  = { .m_Color = 1,  .m_Available = true },
		[2]  = { .m_Color = 2,  .m_Available = true },
		[3]  = { .m_Color = 3,  .m_Available = true },
		[4]  = { .m_Color = 4,  .m_Available = true },
		[5]  = { .m_Color = 5,  .m_Available = true },
		[6]  = { .m_Color = 6,  .m_Available = true },
		[7]  = { .m_Color = 7,  .m_Available = true },
		[8]  = { .m_Color = 8,  .m_Available = true },
		[9]  = { .m_Color = 9,  .m_Available = true },
		[10] = { .m_Color = 10, .m_Available = true },
		[11] = { .m_Color = 11, .m_Available = true },
		[12] = { .m_Color = 12, .m_Available = true },
		[13] = { .m_Color = 13, .m_Available = true },
		[14] = { .m_Color = 14, .m_Available = true },
		[15] = { .m_Color = 15, .m_Available = true },
	};
#else
	static const jmir_hw_reg_desc_t xmmRegDesc[] = {
		[1]  = { .m_Color = 0,  .m_Available = true },
		[2]  = { .m_Color = 1,  .m_Available = true },
		[3]  = { .m_Color = 2,  .m_Available = true },
		[4]  = { .m_Color = 3,  .m_Available = true },
		[5]  = { .m_Color = 4,  .m_Available = true },
		[0]  = { .m_Color = 5,  .m_Available = true },
		[6]  = { .m_Color = 6,  .m_Available = true },
		[7]  = { .m_Color = 7,  .m_Available = true },
		[8]  = { .m_Color = 8,  .m_Available = true },
		[9]  = { .m_Color = 9,  .m_Available = true },
		[10] = { .m_Color = 10, .m_Available = true },
		[11] = { .m_Color = 11, .m_Available = true },
		[12] = { .m_Color = 12, .m_Available = true },
		[13] = { .m_Color = 13, .m_Available = true },
		[14] = { .m_Color = 14, .m_Available = true },
		[15] = { .m_Color = 15, .m_Available = true },
	};
#endif

	uint32_t iter = 0;
	bool changed = true;
	while (changed && iter < JMIR_REGALLOC_MAX_ITERATIONS) {
		changed = false;

		// Liveness analysis + build
		if (!jmir_regAlloc_init(pass, ctx, func, gpRegDesc, xmmRegDesc)) {
			break;
		}

		jmir_regAlloc_makeWorklist(pass);
		while (pass->m_NodeList[JMIR_NODE_STATE_SIMPLIFY] || pass->m_MoveList[JMIR_MOV_STATE_WORKLIST] || pass->m_NodeList[JMIR_NODE_STATE_FREEZE] || pass->m_NodeList[JMIR_NODE_STATE_SPILL]) {
			if (pass->m_NodeList[JMIR_NODE_STATE_SIMPLIFY]) {
				jmir_regAlloc_simplify(pass);
			} else if (pass->m_MoveList[JMIR_MOV_STATE_WORKLIST]) {
				jmir_regAlloc_coalesce(pass);
			} else if (pass->m_NodeList[JMIR_NODE_STATE_FREEZE]) {
				jmir_regAlloc_freeze(pass);
			} else if (pass->m_NodeList[JMIR_NODE_STATE_SPILL]) {
				jmir_regAlloc_selectSpill(pass);
			}
		}

		jmir_regAlloc_assignColors(pass);

		if (pass->m_NodeList[JMIR_NODE_STATE_SPILLED]) {
			jmir_regAlloc_spill(pass);
			changed = true;
		} else {
			jmir_regAlloc_replaceRegs(pass);
		}

		jmir_regAlloc_shutdown(pass);

		++iter;
	}

	JX_CHECK(iter != JMIR_REGALLOC_MAX_ITERATIONS, "Maximum iterations exceeded?");

	return false;
}

static bool jmir_regAlloc_init(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func, const jmir_hw_reg_desc_t* gpRegs, const jmir_hw_reg_desc_t* xmmRegs)
{
	allocator_api->linearAllocatorReset(pass->m_LinearAllocator);

	jx_memset(pass->m_NodeList, 0, sizeof(jmir_graph_node_t*) * JMIR_NODE_STATE_COUNT);
	jx_memset(pass->m_MoveList, 0, sizeof(jmir_mov_instr_t*) * JMIR_MOV_STATE_COUNT);
	pass->m_TotalHWRegs[JMIR_REG_CLASS_GP] = 16;
	pass->m_TotalHWRegs[JMIR_REG_CLASS_XMM] = 16;

	pass->m_AvailHWRegs[JMIR_REG_CLASS_GP] = 0;
	for (uint32_t i = 0; i < 16; ++i) {
		if (gpRegs[i].m_Available) {
			pass->m_AvailHWRegs[JMIR_REG_CLASS_GP] |= (1ull << gpRegs[i].m_Color);
		}
	}
	pass->m_AvailHWRegs[JMIR_REG_CLASS_XMM] = 0;
	for (uint32_t i = 0; i < 16; ++i) {
		if (xmmRegs[i].m_Available) {
			pass->m_AvailHWRegs[JMIR_REG_CLASS_XMM] |= (1ull << xmmRegs[i].m_Color);
		}
	}
	pass->m_NumAvailHWRegs[JMIR_REG_CLASS_GP] = jx_bitcount_u64(pass->m_AvailHWRegs[JMIR_REG_CLASS_GP]);
	pass->m_NumAvailHWRegs[JMIR_REG_CLASS_XMM] = jx_bitcount_u64(pass->m_AvailHWRegs[JMIR_REG_CLASS_XMM]);
	
	jx_mir_funcUpdateSCCs(ctx, func);
	jx_mir_funcUpdateLiveness(ctx, func);

	const uint32_t numNodes = jx_mir_funcGetRegBitsetSize(ctx, func);
	pass->m_Nodes = (jmir_graph_node_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_graph_node_t) * numNodes);
	if (!pass->m_Nodes) {
		return false;
	}
	jx_memset(pass->m_Nodes, 0, sizeof(jmir_graph_node_t) * numNodes);
	pass->m_NumNodes = numNodes;

	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		jmir_graph_node_t* node = &pass->m_Nodes[iNode];
		jx_mir_reg_t reg = jx_mir_funcMapBitsetIDToReg(ctx, func, iNode);
		uint32_t color = UINT32_MAX;
		if (jx_mir_regIsHW(reg)) {
			if (reg.m_Class == JMIR_REG_CLASS_GP) {
				JX_CHECK(reg.m_ID < 16, "Invalid reg id");
				color = gpRegs[reg.m_ID].m_Color;
			} else {
				JX_CHECK(reg.m_Class == JMIR_REG_CLASS_XMM, "Expected XMM register");
				JX_CHECK(reg.m_ID < 16, "Invalid reg id");
				color = xmmRegs[reg.m_ID].m_Color;
			}
		}

		if (!jmir_regAlloc_nodeInit(pass, node, iNode, reg.m_Class, color)) {
			return false;
		}
	}

	// Add edges between registers to graph and create moves
	// 
	// Iterate over all instructions in all basic blocks (assuming dead blocks have been removed)
	// and for each register defined by the instruction, create an interference edge with all 
	// registers live out of the instruction.
	{
		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			JX_CHECK(bb->m_SCCInfo.m_SCC, "Basic block not a part of an SCC?");
			jx_mir_scc_t* scc = bb->m_SCCInfo.m_SCC;
			const uint32_t loopDepth = scc->m_Depth;
			const uint32_t spillCostDelta = jx_pow_u32(10, loopDepth);

			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				const jx_bitset_t* instrLiveOutSet = instr->m_LiveOutSet;
				jx_mir_instr_usedef_t* instrUseDefAnnot = &instr->m_UseDef;

				// Create edges
				{
					const uint32_t numDefs = instrUseDefAnnot->m_NumDefs;
					for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
						jmir_graph_node_t* defNode = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(ctx, func, instrUseDefAnnot->m_Defs[iDef]));

						jx_bitset_iterator_t liveIter;
						jx_bitsetIterBegin(instrLiveOutSet, &liveIter, 0);

						uint32_t liveID = jx_bitsetIterNext(instrLiveOutSet, &liveIter);
						while (liveID != UINT32_MAX) {
							jmir_graph_node_t* liveNode = jmir_regAlloc_getNode(pass, liveID);
							jmir_regAlloc_addEdge(pass, defNode, liveNode);

							liveID = jx_bitsetIterNext(instrLiveOutSet, &liveIter);
						}
					}
				}

				// Create moves
				if (jx_mir_instrIsMovRegReg(instr)) {
					jmir_mov_instr_t* mov = jmir_regAlloc_movAlloc(pass, instr);
					if (!mov) {
						return false;
					}

					jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_WORKLIST);
				}

				// Update use/def spill cost
				{
					// NOTE: This loop can be merged with the edge loop above.
					// Keep it here to make the code more clear. It shouldn't really matter...
					const uint32_t numDefs = instrUseDefAnnot->m_NumDefs;
					for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
						jmir_graph_node_t* defNode = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(ctx, func, instrUseDefAnnot->m_Defs[iDef]));
						defNode->m_SpillCost += spillCostDelta;
					}

					const uint32_t numUses = instrUseDefAnnot->m_NumUses;
					for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
						jmir_graph_node_t* useNode = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(ctx, func, instrUseDefAnnot->m_Uses[iUse]));
						useNode->m_SpillCost += spillCostDelta;
					}
				}

				instr = instr->m_Next;
			}

			bb = bb->m_Next;
		}
	}

	return true;
}

static void jmir_regAlloc_shutdown(jmir_func_pass_regalloc_t* pass)
{
	if (pass->m_Nodes) {
		const uint32_t numNodes = pass->m_NumNodes;
		for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
			jmir_graph_node_t* node = &pass->m_Nodes[iNode];
			jx_array_free(node->m_AdjacentArr);
			jx_array_free(node->m_MoveArr);
		}
		pass->m_Nodes = NULL;
	}
}

static void jmir_regAlloc_makeWorklist(jmir_func_pass_regalloc_t* pass)
{
	jmir_graph_node_t* node = pass->m_NodeList[JMIR_NODE_STATE_INITIAL];
	while (node) {
		jmir_graph_node_t* nextNode = node->m_Next;

		if (node->m_Degree >= pass->m_NumAvailHWRegs[node->m_RegClass]) {
			jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_SPILL);
		} else if (jmir_regAlloc_nodeMoveRelated(pass, node)) {
			jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_FREEZE);
		} else {
			jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_SIMPLIFY);
		}

		node = nextNode;
	}
}

static void jmir_regAlloc_simplify(jmir_func_pass_regalloc_t* pass)
{
	JX_CHECK(pass->m_NodeList[JMIR_NODE_STATE_SIMPLIFY], "Simplify worklist is empty.");

	jmir_graph_node_t* node = pass->m_NodeList[JMIR_NODE_STATE_SIMPLIFY];
	JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_SIMPLIFY), "Node in simplify worklist not in simplify state!");

	jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_SELECT);

	uint32_t adjIter = 0;
	jmir_graph_node_t* adjacent = jmir_regAlloc_nodeAdjacentIterNext(node, &adjIter);
	while (adjacent) {
		jmir_regAlloc_nodeDecrementDegree(pass, adjacent);
		adjacent = jmir_regAlloc_nodeAdjacentIterNext(node, &adjIter);
	}
}

static void jmir_regAlloc_coalesce(jmir_func_pass_regalloc_t* pass)
{
	JX_CHECK(pass->m_MoveList[JMIR_MOV_STATE_WORKLIST], "Move worklist is empty.");

	jmir_mov_instr_t* mov = pass->m_MoveList[JMIR_MOV_STATE_WORKLIST];
	JX_CHECK(jmir_movIs(mov, JMIR_MOV_STATE_WORKLIST), "Move in worklist moves not in worklist state!");

	jmir_graph_node_t* x = jmir_regAlloc_nodeGetAlias(pass, mov->m_Dst);
	jmir_graph_node_t* y = jmir_regAlloc_nodeGetAlias(pass, mov->m_Src);

	jmir_graph_node_t* u = x;
	jmir_graph_node_t* v = y;
	if (jmir_nodeIs(y, JMIR_NODE_STATE_PRECOLORED)) {
		u = y;
		v = x;
	}

	if (u == v) {
		jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_COALESCED);
		jmir_regAlloc_nodeAddWorklist(pass, u);
	} else if (jmir_nodeIs(v, JMIR_NODE_STATE_PRECOLORED) || jmir_regAlloc_areNodesAdjacent(pass, u, v)) {
		jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_CONSTRAINED);
		jmir_regAlloc_nodeAddWorklist(pass, u);
		jmir_regAlloc_nodeAddWorklist(pass, v);
	} else if (jmir_regAlloc_canCombineNodes(pass, u, v)) {
		jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_COALESCED);
		jmir_regAlloc_combineNodes(pass, u, v);
		jmir_regAlloc_nodeAddWorklist(pass, u);
	} else {
		jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_ACTIVE);
	}
}

static void jmir_regAlloc_freeze(jmir_func_pass_regalloc_t* pass)
{
	jmir_graph_node_t* node = pass->m_NodeList[JMIR_NODE_STATE_FREEZE];
	JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_FREEZE), "Node in freeze worklist not in freeze state!");

	jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_SIMPLIFY);
	jmir_regAlloc_nodeFreezeMoves(pass, node);
}

static void jmir_regAlloc_selectSpill(jmir_func_pass_regalloc_t* pass)
{
	jmir_graph_node_t* spillCandidate = pass->m_NodeList[JMIR_NODE_STATE_SPILL];
	JX_CHECK(jmir_nodeIs(spillCandidate, JMIR_NODE_STATE_SPILL), "Node in spill worklist not in spill state!");

	jmir_graph_node_t* spilledNode = spillCandidate;
	JX_CHECK(spilledNode->m_Degree != 0, "A spill candidate has 0 degree!");
	double spillCost = (double)spilledNode->m_SpillCost / (double)spilledNode->m_Degree;

	spillCandidate = spillCandidate->m_Next;
	while (spillCandidate) {
		JX_CHECK(jmir_nodeIs(spillCandidate, JMIR_NODE_STATE_SPILL), "Node in spill worklist not in spill state!");
		JX_CHECK(spillCandidate->m_Degree != 0, "A spill candidate has 0 degree!");

		const double candidateCost = (double)spillCandidate->m_SpillCost / (double)spillCandidate->m_Degree;
		if (spillCost > candidateCost) {
			spilledNode = spillCandidate;
			spillCost = candidateCost;
		}

		spillCandidate = spillCandidate->m_Next;
	}

	jmir_regAlloc_nodeSetState(pass, spilledNode, JMIR_NODE_STATE_SIMPLIFY);
	jmir_regAlloc_nodeFreezeMoves(pass, spilledNode);
}

static void jmir_regAlloc_assignColors(jmir_func_pass_regalloc_t* pass)
{
	jmir_graph_node_t* node = pass->m_NodeList[JMIR_NODE_STATE_SELECT];
	while (node) {
		JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_SELECT), "Node in select stack not in select state!");

		uint64_t availableColors = pass->m_AvailHWRegs[node->m_RegClass];

		const uint32_t numAdjacent = (uint32_t)jx_array_sizeu(node->m_AdjacentArr);
		for (uint32_t iAdjacent = 0; iAdjacent < numAdjacent; ++iAdjacent) {
			jmir_graph_node_t* w = node->m_AdjacentArr[iAdjacent];
			jmir_graph_node_t* aliasW = jmir_regAlloc_nodeGetAlias(pass, w);
			if (jmir_nodeIs(aliasW, JMIR_NODE_STATE_COLORED) || jmir_nodeIs(aliasW, JMIR_NODE_STATE_PRECOLORED)) {
				availableColors &= ~(1ull << aliasW->m_Color);
			}
		}

		if (availableColors == 0) {
			jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_SPILLED);
		} else {
			node->m_Color = jx_ctntz_u64(availableColors);
			jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_COLORED);
		}

		node = pass->m_NodeList[JMIR_NODE_STATE_SELECT];
	}

	node = pass->m_NodeList[JMIR_NODE_STATE_COALESCED];
	while (node) {
		JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_COALESCED), "Node in coalesced list not in coalesced state!");

		jmir_graph_node_t* alias = jmir_regAlloc_nodeGetAlias(pass, node);

		if (alias->m_Color <= pass->m_TotalHWRegs[alias->m_RegClass]) {
			node->m_Color = alias->m_Color;
			jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_COLORED);
		} else {
			jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_SPILLED);
		}

		node = pass->m_NodeList[JMIR_NODE_STATE_COALESCED];
	}
}

static jx_mir_reg_t jmir_regAlloc_getHWRegWithColor(jmir_func_pass_regalloc_t* pass, jx_mir_reg_class regClass, uint32_t color)
{
	const uint32_t firstRegID = regClass == JMIR_REG_CLASS_GP
		? 0
		: 16
		;
	for (uint32_t iHWReg = 0; iHWReg < 16; ++iHWReg) {
		jmir_graph_node_t* node = &pass->m_Nodes[firstRegID + iHWReg];
		if (node->m_Color == color) {
			JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_PRECOLORED), "Expected precolored hw register!");
			JX_CHECK(node->m_RegClass == regClass, "Invalid register class");

			jx_mir_reg_t hwReg = jx_mir_funcMapBitsetIDToReg(pass->m_Ctx, pass->m_Func, node->m_ID);
			JX_CHECK(jx_mir_regIsClass(hwReg, regClass), "Invalid register class");
			return hwReg;
		}
	}

	return kMIRRegGPNone;
}

static void jmir_regAlloc_replaceRegs(jmir_func_pass_regalloc_t* pass)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_function_t* func = pass->m_Func;

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			const uint32_t numOperands = instr->m_NumOperands;
			for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
				jx_mir_operand_t* operand = instr->m_Operands[iOperand];
				if (operand->m_Kind == JMIR_OPERAND_REGISTER) {
					if (jx_mir_regIsVirtual(operand->u.m_Reg)) {
						jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(ctx, func, operand->u.m_Reg));
						if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
							jx_mir_reg_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_RegClass, node->m_Color);
							JX_CHECK(jx_mir_regIsValid(hwReg) && jx_mir_regIsHW(hwReg), "Expected hw reg");
							instr->m_Operands[iOperand] = jx_mir_opHWReg(ctx, func, operand->m_Type, hwReg);
						}
					}
				} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					jx_mir_memory_ref_t memRef = *operand->u.m_MemRef;

					if (jx_mir_regIsValid(memRef.m_BaseReg) && jx_mir_regIsVirtual(memRef.m_BaseReg)) {
						jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(ctx, func, memRef.m_BaseReg));
						if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
							jx_mir_reg_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_RegClass, node->m_Color);
							JX_CHECK(jx_mir_regIsValid(hwReg) && jx_mir_regIsHW(hwReg), "Expected hw reg");
							memRef.m_BaseReg = hwReg;
						}
					}

					if (jx_mir_regIsValid(memRef.m_IndexReg) && jx_mir_regIsVirtual(memRef.m_IndexReg)) {
						jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(ctx, func, memRef.m_IndexReg));
						if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
							jx_mir_reg_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_RegClass, node->m_Color);
							JX_CHECK(jx_mir_regIsValid(hwReg) && jx_mir_regIsHW(hwReg), "Expected hw reg");
							memRef.m_IndexReg = hwReg;
						}
					}

					if (!jx_mir_memRefEqual(&memRef, operand->u.m_MemRef)) {
						instr->m_Operands[iOperand] = jx_mir_opMemoryRef(ctx, func, operand->m_Type, memRef.m_BaseReg, memRef.m_IndexReg, memRef.m_Scale, memRef.m_Displacement);
					}
				}
			}

			instr = instr->m_Next;
		}

		bb = bb->m_Next;
	}
}

static void jmir_regAlloc_spill(jmir_func_pass_regalloc_t* pass)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_function_t* func = pass->m_Func;

	JX_CHECK(pass->m_NodeList[JMIR_NODE_STATE_SPILLED], "Expected at least one node in spilled list!");

	jmir_graph_node_t* node = pass->m_NodeList[JMIR_NODE_STATE_SPILLED];
	while (node) {
		JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_SPILLED), "Expected node from spill list!");

		jx_mir_reg_t vreg = jx_mir_funcMapBitsetIDToReg(ctx, func, node->m_ID);
		JX_CHECK(jx_mir_regIsVirtual(vreg), "Trying to spill a hw reg?");

		if (!jx_mir_funcSpillVirtualReg(ctx, func, vreg)) {
			JX_CHECK(false, "Failed to spill vreg %u", vreg.m_ID);
		}

		node = node->m_Next;
	}
}

static void jmir_regAlloc_addEdge(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v)
{
	if (u != v && u->m_RegClass == v->m_RegClass && !jmir_regAlloc_areNodesAdjacent(pass, u, v)) {
		jx_bitsetSetBit(u->m_AdjacentSet, v->m_ID);
		jx_bitsetSetBit(v->m_AdjacentSet, u->m_ID);

		if (!jmir_nodeIs(u, JMIR_NODE_STATE_PRECOLORED)) {
			jx_array_push_back(u->m_AdjacentArr, v);
			u->m_Degree++;
		}

		if (!jmir_nodeIs(v, JMIR_NODE_STATE_PRECOLORED)) {
			jx_array_push_back(v->m_AdjacentArr, u);
			v->m_Degree++;
		}
	}
}

static bool jmir_regAlloc_areNodesAdjacent(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v)
{
	JX_CHECK(jx_bitsetIsBitSet(u->m_AdjacentSet, v->m_ID) == jx_bitsetIsBitSet(v->m_AdjacentSet, u->m_ID), "Invalid bitset state!");
	return jx_bitsetIsBitSet(u->m_AdjacentSet, v->m_ID);
}

static bool jmir_regAlloc_canCombineNodes(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v)
{
	bool canCombine = false;

	const jx_mir_reg_class regClass = u->m_RegClass;
	JX_CHECK(regClass == v->m_RegClass, "Trying to combine nodes from different register classes?");

	if (jmir_nodeIs(u, JMIR_NODE_STATE_PRECOLORED)) {
		bool allOK = true;

		uint32_t adjIter = 0;
		jmir_graph_node_t* t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
		while (t) {
			if (!jmir_regAlloc_OK(pass, t, u)) {
				allOK = false;
				break;
			}

			t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
		}

		canCombine = allOK;
	} else {
		uint32_t k = 0;

		uint32_t adjIter = 0;
		jmir_graph_node_t* t = jmir_regAlloc_nodeAdjacentIterNext(u, &adjIter);
		while (t) {
			if (t->m_Degree >= pass->m_NumAvailHWRegs[regClass]) {
				++k;
			}

			t = jmir_regAlloc_nodeAdjacentIterNext(u, &adjIter);
		}

		adjIter = 0;
		t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
		while (t) {
			if (t->m_Degree >= pass->m_NumAvailHWRegs[regClass]) {
				// Only count this node if it's not adjacent to u. If it is,
				// it's already counted in the loop above.
				if (!jmir_regAlloc_areNodesAdjacent(pass, u, t)) {
					++k;
				}
			}

			t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
		}

		canCombine = k < pass->m_NumAvailHWRegs[regClass];
	}

	return canCombine;
}

static void jmir_regAlloc_combineNodes(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v)
{
	JX_CHECK(jmir_nodeIs(v, JMIR_NODE_STATE_FREEZE) || jmir_nodeIs(v, JMIR_NODE_STATE_SPILL), "Node expected to be in freeze or spill state");

	jmir_regAlloc_nodeSetState(pass, v, JMIR_NODE_STATE_COALESCED);

	v->m_Alias = u;

	jmir_regAlloc_nodeAddMovesFrom(pass, u, v);

	uint32_t adjIter = 0;
	jmir_graph_node_t* t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
	while (t) {
		uint32_t d = t->m_Degree;
		jmir_regAlloc_addEdge(pass, t, u);
		if (d < pass->m_NumAvailHWRegs[t->m_RegClass] && t->m_Degree >= pass->m_NumAvailHWRegs[t->m_RegClass]) {
			JX_CHECK(jmir_nodeIs(t, JMIR_NODE_STATE_FREEZE), "Node expected to be in freeze state.");
			jmir_regAlloc_nodeSetState(pass, t, JMIR_NODE_STATE_SPILL);
		}
		jmir_regAlloc_nodeDecrementDegree(pass, t);

		t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
	}

	if (u->m_Degree >= pass->m_NumAvailHWRegs[u->m_RegClass] && jmir_nodeIs(u, JMIR_NODE_STATE_FREEZE)) {
		jmir_regAlloc_nodeSetState(pass, u, JMIR_NODE_STATE_SPILL);
	}
}

static bool jmir_regAlloc_OK(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* t, jmir_graph_node_t* r)
{
	return (t->m_Degree < pass->m_NumAvailHWRegs[t->m_RegClass])
		|| jmir_nodeIs(t, JMIR_NODE_STATE_PRECOLORED)
		|| jmir_regAlloc_areNodesAdjacent(pass, t, r)
		;
}

static jmir_graph_node_t* jmir_regAlloc_nodeGetAlias(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node)
{
	if (jmir_nodeIs(node, JMIR_NODE_STATE_COALESCED)) {
		return jmir_regAlloc_nodeGetAlias(pass, node->m_Alias);
	}

	return node;
}

static void jmir_regAlloc_nodeAddMovesFrom(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* dst, jmir_graph_node_t* src)
{
	const uint32_t numSrcMoves = (uint32_t)jx_array_sizeu(src->m_MoveArr);
	for (uint32_t iSrcMove = 0; iSrcMove < numSrcMoves; ++iSrcMove) {
		jmir_mov_instr_t* srcMov = src->m_MoveArr[iSrcMove];

		bool found = false;
		const uint32_t numDstMoves = (uint32_t)jx_array_sizeu(dst->m_MoveArr);
		for (uint32_t iDstMove = 0; iDstMove < numDstMoves; ++iDstMove) {
			if (dst->m_MoveArr[iDstMove] == srcMov) {
				found = true;
				break;
			}
		}

		if (!found) {
			jx_array_push_back(dst->m_MoveArr, srcMov);
		}
	}
}

static void jmir_regAlloc_nodeFreezeMoves(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u)
{
	const uint32_t numMoves = (uint32_t)jx_array_sizeu(u->m_MoveArr);
	for (uint32_t iMove = 0; iMove < numMoves; ++iMove) {
		jmir_mov_instr_t* mov = u->m_MoveArr[iMove];
		if (jmir_movIs(mov, JMIR_MOV_STATE_ACTIVE) || jmir_movIs(mov, JMIR_MOV_STATE_WORKLIST)) {
			jmir_graph_node_t* x = mov->m_Dst;
			jmir_graph_node_t* y = mov->m_Src;

			jmir_graph_node_t* v = jmir_regAlloc_nodeGetAlias(pass, y);
			if (v == jmir_regAlloc_nodeGetAlias(pass, u)) {
				v = jmir_regAlloc_nodeGetAlias(pass, x);
			}

			JX_CHECK(jmir_movIs(mov, JMIR_MOV_STATE_ACTIVE), "Move expected to be in active list!");
			jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_FROZEN);

			uint32_t numNodeMoves = 0;
			const uint32_t numMovesV = (uint32_t)jx_array_sizeu(v->m_MoveArr);
			for (uint32_t jMove = 0; jMove < numMovesV; ++jMove) {
				jmir_mov_instr_t* vmov = v->m_MoveArr[jMove];
				if (jmir_movIs(vmov, JMIR_MOV_STATE_ACTIVE) || jmir_movIs(vmov, JMIR_MOV_STATE_WORKLIST)) {
					numNodeMoves++;
				}
			}

			if (!jmir_nodeIs(v, JMIR_NODE_STATE_PRECOLORED) && numNodeMoves == 0 && v->m_Degree < pass->m_NumAvailHWRegs[v->m_RegClass]) {
				JX_CHECK(jmir_nodeIs(v, JMIR_NODE_STATE_FREEZE), "Node expected to be in freeze worklist");
				jmir_regAlloc_nodeSetState(pass, v, JMIR_NODE_STATE_SIMPLIFY);
			}
		}
	}
}

static bool jmir_regAlloc_nodeMoveRelated(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node)
{
	const uint32_t numMoves = (uint32_t)jx_array_sizeu(node->m_MoveArr);
	for (uint32_t iMove = 0; iMove < numMoves; ++iMove) {
		jmir_mov_instr_t* mov = node->m_MoveArr[iMove];
		if (jmir_movIs(mov, JMIR_MOV_STATE_ACTIVE) || jmir_movIs(mov, JMIR_MOV_STATE_WORKLIST)) {
			return true;
		}
	}

	return false;
}

static void jmir_regAlloc_nodeDecrementDegree(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node)
{
	// Don't change the degree of precolored nodes.
	if (jmir_nodeIs(node, JMIR_NODE_STATE_PRECOLORED)) {
		return;
	}

	const uint32_t d = node->m_Degree;

	--node->m_Degree;

	if (d == pass->m_NumAvailHWRegs[node->m_RegClass]) {
		jmir_regAlloc_nodeEnableMove(pass, node);

		uint32_t adjIter = 0;
		jmir_graph_node_t* adjacent = jmir_regAlloc_nodeAdjacentIterNext(node, &adjIter);
		while (adjacent) {
			jmir_regAlloc_nodeEnableMove(pass, adjacent);
			adjacent = jmir_regAlloc_nodeAdjacentIterNext(node, &adjIter);
		}

		JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_SPILL), "Node expected to be in spill worklist!");

		const bool isMoveRelated = jmir_regAlloc_nodeMoveRelated(pass, node);
		jmir_regAlloc_nodeSetState(pass, node, isMoveRelated ? JMIR_NODE_STATE_FREEZE : JMIR_NODE_STATE_SIMPLIFY);
	}
}

static void jmir_regAlloc_nodeEnableMove(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node)
{
	const uint32_t numMoves = (uint32_t)jx_array_sizeu(node->m_MoveArr);
	for (uint32_t iMove = 0; iMove < numMoves; ++iMove) {
		jmir_mov_instr_t* mov = node->m_MoveArr[iMove];
		if (jmir_movIs(mov, JMIR_MOV_STATE_ACTIVE)) {
			jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_WORKLIST);
		}
	}
}

static void jmir_regAlloc_nodeAddWorklist(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node)
{
	if (!jmir_nodeIs(node, JMIR_NODE_STATE_PRECOLORED) && !jmir_regAlloc_nodeMoveRelated(pass, node) && node->m_Degree < pass->m_NumAvailHWRegs[node->m_RegClass]) {
		JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_FREEZE), "Expected node to be in freeze list");
		jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_SIMPLIFY);
	}
}

static jmir_graph_node_t* jmir_regAlloc_nodeAdjacentIterNext(jmir_graph_node_t* node, uint32_t* iter)
{
	jmir_graph_node_t* res = NULL;
	const uint32_t numAdjacent = (uint32_t)jx_array_sizeu(node->m_AdjacentArr);
	uint32_t i = *iter;
	for (; i < numAdjacent; ++i) {
		jmir_graph_node_t* adjacent = node->m_AdjacentArr[i];
		if (!jmir_nodeIs(adjacent, JMIR_NODE_STATE_SELECT) && !jmir_nodeIs(adjacent, JMIR_NODE_STATE_COALESCED)) {
			res = adjacent;
			break;
		}
	}

	*iter = i + 1;

	return res;
}

static bool jmir_regAlloc_nodeInit(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, uint32_t id, jx_mir_reg_class regClass, uint32_t color)
{
	node->m_AdjacentSet = jx_bitsetCreate(pass->m_NumNodes, pass->m_LinearAllocator);
	if (!node->m_AdjacentSet) {
		return false;
	}

	node->m_MoveArr = (jmir_mov_instr_t**)jx_array_create(pass->m_Allocator);
	if (!node->m_MoveArr) {
		return false;
	}

	if (color == UINT32_MAX) {
		node->m_AdjacentArr = (jmir_graph_node_t**)jx_array_create(pass->m_Allocator);
		if (!node->m_AdjacentArr) {
			return false;
		}
	} else {
		node->m_AdjacentArr = NULL;
	}

	node->m_ID = id;
	node->m_Color = color;
	node->m_State = JMIR_NODE_STATE_ALLOCATED;
	node->m_RegClass = regClass;
	node->m_SpillCost = color != UINT32_MAX ? UINT32_MAX : 0;
	jmir_regAlloc_nodeSetState(pass, node, color != UINT32_MAX ? JMIR_NODE_STATE_PRECOLORED : JMIR_NODE_STATE_INITIAL);

	return true;
}

static void jmir_regAlloc_nodeSetState(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, jmir_graph_node_state state)
{
	// Remove from previous list
	if (jmir_nodeIs(node, JMIR_NODE_STATE_ALLOCATED)) {
		JX_CHECK(!node->m_Next && !node->m_Prev, "Node in allocated state should not be part of a list!");
	} else {
		if (pass->m_NodeList[node->m_State] == node) {
			pass->m_NodeList[node->m_State] = node->m_Next;
		}
	}

	if (node->m_Prev) {
		node->m_Prev->m_Next = node->m_Next;
	}
	if (node->m_Next) {
		node->m_Next->m_Prev = node->m_Prev;
	}

	// Add to new list at the head
	node->m_State = state;
	node->m_Prev = NULL;
	node->m_Next = pass->m_NodeList[state];

	if (pass->m_NodeList[state]) {
		pass->m_NodeList[state]->m_Prev = node;
	}

	pass->m_NodeList[state] = node;
}

static jmir_graph_node_t* jmir_regAlloc_getNode(jmir_func_pass_regalloc_t* pass, uint32_t nodeID)
{
	JX_CHECK(nodeID < pass->m_NumNodes, "Invalid node index");
	return &pass->m_Nodes[nodeID];
}

static inline bool jmir_nodeIs(const jmir_graph_node_t* node, jmir_graph_node_state state)
{
	return node->m_State == state;
}

static jmir_mov_instr_t* jmir_regAlloc_movAlloc(jmir_func_pass_regalloc_t* pass, jx_mir_instruction_t* instr)
{
	jmir_mov_instr_t* mov = (jmir_mov_instr_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_mov_instr_t));
	if (!mov) {
		return NULL;
	}

	jx_mir_instr_usedef_t* instrUseDefAnnot = &instr->m_UseDef;

	jx_memset(mov, 0, sizeof(jmir_mov_instr_t));
	mov->m_State = JMIR_MOV_STATE_ALLOCATED;
	mov->m_Dst = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(pass->m_Ctx, pass->m_Func, instrUseDefAnnot->m_Defs[0]));
	mov->m_Src = jmir_regAlloc_getNode(pass, jx_mir_funcMapRegToBitsetID(pass->m_Ctx, pass->m_Func, instrUseDefAnnot->m_Uses[0]));

	jx_array_push_back(mov->m_Dst->m_MoveArr, mov);
	if (!jx_mir_regEqual(instrUseDefAnnot->m_Defs[0], instrUseDefAnnot->m_Uses[0])) {
		jx_array_push_back(mov->m_Src->m_MoveArr, mov);
	}

	return mov;
}

static void jmir_regAlloc_movSetState(jmir_func_pass_regalloc_t* pass, jmir_mov_instr_t* mov, jmir_mov_instr_state state)
{
	// Remove from previous list
	if (jmir_movIs(mov, JMIR_MOV_STATE_ALLOCATED)) {
		JX_CHECK(!mov->m_Next && !mov->m_Prev, "Move in allocated state should not be part of a list!");
	} else {
		if (pass->m_MoveList[mov->m_State] == mov) {
			pass->m_MoveList[mov->m_State] = mov->m_Next;
		}
	}

	// Add to new list at the head.
	mov->m_State = state;
	mov->m_Prev = NULL;
	mov->m_Next = pass->m_MoveList[state];

	if (pass->m_MoveList[state]) {
		pass->m_MoveList[state]->m_Prev = mov;
	}

	pass->m_MoveList[state] = mov;
}

static inline bool jmir_movIs(const jmir_mov_instr_t* mov, jmir_mov_instr_state state)
{
	return mov->m_State == state;
}

//////////////////////////////////////////////////////////////////////////
// Peephole optimizations
//
typedef struct jmir_func_pass_peephole_t
{
	jx_allocator_i* m_Allocator;
	jx_mir_context_t* m_Ctx;
	jx_mir_function_t* m_Func;
} jmir_func_pass_peephole_t;

static void jmir_funcPass_peepholeDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_peepholeRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static bool jmir_peephole_mov(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_movss(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_movsd(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_comiss(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_comisd(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_jumps(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_imul(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_lea(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);
static bool jmir_peephole_mov_x(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr);

static bool jmir_peephole_isFloatConst(jx_mir_operand_t* op, double val);

bool jx_mir_funcPassCreate_peephole(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jmir_func_pass_peephole_t* inst = (jmir_func_pass_peephole_t*)JX_ALLOC(allocator, sizeof(jmir_func_pass_peephole_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jmir_func_pass_peephole_t));
	inst->m_Allocator = allocator;

	pass->m_Inst = (jx_mir_function_pass_o*)inst;
	pass->run = jmir_funcPass_peepholeRun;
	pass->destroy = jmir_funcPass_peepholeDestroy;

	return true;
}

static void jmir_funcPass_peepholeDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jmir_func_pass_peephole_t* pass = (jmir_func_pass_peephole_t*)inst;
	JX_FREE(allocator, pass);
}

static bool jmir_funcPass_peepholeRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jmir_func_pass_peephole_t* pass = (jmir_func_pass_peephole_t*)inst;

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	uint32_t numOpts = 0;

	bool changed = true;
	while (changed) {
		changed = false;

		const uint32_t prevIterNumOpts = numOpts;

		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_mir_instruction_t* instrNext = instr->m_Next;

				if (instr->m_OpCode == JMIR_OP_MOV) {
					numOpts += jmir_peephole_mov(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_MOVSS) {
					numOpts += jmir_peephole_movss(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_MOVSD) {
					numOpts += jmir_peephole_movsd(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_UCOMISS || instr->m_OpCode == JMIR_OP_COMISS) {
					numOpts += jmir_peephole_comiss(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_UCOMISD || instr->m_OpCode == JMIR_OP_COMISD) {
					numOpts += jmir_peephole_comisd(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_JMP || jx_mir_opcodeIsJcc(instr->m_OpCode)) {
					numOpts += jmir_peephole_jumps(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_IMUL) {
					numOpts += jmir_peephole_imul(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_LEA) {
					numOpts += jmir_peephole_lea(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JMIR_OP_MOVSX || instr->m_OpCode == JMIR_OP_MOVZX) {
					numOpts += jmir_peephole_mov_x(pass, instr) ? 1 : 0;
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}

		changed = prevIterNumOpts != numOpts;
	}
	
	return numOpts != 0;
}

static bool jmir_peephole_mov(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;

	// Try to merge with previous instruction
	if (instr->m_Prev) {
		jx_mir_operand_t* dstOp = instr->m_Operands[0];
		jx_mir_operand_t* srcOp = instr->m_Operands[1];
		const bool dstOp_isMemRef = dstOp->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_opIsStackObj(dstOp);
		const bool srcOp_isMemRef = srcOp->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_opIsStackObj(srcOp);

		if (dstOp_isMemRef || srcOp_isMemRef) {
			JX_CHECK(dstOp_isMemRef ^ srcOp_isMemRef, "Both operands are memory references?");
			jx_mir_memory_ref_t* memRef = dstOp_isMemRef
				? dstOp->u.m_MemRef
				: srcOp->u.m_MemRef
				;

			// Check if the memory reference is [baseReg + disp]
			const bool isMemRef_reg = true
				&& jx_mir_regIsValid(memRef->m_BaseReg)
				&& jx_mir_regIsVirtual(memRef->m_BaseReg)
				&& !jx_mir_regIsValid(memRef->m_IndexReg)
				&& memRef->m_Scale == 1
				;
			if (isMemRef_reg) {
				jx_mir_instruction_t* prevInstr = instr->m_Prev;

				const bool isMovToBaseReg = true
					&& prevInstr->m_OpCode == JMIR_OP_MOV
					&& jx_mir_opIsReg(prevInstr->m_Operands[0], memRef->m_BaseReg)
					&& prevInstr->m_Operands[1]->m_Kind == JMIR_OPERAND_REGISTER
					;
				const bool isLeaToBaseReg = true
					&& prevInstr->m_OpCode == JMIR_OP_LEA
					&& jx_mir_opIsReg(prevInstr->m_Operands[0], memRef->m_BaseReg)
					&& prevInstr->m_Operands[1]->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_opIsStackObj(prevInstr->m_Operands[1])
					;
				if (isMovToBaseReg) {
					// mov vr1, vr_base
					// mov [vr1], val  or  mov reg, [vr1]
					//  =>
					// mov [vr_base], val or mov reg, [vr_base]
					memRef->m_BaseReg = prevInstr->m_Operands[1]->u.m_Reg;

					res = true;
				} else if (isLeaToBaseReg) {
					jx_mir_memory_ref_t* srcMemRef = prevInstr->m_Operands[1]->u.m_MemRef;
					JX_CHECK(!jx_mir_opIsReg(prevInstr->m_Operands[1], srcMemRef->m_BaseReg), "!!!");
					memRef->m_BaseReg = srcMemRef->m_BaseReg;
					memRef->m_IndexReg = srcMemRef->m_IndexReg;
					memRef->m_Scale = srcMemRef->m_Scale;
					memRef->m_Displacement += srcMemRef->m_Displacement;

					res = true;
				}
			}
		}
	}

	return res;
}

static bool jmir_peephole_movss(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	if (jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
		// movss xmm, 0.0
		//  =>
		// xorps xmm, xmm
		jx_mir_instruction_t* xorInstr = jx_mir_xorps(ctx, instr->m_Operands[0], instr->m_Operands[0]);
		jx_mir_bbInsertInstrBefore(ctx, bb, instr, xorInstr);
		jx_mir_bbRemoveInstr(ctx, bb, instr);
		jx_mir_instrFree(ctx, instr);

		res = true;
	}

	return res;
}

static bool jmir_peephole_movsd(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	if (jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
		// movsd xmm, 0.0
		//  =>
		// xorpd xmm, xmm
		jx_mir_instruction_t* xorInstr = jx_mir_xorpd(ctx, instr->m_Operands[0], instr->m_Operands[0]);
		jx_mir_bbInsertInstrBefore(ctx, bb, instr, xorInstr);
		jx_mir_bbRemoveInstr(ctx, bb, instr);
		jx_mir_instrFree(ctx, instr);

		res = true;
	}

	return res;
}

static bool jmir_peephole_comiss(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_function_t* func = pass->m_Func;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	if (jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
		// (u)comiss xmm, 0.0
		//  => 
		// xorps xmmtmp, xmmtmp
		// ucomiss xmm, xmmtmp
		jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx, func, JMIR_TYPE_F128);
		jx_mir_instruction_t* xorInstr = jx_mir_xorps(ctx, tmp, tmp);
		jx_mir_bbInsertInstrBefore(ctx, bb, instr, xorInstr);
		instr->m_Operands[1] = tmp;

		res = true;
	}

	return res;
}

static bool jmir_peephole_comisd(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_function_t* func = pass->m_Func;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	if (jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
		// (u)comisd xmm, 0.0
		//  => 
		// xorps xmmtmp, xmmtmp
		// (u)comisd xmm, xmmtmp
		jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx, func, JMIR_TYPE_F128);
		jx_mir_instruction_t* xorInstr = jx_mir_xorpd(ctx, tmp, tmp);
		jx_mir_bbInsertInstrBefore(ctx, bb, instr, xorInstr);
		instr->m_Operands[1] = tmp;

		res = true;
	}

	return res;
}

static bool jmir_peephole_jumps(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	{
		// Redirect jmp to jmp to the final jmp directly.
		jx_mir_operand_t* targetOp = instr->m_Operands[0];
		JX_CHECK(targetOp->m_Kind == JMIR_OPERAND_BASIC_BLOCK, "Expected basic block as jmp target");

		jx_mir_basic_block_t* targetBB = targetOp->u.m_BB;
		if (targetBB->m_InstrListHead && targetBB->m_InstrListHead->m_OpCode == JMIR_OP_JMP) {
			JX_CHECK(!targetBB->m_InstrListHead->m_Next, "Unconditional jump should be the last instruction in a basic block!");

			jx_mir_operand_t* newTargetOp = targetBB->m_InstrListHead->m_Operands[0];
			JX_CHECK(newTargetOp->m_Kind == JMIR_OPERAND_BASIC_BLOCK, "Expected basic block as jmp target");
			targetOp->u.m_BB = newTargetOp->u.m_BB;

			res = true;
		}
	}

	return res;
}

static bool jmir_peephole_imul(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	
	{
		// mov reg1, const
		// imul reg2, reg1
		//  => 
		// imul reg2, reg2, const
		jx_mir_operand_t* imul_dstOp = instr->m_Operands[0];
		jx_mir_operand_t* imul_srcOp = instr->m_Operands[1];
		if (imul_dstOp->m_Kind == JMIR_OPERAND_REGISTER && imul_srcOp->m_Kind == JMIR_OPERAND_REGISTER) {
			jx_mir_instruction_t* prevInstr = instr->m_Prev;
			if (prevInstr && prevInstr->m_OpCode == JMIR_OP_MOV && jx_mir_opEqual(imul_srcOp, prevInstr->m_Operands[0])) {
				jx_mir_operand_t* mov_srcOp = prevInstr->m_Operands[1];
				// TODO: 
				// mov %vr18d, 0x00000006;
				// mov %vr17d, %vr16d;
				// imul %vr17d, %vr18d;
				//  =>
				// imul %vr17d, %vr16d, 0x00000006
				if (mov_srcOp->m_Kind == JMIR_OPERAND_CONST && mov_srcOp->u.m_ConstI64 >= INT32_MIN && mov_srcOp->u.m_ConstI64 <= INT32_MAX) {
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_imul3(ctx, imul_dstOp, imul_dstOp, mov_srcOp));
					jx_mir_bbRemoveInstr(ctx, bb, instr);
					jx_mir_instrFree(ctx, instr);

					res = true;
				}
			}
		}
	}

	return res;
}

static bool jmir_peephole_lea(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_function_t* func = pass->m_Func;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;

#if 0 // NOTE: This is handled by instruction combiner
	{
		// mov vr1, vr_base
		// lea vr2, [vr1 + offset]
		//  =>
		// lea vr2, [vr_base + offset]
		jx_mir_operand_t* lea_dstOp = instr->m_Operands[0];
		jx_mir_operand_t* lea_srcOp = instr->m_Operands[1];
		JX_CHECK(lea_dstOp->m_Kind == JMIR_OPERAND_REGISTER, "Expected register operand");
		if (lea_srcOp->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_opIsStackObj(lea_srcOp)) {
			jx_mir_instruction_t* prevInstr = instr->m_Prev;
			if (prevInstr && prevInstr->m_OpCode == JMIR_OP_MOV) {
				jx_mir_operand_t* mov_dstOp = prevInstr->m_Operands[0];
				jx_mir_operand_t* mov_srcOp = prevInstr->m_Operands[1];
				if (mov_dstOp->m_Kind == JMIR_OPERAND_REGISTER && mov_srcOp->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regEqual(lea_srcOp->u.m_MemRef->m_BaseReg, mov_dstOp->u.m_Reg)) {
					lea_srcOp->u.m_MemRef->m_BaseReg = mov_srcOp->u.m_Reg;

					res = true;
				}
			}
		}
	}
#endif

	return res;
}

static bool jmir_peephole_mov_x(jmir_func_pass_peephole_t* pass, jx_mir_instruction_t* instr)
{
	jx_mir_context_t* ctx = pass->m_Ctx;
	jx_mir_function_t* func = pass->m_Func;
	jx_mir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;

	{
		jx_mir_operand_t* dst = instr->m_Operands[0];
		jx_mir_operand_t* src = instr->m_Operands[1];
		JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "Expected register operand");
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			// mov vr1, any
			// movsx/movzx vr2, vr1
			//  =>
			// movsx/movzx vr2, any
			jx_mir_instruction_t* prevInstr = instr->m_Prev;
			if (prevInstr && prevInstr->m_OpCode == JMIR_OP_MOV) {
				jx_mir_operand_t* movDst = prevInstr->m_Operands[0];
				if (movDst->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regEqual(src->u.m_Reg, movDst->u.m_Reg) && instr->m_Operands[1]->m_Type == prevInstr->m_Operands[1]->m_Type) {
					instr->m_Operands[1] = prevInstr->m_Operands[1];
					res = true;
				}
			}
		}
	}

	return res;
}

static bool jmir_peephole_isFloatConst(jx_mir_operand_t* op, double val)
{
	return true
		&& jx_mir_typeIsFloatingPoint(op->m_Type)
		&& op->m_Kind == JMIR_OPERAND_CONST
		&& op->u.m_ConstF64 == val
		;
}

//////////////////////////////////////////////////////////////////////////
// Combine LEAs
//
typedef struct jmir_reg_value_item_t
{
	jx_mir_reg_t m_Reg;
	jx_mir_operand_t* m_Value;
} jmir_reg_value_item_t;

typedef struct jmir_func_pass_combine_leas_t
{
	jx_allocator_i* m_Allocator;
	jx_mir_context_t* m_Ctx;
	jx_mir_function_t* m_Func;
	jx_hashmap_t* m_ValueMap;
} jmir_func_pass_combine_leas_t;

static void jmir_funcPass_combineLEAsDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_combineLEAsRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static jx_mir_operand_t* jmir_combineLEAs_replaceMemRef(jmir_func_pass_combine_leas_t* pass, jx_mir_type_kind type, jx_mir_memory_ref_t* memRef);

static uint64_t jir_regValueItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_regValueItemCompare(const void* a, const void* b, void* udata);

bool jx_mir_funcPassCreate_combineLEAs(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jmir_func_pass_combine_leas_t* inst = (jmir_func_pass_combine_leas_t*)JX_ALLOC(allocator, sizeof(jmir_func_pass_combine_leas_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jmir_func_pass_combine_leas_t));
	inst->m_Allocator = allocator;

	inst->m_ValueMap = jx_hashmapCreate(allocator, sizeof(jmir_reg_value_item_t), 64, 0, 0, jir_regValueItemHash, jir_regValueItemCompare, NULL, NULL);
	if (!inst->m_ValueMap) {
		jmir_funcPass_combineLEAsDestroy((jx_mir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_mir_function_pass_o*)inst;
	pass->run = jmir_funcPass_combineLEAsRun;
	pass->destroy = jmir_funcPass_combineLEAsDestroy;

	return true;
}

static void jmir_funcPass_combineLEAsDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jmir_func_pass_combine_leas_t* pass = (jmir_func_pass_combine_leas_t*)inst;
	
	if (pass->m_ValueMap) {
		jx_hashmapDestroy(pass->m_ValueMap);
		pass->m_ValueMap = NULL;
	}

	JX_FREE(pass->m_Allocator, pass);
}

static bool jmir_funcPass_combineLEAsRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jmir_func_pass_combine_leas_t* pass = (jmir_func_pass_combine_leas_t*)inst;

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(pass->m_Allocator);
		jx_mir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s\n", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_hashmapClear(pass->m_ValueMap, false);

		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			if (instr->m_OpCode == JMIR_OP_LEA) {
				jx_mir_operand_t* ptrOp = instr->m_Operands[0];
				jx_mir_operand_t* addrOp = instr->m_Operands[1];

				JX_CHECK(ptrOp->m_Kind == JMIR_OPERAND_REGISTER, "Expected lea destination operand to be a register.");
				if (jx_mir_opIsStackObj(addrOp)) {
					jx_hashmapSet(pass->m_ValueMap, &(jmir_reg_value_item_t){ .m_Reg = ptrOp->u.m_Reg, .m_Value = addrOp });
				} else if (addrOp->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_regIsValid(addrOp->u.m_MemRef->m_IndexReg)) {
					jx_mir_operand_t* newOperand = jmir_combineLEAs_replaceMemRef(pass, addrOp->m_Type, addrOp->u.m_MemRef);
					if (newOperand) {
						instr->m_Operands[1] = newOperand;
						jx_hashmapSet(pass->m_ValueMap, &(jmir_reg_value_item_t){ .m_Reg = ptrOp->u.m_Reg, .m_Value = newOperand });
					} else {
						jx_hashmapDelete(pass->m_ValueMap, &(jmir_reg_value_item_t){ .m_Reg = ptrOp->u.m_Reg });
					}
				} else if (addrOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
//					JX_NOT_IMPLEMENTED();
					jx_hashmapDelete(pass->m_ValueMap, &(jmir_reg_value_item_t){ .m_Reg = ptrOp->u.m_Reg });
				} else {
					jx_hashmapDelete(pass->m_ValueMap, &(jmir_reg_value_item_t){ .m_Reg = ptrOp->u.m_Reg });
				}
			} else if (instr->m_OpCode == JMIR_OP_MOV
				|| instr->m_OpCode == JMIR_OP_MOVSX 
				|| instr->m_OpCode == JMIR_OP_MOVZX 
				|| instr->m_OpCode == JMIR_OP_MOVD 
				|| instr->m_OpCode == JMIR_OP_MOVQ 
				|| instr->m_OpCode == JMIR_OP_MOVSS 
				|| instr->m_OpCode == JMIR_OP_MOVSD) {
				jx_mir_operand_t* dstOp = instr->m_Operands[0];
				jx_mir_operand_t* srcOp = instr->m_Operands[1];

				if (dstOp->m_Kind == JMIR_OPERAND_REGISTER && srcOp->m_Kind == JMIR_OPERAND_REGISTER) {
					// mov reg, reg;
					// If the source reg is in the hashmap, add destination reg to the hashmap 
					// using the same value as the source register.
					jmir_reg_value_item_t* item = (jmir_reg_value_item_t*)jx_hashmapGet(pass->m_ValueMap, &(jmir_reg_value_item_t){.m_Reg = srcOp->u.m_Reg });
					if (item) {
						jx_hashmapSet(pass->m_ValueMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg, .m_Value = item->m_Value });
					}
				} else if (dstOp->m_Kind == JMIR_OPERAND_REGISTER && srcOp->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_regIsValid(srcOp->u.m_MemRef->m_IndexReg)) {
					// mov reg, [reg + disp];
					// If the base reg is in the hashmap replace memory ref with the value from the hashmap.
					jx_mir_operand_t* newOperand = jmir_combineLEAs_replaceMemRef(pass, dstOp->m_Type, srcOp->u.m_MemRef);
					if (newOperand) {
						instr->m_Operands[1] = newOperand;
					}
				} else if (dstOp->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_regIsValid(dstOp->u.m_MemRef->m_IndexReg)) {
					// mov [baseReg + offset], value
					jx_mir_operand_t* newOperand = jmir_combineLEAs_replaceMemRef(pass, dstOp->m_Type, dstOp->u.m_MemRef);
					if (newOperand) {
						instr->m_Operands[0] = newOperand;
					}
				}
			}

			instr = instr->m_Next;
		}

		bb = bb->m_Next;
	}

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(pass->m_Allocator);
		jx_mir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s\n", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif

	return false;
}

static jx_mir_operand_t* jmir_combineLEAs_replaceMemRef(jmir_func_pass_combine_leas_t* pass, jx_mir_type_kind type, jx_mir_memory_ref_t* memRef)
{
	jmir_reg_value_item_t* item = (jmir_reg_value_item_t*)jx_hashmapGet(pass->m_ValueMap, &(jmir_reg_value_item_t){ .m_Reg = memRef->m_BaseReg });
	if (!item) {
		return NULL;
	}

	jx_mir_operand_t* newOp = NULL;
	jx_mir_operand_t* addr = item->m_Value;
	if (addr->m_Kind == JMIR_OPERAND_MEMORY_REF) {
		if (jx_mir_opIsStackObj(addr)) {
			newOp = jx_mir_opStackObjRel(pass->m_Ctx, pass->m_Func, type, addr->u.m_MemRef, memRef->m_Displacement);
		} else {
			newOp = jx_mir_opMemoryRef(pass->m_Ctx, pass->m_Func, type, addr->u.m_MemRef->m_BaseReg, addr->u.m_MemRef->m_IndexReg, addr->u.m_MemRef->m_Scale, addr->u.m_MemRef->m_Displacement + memRef->m_Displacement);
		}
	} else if (addr->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
		JX_NOT_IMPLEMENTED();
	} else {
		JX_CHECK(false, "Unknown address operand!");
	}

	return newOp;
}

static uint64_t jir_regValueItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jmir_reg_value_item_t* var = (const jmir_reg_value_item_t*)item;
	uint64_t hash = jx_hashFNV1a(&var->m_Reg, sizeof(jx_mir_reg_t), seed0, seed1);
	return hash;
}

static int32_t jir_regValueItemCompare(const void* a, const void* b, void* udata)
{
	const jmir_reg_value_item_t* varA = (const jmir_reg_value_item_t*)a;
	const jmir_reg_value_item_t* varB = (const jmir_reg_value_item_t*)b;
	const jx_mir_reg_t regA = varA->m_Reg;
	const jx_mir_reg_t regB = varB->m_Reg;
	int32_t res = regA.m_Class < regB.m_Class
		? -1
		: (regA.m_Class > regB.m_Class ? 1 : 0)
		;
	if (res == 0) {
		res = regA.m_IsVirtual < regB.m_IsVirtual
			? -1
			: (regA.m_IsVirtual > regB.m_IsVirtual ? 1 : 0)
			;
		if (res == 0) {
			res = regA.m_ID < regB.m_ID
				? -1
				: (regA.m_ID > regB.m_ID ? 1 : 0)
				;
		}
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
// Instruction Combiner
//
typedef struct jmir_reg_instr_item_t
{
	jx_mir_reg_t m_Reg;
	jx_mir_instruction_t* m_Instr;
} jmir_reg_instr_item_t;

typedef struct jmir_func_pass_instr_combine_t
{
	jx_allocator_i* m_Allocator;
	jx_mir_context_t* m_Ctx;
	jx_mir_function_t* m_Func;
	jx_hashmap_t* m_ValueMap;
} jmir_func_pass_instr_combine_t;

static void jmir_funcPass_instrCombineDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_instrCombineRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static void jmir_instrCombine_setRegDef(jmir_func_pass_instr_combine_t* pass, jx_mir_reg_t reg, jx_mir_instruction_t* value);
static jx_mir_instruction_t* jmir_instrCombine_getRegDef(jmir_func_pass_instr_combine_t* pass, jx_mir_reg_t reg);
static void jmir_instrCombine_removeRegDef(jmir_func_pass_instr_combine_t* pass, jx_mir_reg_t reg);
static bool jmir_instrCombine_isMovVRegAny(jx_mir_instruction_t* instr);
static bool jmir_instrCombine_isMovVRegVReg(jx_mir_instruction_t* instr);
static bool jmir_instrCombine_isMovVRegMem(jx_mir_instruction_t* instr);
static bool jmir_instrCombine_isLeaVReg(jx_mir_instruction_t* instr);

static uint64_t jir_regInstrItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_regInstrItemCompare(const void* a, const void* b, void* udata);

bool jx_mir_funcPassCreate_instrCombine(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jmir_func_pass_instr_combine_t* inst = (jmir_func_pass_instr_combine_t*)JX_ALLOC(allocator, sizeof(jmir_func_pass_instr_combine_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jmir_func_pass_instr_combine_t));
	inst->m_Allocator = allocator;

	inst->m_ValueMap = jx_hashmapCreate(allocator, sizeof(jmir_reg_instr_item_t), 64, 0, 0, jir_regInstrItemHash, jir_regInstrItemCompare, NULL, NULL);
	if (!inst->m_ValueMap) {
		jmir_funcPass_instrCombineDestroy((jx_mir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_mir_function_pass_o*)inst;
	pass->run = jmir_funcPass_instrCombineRun;
	pass->destroy = jmir_funcPass_instrCombineDestroy;

	return true;
}

static void jmir_funcPass_instrCombineDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jmir_func_pass_instr_combine_t* pass = (jmir_func_pass_instr_combine_t*)inst;

	if (pass->m_ValueMap) {
		jx_hashmapDestroy(pass->m_ValueMap);
		pass->m_ValueMap = NULL;
	}

	JX_FREE(pass->m_Allocator, pass);
}

static bool jmir_funcPass_instrCombineRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jmir_func_pass_instr_combine_t* pass = (jmir_func_pass_instr_combine_t*)inst;

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(pass->m_Allocator);
		jx_mir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s\n", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_hashmapClear(pass->m_ValueMap, false);

		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_mir_instruction_t* instrNext = instr->m_Next;

			switch (instr->m_OpCode) {
			case JMIR_OP_CMP: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				jx_mir_operand_t* rhs = instr->m_Operands[1];

				if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// cmp reg, const
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// cmp reg, reg
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// cmp reg, [mem]
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// cmp reg, [sym]
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// cmp [mem], const
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// cmp [mem], reg
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// cmp [sym], const
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// cmp [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = lhs;
				instr->m_Operands[1] = rhs;
			} break;
			case JMIR_OP_TEST: {
				// No-op
			} break;
			case JMIR_OP_MOV: 
			case JMIR_OP_MOVSS:
			case JMIR_OP_MOVSD:
			case JMIR_OP_MOVAPS:
			case JMIR_OP_MOVAPD: {
				jx_mir_operand_t* dst = instr->m_Operands[0];
				jx_mir_operand_t* src = instr->m_Operands[1];

				if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_CONST) {
					// mov reg, const
					jmir_instrCombine_setRegDef(pass, dst->u.m_Reg, instr);
				} else if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_REGISTER) {
					// mov reg, reg
					jx_mir_instruction_t* srcRegDef = jmir_instrCombine_getRegDef(pass, src->u.m_Reg);
					if (jmir_instrCombine_isMovVRegAny(srcRegDef) && srcRegDef->m_Operands[1]->m_Type == src->m_Type) {
						// The src reg definition instruction is a mov reg, any. 
						// 
						// TODO: If 'any' is a memory reference replace operand only if the reg
						// is not live out of this instruction. Otherwise it might be better to leave
						// the load to reg. Requires liveness analysis.
						src = srcRegDef->m_Operands[1];
					}

					if (src->m_Kind != JMIR_OPERAND_REGISTER || jx_mir_regIsVirtual(src->u.m_Reg)) {
						jmir_instrCombine_setRegDef(pass, dst->u.m_Reg, instr);
					} else {
						jmir_instrCombine_removeRegDef(pass, dst->u.m_Reg);
					}
				} else if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// mov reg, [mem]
					jx_mir_memory_ref_t memRef = *src->u.m_MemRef;
					if (jx_mir_regIsValid(memRef.m_BaseReg) && jx_mir_regIsVirtual(memRef.m_BaseReg)) {
						jx_mir_instruction_t* baseRegDef = jmir_instrCombine_getRegDef(pass, memRef.m_BaseReg);
						if (jmir_instrCombine_isMovVRegVReg(baseRegDef)) {
							memRef.m_BaseReg = baseRegDef->m_Operands[1]->u.m_Reg;
						} else if (!jx_mir_regIsValid(memRef.m_IndexReg) && jmir_instrCombine_isLeaVReg(baseRegDef)) {
							if (baseRegDef->m_Operands[1]->m_Kind == JMIR_OPERAND_MEMORY_REF) {
								memRef.m_BaseReg = baseRegDef->m_Operands[1]->u.m_MemRef->m_BaseReg;
								memRef.m_IndexReg = baseRegDef->m_Operands[1]->u.m_MemRef->m_IndexReg;
								memRef.m_Scale = baseRegDef->m_Operands[1]->u.m_MemRef->m_Scale;
								memRef.m_Displacement += baseRegDef->m_Operands[1]->u.m_MemRef->m_Displacement;
							} else if (baseRegDef->m_Operands[1]->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
								// TODO: 
							}
						}
					}

					if (jx_mir_regIsValid(memRef.m_IndexReg) && jx_mir_regIsVirtual(memRef.m_IndexReg)) {
						jx_mir_instruction_t* indexRegDef = jmir_instrCombine_getRegDef(pass, memRef.m_IndexReg);
						if (jmir_instrCombine_isMovVRegVReg(indexRegDef)) {
							memRef.m_IndexReg = indexRegDef->m_Operands[1]->u.m_Reg;
						}
					}

					if (!jx_mir_memRefEqual(&memRef, src->u.m_MemRef)) {
						src = jx_mir_opMemoryRef(ctx, func, src->m_Type, memRef.m_BaseReg, memRef.m_IndexReg, memRef.m_Scale, memRef.m_Displacement);
					}

					jmir_instrCombine_removeRegDef(pass, dst->u.m_Reg);
				} else if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// mov reg, [sym]
					jmir_instrCombine_removeRegDef(pass, dst->u.m_Reg);
				} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF && (src->m_Kind == JMIR_OPERAND_REGISTER || src->m_Kind == JMIR_OPERAND_CONST)) {
					// mov [mem], reg
					// mov [mem], const
					jx_mir_memory_ref_t memRef = *dst->u.m_MemRef;
					if (jx_mir_regIsValid(memRef.m_BaseReg) && jx_mir_regIsVirtual(memRef.m_BaseReg)) {
						jx_mir_instruction_t* baseRegDef = jmir_instrCombine_getRegDef(pass, memRef.m_BaseReg);
						if (jmir_instrCombine_isMovVRegVReg(baseRegDef)) {
							memRef.m_BaseReg = baseRegDef->m_Operands[1]->u.m_Reg;
						} else if (jmir_instrCombine_isLeaVReg(baseRegDef)) {
							jx_mir_operand_t* leaSrc = baseRegDef->m_Operands[1];
							if (leaSrc->m_Kind == JMIR_OPERAND_MEMORY_REF) {
								const jx_mir_memory_ref_t* leaSrcMemRef = leaSrc->u.m_MemRef;

								// If the original memory reference's index reg was invalid (e.g. [baseReg + offset])
								// then use lea's index reg (might still be invalid).
								// Otherwise, replace memory reference only if lea's index reg is invalid.
								if (!jx_mir_regIsValid(memRef.m_IndexReg)) {
									memRef.m_BaseReg = leaSrcMemRef->m_BaseReg;
									memRef.m_IndexReg = leaSrcMemRef->m_IndexReg;
									memRef.m_Scale = leaSrcMemRef->m_Scale;
									memRef.m_Displacement += leaSrcMemRef->m_Displacement;
								} else if (!jx_mir_regIsValid(leaSrcMemRef->m_IndexReg)) {
									memRef.m_BaseReg = leaSrcMemRef->m_BaseReg;
									memRef.m_Displacement += leaSrcMemRef->m_Displacement;
								}
							} else if (leaSrc->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
								// TODO: 
							}
						}
					} else {
#if 0
						if (jx_mir_regIsValid(memRef.m_IndexReg) && jx_mir_regIsVirtual(memRef.m_IndexReg)) {
							jx_mir_instruction_t* indexRegDef = jmir_instrCombine_getRegDef(pass, memRef.m_IndexReg);
							if (jmir_instrCombine_isMovVRegVReg(indexRegDef)) {
								memRef.m_IndexReg = indexRegDef->m_Operands[1]->u.m_Reg;
							}
						}
#endif
					}

					if (!jx_mir_memRefEqual(&memRef, dst->u.m_MemRef)) {
						dst = jx_mir_opMemoryRef(ctx, func, dst->m_Type, memRef.m_BaseReg, memRef.m_IndexReg, memRef.m_Scale, memRef.m_Displacement);
					}
				} else if (dst->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && src->m_Kind == JMIR_OPERAND_CONST) {
					// mov [sym], const
				} else if (dst->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && src->m_Kind == JMIR_OPERAND_REGISTER) {
					// mov [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = dst;
				instr->m_Operands[1] = src;
			} break;
			case JMIR_OP_MOVSX: 
			case JMIR_OP_MOVZX: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				JX_CHECK(lhs->m_Kind == JMIR_OPERAND_REGISTER, "Invalid operand type");

				jx_mir_operand_t* rhs = instr->m_Operands[1];

				// TODO: 

				instr->m_Operands[1] = rhs;

				jmir_instrCombine_setRegDef(pass, lhs->u.m_Reg, instr);
			} break;
			case JMIR_OP_IMUL3: {
				JX_NOT_IMPLEMENTED();
			} break;
			case JMIR_OP_LEA: {
				jx_mir_operand_t* dst = instr->m_Operands[0];
				JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "Invalid operand type");

				jx_mir_operand_t* src = instr->m_Operands[1];
				if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					jx_mir_memory_ref_t memRef = *src->u.m_MemRef;
					if (jx_mir_regIsValid(memRef.m_BaseReg) && jx_mir_regIsVirtual(memRef.m_BaseReg)) {
						jx_mir_instruction_t* baseRegDef = jmir_instrCombine_getRegDef(pass, memRef.m_BaseReg);
						if (jmir_instrCombine_isMovVRegVReg(baseRegDef)) {
							memRef.m_BaseReg = baseRegDef->m_Operands[1]->u.m_Reg;
						} else if (jmir_instrCombine_isLeaVReg(baseRegDef)) {
							jx_mir_operand_t* leaSrc = baseRegDef->m_Operands[1];
							if (leaSrc->m_Kind == JMIR_OPERAND_MEMORY_REF) {
								const jx_mir_memory_ref_t* leaSrcMemRef = leaSrc->u.m_MemRef;

								// If the original memory reference's index reg was invalid (e.g. [baseReg + offset])
								// then use lea's index reg (might still be invalid).
								// Otherwise, replace memory reference only if lea's index reg is invalid.
								if (!jx_mir_regIsValid(memRef.m_IndexReg)) {
									memRef.m_BaseReg = leaSrcMemRef->m_BaseReg;
									memRef.m_IndexReg = leaSrcMemRef->m_IndexReg;
									memRef.m_Scale = leaSrcMemRef->m_Scale;
									memRef.m_Displacement += leaSrcMemRef->m_Displacement;
								} else if (!jx_mir_regIsValid(leaSrcMemRef->m_IndexReg)) {
									memRef.m_BaseReg = leaSrcMemRef->m_BaseReg;
									memRef.m_Displacement += leaSrcMemRef->m_Displacement;
								}
							} else if (leaSrc->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
								// TODO: 
							}
						}
					} else {
#if 0
						if (jx_mir_regIsValid(memRef.m_IndexReg) && jx_mir_regIsVirtual(memRef.m_IndexReg)) {
							jx_mir_instruction_t* indexRegDef = jmir_instrCombine_getRegDef(pass, memRef.m_IndexReg);
							if (jmir_instrCombine_isMovVRegVReg(indexRegDef)) {
								memRef.m_IndexReg = indexRegDef->m_Operands[1]->u.m_Reg;
							}
						}
#endif
					}

					if (!jx_mir_memRefEqual(&memRef, src->u.m_MemRef)) {
						src = jx_mir_opMemoryRef(ctx, func, src->m_Type, memRef.m_BaseReg, memRef.m_IndexReg, memRef.m_Scale, memRef.m_Displacement);
					}
				} else {
					JX_CHECK(src->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL, "Invalid lea source operand.");
				}

				instr->m_Operands[1] = src;
				jmir_instrCombine_setRegDef(pass, dst->u.m_Reg, instr);
			} break;
			case JMIR_OP_IDIV:
			case JMIR_OP_DIV: {
				jx_mir_operand_t* op = instr->m_Operands[0];

				// TODO: 

				instr->m_Operands[0] = op;
			} break;
			case JMIR_OP_IMUL:
			case JMIR_OP_ADD:
			case JMIR_OP_SUB:
			case JMIR_OP_XOR:
			case JMIR_OP_AND:
			case JMIR_OP_OR: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				jx_mir_operand_t* rhs = instr->m_Operands[1];

				if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op reg, const
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op reg, reg
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// op reg, [mem]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// op reg, [sym]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [mem], const
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [mem], reg
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [sym], const
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = lhs;
				instr->m_Operands[1] = rhs;
			} break;
			case JMIR_OP_SAR:
			case JMIR_OP_SHR:
			case JMIR_OP_SHL: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				if (lhs->m_Kind == JMIR_OPERAND_REGISTER) {
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				}
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
				// Nop
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
			case JMIR_OP_JNLE: {
				// Nop
			} break;
			case JMIR_OP_MOVD:
			case JMIR_OP_MOVQ: {
				jx_mir_operand_t* dst = instr->m_Operands[0];
				jx_mir_operand_t* src = instr->m_Operands[1];

				if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_REGISTER) {
					// op reg, reg
					jmir_instrCombine_removeRegDef(pass, dst->u.m_Reg);
				} else if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// op reg, [mem]
					jmir_instrCombine_removeRegDef(pass, dst->u.m_Reg);
				} else if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// op reg, [sym]
					jmir_instrCombine_removeRegDef(pass, dst->u.m_Reg);
				} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF && src->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [mem], reg
				} else if (dst->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && src->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = dst;
				instr->m_Operands[1] = src;
			} break;
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
			case JMIR_OP_RCPPS:
			case JMIR_OP_RCPSS:
			case JMIR_OP_RSQRTPS:
			case JMIR_OP_RSQRTSS:
			case JMIR_OP_SQRTPS:
			case JMIR_OP_SQRTSS:
			case JMIR_OP_SQRTPD:
			case JMIR_OP_SQRTSD:
			case JMIR_OP_SUBPS:
			case JMIR_OP_SUBSS:
			case JMIR_OP_SUBPD:
			case JMIR_OP_SUBSD: 
			case JMIR_OP_XORPS:
			case JMIR_OP_XORPD: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				jx_mir_operand_t* rhs = instr->m_Operands[1];

				if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op reg, const
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op reg, reg
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// op reg, [mem]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// op reg, [sym]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [mem], const
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [mem], reg
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [sym], const
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = lhs;
				instr->m_Operands[1] = rhs;
			} break;
			case JMIR_OP_COMISS:
			case JMIR_OP_COMISD: 
			case JMIR_OP_UCOMISS:
			case JMIR_OP_UCOMISD: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				jx_mir_operand_t* rhs = instr->m_Operands[1];

				if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op reg, const
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op reg, reg
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// op reg, [mem]
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// op reg, [sym]
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [mem], const
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [mem], reg
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [sym], const
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = lhs;
				instr->m_Operands[1] = rhs;
			} break;
			case JMIR_OP_CVTSI2SS:
			case JMIR_OP_CVTSI2SD:
			case JMIR_OP_CVTSS2SI:
			case JMIR_OP_CVTSD2SI:
			case JMIR_OP_CVTTSS2SI:
			case JMIR_OP_CVTTSD2SI:
			case JMIR_OP_CVTSD2SS:
			case JMIR_OP_CVTSS2SD: {
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				jx_mir_operand_t* rhs = instr->m_Operands[1];

				if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op reg, const
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op reg, reg
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// op reg, [mem]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// op reg, [sym]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [mem], const
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [mem], reg
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [sym], const
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = lhs;
				instr->m_Operands[1] = rhs;
			} break;
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
				jx_mir_operand_t* lhs = instr->m_Operands[0];
				jx_mir_operand_t* rhs = instr->m_Operands[1];

				if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op reg, const
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op reg, reg
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					// op reg, [mem]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_REGISTER && rhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
					// op reg, [sym]
					jmir_instrCombine_removeRegDef(pass, lhs->u.m_Reg);
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [mem], const
				} else if (lhs->m_Kind == JMIR_OPERAND_MEMORY_REF && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [mem], reg
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_CONST) {
					// op [sym], const
				} else if (lhs->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL && rhs->m_Kind == JMIR_OPERAND_REGISTER) {
					// op [sym], reg
				} else {
					JX_CHECK(false, "Invalid operands?");
				}

				instr->m_Operands[0] = lhs;
				instr->m_Operands[1] = rhs;
			} break;
			case JMIR_OP_PUSH:
			case JMIR_OP_POP: {
				JX_NOT_IMPLEMENTED();
			} break;
			case JMIR_OP_RET:
			case JMIR_OP_JMP:
			case JMIR_OP_CALL:
			case JMIR_OP_CDQ:
			case JMIR_OP_CQO: {
				// No-ops
			} break;
			default: {
				JX_CHECK(false, "Unknown mir opcode");
			} break;
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(pass->m_Allocator);
		jx_mir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s\n", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif

	return false;
}

static void jmir_instrCombine_setRegDef(jmir_func_pass_instr_combine_t* pass, jx_mir_reg_t reg, jx_mir_instruction_t* def)
{
	if (!jx_mir_regIsVirtual(reg)) {
		return;
	}

	jx_hashmapSet(pass->m_ValueMap, &(jmir_reg_instr_item_t){.m_Reg = reg, .m_Instr = def});
}

static jx_mir_instruction_t* jmir_instrCombine_getRegDef(jmir_func_pass_instr_combine_t* pass, jx_mir_reg_t reg)
{
	if (!jx_mir_regIsVirtual(reg)) {
		return NULL;
	}

	jmir_reg_instr_item_t* item = jx_hashmapGet(pass->m_ValueMap, &(jmir_reg_instr_item_t){.m_Reg = reg});
	return item
		? item->m_Instr
		: NULL
		;
}

static void jmir_instrCombine_removeRegDef(jmir_func_pass_instr_combine_t* pass, jx_mir_reg_t reg)
{
	if (!jx_mir_regIsVirtual(reg)) {
		return;
	}

	jx_hashmapDelete(pass->m_ValueMap, &(jmir_reg_instr_item_t){.m_Reg = reg});
}

static bool jmir_instrCombine_isMovVRegAny(jx_mir_instruction_t* instr)
{
	return instr
		&& (instr->m_OpCode == JMIR_OP_MOV || instr->m_OpCode == JMIR_OP_MOVSS || instr->m_OpCode == JMIR_OP_MOVSD)
		&& instr->m_Operands[0]->m_Kind == JMIR_OPERAND_REGISTER
		&& jx_mir_regIsVirtual(instr->m_Operands[0]->u.m_Reg)
		;
}

static bool jmir_instrCombine_isMovVRegVReg(jx_mir_instruction_t* instr)
{
	return jmir_instrCombine_isMovVRegAny(instr)
		&& instr->m_Operands[1]->m_Kind == JMIR_OPERAND_REGISTER
		&& jx_mir_regIsVirtual(instr->m_Operands[1]->u.m_Reg)
		;
}

static bool jmir_instrCombine_isMovVRegMem(jx_mir_instruction_t* instr)
{
	return jmir_instrCombine_isMovVRegAny(instr)
		&& instr->m_Operands[1]->m_Kind == JMIR_OPERAND_MEMORY_REF
		;
}

static bool jmir_instrCombine_isLeaVReg(jx_mir_instruction_t* instr)
{
	return instr
		&& instr->m_OpCode == JMIR_OP_LEA
		&& jx_mir_regIsVirtual(instr->m_Operands[0]->u.m_Reg)
		;
}

static uint64_t jir_regInstrItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jmir_reg_instr_item_t* var = (const jmir_reg_instr_item_t*)item;
	uint64_t hash = jx_hashFNV1a(&var->m_Reg, sizeof(jx_mir_reg_t), seed0, seed1);
	return hash;
}

static int32_t jir_regInstrItemCompare(const void* a, const void* b, void* udata)
{
	const jmir_reg_instr_item_t* varA = (const jmir_reg_instr_item_t*)a;
	const jmir_reg_instr_item_t* varB = (const jmir_reg_instr_item_t*)b;
	const jx_mir_reg_t regA = varA->m_Reg;
	const jx_mir_reg_t regB = varB->m_Reg;
	int32_t res = regA.m_Class < regB.m_Class
		? -1
		: (regA.m_Class > regB.m_Class ? 1 : 0)
		;
	if (res == 0) {
		res = regA.m_IsVirtual < regB.m_IsVirtual
			? -1
			: (regA.m_IsVirtual > regB.m_IsVirtual ? 1 : 0)
			;
		if (res == 0) {
			res = regA.m_ID < regB.m_ID
				? -1
				: (regA.m_ID > regB.m_ID ? 1 : 0)
				;
		}
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
// Dead Code Elimination
//
static void jmir_funcPass_dceDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_dceRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

bool jx_mir_funcPassCreate_deadCodeElimination(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	pass->m_Inst = NULL;
	pass->run = jmir_funcPass_dceRun;
	pass->destroy = jmir_funcPass_dceDestroy;

	return true;
}

static void jmir_funcPass_dceDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
}

static bool jmir_funcPass_dceRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	bool changed = true;
	while (changed) {
		changed = false;

		jx_mir_funcUpdateLiveness(ctx, func);

		// Remove dead instructions.
		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_mir_instruction_t* instrNext = instr->m_Next;

				jx_mir_instr_usedef_t* instrUseDefAnnot = &instr->m_UseDef;
				const uint32_t numDefs = instrUseDefAnnot->m_NumDefs;
				if (numDefs) {
					const jx_bitset_t* instrLiveOutSet = instr->m_LiveOutSet;

					uint32_t numDeadDefs = 0;
					for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
						jx_mir_reg_t def = instrUseDefAnnot->m_Defs[iDef];
						if (jx_mir_regIsVirtual(def) && !jx_bitsetIsBitSet(instrLiveOutSet, jx_mir_funcMapRegToBitsetID(ctx, func, def))) {
							++numDeadDefs;
						}
					}

					// TODO: Do I need to check if instruction has side-effects? 
					// Memory stores or comparisons do not have any defs so they are ignored 
					// by this code. What other instruction can have a definition and side-effects?
					if (numDefs == numDeadDefs) {
						jx_mir_bbRemoveInstr(ctx, bb, instr);
						jx_mir_instrFree(ctx, instr);
						changed = true;
					}
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Redundant Const Elimination 
//
typedef struct jmir_func_pass_rce_t
{
	jx_allocator_i* m_Allocator;
	jx_hashmap_t* m_RegConstMap;
} jmir_func_pass_rce_t;

static void jmir_funcPass_redundantConstEliminationDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_redundantConstEliminationRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

bool jx_mir_funcPassCreate_redundantConstElimination(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jmir_func_pass_rce_t* inst = (jmir_func_pass_rce_t*)JX_ALLOC(allocator, sizeof(jmir_func_pass_rce_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jmir_func_pass_rce_t));
	inst->m_Allocator = allocator;

	inst->m_RegConstMap = jx_hashmapCreate(allocator, sizeof(jmir_reg_value_item_t), 64, 0, 0, jir_regValueItemHash, jir_regValueItemCompare, NULL, NULL);
	if (!inst->m_RegConstMap) {
		jmir_funcPass_redundantConstEliminationDestroy((jx_mir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_mir_function_pass_o*)inst;
	pass->run = jmir_funcPass_redundantConstEliminationRun;
	pass->destroy = jmir_funcPass_redundantConstEliminationDestroy;

	return true;
}

static void jmir_funcPass_redundantConstEliminationDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jmir_func_pass_rce_t* pass = (jmir_func_pass_rce_t*)inst;

	if (pass->m_RegConstMap) {
		jx_hashmapDestroy(pass->m_RegConstMap);
		pass->m_RegConstMap = NULL;
	}

	JX_FREE(pass->m_Allocator, pass);
}

static bool jmir_funcPass_redundantConstEliminationRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jmir_func_pass_rce_t* pass = (jmir_func_pass_rce_t*)inst;

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_hashmapClear(pass->m_RegConstMap, false);

		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_mir_instruction_t* instrNext = instr->m_Next;

			switch (instr->m_OpCode) {
			case JMIR_OP_RET:
			case JMIR_OP_CMP:
			case JMIR_OP_TEST:
			case JMIR_OP_JMP:
			case JMIR_OP_PUSH:
			case JMIR_OP_POP: 
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
			case JMIR_OP_COMISS:
			case JMIR_OP_COMISD:
			case JMIR_OP_UCOMISS:
			case JMIR_OP_UCOMISD: {
				// Nop; no register is affected
			} break;

			case JMIR_OP_MOV: 
			case JMIR_OP_MOVSS:
			case JMIR_OP_MOVSD: {
				// mov reg, value
				// If value is const and the same const is already stored in the specified reg, the 
				// instruction is redundant and can be removed. If the value is a const but different 
				// than what's is already stored, update the map with the new const. Otherwise, remove
				// the entry for this reg from the map.
				jx_mir_operand_t* dstOp = instr->m_Operands[0];
				jx_mir_operand_t* srcOp = instr->m_Operands[1];
				if (dstOp->m_Kind == JMIR_OPERAND_REGISTER) {
					if (srcOp->m_Kind == JMIR_OPERAND_CONST) {
						// Check if the same constant has already been assigned to this register.
						// If it is, the instruction is redundant.
						jmir_reg_value_item_t* item = (jmir_reg_value_item_t*)jx_hashmapGet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg});
						if (item && item->m_Value->m_Type == srcOp->m_Type && item->m_Value->u.m_ConstI64 == srcOp->u.m_ConstI64) {
							// Redundant instruction; remove.
							jx_mir_bbRemoveInstr(ctx, bb, instr);
							jx_mir_instrFree(ctx, instr);
						} else {
							jx_hashmapSet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg, .m_Value = srcOp});
						}
					} else if (srcOp->m_Kind == JMIR_OPERAND_REGISTER) {
						// Check if source reg is a constant. If it is, add the destination reg as constant to the map.
						// Otherwise remove the destination reg from the map.
						jmir_reg_value_item_t* item = (jmir_reg_value_item_t*)jx_hashmapGet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = srcOp->u.m_Reg});
						if (item) {
							jx_hashmapSet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg, .m_Value = item->m_Value});
						} else {
							jx_hashmapDelete(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg});
						}
					} else {
						jx_hashmapDelete(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg});
					}
				}
			} break;
			case JMIR_OP_MOVSX:
			case JMIR_OP_MOVZX:
			case JMIR_OP_IMUL: 
			case JMIR_OP_ADD:
			case JMIR_OP_SUB:
			case JMIR_OP_LEA:
			case JMIR_OP_AND:
			case JMIR_OP_OR:
			case JMIR_OP_SAR:
			case JMIR_OP_SHR:
			case JMIR_OP_SHL: 
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
			case JMIR_OP_SETNLE:
			case JMIR_OP_MOVAPS:
			case JMIR_OP_MOVAPD:
			case JMIR_OP_MOVD:
			case JMIR_OP_MOVQ:
			case JMIR_OP_ADDPS:
			case JMIR_OP_ADDSS:
			case JMIR_OP_ADDPD:
			case JMIR_OP_ADDSD:
			case JMIR_OP_ANDNPS:
			case JMIR_OP_ANDNPD:
			case JMIR_OP_ANDPS:
			case JMIR_OP_ANDPD:
			case JMIR_OP_CVTSI2SS:
			case JMIR_OP_CVTSI2SD:
			case JMIR_OP_CVTSS2SI:
			case JMIR_OP_CVTSD2SI:
			case JMIR_OP_CVTTSS2SI:
			case JMIR_OP_CVTTSD2SI:
			case JMIR_OP_CVTSD2SS:
			case JMIR_OP_CVTSS2SD:
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
			case JMIR_OP_RCPPS:
			case JMIR_OP_RCPSS:
			case JMIR_OP_RSQRTPS:
			case JMIR_OP_RSQRTSS:
			case JMIR_OP_SQRTPS:
			case JMIR_OP_SQRTSS:
			case JMIR_OP_SQRTPD:
			case JMIR_OP_SQRTSD:
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
				// Any binary operation which affects the first register operand should be removed from the map.
				jx_mir_operand_t* dstOp = instr->m_Operands[0];
				if (dstOp->m_Kind == JMIR_OPERAND_REGISTER) {
					jx_hashmapDelete(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg});
				}
			} break;
			case JMIR_OP_IDIV:
			case JMIR_OP_DIV: {
				// IDIV/DIV implicitly affect RAX and RDX. Remove them from the map.
				jx_hashmapDelete(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = kMIRRegGP_A});
				jx_hashmapDelete(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = kMIRRegGP_D});
			} break;
			case JMIR_OP_CALL: {
				// Keep callee-saved regs because they will/should be preserved by the called function.
				jx_mir_operand_t* calleeSavedIRegVal[JX_COUNTOF(kMIRFuncCalleeSavedIReg)] = { 0 };
				jx_mir_operand_t* calleeSavedFRegVal[JX_COUNTOF(kMIRFuncCalleeSavedFReg)] = { 0 };

				for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedIReg); ++iReg) {
					jmir_reg_value_item_t* item = (jmir_reg_value_item_t*)jx_hashmapGet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = kMIRFuncCalleeSavedIReg[iReg]});
					if (item) {
						calleeSavedIRegVal[iReg] = item->m_Value;
					}
				}
				for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedFReg); ++iReg) {
					jmir_reg_value_item_t* item = (jmir_reg_value_item_t*)jx_hashmapGet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = kMIRFuncCalleeSavedFReg[iReg]});
					if (item) {
						calleeSavedFRegVal[iReg] = item->m_Value;
					}
				}

				jx_hashmapClear(pass->m_RegConstMap, false);

				for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedIReg); ++iReg) {
					if (calleeSavedIRegVal[iReg]) {
						jx_hashmapSet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = kMIRFuncCalleeSavedIReg[iReg], .m_Value = calleeSavedIRegVal[iReg]});
					}
				}
				for (uint32_t iReg = 0; iReg < JX_COUNTOF(kMIRFuncCalleeSavedFReg); ++iReg) {
					if (calleeSavedFRegVal[iReg]) {
						jx_hashmapSet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = kMIRFuncCalleeSavedFReg[iReg], .m_Value = calleeSavedFRegVal[iReg]});
					}
				}
			} break;
			case JMIR_OP_CDQ:
			case JMIR_OP_CQO: {
				// CDQ/CQO implicitly affect RDX
				jx_hashmapDelete(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = kMIRRegGP_D});
			} break;
			case JMIR_OP_XOR:
			case JMIR_OP_XORPS:
			case JMIR_OP_XORPD: {
				// Check for xor reg, reg and treat like mov reg, 0
				jx_mir_operand_t* dstOp = instr->m_Operands[0];
				jx_mir_operand_t* srcOp = instr->m_Operands[1];
				if (dstOp->m_Kind == JMIR_OPERAND_REGISTER && srcOp->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regEqual(dstOp->u.m_Reg, srcOp->u.m_Reg)) {
					// xor reg, reg
					jmir_reg_value_item_t* item = (jmir_reg_value_item_t*)jx_hashmapGet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg});
					if (item && item->m_Value->m_Type == srcOp->m_Type && item->m_Value->u.m_ConstI64 == 0) {
						// Redundant instruction; remove.
						jx_mir_bbRemoveInstr(ctx, bb, instr);
						jx_mir_instrFree(ctx, instr);
					} else {
						jx_mir_operand_t* zeroOp = jx_mir_opIConst(ctx, func, dstOp->m_Type, 0);
						jx_hashmapSet(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg, .m_Value = zeroOp});
					}
				} else if (dstOp->m_Kind == JMIR_OPERAND_REGISTER) {
					jx_hashmapDelete(pass->m_RegConstMap, &(jmir_reg_value_item_t){.m_Reg = dstOp->u.m_Reg});
				}
			} break;
			default:
				JX_CHECK(false, "Unknown mir opcode!");
				break;
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Simplify CFG
//
typedef struct jmir_func_pass_simplify_cfg_t
{
	jx_allocator_i* m_Allocator;
} jmir_func_pass_simplify_cfg_t;

static void jmir_funcPass_simplifyCFGDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_simplifyCFGRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

bool jx_mir_funcPassCreate_simplifyCFG(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jmir_func_pass_simplify_cfg_t* inst = (jmir_func_pass_simplify_cfg_t*)JX_ALLOC(allocator, sizeof(jmir_func_pass_simplify_cfg_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jmir_func_pass_simplify_cfg_t));
	inst->m_Allocator = allocator;

	pass->m_Inst = (jx_mir_function_pass_o*)inst;
	pass->run = jmir_funcPass_simplifyCFGRun;
	pass->destroy = jmir_funcPass_simplifyCFGDestroy;

	return true;
}

static void jmir_funcPass_simplifyCFGDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jmir_func_pass_simplify_cfg_t* pass = (jmir_func_pass_simplify_cfg_t*)inst;

	JX_FREE(pass->m_Allocator, pass);
}

static bool jmir_funcPass_simplifyCFGRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jmir_func_pass_simplify_cfg_t* pass = (jmir_func_pass_simplify_cfg_t*)inst;

	uint32_t numBasicBlocksChanged = 0;

	bool cfgChanged = true;
	while (cfgChanged) {
		cfgChanged = false;

		jx_mir_funcUpdateCFG(ctx, func);

		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead->m_Next;
		while (bb) {
			jx_mir_basic_block_t* bbNext = bb->m_Next;

			const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
			if (numPred == 0 || (numPred == 1 && bb->m_PredArr[0] == bb)) {
				// Remove the block if it has no predecessors
				jx_mir_funcRemoveBasicBlock(ctx, func, bb);
				jx_mir_bbFree(ctx, bb);

				numBasicBlocksChanged++;
				cfgChanged = true;
			} else if (numPred == 1) {
				jx_mir_basic_block_t* pred = bb->m_PredArr[0];

				const uint32_t numPredSucc = (uint32_t)jx_array_sizeu(pred->m_SuccArr);
				if (numPredSucc == 1) {
					JX_CHECK(pred->m_SuccArr[0] == bb, "Invalid CFG state");

					// Remove the terminator instruction from the predecessor.
					jx_mir_instruction_t* termInstr = jx_mir_bbGetFirstTerminatorInstr(ctx, pred);
					if (termInstr) {
						JX_CHECK(termInstr->m_OpCode == JMIR_OP_JMP, "Expected jmp as terminator instruction");
						jx_mir_bbRemoveInstr(ctx, pred, termInstr);
						jx_mir_instrFree(ctx, termInstr);
					}

					// Remove the basic block from the function
					jx_mir_funcRemoveBasicBlock(ctx, func, bb);

					// Add all basic block instructions to the predecessor.
					// Note that this includes the terminator instruction so the 
					// predecessor will end up as a valid basic block again.
					jx_mir_instruction_t* bbInstr = bb->m_InstrListHead;
					while (bbInstr) {
						jx_mir_instruction_t* bbInstrNext = bbInstr->m_Next;

						jx_mir_bbRemoveInstr(ctx, bb, bbInstr);
						jx_mir_bbAppendInstr(ctx, pred, bbInstr);

						bbInstr = bbInstrNext;
					}

					// Free the basic block
					jx_mir_bbFree(ctx, bb);

					jx_mir_funcUpdateCFG(ctx, func);

					numBasicBlocksChanged++;
					cfgChanged = true;
				}
			}

			bb = bbNext;
		}
	}

	return numBasicBlocksChanged != 0;
}
