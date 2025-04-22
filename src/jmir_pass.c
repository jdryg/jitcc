#include "jmir_pass.h"
#include "jmir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>

// TODO: Move to jlib
typedef struct jx_bitset_iterator_t
{
	uint64_t m_Bits;
	uint32_t m_WordID;
	JX_PAD(4);
} jx_bitset_iterator_t;

typedef struct jx_bitset_t
{
	uint64_t* m_Bits;
	uint32_t m_NumBits;
	JX_PAD(4);
} jx_bitset_t;

static jx_bitset_t* jx_bitsetCreate(uint32_t numBits, jx_allocator_i* allocator);
static void jx_bitsetDestroy(jx_bitset_t* bs, jx_allocator_i* allocator);
static void jx_bitsetInit(jx_bitset_t* bs, uint64_t* bits, uint32_t numBits);
static uint64_t jx_bitsetCalcBufferSize(uint32_t numBits);
static void jx_bitsetSetBit(jx_bitset_t* bs, uint32_t bit);
static void jx_bitsetResetBit(jx_bitset_t* bs, uint32_t bit);
static bool jx_bitsetIsBitSet(const jx_bitset_t* bs, uint32_t bit);
static bool jx_bitsetUnion(jx_bitset_t* dst, const jx_bitset_t* src);         // dst = dst | src
static bool jx_bitsetIntersection(jx_bitset_t* dst, const jx_bitset_t* src);  // dst = dst & src
static bool jx_bitsetCopy(jx_bitset_t* dst, const jx_bitset_t* src);          // dst = src
static bool jx_bitsetSub(jx_bitset_t* dst, const jx_bitset_t* src);           // dst = dst - src
static bool jx_bitsetEqual(const jx_bitset_t* a, const jx_bitset_t* b);       // dst == src
static void jx_bitsetClear(jx_bitset_t* bs);
static void jx_bitsetIterBegin(const jx_bitset_t* bs, jx_bitset_iterator_t* iter, uint32_t firstBit);
static uint32_t jx_bitsetIterNext(const jx_bitset_t* bs, jx_bitset_iterator_t* iter);

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

			if (instr->m_OpCode == JMIR_OP_MOV) {
				jx_mir_operand_t* dst = instr->m_Operands[0];
				jx_mir_operand_t* src = instr->m_Operands[1];

				if (dst->m_Kind == JMIR_OPERAND_REGISTER && src->m_Kind == JMIR_OPERAND_REGISTER && dst->u.m_RegID == src->u.m_RegID) {
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
				// cmp ...
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
						&& op0->u.m_RegID == op1->u.m_RegID
						;
					if (isValidTest) {
						jx_mir_instruction_t* setccInstr = testInstr->m_Prev;
						if (setccInstr && jx_mir_opcodeIsSetcc(setccInstr->m_OpCode)) {
							jx_mir_operand_t* op = setccInstr->m_Operands[0];
							const bool isValidSetcc = true
								&& op->m_Kind == JMIR_OPERAND_REGISTER
								&& op->m_Type == JMIR_TYPE_I8
								&& op->u.m_RegID == op0->u.m_RegID
								;
							if (isValidSetcc) {
								jx_mir_instruction_t* cmpInstr = setccInstr->m_Prev;
								if (cmpInstr && cmpInstr->m_OpCode == JMIR_OP_CMP) {
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
			case JMIR_OP_MOVZX: {
				jx_mir_operand_t* dst = instr->m_Operands[0];
				jx_mir_operand_t* src = instr->m_Operands[1];
				const bool isDstMem = dst->m_Kind == JMIR_OPERAND_MEMORY_REF || dst->m_Kind == JMIR_OPERAND_STACK_OBJECT;
				const bool isSrcMem = src->m_Kind == JMIR_OPERAND_MEMORY_REF || src->m_Kind == JMIR_OPERAND_STACK_OBJECT;
				if (isDstMem && isSrcMem) {
					jx_mir_operand_t* vreg = jx_mir_opVirtualReg(ctx, func, dst->m_Type);
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, vreg, src));
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, dst, vreg));
					jx_mir_bbRemoveInstr(ctx, bb, instr);
					jx_mir_instrFree(ctx, instr);
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
#define JMIR_REGALLOC_MAX_INSTR_DEFS    8
#define JMIR_REGALLOC_MAX_INSTR_USES    4
#define JMIR_REGALLOC_MAX_ITERATIONS    10

typedef struct jmir_graph_node_t jmir_graph_node_t;
typedef struct jmir_mov_instr_t jmir_mov_instr_t;
typedef struct jmir_basic_block_info_t jmir_basic_block_info_t;

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

typedef struct jmir_instruction_info_t
{
	jx_mir_instruction_t* m_Instr;
	jx_bitset_t* m_LiveOutSet;
	uint32_t m_Defs[JMIR_REGALLOC_MAX_INSTR_DEFS];
	uint32_t m_Uses[JMIR_REGALLOC_MAX_INSTR_USES];
	uint32_t m_NumDefs;
	uint32_t m_NumUses;
} jmir_instruction_info_t;

typedef struct jmir_basic_block_info_t
{
	jx_mir_basic_block_t* m_BB;
	jmir_instruction_info_t* m_InstrInfo;
	jmir_basic_block_info_t** m_SuccArr;
	jx_bitset_t* m_LiveInSet;
	jx_bitset_t* m_LiveOutSet;
	uint32_t m_NumInstructions;
	JX_PAD(4);
} jmir_basic_block_info_t;

typedef struct jmir_graph_node_t
{
	jmir_graph_node_t* m_Next;
	jmir_graph_node_t* m_Prev;
	jx_bitset_t* m_AdjacentSet;
	jmir_graph_node_t** m_AdjacentArr;
	jmir_mov_instr_t** m_MoveArr;
	jmir_graph_node_t* m_Alias;
	jmir_graph_node_state m_State;
	uint32_t m_Degree;
	uint32_t m_RegID;
	uint32_t m_Color;
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

	uint32_t* m_HWRegs;
	uint32_t m_NumHWRegs; // K
	JX_PAD(4);

	jmir_basic_block_info_t* m_BBInfo;
	uint32_t m_NumBasicBlocks;
	JX_PAD(4);
} jmir_func_pass_regalloc_t;

static void jmir_funcPass_regAllocDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_regAllocRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static bool jmir_regAlloc_init(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func, const uint32_t* hwRegs, uint32_t numHWRegs);
static void jmir_regAlloc_shutdown(jmir_func_pass_regalloc_t* pass);
static bool jmir_regAlloc_initInfo(jmir_func_pass_regalloc_t* pass, jx_mir_function_t* func);
static void jmir_regAlloc_destroyInfo(jmir_func_pass_regalloc_t* pass);
static bool jmir_regAlloc_initBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jmir_basic_block_info_t* bbInfo, jx_mir_basic_block_t* bb);
static bool jmir_regAlloc_initInstrInfo(jmir_func_pass_regalloc_t* pass, jmir_instruction_info_t* instrInfo, jx_mir_instruction_t* instr);
static void jmir_regAlloc_buildCFG(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func);
static bool jmir_regAlloc_livenessAnalysis(jmir_func_pass_regalloc_t* pass, jx_mir_function_t* func);
static bool jmir_regAlloc_isMoveInstr(jmir_instruction_info_t* instrInfo);
static jmir_basic_block_info_t* jmir_regAlloc_getBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jx_mir_basic_block_t* bb);
static uint32_t jmir_regAlloc_mapRegToID(jmir_func_pass_regalloc_t* pass, uint32_t regID);
static jmir_graph_node_t* jmir_regAlloc_getNode(jmir_func_pass_regalloc_t* pass, uint32_t nodeID);
static uint32_t jmir_regAlloc_getHWRegWithColor(jmir_func_pass_regalloc_t* pass, uint32_t color);

static void jmir_regAlloc_makeWorklist(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_simplify(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_coalesce(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_freeze(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_selectSpill(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_assignColors(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_replaceRegs(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_spill(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_replaceInstrRegDef(jx_mir_instruction_t* instr, uint32_t vregID, jx_mir_operand_t* newReg);
static void jmir_regAlloc_replaceInstrRegUse(jx_mir_instruction_t* instr, uint32_t vregID, jx_mir_operand_t* newReg);

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
static bool jmir_regAlloc_nodeInit(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, uint32_t regID, jmir_graph_node_state initialState);
static void jmir_regAlloc_nodeSetState(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, jmir_graph_node_state state);
static bool jmir_nodeIs(const jmir_graph_node_t* node, jmir_graph_node_state state);

static jmir_mov_instr_t* jmir_regAlloc_movAlloc(jmir_func_pass_regalloc_t* pass, jmir_instruction_info_t* instrInfo);
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

	const uint32_t hwRegs[] = {
		JMIR_HWREG_A,
		JMIR_HWREG_C,
		JMIR_HWREG_D,
		JMIR_HWREG_B,
		JMIR_HWREG_SI,
		JMIR_HWREG_DI,  
		JMIR_HWREG_R8,
		JMIR_HWREG_R9,
		JMIR_HWREG_R10,
		JMIR_HWREG_R11,
		JMIR_HWREG_R12,
		JMIR_HWREG_R13,
		JMIR_HWREG_R14,
		JMIR_HWREG_R15,
	};

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	uint32_t iter = 0;
	bool changed = true;
	while (changed && iter < JMIR_REGALLOC_MAX_ITERATIONS) {
		changed = false;

		// Liveness analysis + build
		if (!jmir_regAlloc_init(pass, ctx, func, hwRegs, JX_COUNTOF(hwRegs))) {
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

	return false;
}

static bool jmir_regAlloc_init(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func, const uint32_t* hwRegs, uint32_t numHWRegs)
{
	allocator_api->linearAllocatorReset(pass->m_LinearAllocator);

	jx_memset(pass->m_NodeList, 0, sizeof(jmir_graph_node_t*) * JMIR_NODE_STATE_COUNT);
	jx_memset(pass->m_MoveList, 0, sizeof(jmir_mov_instr_t*) * JMIR_MOV_STATE_COUNT);

	pass->m_HWRegs = (uint32_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(uint32_t) * numHWRegs);
	if (!pass->m_HWRegs) {
		return false;
	}
	jx_memcpy(pass->m_HWRegs, hwRegs, sizeof(uint32_t) * numHWRegs);
	pass->m_NumHWRegs = numHWRegs;

	const uint32_t numVRegs = func->m_NextVirtualRegID; // Assume all allocated virtual regs are used by the function's code.
	const uint32_t numNodes = numHWRegs + numVRegs;

	pass->m_Nodes = (jmir_graph_node_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_graph_node_t) * numNodes);
	if (!pass->m_Nodes) {
		return false;
	}
	jx_memset(pass->m_Nodes, 0, sizeof(jmir_graph_node_t) * numNodes);
	pass->m_NumNodes = numNodes;

	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		jmir_graph_node_t* node = &pass->m_Nodes[iNode];
		if (!jmir_regAlloc_nodeInit(pass, node, iNode, iNode < numHWRegs ? JMIR_NODE_STATE_PRECOLORED : JMIR_NODE_STATE_INITIAL)) {
			return false;
		}
	}

	if (!jmir_regAlloc_initInfo(pass, func)) {
		return false;
	}

	jmir_regAlloc_buildCFG(pass, ctx, func);

	if (!jmir_regAlloc_livenessAnalysis(pass, func)) {
		return false;
	}

	// Add edges between registers to graph
	{
		const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
		for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
			jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];

			const uint32_t numInstr = bbInfo->m_NumInstructions;
			for (uint32_t iInstr = 0; iInstr < numInstr; ++iInstr) {
				jmir_instruction_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

				const uint32_t numDefs = instrInfo->m_NumDefs;
				for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
					jmir_graph_node_t* defNode = jmir_regAlloc_getNode(pass, instrInfo->m_Defs[iDef]);

					jx_bitset_iterator_t liveIter;
					jx_bitsetIterBegin(instrInfo->m_LiveOutSet, &liveIter, 0);

					uint32_t liveID = jx_bitsetIterNext(instrInfo->m_LiveOutSet, &liveIter);
					while (liveID != UINT32_MAX) {
						jmir_graph_node_t* liveNode = jmir_regAlloc_getNode(pass, liveID);
						jmir_regAlloc_addEdge(pass, defNode, liveNode);

						liveID = jx_bitsetIterNext(instrInfo->m_LiveOutSet, &liveIter);
					}
				}
			}
		}
	}

	return true;
}

static void jmir_regAlloc_shutdown(jmir_func_pass_regalloc_t* pass)
{
	jmir_regAlloc_destroyInfo(pass);

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

static bool jmir_regAlloc_initInfo(jmir_func_pass_regalloc_t* pass, jx_mir_function_t* func)
{
	// Count number of basic blocks
	uint32_t numBasicBlocks = 0;
	{
		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			++numBasicBlocks;
			bb = bb->m_Next;
		}
	}
	JX_CHECK(numBasicBlocks, "Empty function found!");

	pass->m_BBInfo = (jmir_basic_block_info_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_basic_block_info_t) * numBasicBlocks);
	if (!pass->m_BBInfo) {
		return false;
	}

	jx_memset(pass->m_BBInfo, 0, sizeof(jmir_basic_block_info_t) * numBasicBlocks);
	pass->m_NumBasicBlocks = numBasicBlocks;

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[0];
	while (bb) {
		if (!jmir_regAlloc_initBasicBlockInfo(pass, bbInfo, bb)) {
			return false;
		}

		++bbInfo;
		bb = bb->m_Next;
	}

	return true;
}

static void jmir_regAlloc_destroyInfo(jmir_func_pass_regalloc_t* pass)
{
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_array_free(bbInfo->m_SuccArr);
	}
}

static bool jmir_regAlloc_initBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jmir_basic_block_info_t* bbInfo, jx_mir_basic_block_t* bb)
{
	bbInfo->m_BB = bb;

	// Count number of instructions in basic block
	uint32_t numInstr = 0;
	{
		jx_mir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			++numInstr;
			instr = instr->m_Next;
		}
	}

	if (numInstr) {
		bbInfo->m_InstrInfo = (jmir_instruction_info_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_instruction_info_t) * numInstr);
		if (!bbInfo->m_InstrInfo) {
			return false;
		}

		jx_memset(bbInfo->m_InstrInfo, 0, sizeof(jmir_instruction_info_t) * numInstr);

		// Initialize instruction info
		{
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			jmir_instruction_info_t* instrInfo = &bbInfo->m_InstrInfo[0];
			while (instr) {
				jmir_regAlloc_initInstrInfo(pass, instrInfo, instr);

				++instrInfo;
				instr = instr->m_Next;
			}
		}
	}

	bbInfo->m_NumInstructions = numInstr;

	bbInfo->m_SuccArr = (jmir_basic_block_info_t**)jx_array_create(pass->m_Allocator);
	if (!bbInfo->m_SuccArr) {
		return false;
	}

	bbInfo->m_LiveInSet = jx_bitsetCreate(pass->m_NumNodes, pass->m_LinearAllocator);
	if (!bbInfo->m_LiveInSet) {
		return false;
	}

	bbInfo->m_LiveOutSet = jx_bitsetCreate(pass->m_NumNodes, pass->m_LinearAllocator);
	if (!bbInfo->m_LiveOutSet) {
		return false;
	}

	return true;
}

static void jmir_regAlloc_instrAddUse(jmir_func_pass_regalloc_t* pass, jmir_instruction_info_t* instrInfo, uint32_t regID)
{
	const uint32_t id = jmir_regAlloc_mapRegToID(pass, regID);
	if (id == UINT32_MAX) {
		return;
	}

	JX_CHECK(instrInfo->m_NumUses + 1 < JMIR_REGALLOC_MAX_INSTR_DEFS, "Too many instruction uses");
	instrInfo->m_Uses[instrInfo->m_NumUses++] = id;
}

static void jmir_regAlloc_instrAddDef(jmir_func_pass_regalloc_t* pass, jmir_instruction_info_t* instrInfo, uint32_t regID)
{
	JX_CHECK(regID != JMIR_MEMORY_REG_NONE, "Invalid register ID");
	const uint32_t id = jmir_regAlloc_mapRegToID(pass, regID);
	JX_CHECK(id != UINT32_MAX, "Failed to map register to node index");
	JX_CHECK(instrInfo->m_NumDefs + 1 < JMIR_REGALLOC_MAX_INSTR_DEFS, "Too many instruction defs");
	instrInfo->m_Defs[instrInfo->m_NumDefs++] = id;
}

static bool jmir_regAlloc_initInstrInfo(jmir_func_pass_regalloc_t* pass, jmir_instruction_info_t* instrInfo, jx_mir_instruction_t* instr)
{
	instrInfo->m_Instr = instr;
	instrInfo->m_LiveOutSet = jx_bitsetCreate(pass->m_NumNodes, pass->m_LinearAllocator);
	if (!instrInfo->m_LiveOutSet) {
		return false;
	}

	instrInfo->m_NumDefs = 0;
	instrInfo->m_NumUses = 0;
	switch (instr->m_OpCode) {
	case JMIR_OP_RET: {
		// ret implicitly uses RAX?
		jmir_regAlloc_instrAddUse(pass, instrInfo, JMIR_HWREG_A);
	} break;
	case JMIR_OP_CMP:
	case JMIR_OP_TEST: {
		for (uint32_t iOperand = 0; iOperand < 2; ++iOperand) {
			jx_mir_operand_t* src = instr->m_Operands[iOperand];
			if (src->m_Kind == JMIR_OPERAND_REGISTER) {
				jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_RegID);
			} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
				jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_BaseRegID);
				jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_IndexRegID);
			}
		}
	} break;
	case JMIR_OP_PHI: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_MOV:
	case JMIR_OP_MOVSX:
	case JMIR_OP_MOVZX: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_RegID);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_BaseRegID);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_IndexRegID);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddDef(pass, instrInfo, dst->u.m_RegID);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef.m_BaseRegID);
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef.m_IndexRegID);
		}
	} break;
	case JMIR_OP_IDIV:
	case JMIR_OP_DIV: {
		jx_mir_operand_t* op = instr->m_Operands[0];

		jmir_regAlloc_instrAddUse(pass, instrInfo, JMIR_HWREG_A);
		jmir_regAlloc_instrAddUse(pass, instrInfo, JMIR_HWREG_D);

		if (op->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, op->u.m_RegID);
		} else if (op->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, op->u.m_MemRef.m_BaseRegID);
			jmir_regAlloc_instrAddUse(pass, instrInfo, op->u.m_MemRef.m_IndexRegID);
		}

		jmir_regAlloc_instrAddDef(pass, instrInfo, JMIR_HWREG_A);
		jmir_regAlloc_instrAddDef(pass, instrInfo, JMIR_HWREG_D);
	} break;
	case JMIR_OP_ADD:
	case JMIR_OP_SUB:
	case JMIR_OP_IMUL:
	case JMIR_OP_XOR:
	case JMIR_OP_AND:
	case JMIR_OP_OR:
	case JMIR_OP_SAR:
	case JMIR_OP_SHR:
	case JMIR_OP_SHL: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_RegID);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_BaseRegID);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_IndexRegID);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_RegID); // binary operators use both src and dst operands.
			jmir_regAlloc_instrAddDef(pass, instrInfo, dst->u.m_RegID);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef.m_BaseRegID);
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef.m_IndexRegID);
		}
	} break;
	case JMIR_OP_LEA: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_BaseRegID);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_IndexRegID);
		} else if (src->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			// NOTE: External symbols are RIP based so there is no register to use.
		} else {
			JX_CHECK(src->m_Kind == JMIR_OPERAND_STACK_OBJECT, "lea source operand expected to be a memory ref or a stack object.");
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "lea destination operand expected to be a register.");
		jmir_regAlloc_instrAddDef(pass, instrInfo, dst->u.m_RegID);
	} break;
	case JMIR_OP_CALL: {
#if 0
		jx_mir_operand_t* targetOp = instr->m_Operands[0];
		JX_CHECK(targetOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL, "call reg not implemented yet. This will crash below!");
		jx_mir_function_t* targetFunc = jx_mir_getFunctionByName(pass->m_Ctx, targetOp->u.m_ExternalSymbolName);
		JX_CHECK(targetFunc, "Function not found!");
		const uint32_t numRegArgs = jx_min_u32(targetFunc->m_NumArgs, JX_COUNTOF(kMIRFuncArgIReg));
		for (uint32_t iRegArg = 0; iRegArg < numRegArgs; ++iRegArg) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRFuncArgIReg[iRegArg]);
		}
