#include "jmir_pass.h"
#include "jmir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/logger.h>
#include <jlib/hashmap.h>
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
			case JMIR_OP_CMP: 
			case JMIR_OP_MOVSS: 
			case JMIR_OP_MOVSD:
			case JMIR_OP_MOVD: 
			case JMIR_OP_MOVQ: {
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
#define JMIR_REGALLOC_MAX_INSTR_DEFS    8
#define JMIR_REGALLOC_MAX_INSTR_USES    8
#define JMIR_REGALLOC_MAX_ITERATIONS    10

typedef struct jmir_graph_node_t jmir_graph_node_t;
typedef struct jmir_mov_instr_t jmir_mov_instr_t;
typedef struct jmir_regalloc_bb_info_t jmir_regalloc_bb_info_t;

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

typedef struct jmir_regalloc_instr_info_t
{
	jx_mir_instruction_t* m_Instr;
	jx_bitset_t* m_LiveOutSet;
	uint32_t m_Defs[JMIR_REGALLOC_MAX_INSTR_DEFS];
	uint32_t m_Uses[JMIR_REGALLOC_MAX_INSTR_USES];
	uint32_t m_NumDefs;
	uint32_t m_NumUses;
} jmir_regalloc_instr_info_t;

typedef struct jmir_regalloc_bb_info_t
{
	jx_mir_basic_block_t* m_BB;
	jmir_regalloc_instr_info_t* m_InstrInfo;
	jmir_regalloc_bb_info_t** m_SuccArr;
	jx_bitset_t* m_LiveInSet;
	jx_bitset_t* m_LiveOutSet;
	uint32_t m_NumInstructions;
	JX_PAD(4);
} jmir_regalloc_bb_info_t;

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
	uint32_t m_ID; // ID of the node; used as the index in all bitsets; must be unique
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

	jx_mir_reg_t* m_HWRegs;
	uint32_t m_NumHWRegs; // K
	jx_mir_reg_class m_RegClass;

	jmir_regalloc_bb_info_t* m_BBInfo;
	uint32_t m_NumBasicBlocks;
	JX_PAD(4);
} jmir_func_pass_regalloc_t;

static void jmir_funcPass_regAllocDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_regAllocRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static bool jmir_regAlloc_init(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_reg_class regClass, const uint32_t* hwRegs, uint32_t numHWRegs);
static void jmir_regAlloc_shutdown(jmir_func_pass_regalloc_t* pass);
static bool jmir_regAlloc_initInfo(jmir_func_pass_regalloc_t* pass, jx_mir_function_t* func);
static void jmir_regAlloc_destroyInfo(jmir_func_pass_regalloc_t* pass);
static bool jmir_regAlloc_initBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jmir_regalloc_bb_info_t* bbInfo, jx_mir_basic_block_t* bb);
static bool jmir_regAlloc_initInstrInfo(jmir_func_pass_regalloc_t* pass, jmir_regalloc_instr_info_t* instrInfo, jx_mir_instruction_t* instr);
static void jmir_regAlloc_buildCFG(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func);
static bool jmir_regAlloc_livenessAnalysis(jmir_func_pass_regalloc_t* pass, jx_mir_function_t* func);
static bool jmir_regAlloc_isMoveInstr(jmir_regalloc_instr_info_t* instrInfo, jx_mir_reg_class regClass);
static jmir_regalloc_bb_info_t* jmir_regAlloc_getBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jx_mir_basic_block_t* bb);
static uint32_t jmir_regAlloc_mapRegToID(jmir_func_pass_regalloc_t* pass, jx_mir_reg_t reg);
static jmir_graph_node_t* jmir_regAlloc_getNode(jmir_func_pass_regalloc_t* pass, uint32_t nodeID);
static jx_mir_reg_t jmir_regAlloc_getHWRegWithColor(jmir_func_pass_regalloc_t* pass, uint32_t color);

static void jmir_regAlloc_makeWorklist(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_simplify(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_coalesce(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_freeze(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_selectSpill(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_assignColors(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_replaceRegs(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_spill(jmir_func_pass_regalloc_t* pass);
static void jmir_regAlloc_replaceInstrRegDef(jx_mir_instruction_t* instr, jx_mir_reg_t vreg, jx_mir_operand_t* newReg);
static void jmir_regAlloc_replaceInstrRegUse(jx_mir_instruction_t* instr, jx_mir_reg_t vreg, jx_mir_operand_t* newReg);

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

static jmir_mov_instr_t* jmir_regAlloc_movAlloc(jmir_func_pass_regalloc_t* pass, jmir_regalloc_instr_info_t* instrInfo);
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

	const uint32_t gpRegIDs[] = {
		JMIR_HWREGID_A,
		JMIR_HWREGID_C,
		JMIR_HWREGID_D,
		JMIR_HWREGID_B,
		JMIR_HWREGID_SI,
		JMIR_HWREGID_DI,  
		JMIR_HWREGID_R8,
		JMIR_HWREGID_R9,
		JMIR_HWREGID_R10,
		JMIR_HWREGID_R11,
		JMIR_HWREGID_R12,
		JMIR_HWREGID_R13,
		JMIR_HWREGID_R14,
		JMIR_HWREGID_R15,
	};

	const uint32_t xmmRegIDs[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
	};

	const uint32_t* regIDs[JMIR_REG_CLASS_COUNT] = {
		[JMIR_REG_CLASS_GP] = gpRegIDs,
		[JMIR_REG_CLASS_XMM] = xmmRegIDs
	};
	const uint32_t numRegIDs[JMIR_REG_CLASS_COUNT] = {
		[JMIR_REG_CLASS_GP] = JX_COUNTOF(gpRegIDs),
		[JMIR_REG_CLASS_XMM] = JX_COUNTOF(xmmRegIDs)
	};

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	for (uint32_t iRegClass = 0; iRegClass < JMIR_REG_CLASS_COUNT; ++iRegClass) {
		if (func->m_NextVirtualRegID[iRegClass] == 0) {
			continue;
		}

		uint32_t iter = 0;
		bool changed = true;
		while (changed && iter < JMIR_REGALLOC_MAX_ITERATIONS) {
			changed = false;

			// Liveness analysis + build
			if (!jmir_regAlloc_init(pass, ctx, func, (jx_mir_reg_class)iRegClass, regIDs[iRegClass], numRegIDs[iRegClass])) {
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
	}

	return false;
}

static bool jmir_regAlloc_init(jmir_func_pass_regalloc_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func, jx_mir_reg_class regClass, const uint32_t* hwRegs, uint32_t numHWRegs)
{
	allocator_api->linearAllocatorReset(pass->m_LinearAllocator);

	jx_memset(pass->m_NodeList, 0, sizeof(jmir_graph_node_t*) * JMIR_NODE_STATE_COUNT);
	jx_memset(pass->m_MoveList, 0, sizeof(jmir_mov_instr_t*) * JMIR_MOV_STATE_COUNT);

	pass->m_HWRegs = (jx_mir_reg_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jx_mir_reg_t) * numHWRegs);
	if (!pass->m_HWRegs) {
		return false;
	}
	for (uint32_t iReg = 0; iReg < numHWRegs; ++iReg) {
		pass->m_HWRegs[iReg] = (jx_mir_reg_t){
			.m_ID = hwRegs[iReg],
			.m_Class = regClass,
			.m_IsVirtual = false
		};
	}
	pass->m_NumHWRegs = numHWRegs;
	pass->m_RegClass = regClass;

	const uint32_t numVRegs = func->m_NextVirtualRegID[regClass]; // Assume all allocated virtual regs are used by the function's code.
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
			jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];

			const uint32_t numInstr = bbInfo->m_NumInstructions;
			for (uint32_t iInstr = 0; iInstr < numInstr; ++iInstr) {
				jmir_regalloc_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

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

	pass->m_BBInfo = (jmir_regalloc_bb_info_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_regalloc_bb_info_t) * numBasicBlocks);
	if (!pass->m_BBInfo) {
		return false;
	}

	jx_memset(pass->m_BBInfo, 0, sizeof(jmir_regalloc_bb_info_t) * numBasicBlocks);
	pass->m_NumBasicBlocks = numBasicBlocks;

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[0];
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
		jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_array_free(bbInfo->m_SuccArr);
	}
}

static bool jmir_regAlloc_initBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jmir_regalloc_bb_info_t* bbInfo, jx_mir_basic_block_t* bb)
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
		bbInfo->m_InstrInfo = (jmir_regalloc_instr_info_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_regalloc_instr_info_t) * numInstr);
		if (!bbInfo->m_InstrInfo) {
			return false;
		}

		jx_memset(bbInfo->m_InstrInfo, 0, sizeof(jmir_regalloc_instr_info_t) * numInstr);

		// Initialize instruction info
		{
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			jmir_regalloc_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[0];
			while (instr) {
				jmir_regAlloc_initInstrInfo(pass, instrInfo, instr);

				++instrInfo;
				instr = instr->m_Next;
			}
		}
	}

	bbInfo->m_NumInstructions = numInstr;

	bbInfo->m_SuccArr = (jmir_regalloc_bb_info_t**)jx_array_create(pass->m_Allocator);
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

static void jmir_regAlloc_instrAddUse(jmir_func_pass_regalloc_t* pass, jmir_regalloc_instr_info_t* instrInfo, jx_mir_reg_t reg)
{
	if (reg.m_Class != pass->m_RegClass) {
		return;
	}

	const uint32_t id = jmir_regAlloc_mapRegToID(pass, reg);
	if (id == UINT32_MAX) {
		return;
	}

	JX_CHECK(instrInfo->m_NumUses + 1 <= JMIR_REGALLOC_MAX_INSTR_USES, "Too many instruction uses");
	instrInfo->m_Uses[instrInfo->m_NumUses++] = id;
}

static void jmir_regAlloc_instrAddDef(jmir_func_pass_regalloc_t* pass, jmir_regalloc_instr_info_t* instrInfo, jx_mir_reg_t reg)
{
	if (reg.m_Class != pass->m_RegClass) {
		return;
	}

	JX_CHECK(jx_mir_regIsValid(reg), "Invalid register ID");
	const uint32_t id = jmir_regAlloc_mapRegToID(pass, reg);
	JX_CHECK(id != UINT32_MAX, "Failed to map register to node index");
	JX_CHECK(instrInfo->m_NumDefs + 1 <= JMIR_REGALLOC_MAX_INSTR_DEFS, "Too many instruction defs");
	instrInfo->m_Defs[instrInfo->m_NumDefs++] = id;
}

static bool jmir_regAlloc_initInstrInfo(jmir_func_pass_regalloc_t* pass, jmir_regalloc_instr_info_t* instrInfo, jx_mir_instruction_t* instr)
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
		jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRRegGP_A);
	} break;
	case JMIR_OP_CMP:
	case JMIR_OP_TEST: 
	case JMIR_OP_COMISS:
	case JMIR_OP_COMISD:
	case JMIR_OP_UCOMISS:
	case JMIR_OP_UCOMISD: {
		for (uint32_t iOperand = 0; iOperand < 2; ++iOperand) {
			jx_mir_operand_t* src = instr->m_Operands[iOperand];
			if (src->m_Kind == JMIR_OPERAND_REGISTER) {
				jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_Reg);
			} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
				jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
				jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
			}
		}
	} break;
	case JMIR_OP_MOV:
	case JMIR_OP_MOVSX:
	case JMIR_OP_MOVZX: 
	case JMIR_OP_MOVSS: 
	case JMIR_OP_MOVSD: 
	case JMIR_OP_MOVD:
	case JMIR_OP_MOVQ:
	case JMIR_OP_MOVAPS:
	case JMIR_OP_MOVAPD:
	case JMIR_OP_CVTSI2SS:
	case JMIR_OP_CVTSI2SD:
	case JMIR_OP_CVTSS2SI:
	case JMIR_OP_CVTSD2SI:
	case JMIR_OP_CVTTSS2SI:
	case JMIR_OP_CVTTSD2SI:
	case JMIR_OP_CVTSD2SS:
	case JMIR_OP_CVTSS2SD: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddDef(pass, instrInfo, dst->u.m_Reg);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_BaseReg);
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_IDIV:
	case JMIR_OP_DIV: {
		jx_mir_operand_t* op = instr->m_Operands[0];

		jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRRegGP_A);
		jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRRegGP_D);

		if (op->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, op->u.m_Reg);
		} else if (op->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, op->u.m_MemRef->m_BaseReg);
			jmir_regAlloc_instrAddUse(pass, instrInfo, op->u.m_MemRef->m_IndexReg);
		}

		jmir_regAlloc_instrAddDef(pass, instrInfo, kMIRRegGP_A);
		jmir_regAlloc_instrAddDef(pass, instrInfo, kMIRRegGP_D);
	} break;
	case JMIR_OP_ADD:
	case JMIR_OP_SUB:
	case JMIR_OP_IMUL:
	case JMIR_OP_XOR:
	case JMIR_OP_AND:
	case JMIR_OP_OR:
	case JMIR_OP_SAR:
	case JMIR_OP_SHR:
	case JMIR_OP_SHL: 
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
	case JMIR_OP_UNPCKHPS:
	case JMIR_OP_UNPCKHPD:
	case JMIR_OP_UNPCKLPS:
	case JMIR_OP_UNPCKLPD:
	case JMIR_OP_XORPS:
	case JMIR_OP_XORPD: 
	case JMIR_OP_PUNPCKLBW:
	case JMIR_OP_PUNPCKLWD:
	case JMIR_OP_PUNPCKLDQ:
	case JMIR_OP_PUNPCKLQDQ:
	case JMIR_OP_PUNPCKHBW:
	case JMIR_OP_PUNPCKHWD:
	case JMIR_OP_PUNPCKHDQ:
	case JMIR_OP_PUNPCKHQDQ: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_Reg); // binary operators use both src and dst operands.
			jmir_regAlloc_instrAddDef(pass, instrInfo, dst->u.m_Reg);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_BaseReg);
			jmir_regAlloc_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_LEA: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
		} else if (src->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			// NOTE: External symbols are RIP based so there is no register to use.
		} else {
			JX_CHECK(false, "lea source operand expected to be a memory ref or a stack object.");
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "lea destination operand expected to be a register.");
		jmir_regAlloc_instrAddDef(pass, instrInfo, dst->u.m_Reg);
	} break;
	case JMIR_OP_CALL: {
		jx_mir_operand_t* funcOp = instr->m_Operands[0];
		if (funcOp->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, funcOp->u.m_Reg);
		} else {
			JX_CHECK(funcOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL, "TODO: Handle call [memRef]/[stack object]?");
		}

		jx_mir_annotation_func_proto_t* funcProtoAnnotation = (jx_mir_annotation_func_proto_t*)jx_mir_instrGetAnnotation(pass->m_Ctx, instrInfo->m_Instr, JMIR_ANNOT_INSTR_CALL_FUNC_PROTO);
		if (!funcProtoAnnotation) {
			// No function prototype specified for this call. Assume all func arg regs are used.
			if (pass->m_RegClass == JMIR_REG_CLASS_GP) {
				for (uint32_t iRegArg = 0; iRegArg < JX_COUNTOF(kMIRFuncArgIReg); ++iRegArg) {
					jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRFuncArgIReg[iRegArg]);
				}
			} else if (pass->m_RegClass == JMIR_REG_CLASS_XMM) {
				for (uint32_t iRegArg = 0; iRegArg < JX_COUNTOF(kMIRFuncArgFReg); ++iRegArg) {
					jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRFuncArgFReg[iRegArg]);
				}
			} else {
				JX_CHECK(false, "Unknown register class");
			}
		} else {
			jx_mir_function_proto_t* funcProto = funcProtoAnnotation->m_FuncProto;
			const uint32_t numArgs = jx_min_u32(funcProto->m_NumArgs, JX_COUNTOF(kMIRFuncArgIReg));
			for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
				jx_mir_reg_class argClass = jx_mir_typeGetClass(funcProto->m_Args[iArg]);
				if (argClass == JMIR_REG_CLASS_GP) {
					jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRFuncArgIReg[iArg]);
				} else if (argClass== JMIR_REG_CLASS_XMM) {
					jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRFuncArgFReg[iArg]);
				} else {
					JX_CHECK(false, "Unknown register class");
				}
			}
		}

		if (pass->m_RegClass == JMIR_REG_CLASS_GP) {
			const uint32_t numCallerSavedRegs = JX_COUNTOF(kMIRFuncCallerSavedIReg);
			for (uint32_t iReg = 0; iReg < numCallerSavedRegs; ++iReg) {
				jmir_regAlloc_instrAddDef(pass, instrInfo, kMIRFuncCallerSavedIReg[iReg]);
			}
		} else if (pass->m_RegClass == JMIR_REG_CLASS_XMM) {
			const uint32_t numCallerSavedRegs = JX_COUNTOF(kMIRFuncCallerSavedFReg);
			for (uint32_t iReg = 0; iReg < numCallerSavedRegs; ++iReg) {
				jmir_regAlloc_instrAddDef(pass, instrInfo, kMIRFuncCallerSavedFReg[iReg]);
			}
		} else {
			JX_CHECK(false, "Unknown register class");
		}

		// No need to specify RAX as def because it's a caller-saved register and it's
		// already added to the def list.
	} break;
	case JMIR_OP_PUSH: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_POP: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OP_CDQ:
	case JMIR_OP_CQO: {
		jmir_regAlloc_instrAddUse(pass, instrInfo, kMIRRegGP_A);
		jmir_regAlloc_instrAddDef(pass, instrInfo, kMIRRegGP_D);
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
			jmir_regAlloc_instrAddDef(pass, instrInfo, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_regAlloc_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
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
		jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_array_resize(bbInfo->m_SuccArr, 0);
	}

	// Rebuild CFG
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
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
					jmir_regalloc_bb_info_t* targetBBInfo = jmir_regAlloc_getBasicBlockInfo(pass, targetBB);

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

			jmir_regalloc_bb_info_t* nextBBInfo = jmir_regAlloc_getBasicBlockInfo(pass, bb->m_Next);

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
		jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
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
			jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];

			// out'[v] = out[v]
			// in'[v] = in[v]
			jx_bitsetCopy(prevLiveIn, bbInfo->m_LiveInSet);
			jx_bitsetCopy(prevLiveOut, bbInfo->m_LiveOutSet);

			// out[v] = Union(w in succ, in[w])
			const uint32_t numSucc = (uint32_t)jx_array_sizeu(bbInfo->m_SuccArr);
			if (numSucc) {
				jmir_regalloc_bb_info_t* succBBInfo = bbInfo->m_SuccArr[0];

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
					jmir_regalloc_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

					if (jmir_regAlloc_isMoveInstr(instrInfo, pass->m_RegClass)) {
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

static bool jmir_regAlloc_isMoveInstr(jmir_regalloc_instr_info_t* instrInfo, jx_mir_reg_class regClass)
{
	jx_mir_instruction_t* instr = instrInfo->m_Instr;

	const bool isMov = false
		|| instr->m_OpCode == JMIR_OP_MOV
		|| instr->m_OpCode == JMIR_OP_MOVSX
		|| instr->m_OpCode == JMIR_OP_MOVZX
		|| instr->m_OpCode == JMIR_OP_MOVSS
		|| instr->m_OpCode == JMIR_OP_MOVSD
		;
	if (!isMov) {
		return false;
	}

	jx_mir_operand_t* dst = instr->m_Operands[0];
	jx_mir_operand_t* src = instr->m_Operands[1];
	return dst->m_Kind == JMIR_OPERAND_REGISTER
		&& src->m_Kind == JMIR_OPERAND_REGISTER
		&& jx_mir_regIsClass(dst->u.m_Reg, regClass)
		&& jx_mir_regIsClass(src->u.m_Reg, regClass)
		;
}

static jmir_regalloc_bb_info_t* jmir_regAlloc_getBasicBlockInfo(jmir_func_pass_regalloc_t* pass, jx_mir_basic_block_t* bb)
{
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		if (pass->m_BBInfo[iBB].m_BB == bb) {
			return &pass->m_BBInfo[iBB];
		}
	}

	return NULL;
}

static uint32_t jmir_regAlloc_mapRegToID(jmir_func_pass_regalloc_t* pass, jx_mir_reg_t reg)
{
	uint32_t id = UINT32_MAX;
	if (!jx_mir_regIsValid(reg)) {
		// No op
	} else if (jx_mir_regIsVirtual(reg)) {
		id = reg.m_ID + pass->m_NumHWRegs;
		JX_CHECK(id < pass->m_NumNodes, "Invalid virtual register node index!");
	} else {
		// Must be a hardware register. Find index in hw regs array.
		const uint32_t numHWRegs = pass->m_NumHWRegs;
		for (uint32_t iHWReg = 0; iHWReg < numHWRegs; ++iHWReg) {
			if (jx_mir_regEqual(pass->m_HWRegs[iHWReg], reg)) {
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

static jx_mir_reg_t jmir_regAlloc_getHWRegWithColor(jmir_func_pass_regalloc_t* pass, uint32_t color)
{
	const uint32_t numHWRegs = pass->m_NumHWRegs;
	for (uint32_t iHWReg = 0; iHWReg < numHWRegs; ++iHWReg) {
		jmir_graph_node_t* node = &pass->m_Nodes[iHWReg];
		if (node->m_Color == color) {
			JX_CHECK(jmir_nodeIs(node, JMIR_NODE_STATE_PRECOLORED), "Expected precolored hw register!");
			return pass->m_HWRegs[iHWReg];
		}
	}

	return kMIRRegGPNone;
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
		jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];

		const uint32_t numInstr = bbInfo->m_NumInstructions;
		for (uint32_t iInstr = 0; iInstr < numInstr; ++iInstr) {
			jmir_regalloc_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

			jx_mir_instruction_t* instr = instrInfo->m_Instr;
			const uint32_t numOperands = instr->m_NumOperands;
			for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
				jx_mir_operand_t* operand = instr->m_Operands[iOperand];
				if (operand->m_Kind == JMIR_OPERAND_REGISTER && jx_mir_regIsVirtual(operand->u.m_Reg) && jx_mir_regIsClass(operand->u.m_Reg, pass->m_RegClass)) {
					jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jmir_regAlloc_mapRegToID(pass, operand->u.m_Reg));
					if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
						jx_mir_reg_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_Color);
						if (jx_mir_regIsValid(hwReg)) {
							JX_CHECK(jx_mir_regIsHW(hwReg), "Expected hw register.");
							pass->m_Func->m_UsedHWRegs[hwReg.m_Class] |= 1u << (uint32_t)hwReg.m_ID;
							operand->u.m_Reg = hwReg;
						}
					}
				} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
					jx_mir_memory_ref_t* memRef = operand->u.m_MemRef;

					if (jx_mir_regIsValid(memRef->m_BaseReg) && jx_mir_regIsVirtual(memRef->m_BaseReg) && jx_mir_regIsClass(memRef->m_BaseReg, pass->m_RegClass)) {
						jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jmir_regAlloc_mapRegToID(pass, memRef->m_BaseReg));
						if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
							jx_mir_reg_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_Color);
							if (jx_mir_regIsValid(hwReg)) {
								JX_CHECK(jx_mir_regIsHW(hwReg), "Expected hw register.");
								pass->m_Func->m_UsedHWRegs[hwReg.m_Class] |= 1u << (uint32_t)hwReg.m_ID;
								memRef->m_BaseReg = hwReg;
							}
						}
					}

					if (jx_mir_regIsValid(memRef->m_IndexReg) && jx_mir_regIsVirtual(memRef->m_IndexReg) && jx_mir_regIsClass(memRef->m_IndexReg, pass->m_RegClass)) {
						jmir_graph_node_t* node = jmir_regAlloc_getNode(pass, jmir_regAlloc_mapRegToID(pass, memRef->m_IndexReg));
						if (jmir_nodeIs(node, JMIR_NODE_STATE_COLORED)) {
							jx_mir_reg_t hwReg = jmir_regAlloc_getHWRegWithColor(pass, node->m_Color);
							if (jx_mir_regIsValid(hwReg)) {
								JX_CHECK(jx_mir_regIsHW(hwReg), "Expected hw register.");
								pass->m_Func->m_UsedHWRegs[hwReg.m_Class] |= 1u << (uint32_t)hwReg.m_ID;
								memRef->m_IndexReg = hwReg;
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
		JX_CHECK(node->m_ID >= pass->m_NumHWRegs, "Expected a virtual register!");

		const uint32_t nodeID = node->m_ID;
		const jx_mir_reg_t vreg = {
			.m_ID = nodeID - pass->m_NumHWRegs,
			.m_Class = pass->m_RegClass,
			.m_IsVirtual = true
		};

		jx_mir_operand_t* stackSlot = NULL;
		jx_mir_operand_t* temp = NULL;

		// Walk the whole function and rewrite every definition as a store to stack
		// and every use as a load from stack.
		const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
		for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
			jmir_regalloc_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
			jx_mir_basic_block_t* bb = bbInfo->m_BB;

			const uint32_t numInstructions = bbInfo->m_NumInstructions;
			for (uint32_t iInstr = 0; iInstr < numInstructions; ++iInstr) {
				jmir_regalloc_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];
				jx_mir_instruction_t* instr = instrInfo->m_Instr;

				bool use = false;
				bool def = false;

				const uint32_t numUses = instrInfo->m_NumUses;
				for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
					if (instrInfo->m_Uses[iUse] == nodeID) {
						use = true;
						break;
					}
				}

				const uint32_t numDefs = instrInfo->m_NumDefs;
				for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
					if (instrInfo->m_Defs[iDef] == nodeID) {
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
							if (jx_mir_regEqual(operand->u.m_Reg, vreg)) {
								regType = operand->m_Type;
								break;
							}
						} else if (operand->m_Kind == JMIR_OPERAND_MEMORY_REF) {
							if (jx_mir_regEqual(operand->u.m_MemRef->m_BaseReg, vreg) || jx_mir_regEqual(operand->u.m_MemRef->m_IndexReg, vreg)) {
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
					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movss(ctx, temp, stackSlot));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movsd(ctx, temp, stackSlot));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movaps(ctx, temp, stackSlot));
					} else {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, temp, stackSlot));
					}

					jmir_regAlloc_replaceInstrRegUse(instr, vreg, temp);

					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movss(ctx, stackSlot, temp));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movsd(ctx, stackSlot, temp));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movaps(ctx, stackSlot, temp));
					} else {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_mov(ctx, stackSlot, temp));
					}
				} else if (use) {
					// Load from stack into new temporary
					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movss(ctx, temp, stackSlot));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movsd(ctx, temp, stackSlot));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_movaps(ctx, temp, stackSlot));
					} else {
						jx_mir_bbInsertInstrBefore(ctx, bb, instr, jx_mir_mov(ctx, temp, stackSlot));
					}

					jmir_regAlloc_replaceInstrRegUse(instr, vreg, temp);
				} else if (def) {
					jmir_regAlloc_replaceInstrRegDef(instr, vreg, temp);

					if (temp->m_Type == JMIR_TYPE_F32) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movss(ctx, stackSlot, temp));
					} else if (temp->m_Type == JMIR_TYPE_F64) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movsd(ctx, stackSlot, temp));
					} else if (temp->m_Type == JMIR_TYPE_F128) {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_movaps(ctx, stackSlot, temp));
					} else {
						jx_mir_bbInsertInstrAfter(ctx, bb, instr, jx_mir_mov(ctx, stackSlot, temp));
					}
				}
			}
		}

		node = node->m_Next;
	}
}