#else
		// TODO: Annotate call with the function signature so I can know which registers are actually used
		// by the call. For now assume all registers are used.
		for (uint32_t iRegArg = 0; iRegArg < JX_COUNTOF(kMIRFuncArgIReg); ++iRegArg) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRFuncArgIReg[iRegArg]);
		}
#endif

		const uint32_t numCallerSavedRegs = JX_COUNTOF(kMIRFuncCallerSavedIReg);
		for (uint32_t iReg = 0; iReg < numCallerSavedRegs; ++iReg) {
			jmir_regAlloc_instrAddDef(pass, instrInfo, kMIRFuncCallerSavedIReg[iReg]);
		}
	} break;
	case JMIR_OP_PUSH: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_POP: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_CDQ:
	case JMIR_OP_CQO: {
		jmir_regAlloc_instrAddUse(pass, instrInfo, JMIR_HWREG_A);
		jmir_regAlloc_instrAddDef(pass, instrInfo, JMIR_HWREG_D);
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
			jmir_regAlloc_instrAddDef(pass, instrInfo, src->u.m_RegID);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_BaseRegID);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef.m_IndexRegID);
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

	for (uint32_t iDef = instrInfo->m_NumDefs; iDef < JMIR_REGALLOC_MAX_INSTR_DEFS; ++iDef) {
		instrInfo->m_Defs[iDef] = UINT32_MAX;
	}
	for (uint32_t iUse = instrInfo->m_NumUses; iUse < JMIR_REGALLOC_MAX_INSTR_USES; ++iUse) {
		instrInfo->m_Uses[iUse] = UINT32_MAX;
	}

	return true;
}