static void jmir_regAlloc_replaceInstrRegDef(jx_mir_instruction_t* instr, jx_mir_reg_t vreg, jx_mir_operand_t* newReg)
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
		JX_CHECK(jx_mir_regEqual(dst->u.m_Reg, vreg), "Expected a different virtual register!");
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

static void jmir_regAlloc_replaceInstrRegUse(jx_mir_instruction_t* instr, jx_mir_reg_t vreg, jx_mir_operand_t* newReg)
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
	case JMIR_OP_MOV:
	case JMIR_OP_MOVSX:
	case JMIR_OP_MOVZX: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		JX_CHECK(src->m_Kind == JMIR_OPERAND_REGISTER, "Expected register operand");
		JX_CHECK(jx_mir_regEqual(src->u.m_Reg, vreg), "Wrong virtual register!");
		instr->m_Operands[1] = newReg;
	} break;
	case JMIR_OP_CMP:
	case JMIR_OP_IMUL:
	case JMIR_OP_ADD:
	case JMIR_OP_SUB: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			if (jx_mir_regEqual(src->u.m_Reg, vreg)) {
				instr->m_Operands[1] = newReg;
			}
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			JX_NOT_IMPLEMENTED();
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			if (jx_mir_regEqual(dst->u.m_Reg, vreg)) {
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
			JX_CHECK(jmir_nodeIs(t, JMIR_NODE_STATE_FREEZE), "Node expected to be in freeze state.");
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

static bool jmir_regAlloc_nodeInit(jmir_func_pass_regalloc_t* pass, jmir_graph_node_t* node, uint32_t id, jmir_graph_node_state initialState)
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
		JX_CHECK(id < pass->m_NumHWRegs, "Trying to precolor a virtual register?");
		node->m_AdjacentArr = NULL;
		node->m_Color = id;
	} else {
		node->m_AdjacentArr = (jmir_graph_node_t**)jx_array_create(pass->m_Allocator);
		if (!node->m_AdjacentArr) {
			return false;
		}

		node->m_Color = UINT32_MAX;
	}

	node->m_ID = id;
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