static void jmir_regAlloc_buildCFG(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	// Clear existing CFG
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_array_resize(bbInfo->m_SuccArr, 0);
	}

	// Rebuild CFG
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_mir_basic_block_t* bb = bbInfo->m_BB;

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
					jmir_basic_block_info_t* targetBBInfo = jmir_regAlloc_getBasicBlockInfo(pass, targetBB);

					jx_array_push_back(bbInfo->m_SuccArr, targetBBInfo);
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

			jmir_basic_block_info_t* nextBBInfo = jmir_regAlloc_getBasicBlockInfo(pass, bb->m_Next);

			jx_array_push_back(bbInfo->m_SuccArr, nextBBInfo);
		}
	}
}

static bool jmir_regAlloc_livenessAnalysis(jmir_func_pass_regalloc_t* pass, jx_mir_function_t* func)
{
	const uint32_t numNodes = pass->m_NumNodes;
	jx_bitset_t* prevLiveIn = jx_bitsetCreate(numNodes, pass->m_Allocator);
	jx_bitset_t* prevLiveOut = jx_bitsetCreate(numNodes, pass->m_Allocator);
	jx_bitset_t* instrLive = jx_bitsetCreate(numNodes, pass->m_Allocator);
	if (!prevLiveIn || !prevLiveOut || !instrLive) {
		return false;
	}

	// Reset live in/out sets
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_bitsetClear(bbInfo->m_LiveInSet);
		jx_bitsetClear(bbInfo->m_LiveOutSet);
	}

	// Rebuild live in/out sets
	bool changed = true;
	while (changed) {
		changed = false;

		// Process basic blocks in reverse order. This is not technically correct
		// but any order will do. Ideally I'd need "reverse postorder" for performance.
		// Let's not worry about that for now.
		uint32_t iBB = numBasicBlocks;
		while (iBB--) {
			jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];

			// out'[v] = out[v]
			// in'[v] = in[v]
			jx_bitsetCopy(prevLiveIn, bbInfo->m_LiveInSet);
			jx_bitsetCopy(prevLiveOut, bbInfo->m_LiveOutSet);

			// out[v] = Union(w in succ, in[w])
			const uint32_t numSucc = (uint32_t)jx_array_sizeu(bbInfo->m_SuccArr);
			if (numSucc) {
				jmir_basic_block_info_t* succBBInfo = bbInfo->m_SuccArr[0];

				jx_bitsetCopy(bbInfo->m_LiveOutSet, succBBInfo->m_LiveInSet);
				for (uint32_t iSucc = 1; iSucc < numSucc; ++iSucc) {
					succBBInfo = bbInfo->m_SuccArr[iSucc];
					jx_bitsetUnion(bbInfo->m_LiveOutSet, succBBInfo->m_LiveInSet);
				}
			}

			// Calculate live in by walking the basic block's instruction list backwards,
			// while storing live info for each instruction.
			{
				jx_bitsetCopy(instrLive, bbInfo->m_LiveOutSet);

				const uint32_t numInstructions = bbInfo->m_NumInstructions;
				uint32_t iInstr = numInstructions;
				while (iInstr-- != 0) {
					jmir_instruction_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

					if (jmir_regAlloc_isMoveInstr(instrInfo)) {
						JX_CHECK(instrInfo->m_NumUses == 1, "Move instruction expected to have 1 use.");
						jx_bitsetResetBit(instrLive, instrInfo->m_Uses[0]);

						jmir_mov_instr_t* mov = jmir_regAlloc_movAlloc(pass, instrInfo);
						if (!mov) {
							return false;
						}

						jx_array_push_back(mov->m_Dst->m_MoveArr, mov);
						if (instrInfo->m_Defs[0] != instrInfo->m_Uses[0]) {
							jx_array_push_back(mov->m_Src->m_MoveArr, mov);
						}

						jmir_regAlloc_movSetState(pass, mov, JMIR_MOV_STATE_WORKLIST);
					}

					jx_bitsetCopy(instrInfo->m_LiveOutSet, instrLive);

					const uint32_t numDefs = instrInfo->m_NumDefs;
					for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
						jx_bitsetResetBit(instrLive, instrInfo->m_Defs[iDef]);
					}

					const uint32_t numUses = instrInfo->m_NumUses;
					for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
						jx_bitsetSetBit(instrLive, instrInfo->m_Uses[iUse]);
					}
				}

				jx_bitsetCopy(bbInfo->m_LiveInSet, instrLive);
			}

			// Check if something changed
			changed = changed
				|| !jx_bitsetEqual(bbInfo->m_LiveInSet, prevLiveIn)
				|| !jx_bitsetEqual(bbInfo->m_LiveOutSet, prevLiveOut)
				;
		}
	}

	jx_bitsetDestroy(instrLive, pass->m_Allocator);
	jx_bitsetDestroy(prevLiveIn, pass->m_Allocator);
	jx_bitsetDestroy(prevLiveOut, pass->m_Allocator);

	return true;
}

static bool jmir_regAlloc_isMoveInstr(jmir_instruction_info_t* instrInfo)
{
	jx_mir_instruction_t* instr = instrInfo->m_Instr;

	if (instr->m_OpCode != JMIR_OP_MOV && instr->m_OpCode != JMIR_OP_MOVSX && instr->m_OpCode != JMIR_OP_MOVZX) {
		return false;
	}

	jx_mir_operand_t* dst = instr->m_Operands[0];
	jx_mir_operand_t* src = instr->m_Operands[1];
	return dst->m_Kind == JMIR_OPERAND_REGISTER
		&& src->m_Kind == JMIR_OPERAND_REGISTER
		;
}

static jmir_basic_block_info_t* jmir_regAlloc_getBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jx_mir_basic_block_t* bb)
{
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		if (pass->m_BBInfo[iBB].m_BB == bb) {
			return &pass->m_BBInfo[iBB];
		}
	}

	return NULL;
}

static uint32_t jmir_regAlloc_mapRegToID(jmir_func_pass_regalloc_t* pass, uint32_t regID)
{
	uint32_t id = UINT32_MAX;
	if (regID == JMIR_MEMORY_REG_NONE) {
		// No op
	} else if (regID >= JMIR_FIRST_VIRTUAL_REGISTER) {
		id = (regID - JMIR_FIRST_VIRTUAL_REGISTER) + pass->m_NumHWRegs;
		JX_CHECK(id < pass->m_NumNodes, "Invalid virtual register node index!");
	} else {
		// Must be a hardware register. Find index in hw regs array.
		const uint32_t numHWRegs = pass->m_NumHWRegs;
		for (uint32_t iHWReg = 0; iHWReg < numHWRegs; ++iHWReg) {
			if (pass->m_HWRegs[iHWReg] == regID) {
				id = iHWReg;
				break;
			}
		}
	}

	return id;
}