static jmir_mov_instr_t* jmir_regAlloc_movAlloc(jmir_func_pass_regalloc_t* pass, jmir_regalloc_instr_info_t* instrInfo)
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
// Peephole optimizations
//
static void jmir_funcPass_peepholeDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_peepholeRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static bool jmir_peephole_isFloatConst(jx_mir_operand_t* op, double val);

bool jx_mir_funcPassCreate_peephole(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	pass->m_Inst = NULL;
	pass->run = jmir_funcPass_peepholeRun;
	pass->destroy = jmir_funcPass_peepholeDestroy;

	return true;
}

static void jmir_funcPass_peepholeDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
}

static bool jmir_funcPass_peepholeRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	bool changed = true;

	while (changed) {
		changed = false;

		jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_mir_instruction_t* instrNext = instr->m_Next;

				if (instr->m_OpCode == JMIR_OP_MOVSS && jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
					// movss xmm, 0.0
					//  =>
					// xorps xmm, xmm
					jx_mir_instruction_t* xorInstr = jx_mir_xorps(ctx, instr->m_Operands[0], instr->m_Operands[0]);
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, xorInstr);
					jx_mir_bbRemoveInstr(ctx, bb, instr);
					jx_mir_instrFree(ctx, instr);

					changed = true;
				} else if (instr->m_OpCode == JMIR_OP_MOVSD && jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
					JX_CHECK(false, "Implement like the movss above.");
				} else if ((instr->m_OpCode == JMIR_OP_UCOMISS || instr->m_OpCode == JMIR_OP_COMISS) && jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
					// ucomiss xmm, 0.0
					//  => 
					// xorps xmmtmp, xmmtmp
					// ucomiss xmm, xmmtmp
					jx_mir_operand_t* tmp = jx_mir_opVirtualReg(ctx, func, JMIR_TYPE_F128);
					jx_mir_instruction_t* xorInstr = jx_mir_xorps(ctx, tmp, tmp);
					jx_mir_bbInsertInstrBefore(ctx, bb, instr, xorInstr);
					instr->m_Operands[1] = tmp;

					changed = true;
				} else if ((instr->m_OpCode == JMIR_OP_UCOMISD || instr->m_OpCode == JMIR_OP_COMISD) && jmir_peephole_isFloatConst(instr->m_Operands[1], 0.0)) {
					JX_CHECK(false, "Implement like the ucomiss/comiss above.");
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}
	}

	return false;
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
				} else 
				if (addrOp->m_Kind == JMIR_OPERAND_MEMORY_REF && !jx_mir_regIsValid(addrOp->u.m_MemRef->m_IndexReg)) {
					jx_mir_operand_t* newOperand = jmir_combineLEAs_replaceMemRef(pass, addrOp->m_Type, addrOp->u.m_MemRef);
					if (newOperand) {
						instr->m_Operands[1] = newOperand;
						jx_hashmapSet(pass->m_ValueMap, &(jmir_reg_value_item_t){.m_Reg = ptrOp->u.m_Reg, .m_Value = newOperand });
					} else {
						jx_hashmapDelete(pass->m_ValueMap, &(jmir_reg_value_item_t){.m_Reg = ptrOp->u.m_Reg });
					}
				} else if (addrOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
//					JX_NOT_IMPLEMENTED();
					jx_hashmapDelete(pass->m_ValueMap, &(jmir_reg_value_item_t){.m_Reg = ptrOp->u.m_Reg });
				} else {
					jx_hashmapDelete(pass->m_ValueMap, &(jmir_reg_value_item_t){.m_Reg = ptrOp->u.m_Reg });
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
// Dead Code Elimination
// 
// The code is very similar (but not identical) to the register allocator's liveliness
// analysis part. The main difference is that it handles all register classes in one pass
// instead of one per register class. TODO: Find a way to separate common parts into
// standalone code which will calculate liveliness analysis for a given function.
// 
// The code iteratively (fixed-point) calculates the liveliness of each register at each
// instruction. Once liveliness is calculated, it loops over all instructions. For each
// instruction definition, if it's a virtual register which is not live out of the instruction
// the definition is considered dead. If all instruction definitions are dead the instruction
// is removed.
//
#define JMIR_DCE_MAX_INSTR_DEFS 16 // NOTE: Large enough to hold all GP and XMM caller saved regs
#define JMIR_DCE_MAX_INSTR_USES 16 // NOTE: Large enough to hold all GP and XMM func arg regs + called func in case it's a register.

typedef struct jmir_dce_bb_info_t jmir_dce_bb_info_t;
typedef struct jmir_dce_instr_info_t jmir_dce_instr_info_t;

typedef struct jmir_dce_instr_info_t
{
	jx_mir_instruction_t* m_Instr;
	jx_bitset_t* m_LiveOutSet;
	jx_mir_reg_t m_Defs[JMIR_DCE_MAX_INSTR_DEFS];
	jx_mir_reg_t m_Uses[JMIR_DCE_MAX_INSTR_USES];
	uint32_t m_NumDefs;
	uint32_t m_NumUses;
} jmir_dce_instr_info_t;

typedef struct jmir_dce_bb_info_t
{
	jx_mir_basic_block_t* m_BB;
	jmir_dce_instr_info_t* m_InstrInfo;
	jmir_dce_bb_info_t** m_SuccArr;
	jx_bitset_t* m_LiveInSet;
	jx_bitset_t* m_LiveOutSet;
	uint32_t m_NumInstructions;
	JX_PAD(4);
} jmir_dce_bb_info_t;

typedef struct jmir_func_pass_dce_t
{
	jx_allocator_i* m_Allocator;
	jx_allocator_i* m_LinearAllocator;
	jx_mir_context_t* m_Ctx;
	jx_mir_function_t* m_Func;
	jmir_dce_bb_info_t* m_BBInfo;
	uint32_t m_NumBasicBlocks;
	uint32_t m_NumRegs;
} jmir_func_pass_dce_t;

static void jmir_funcPass_dceDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jmir_funcPass_dceRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func);