static inline jmir_graph_node_t* jmir_regAlloc_getNode(jmir_func_pass_regalloc_t* pass, uint32_t nodeID)
{
	JX_CHECK(nodeID < pass->m_NumNodes, "Invalid node index.");
	return &pass->m_Nodes[nodeID];
}

static uint32_t jmir_regAlloc_getHWRegWithColor(jmir_func_pass_regalloc_t* pass, uint32_t color)
{
	const uint32_t numHWRegs = pass->m_NumHWRegs;
	for (uint32_t iHWReg = 0; iHWReg < numHWRegs; ++iHWReg) {
		jmir_graph_node_t* node = &pass->m_Nodes[iHWReg];
		if (node->m_Color == color) {
			JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_PRECOLORED), "Expected precolored hw register!");
			return pass->m_HWRegs[iHWReg];
		}
	}

	return JMIR_MEMORY_REG_NONE;
}

static void jmir_regAlloc_makeWorklist(jmir_func_pass_regalloc_t* pass)
{
	jmir_graph_node_t* node = pass->m_NodeList[JMIR_NODE_STATE_INITIAL];
	while (node) {
		jmir_graph_node_t* nextNode = node->m_Next;

		if (node->m_Degree >= pass->m_NumHWRegs) {
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
	uint32_t spillCost = spillCandidate->m_Degree;
	spillCandidate = spillCandidate->m_Next;
	while (spillCandidate) {
		JX_CHECK(jmir_nodeIs(spillCandidate, JMIR_NODE_STATE_SPILL), "Node in spill worklist not in spill state!");

		if (spillCost < spillCandidate->m_Degree) {
			spilledNode = spillCandidate;
			spillCost = spillCandidate->m_Degree;
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

		JX_CHECK(pass->m_NumHWRegs < 64, "Too many hardware registers! Requires different handling of available colors.");
		uint64_t availableColors = (1ull << pass->m_NumHWRegs) - 1;

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

		node->m_Color = alias->m_Color;
		jmir_regAlloc_nodeSetState(pass, node, JMIR_NODE_STATE_COLORED);

		node = pass->m_NodeList[JMIR_NODE_STATE_COALESCED];
	}
}

static void jmir_regAlloc_replaceRegs(jmir_func_pass_regalloc_t* pass)
{
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];

		const uint32_t numInstr = bbInfo->m_NumInstructions;
		for (uint32_t iInstr = 0; iInstr < numInstr; ++iInstr) {
			jmir_instruction_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

			jx_mir_instruction_t* instr = instrInfo->m_Instr;
			const uint32_t numOperands = instr->m_NumOperands;
			for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
				jx_mir_operand_t* operand = instr->m_Operands[iOperand];
				if (operand->m_Kind == JMIR_OPERAND_REGISTER && operand->u.m_RegID >= JMIR_FIRST_VIRTUAL_REGISTER) {
					jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jmir_regAlloc_mapRegToID(pass, operand->u.m_RegID));
					if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
						uint32_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_Color);
						if (hwReg != JMIR_MEMORY_REG_NONE) {
							operand->u.m_RegID = hwReg;
						}
					}
				} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					jx_mir_memory_ref_t* memRef = &operand->u.m_MemRef;

					if (memRef->m_BaseRegID != JMIR_MEMORY_REG_NONE && memRef->m_BaseRegID >= JMIR_FIRST_VIRTUAL_REGISTER) {
						jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jmir_regAlloc_mapRegToID(pass, memRef->m_BaseRegID));
						if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
							uint32_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_Color);
							if (hwReg != JMIR_MEMORY_REG_NONE) {
								memRef->m_BaseRegID = hwReg;
							}
						}
					}

					if (memRef->m_IndexRegID != JMIR_MEMORY_REG_NONE && memRef->m_IndexRegID >= JMIR_FIRST_VIRTUAL_REGISTER) {
						jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jmir_regAlloc_mapRegToID(pass, memRef->m_IndexRegID));
						if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
							uint32_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_Color);
							if (hwReg != JMIR_MEMORY_REG_NONE) {
								memRef->m_IndexRegID = hwReg;
							}
						}
					}
				}
			}
		}
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
		JX_CHECK(node->m_RegID >= pass->m_NumHWRegs, "Expected a virtual register!");

		const uint32_t regID = node->m_RegID;
		const uint32_t vregID = (regID - pass->m_NumHWRegs) + JMIR_FIRST_VIRTUAL_REGISTER;

		jx_mir_operand_t* stackSlot = NULL;
		jx_mir_operand_t* temp = NULL;

		// Walk the whole function and rewrite every definition as a store to stack
		// and every use as a load from stack.
		const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
		for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
			jmir_basic_block_info_t* bbInfo = &pass->m_BBInfo[iBB];
			jx_mir_basic_block_t* bb = bbInfo->m_BB;

			const uint32_t numInstructions = bbInfo->m_NumInstructions;
			for (uint32_t iInstr = 0; iInstr < numInstructions; ++iInstr) {
				jmir_instruction_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];
				jx_mir_instruction_t* instr = instrInfo->m_Instr;

				bool use = false;
				bool def = false;

				const uint32_t numUses = instrInfo->m_NumUses;
				for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
					if (instrInfo->m_Uses[iUse] == regID) {
						use = true;
						break;
					}
				}

				const uint32_t numDefs = instrInfo->m_NumDefs;
				for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
					if (instrInfo->m_Defs[iDef] == regID) {
						def = true;
						break;
					}
				}

				if (!stackSlot && (use || def)) {
					// instr uses this register. Find the register in the operand list and figure
					// out its type
					jx_mir_type_kind regType = JMIR_TYPE_VOID;
					const uint32_t numOperands = instr->m_NumOperands;
					for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
						jx_mir_operand_t* operand = instr->m_Operands[iOperand];
						if (operand->m_Kind == JMIR_OPERAND_REGISTER) {
							if (operand->u.m_RegID == vregID) {
								regType = operand->m_Type;
								break;
							}
						} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
							if (operand->u.m_MemRef.m_BaseRegID == vregID || operand->u.m_MemRef.m_IndexRegID == vregID) {
								regType = JMIR_TYPE_I64;
								break;
							}
						}
					}

					JX_CHECK(regType != JMIR_TYPE_VOID, "Operand type not found!");
					stackSlot = jx_mir_opStackObj(ctx, func, regType, jx_mir_typeGetSize(regType), jx_mir_typeGetAlignment(regType));
					temp = jx_mir_opVirtualReg(ctx, func, regType);
				} else {
					// TODO: Check if the operand has the same size as the allocated stack slot. 
					// Otherwise fail...
				}

				if (use && def) {
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, temp, stackSlot));
					jmir_regAlloc_replaceInstrRegUse(instr, vregID, temp);
					jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_mov(ctx, stackSlot, temp));
				} else if (use) {
					// Load from stack into new temporary
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, temp, stackSlot));
					jmir_regAlloc_replaceInstrRegUse(instr, vregID, temp);
				} else if (def) {
					jmir_regAlloc_replaceInstrRegDef(instr, vregID, temp);
					jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_mov(ctx, stackSlot, temp));
				}
			}
		}

		node = node->m_Next;
	}
}