static bool jmir_dce_initInfo(jmir_func_pass_dce_t* pass, jx_mir_function_t* func);
static void jmir_dce_destroyInfo(jmir_func_pass_dce_t* pass);
static bool jmir_dce_initBasicBlockInfo(jmir_func_pass_dce_t* pass, jmir_dce_bb_info_t* bbInfo, jx_mir_basic_block_t* bb);
static void jmir_dce_instrAddUse(jmir_func_pass_dce_t* pass, jmir_dce_instr_info_t* instrInfo, jx_mir_reg_t reg);
static void jmir_dce_instrAddDef(jmir_func_pass_dce_t* pass, jmir_dce_instr_info_t* instrInfo, jx_mir_reg_t reg);
static bool jmir_dce_initInstrInfo(jmir_func_pass_dce_t* pass, jmir_dce_instr_info_t* instrInfo, jx_mir_instruction_t* instr);
static void jmir_dce_buildCFG(jmir_func_pass_dce_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func);
static bool jmir_dce_livenessAnalysis(jmir_func_pass_dce_t* pass, jx_mir_function_t* func);
static jmir_dce_bb_info_t* jmir_dce_getBasicBlockInfo(jmir_func_pass_dce_t* pass, jx_mir_basic_block_t* bb);
static uint32_t jmir_dce_mapRegToID(jmir_func_pass_dce_t* pass, jx_mir_reg_t reg);
static bool jmir_dce_isMoveInstr(jmir_dce_instr_info_t* instrInfo);