static void jmir_regAlloc_replaceInstrRegDef(jx_mir_instruction_t* instr, uint32_t vregID, jx_mir_operand_t* newReg)
{
	switch (instr->m_OpCode) {
	case JMIR_OP_RET: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_CMP: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_TEST: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_JMP: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_PHI: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_MOV:
	case JMIR_OP_MOVSX:
	case JMIR_OP_MOVZX:
	case JMIR_OP_IMUL:
	case JMIR_OP_ADD:
	case JMIR_OP_SUB:
	case JMIR_OP_LEA:
	case JMIR_OP_XOR:
	case JMIR_OP_AND:
	case JMIR_OP_OR:
	case JMIR_OP_SAR:
	case JMIR_OP_SHR:
	case JMIR_OP_SHL: {
		jx_mir_operand_t* dst = instr->m_Operands[0];
		JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "Move dest expected to be a register.");
		JX_CHECK(dst->u.m_RegID == vregID, "Expected a different virtual register!");
		instr->m_Operands[0] = newReg;
	} break;
	case JMIR_OP_IDIV:
	case JMIR_OP_DIV: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_CALL: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_PUSH: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_POP: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_CDQ:
	case JMIR_OP_CQO: {
		JX_NOT_IMPLEMENTED();
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
		JX_NOT_IMPLEMENTED();
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
		JX_NOT_IMPLEMENTED();
	} break;
	default:
		JX_CHECK(false, "Unknown mir opcode");
		break;
	}
}