bool jx_mir_funcPassCreate_deadCodeElimination(jx_mir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jmir_func_pass_dce_t* inst = (jmir_func_pass_dce_t*)JX_ALLOC(allocator, sizeof(jmir_func_pass_dce_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jmir_func_pass_dce_t));
	inst->m_Allocator = allocator;
	inst->m_LinearAllocator = allocator_api->createLinearAllocator(1 << 20, allocator);
	if (!inst->m_LinearAllocator) {
		jmir_funcPass_dceDestroy((jx_mir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_mir_function_pass_o*)inst;
	pass->run = jmir_funcPass_dceRun;
	pass->destroy = jmir_funcPass_dceDestroy;

	return true;
}

static void jmir_funcPass_dceDestroy(jx_mir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jmir_func_pass_dce_t* pass = (jmir_func_pass_dce_t*)inst;

	if (pass->m_LinearAllocator) {
		allocator_api->destroyLinearAllocator(pass->m_LinearAllocator);
		pass->m_LinearAllocator = NULL;
	}

	JX_FREE(pass->m_Allocator, pass);
}

static bool jmir_funcPass_dceRun(jx_mir_function_pass_o* inst, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	jmir_func_pass_dce_t* pass = (jmir_func_pass_dce_t*)inst;

	pass->m_Ctx = ctx;
	pass->m_Func = func;
	pass->m_NumRegs = 0
		+ 16 // GP regs
		+ 16 // XMM regs
		+ func->m_NextVirtualRegID[JMIR_REG_CLASS_GP]
		+ func->m_NextVirtualRegID[JMIR_REG_CLASS_XMM]
		;

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(pass->m_Allocator);
		jx_mir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s\n", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif

	bool changed = true;
	while (changed) {
		changed = false;

		if (!jmir_dce_initInfo(pass, func)) {
			return false;
		}

		jmir_dce_buildCFG(pass, ctx, func);

		if (!jmir_dce_livenessAnalysis(pass, func)) {
			return false;
		}

		// Remove dead instructions.
		const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
		for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
			jmir_dce_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];

			const uint32_t numInstructions = bbInfo->m_NumInstructions;
			for (uint32_t iInstr = 0; iInstr < numInstructions; ++iInstr) {
				jmir_dce_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

				const uint32_t numDefs = instrInfo->m_NumDefs;
				if (numDefs) {
					uint32_t numDeadDefs = 0;
					for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
						jx_mir_reg_t def = instrInfo->m_Defs[iDef];
						if (jx_mir_regIsVirtual(def) && !jx_bitsetIsBitSet(instrInfo->m_LiveOutSet, jmir_dce_mapRegToID(pass, def))) {
							++numDeadDefs;
						}
					}

					// TODO: Do I need to check if instruction has side-effects? 
					// Memory stores or comparisons do not have any defs so they are ignored 
					// by this code. What other instruction can have a definition and side-effects?
					if (numDefs == numDeadDefs) {
						jx_mir_bbRemoveInstr(ctx, bbInfo->m_BB, instrInfo->m_Instr);
						jx_mir_instrFree(ctx, instrInfo->m_Instr);
						changed = true;
					}
				}
			}
		}

		jmir_dce_destroyInfo(pass);
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

static bool jmir_dce_initInfo(jmir_func_pass_dce_t* pass, jx_mir_function_t* func)
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

	pass->m_BBInfo = (jmir_dce_bb_info_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_dce_bb_info_t) * numBasicBlocks);
	if (!pass->m_BBInfo) {
		return false;
	}

	jx_memset(pass->m_BBInfo, 0, sizeof(jmir_dce_bb_info_t) * numBasicBlocks);
	pass->m_NumBasicBlocks = numBasicBlocks;

	jx_mir_basic_block_t* bb = func->m_BasicBlockListHead;
	jmir_dce_bb_info_t* bbInfo = &pass->m_BBInfo[0];
	while (bb) {
		if (!jmir_dce_initBasicBlockInfo(pass, bbInfo, bb)) {
			return false;
		}

		++bbInfo;
		bb = bb->m_Next;
	}

	return true;
}

static void jmir_dce_destroyInfo(jmir_func_pass_dce_t* pass)
{
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_dce_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_array_free(bbInfo->m_SuccArr);
	}
}

static bool jmir_dce_initBasicBlockInfo(jmir_func_pass_dce_t* pass, jmir_dce_bb_info_t* bbInfo, jx_mir_basic_block_t* bb)
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
		bbInfo->m_InstrInfo = (jmir_dce_instr_info_t*)JX_ALLOC(pass->m_LinearAllocator, sizeof(jmir_dce_instr_info_t) * numInstr);
		if (!bbInfo->m_InstrInfo) {
			return false;
		}

		jx_memset(bbInfo->m_InstrInfo, 0, sizeof(jmir_dce_instr_info_t) * numInstr);

		// Initialize instruction info
		{
			jx_mir_instruction_t* instr = bb->m_InstrListHead;
			jmir_dce_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[0];
			while (instr) {
				jmir_dce_initInstrInfo(pass, instrInfo, instr);

				++instrInfo;
				instr = instr->m_Next;
			}
		}
	}

	bbInfo->m_NumInstructions = numInstr;

	bbInfo->m_SuccArr = (jmir_dce_bb_info_t**)jx_array_create(pass->m_Allocator);
	if (!bbInfo->m_SuccArr) {
		return false;
	}

	bbInfo->m_LiveInSet = jx_bitsetCreate(pass->m_NumRegs, pass->m_LinearAllocator);
	if (!bbInfo->m_LiveInSet) {
		return false;
	}

	bbInfo->m_LiveOutSet = jx_bitsetCreate(pass->m_NumRegs, pass->m_LinearAllocator);
	if (!bbInfo->m_LiveOutSet) {
		return false;
	}

	return true;
}

static void jmir_dce_instrAddUse(jmir_func_pass_dce_t* pass, jmir_dce_instr_info_t* instrInfo, jx_mir_reg_t reg)
{
	if (jx_mir_regIsValid(reg)) {
		JX_CHECK(instrInfo->m_NumUses + 1 <= JMIR_DCE_MAX_INSTR_USES, "Too many instruction uses");
		instrInfo->m_Uses[instrInfo->m_NumUses++] = reg;
	}
}