static void jmir_regAlloc_replaceInstrRegUse(jx_mir_instruction_t* instr, uint32_t vregID, jx_mir_operand_t* newReg)
{
	switch (instr->m_OpCode) {
	case JMIR_OP_RET: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_TEST: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_JMP: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_PHI: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_MOV:
	case JMIR_OP_MOVSX:
	case JMIR_OP_MOVZX: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		JX_CHECK(src->m_Kind == JMIR_OPERAND_REGISTER, "Expected register operand");
		JX_CHECK(src->u.m_RegID == vregID, "Wrong virtual register!");
		instr->m_Operands[1] = newReg;
	} break;
	case JMIR_OP_CMP:
	case JMIR_OP_IMUL:
	case JMIR_OP_ADD:
	case JMIR_OP_SUB: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			if (src->u.m_RegID == vregID) {
				instr->m_Operands[1] = newReg;
			}
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			JX_NOT_IMPLEMENTED();
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			if (dst->u.m_RegID == vregID) {
				instr->m_Operands[0] = newReg;
			}
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			JX_NOT_IMPLEMENTED();
		}
	} break;
	case JMIR_OP_IDIV:
	case JMIR_OP_DIV: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_LEA: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_XOR:
	case JMIR_OP_AND:
	case JMIR_OP_OR: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_SAR:
	case JMIR_OP_SHR:
	case JMIR_OP_SHL: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_CALL: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_PUSH: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_POP: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_CDQ:
	case JMIR_OP_CQO: {
		JX_CHECK(false, "cdq/cqo do not have any operands!");
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
		JX_CHECK(false, "setcc do not use any registers!");
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
		JX_NOT_IMPLEMENTED();
	} break;
	default:
		JX_CHECK(false, "Unknown mir opcode");
		break;
	}
}

static void jmir_regAlloc_addEdge(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v)
{
	if (u != v && !jmir_regAlloc_areNodesAdjacent(pass, u, v)) {
		jx_bitsetSetBit(u->m_AdjacentSet, v->m_RegID);
		jx_bitsetSetBit(v->m_AdjacentSet, u->m_RegID);

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
	JX_CHECK(jx_bitsetIsBitSet(u->m_AdjacentSet, v->m_RegID) == jx_bitsetIsBitSet(v->m_AdjacentSet, u->m_RegID), "Invalid bitset state!");
	return jx_bitsetIsBitSet(u->m_AdjacentSet, v->m_RegID);
}

static bool jmir_regAlloc_canCombineNodes(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* u, jmir_graph_node_t* v)
{
	bool canCombine = false;

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
			if (t->m_Degree >= pass->m_NumHWRegs) {
				++k;
			}

			t = jmir_regAlloc_nodeAdjacentIterNext(u, &adjIter);
		}

		adjIter = 0;
		t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
		while (t) {
			if (t->m_Degree >= pass->m_NumHWRegs) {
				// Only count this node if it's not adjacent to u. If it is,
				// it's already counted in the loop above.
				if (!jmir_regAlloc_areNodesAdjacent(pass, u, t)) {
					++k;
				}
			}

			t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
		}

		canCombine = k < pass->m_NumHWRegs;
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
		if (d < pass->m_NumHWRegs && t->m_Degree >= pass->m_NumHWRegs) {
			jmir_regAlloc_nodeSetState(pass, t, JMIR_NODE_STATE_SPILL);
		}
		jmir_regAlloc_nodeDecrementDegree(pass, t);

		t = jmir_regAlloc_nodeAdjacentIterNext(v, &adjIter);
	}

	if (u->m_Degree >= pass->m_NumHWRegs && jmir_nodeIs(u, JMIR_NODE_STATE_FREEZE)) {
		jmir_regAlloc_nodeSetState(pass, u, JMIR_NODE_STATE_SPILL);
	}
}