static void jmir_dce_instrAddDef(jmir_func_pass_dce_t* pass, jmir_dce_instr_info_t* instrInfo, jx_mir_reg_t reg)
{
	JX_CHECK(jx_mir_regIsValid(reg), "Invalid register ID");
	JX_CHECK(instrInfo->m_NumDefs + 1 <= JMIR_DCE_MAX_INSTR_DEFS, "Too many instruction defs");
	instrInfo->m_Defs[instrInfo->m_NumDefs++] = reg;
}

static bool jmir_dce_initInstrInfo(jmir_func_pass_dce_t* pass, jmir_dce_instr_info_t* instrInfo, jx_mir_instruction_t* instr)
{
	instrInfo->m_Instr = instr;
	instrInfo->m_LiveOutSet = jx_bitsetCreate(pass->m_NumRegs, pass->m_LinearAllocator);
	if (!instrInfo->m_LiveOutSet) {
		return false;
	}

	instrInfo->m_NumDefs = 0;
	instrInfo->m_NumUses = 0;
	switch (instr->m_OpCode) {
	case JMIR_OP_RET: {
		// ret implicitly uses RAX?
		jmir_dce_instrAddUse(pass, instrInfo, kMIRRegGP_A);
	} break;
	case JMIR_OP_CMP:
	case JMIR_OP_TEST: 
	case JMIR_OP_COMISS:
	case JMIR_OP_COMISD:
	case JMIR_OP_UCOMISS:
	case JMIR_OP_UCOMISD: {
		for (uint32_t iOperand = 0; iOperand < 2; ++iOperand) {
			jx_mir_operand_t* src = instr->m_Operands[iOperand];
			if (src->m_Kind == JMIR_OPERAND_REGISTER) {
				jmir_dce_instrAddUse(pass, instrInfo, src->u.m_Reg);
			} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
				jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
				jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
			}
		}
	} break;
	case JMIR_OP_MOV:
	case JMIR_OP_MOVSX:
	case JMIR_OP_MOVZX: 
	case JMIR_OP_MOVSS: 
	case JMIR_OP_MOVSD: 
	case JMIR_OP_MOVD:
	case JMIR_OP_MOVQ:
	case JMIR_OP_MOVAPS:
	case JMIR_OP_MOVAPD:
	case JMIR_OP_CVTSI2SS:
	case JMIR_OP_CVTSI2SD:
	case JMIR_OP_CVTSS2SI:
	case JMIR_OP_CVTSD2SI:
	case JMIR_OP_CVTTSS2SI:
	case JMIR_OP_CVTTSD2SI:
	case JMIR_OP_CVTSD2SS:
	case JMIR_OP_CVTSS2SD: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_dce_instrAddDef(pass, instrInfo, dst->u.m_Reg);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_dce_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_BaseReg);
			jmir_dce_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_IDIV:
	case JMIR_OP_DIV: {
		jx_mir_operand_t* op = instr->m_Operands[0];

		jmir_dce_instrAddUse(pass, instrInfo, kMIRRegGP_A);
		jmir_dce_instrAddUse(pass, instrInfo, kMIRRegGP_D);

		if (op->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_dce_instrAddUse(pass, instrInfo, op->u.m_Reg);
		} else if (op->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_dce_instrAddUse(pass, instrInfo, op->u.m_MemRef->m_BaseReg);
			jmir_dce_instrAddUse(pass, instrInfo, op->u.m_MemRef->m_IndexReg);
		}

		jmir_dce_instrAddDef(pass, instrInfo, kMIRRegGP_A);
		jmir_dce_instrAddDef(pass, instrInfo, kMIRRegGP_D);
	} break;
	case JMIR_OP_ADD:
	case JMIR_OP_SUB:
	case JMIR_OP_IMUL:
	case JMIR_OP_XOR:
	case JMIR_OP_AND:
	case JMIR_OP_OR:
	case JMIR_OP_SAR:
	case JMIR_OP_SHR:
	case JMIR_OP_SHL: 
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
	case JMIR_OP_UNPCKHPS:
	case JMIR_OP_UNPCKHPD:
	case JMIR_OP_UNPCKLPS:
	case JMIR_OP_UNPCKLPD:
	case JMIR_OP_XORPS:
	case JMIR_OP_XORPD: 
	case JMIR_OP_PUNPCKLBW:
	case JMIR_OP_PUNPCKLWD:
	case JMIR_OP_PUNPCKLDQ:
	case JMIR_OP_PUNPCKLQDQ:
	case JMIR_OP_PUNPCKHBW:
	case JMIR_OP_PUNPCKHWD:
	case JMIR_OP_PUNPCKHDQ:
	case JMIR_OP_PUNPCKHQDQ: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		if (dst->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_dce_instrAddUse(pass, instrInfo, dst->u.m_Reg); // binary operators use both src and dst operands.
			jmir_dce_instrAddDef(pass, instrInfo, dst->u.m_Reg);
		} else if (dst->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_dce_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_BaseReg);
			jmir_dce_instrAddUse(pass, instrInfo, dst->u.m_MemRef->m_IndexReg);
		}
	} break;
	case JMIR_OP_LEA: {
		jx_mir_operand_t* src = instr->m_Operands[1];
		if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
		} else if (src->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL) {
			// NOTE: External symbols are RIP based so there is no register to use.
		} else {
			JX_CHECK(false, "lea source operand expected to be a memory ref or a stack object.");
		}

		jx_mir_operand_t* dst = instr->m_Operands[0];
		JX_CHECK(dst->m_Kind == JMIR_OPERAND_REGISTER, "lea destination operand expected to be a register.");
		jmir_dce_instrAddDef(pass, instrInfo, dst->u.m_Reg);
	} break;
	case JMIR_OP_CALL: {
		jx_mir_operand_t* funcOp = instr->m_Operands[0];
		if (funcOp->m_Kind == JMIR_OPERAND_REGISTER) {
			jmir_dce_instrAddUse(pass, instrInfo, funcOp->u.m_Reg);
		} else {
			JX_CHECK(funcOp->m_Kind == JMIR_OPERAND_EXTERNAL_SYMBOL, "TODO: Handle call [memRef]/[stack object]?");
		}

		jx_mir_annotation_func_proto_t* funcProtoAnnot = (jx_mir_annotation_func_proto_t*)jx_mir_instrGetAnnotation(pass->m_Ctx, instrInfo->m_Instr, JMIR_ANNOT_INSTR_CALL_FUNC_PROTO);
		if (!funcProtoAnnot) {
			for (uint32_t iRegArg = 0; iRegArg < JX_COUNTOF(kMIRFuncArgIReg); ++iRegArg) {
				jmir_dce_instrAddUse(pass, instrInfo, kMIRFuncArgIReg[iRegArg]);
			}
			for (uint32_t iRegArg = 0; iRegArg < JX_COUNTOF(kMIRFuncArgFReg); ++iRegArg) {
				jmir_dce_instrAddUse(pass, instrInfo, kMIRFuncArgFReg[iRegArg]);
			}
		} else {
			jx_mir_function_proto_t* funcProto = funcProtoAnnot->m_FuncProto;
			const uint32_t numArgs = jx_min_u32(funcProto->m_NumArgs, JX_COUNTOF(kMIRFuncArgIReg));
			for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
				jx_mir_reg_class argClass = jx_mir_typeGetClass(funcProto->m_Args[iArg]);
				if (argClass == JMIR_REG_CLASS_GP) {
					jmir_dce_instrAddUse(pass, instrInfo, kMIRFuncArgIReg[iArg]);
				} else if (argClass == JMIR_REG_CLASS_XMM) {
					jmir_dce_instrAddUse(pass, instrInfo, kMIRFuncArgFReg[iArg]);
				} else {
					JX_CHECK(false, "Unknown register class");
				}
			}
		}

		{
			const uint32_t numCallerSavedIRegs = JX_COUNTOF(kMIRFuncCallerSavedIReg);
			for (uint32_t iReg = 0; iReg < numCallerSavedIRegs; ++iReg) {
				jmir_dce_instrAddDef(pass, instrInfo, kMIRFuncCallerSavedIReg[iReg]);
			}

			const uint32_t numCallerSavedFRegs = JX_COUNTOF(kMIRFuncCallerSavedFReg);
			for (uint32_t iReg = 0; iReg < numCallerSavedFRegs; ++iReg) {
				jmir_dce_instrAddDef(pass, instrInfo, kMIRFuncCallerSavedFReg[iReg]);
			}
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
		jmir_dce_instrAddUse(pass, instrInfo, kMIRRegGP_A);
		jmir_dce_instrAddDef(pass, instrInfo, kMIRRegGP_D);
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
			jmir_dce_instrAddDef(pass, instrInfo, src->u.m_Reg);
		} else if (src->m_Kind == JMIR_OPERAND_MEMORY_REF) {
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_BaseReg);
			jmir_dce_instrAddUse(pass, instrInfo, src->u.m_MemRef->m_IndexReg);
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

	for (uint32_t iDef = instrInfo->m_NumDefs; iDef < JMIR_DCE_MAX_INSTR_DEFS; ++iDef) {
		instrInfo->m_Defs[iDef] = kMIRRegGPNone;
	}
	for (uint32_t iUse = instrInfo->m_NumUses; iUse < JMIR_DCE_MAX_INSTR_USES; ++iUse) {
		instrInfo->m_Uses[iUse] = kMIRRegGPNone;
	}

	return true;
}