static bool jmir_regAlloc_OK(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* t, jmir_graph_node_t* r)
{
	return (t->m_Degree < pass->m_NumHWRegs)
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

			if (numNodeMoves == 0 && v->m_Degree < pass->m_NumHWRegs) {
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

	if (d == pass->m_NumHWRegs) {
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
	if (!jmir_nodeIs(node, JMIR_NODE_STATE_PRECOLORED) && !jmir_regAlloc_nodeMoveRelated(pass, node) && node->m_Degree < pass->m_NumHWRegs) {
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

static bool jmir_regAlloc_nodeInit(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, uint32_t regID, jmir_graph_node_state initialState)
{
	node->m_AdjacentSet = jx_bitsetCreate(pass->m_NumNodes, pass->m_LinearAllocator);
	if (!node->m_AdjacentSet) {
		return false;
	}

	node->m_MoveArr = (jmir_mov_instr_t**)jx_array_create(pass->m_Allocator);
	if (!node->m_MoveArr) {
		return false;
	}

	if (initialState == JMIR_NODE_STATE_PRECOLORED) {
		node->m_AdjacentArr = NULL;
		node->m_Color = regID;
	} else {
		node->m_AdjacentArr = (jmir_graph_node_t**)jx_array_create(pass->m_Allocator);
		if (!node->m_AdjacentArr) {
			return false;
		}

		node->m_Color = UINT32_MAX;
	}

	node->m_RegID = regID;
	node->m_State = JMIR_NODE_STATE_ALLOCATED;
	jmir_regAlloc_nodeSetState(pass, node, initialState);

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

static inline bool jmir_nodeIs(const jmir_graph_node_t* node, jmir_graph_node_state state)
{
	return node->m_State == state;
}

static jmir_mov_instr_t* jmir_regAlloc_movAlloc(jmir_func_pass_regalloc_t* pass, jmir_instruction_info_t* instrInfo)
{
	jmir_mov_instr_t* mov = (jmir_mov_instr_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_mov_instr_t));
	if (!mov) {
		return NULL;
	}

	jx_memset(mov, 0, sizeof(jmir_mov_instr_t));
	mov->m_State = JMIR_MOV_STATE_ALLOCATED;
	mov->m_Dst = jmir_regAlloc_getNode(pass, instrInfo->m_Defs[0]);
	mov->m_Src = jmir_regAlloc_getNode(pass, instrInfo->m_Uses[0]);

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
// Bitset
//
static jx_bitset_t* jx_bitsetCreate(uint32_t numBits, jx_allocator_i* allocator)
{
	const uint32_t numWords = (numBits + 63) / 64;
	uint8_t* buffer = (uint8_t*)JX_ALLOC(allocator, sizeof(jx_bitset_t) + sizeof(uint64_t) * numWords);
	if (!buffer) {
		return NULL;
	}

	uint8_t* ptr = buffer;
	jx_bitset_t* bs = (jx_bitset_t*)ptr;
	ptr += sizeof(jx_bitset_t);

	bs->m_Bits = (uint64_t*)ptr;
	bs->m_NumBits = numBits;

	jx_memset(bs->m_Bits, 0, sizeof(uint64_t) * numWords);

	return bs;
}

static void jx_bitsetDestroy(jx_bitset_t* bs, jx_allocator_i* allocator)
{
	JX_FREE(allocator, bs);
}

static void jx_bitsetInit(jx_bitset_t* bs, uint64_t* bits, uint32_t numBits)
{
	JX_CHECK(bits != NULL, "Invalid bit buffer");
	bs->m_Bits = bits;
	bs->m_NumBits = numBits;

	const uint32_t numWords = (numBits + 63) / 64;
	jx_memset(bits, 0, sizeof(uint64_t) * numWords);
}

static uint64_t jx_bitsetCalcBufferSize(uint32_t numBits)
{
	return sizeof(uint64_t) * ((numBits + 63) / 64);
}

static void jx_bitsetSetBit(jx_bitset_t* bs, uint32_t bit)
{
	JX_CHECK(bit < bs->m_NumBits, "Invalid bit index");
	bs->m_Bits[bit / 64] |= (1ull << (bit & 63));
}

static void jx_bitsetResetBit(jx_bitset_t* bs, uint32_t bit)
{
	JX_CHECK(bit < bs->m_NumBits, "Invalid bit index");
	bs->m_Bits[bit / 64] &= ~(1ull << (bit & 63));
}

static bool jx_bitsetIsBitSet(const jx_bitset_t* bs, uint32_t bit)
{
	JX_CHECK(bit < bs->m_NumBits, "Invalid bit index");
	return (bs->m_Bits[bit / 64] & (1ull << (bit & 63))) != 0;
}

static bool jx_bitsetUnion(jx_bitset_t* dst, const jx_bitset_t* src)
{
	if (dst->m_NumBits != src->m_NumBits) {
		JX_CHECK(false, "Can only perform bitset operation on identically sized sets.");
		return false;
	}

	const uint32_t numBits = dst->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		dst->m_Bits[iWord] |= src->m_Bits[iWord];
	}

	return true;
}

static bool jx_bitsetIntersection(jx_bitset_t* dst, const jx_bitset_t* src)
{
	if (dst->m_NumBits != src->m_NumBits) {
		JX_CHECK(false, "Can only perform bitset operation on identically sized sets.");
		return false;
	}

	const uint32_t numBits = dst->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		dst->m_Bits[iWord] &= src->m_Bits[iWord];
	}

	return true;
}

static bool jx_bitsetCopy(jx_bitset_t* dst, const jx_bitset_t* src)
{
	if (dst->m_NumBits != src->m_NumBits) {
		JX_CHECK(false, "Can only perform bitset operation on identically sized sets.");
		return false;
	}

	const uint32_t numBits = dst->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		dst->m_Bits[iWord] = src->m_Bits[iWord];
	}

	return true;
}

static bool jx_bitsetSub(jx_bitset_t* dst, const jx_bitset_t* src)
{
	if (dst->m_NumBits != src->m_NumBits) {
		JX_CHECK(false, "Can only perform bitset operation on identically sized sets.");
		return false;
	}

	jx_bitset_iterator_t iter;
	jx_bitsetIterBegin(dst, &iter, 0);
	
	uint32_t nextSetBit = jx_bitsetIterNext(dst, &iter);
	while (nextSetBit != UINT32_MAX) {
		if (jx_bitsetIsBitSet(src, nextSetBit)) {
			jx_bitsetResetBit(dst, nextSetBit);
		}

		nextSetBit = jx_bitsetIterNext(dst, &iter);
	}

	return true;
}

static bool jx_bitsetEqual(const jx_bitset_t* a, const jx_bitset_t* b)
{
	if (a->m_NumBits != b->m_NumBits) {
		JX_CHECK(false, "Can only perform bitset operation on identically sized sets.");
		return false;
	}

	const uint32_t numBits = a->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		if (a->m_Bits[iWord] != b->m_Bits[iWord]) {
			return false;
		}
	}

	return true;
}

static void jx_bitsetClear(jx_bitset_t* bs)
{
	const uint32_t numBits = bs->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	jx_memset(bs->m_Bits, 0, sizeof(uint64_t) * numWords);
}

static void jx_bitsetIterBegin(const jx_bitset_t* bs, jx_bitset_iterator_t* iter, uint32_t firstBit)
{
	JX_CHECK(firstBit < bs->m_NumBits, "Invalid bit index");
	const uint32_t wordID = firstBit / 64;
	const uint32_t bitID = firstBit & 63;
	iter->m_WordID = wordID;

	const uint64_t word = bs->m_Bits[wordID];
	iter->m_Bits = word & (~((1ull << bitID) - 1));
}

static uint32_t jx_bitsetIterNext(const jx_bitset_t* bs, jx_bitset_iterator_t* iter)
{
	const uint32_t numWords = (bs->m_NumBits + 63) / 64;
	while (iter->m_Bits == 0 && iter->m_WordID < numWords - 1) {
		iter->m_WordID++;
		iter->m_Bits = bs->m_Bits[iter->m_WordID];
	}

	const uint64_t word = iter->m_Bits;
	if (word) {
		const uint64_t lsbSetMask = word & ((~word) + 1);
		const uint32_t lsbSetPos = jx_ctntz_u64(word);
		iter->m_Bits ^= lsbSetMask; // Toggle (clear) the least significant set bit
		return iter->m_WordID * 64 + lsbSetPos;
	}

	return UINT32_MAX;
}