static void jmir_dce_buildCFG(jmir_func_pass_dce_t* pass, jx_mir_context_t* ctx, jx_mir_function_t* func)
{
	// Clear existing CFG
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_dce_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
		jx_array_resize(bbInfo->m_SuccArr, 0);
	}

	// Rebuild CFG
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_dce_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
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
					jmir_dce_bb_info_t* targetBBInfo = jmir_dce_getBasicBlockInfo(pass, targetBB);

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

			jmir_dce_bb_info_t* nextBBInfo = jmir_dce_getBasicBlockInfo(pass, bb->m_Next);

			jx_array_push_back(bbInfo->m_SuccArr, nextBBInfo);
		}
	}
}

static bool jmir_dce_livenessAnalysis(jmir_func_pass_dce_t* pass, jx_mir_function_t* func)
{
	const uint32_t numNodes = pass->m_NumRegs;
	jx_bitset_t* prevLiveIn = jx_bitsetCreate(numNodes, pass->m_Allocator);
	jx_bitset_t* prevLiveOut = jx_bitsetCreate(numNodes, pass->m_Allocator);
	jx_bitset_t* instrLive = jx_bitsetCreate(numNodes, pass->m_Allocator);
	if (!prevLiveIn || !prevLiveOut || !instrLive) {
		return false;
	}

	// Reset live in/out sets
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jmir_dce_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];
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
			jmir_dce_bb_info_t* bbInfo = &pass->m_BBInfo[iBB];

			// out'[v] = out[v]
			// in'[v] = in[v]
			jx_bitsetCopy(prevLiveIn, bbInfo->m_LiveInSet);
			jx_bitsetCopy(prevLiveOut, bbInfo->m_LiveOutSet);

			// out[v] = Union(w in succ, in[w])
			const uint32_t numSucc = (uint32_t)jx_array_sizeu(bbInfo->m_SuccArr);
			if (numSucc) {
				jmir_dce_bb_info_t* succBBInfo = bbInfo->m_SuccArr[0];

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
					jmir_dce_instr_info_t* instrInfo = &bbInfo->m_InstrInfo[iInstr];

					if (jmir_dce_isMoveInstr(instrInfo)) {
						JX_CHECK(instrInfo->m_NumUses == 1, "Move instruction expected to have 1 use.");
						const uint32_t id = jmir_dce_mapRegToID(pass, instrInfo->m_Uses[0]);
						jx_bitsetResetBit(instrLive, id);
					}

					jx_bitsetCopy(instrInfo->m_LiveOutSet, instrLive);

					const uint32_t numDefs = instrInfo->m_NumDefs;
					for (uint32_t iDef = 0; iDef < numDefs; ++iDef) {
						const uint32_t id = jmir_dce_mapRegToID(pass, instrInfo->m_Defs[iDef]);
						jx_bitsetResetBit(instrLive, id);
					}

					const uint32_t numUses = instrInfo->m_NumUses;
					for (uint32_t iUse = 0; iUse < numUses; ++iUse) {
						const uint32_t id = jmir_dce_mapRegToID(pass, instrInfo->m_Uses[iUse]);
						jx_bitsetSetBit(instrLive, id);
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

static jmir_dce_bb_info_t* jmir_dce_getBasicBlockInfo(jmir_func_pass_dce_t* pass, jx_mir_basic_block_t* bb)
{
	const uint32_t numBasicBlocks = pass->m_NumBasicBlocks;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		if (pass->m_BBInfo[iBB].m_BB == bb) {
			return &pass->m_BBInfo[iBB];
		}
	}

	return NULL;
}

static uint32_t jmir_dce_mapRegToID(jmir_func_pass_dce_t* pass, jx_mir_reg_t reg)
{
	// Registers are laid out as:
	//  hw_gp, hw_gp, hw_gp, ..., hw_gp, hw_xmm, hw_xmm, ..., hw_xmm, v_gp, v_gp, ..., v_gp, v_xmm, v_xmm, ..., v_xmm
	uint32_t id = 0;
	if (reg.m_IsVirtual) {
		id = (16 + 16); // 16 GP regs + 16 XMM regs

		for (uint32_t iClass = 0; iClass < reg.m_Class; ++iClass) {
			id += pass->m_Func->m_NextVirtualRegID[iClass];
		}

		id += reg.m_ID;
	} else {
		for (uint32_t iClass = 0; iClass < reg.m_Class; ++iClass) {
			id += 16;
		}

		id += reg.m_ID;
	}

	return id;
}

static bool jmir_dce_isMoveInstr(jmir_dce_instr_info_t* instrInfo)
{
	jx_mir_instruction_t* instr = instrInfo->m_Instr;

	const bool isMov = false
		|| instr->m_OpCode == JMIR_OP_MOV
		|| instr->m_OpCode == JMIR_OP_MOVSX
		|| instr->m_OpCode == JMIR_OP_MOVZX
		|| instr->m_OpCode == JMIR_OP_MOVSS
		|| instr->m_OpCode == JMIR_OP_MOVSD
		;
	if (!isMov) {
		return false;
	}

	jx_mir_operand_t* dst = instr->m_Operands[0];
	jx_mir_operand_t* src = instr->m_Operands[1];
	return dst->m_Kind == JMIR_OPERAND_REGISTER
		&& src->m_Kind == JMIR_OPERAND_REGISTER
		&& jx_mir_regIsSameClass(dst->u.m_Reg, src->u.m_Reg)
		;
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
