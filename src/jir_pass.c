// TODO:
// - Dead store elimination
// - Constant propagation
// - 
#include "jir_pass.h"
#include "jir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/hashmap.h>
#include <jlib/logger.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>
#include <tracy/tracy/TracyC.h>

static int32_t jir_comparePtrs(void* a, void* b);

typedef struct jir_value_map_item_t
{
	jx_ir_value_t* m_Key;
	jx_ir_value_t* m_Value;
} jir_value_map_item_t;

static uint64_t jir_valueMapItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jir_value_map_item_t* var = (const jir_value_map_item_t*)item;
	uint64_t hash = jx_hashFNV1a(&var->m_Key, sizeof(jx_ir_value_t*), seed0, seed1);
	return hash;
}

static int32_t jir_valueMapItemCompare(const void* a, const void* b, void* udata)
{
	const jir_value_map_item_t* varA = (const jir_value_map_item_t*)a;
	const jir_value_map_item_t* varB = (const jir_value_map_item_t*)b;
	int32_t res = jir_comparePtrs(varA->m_Key, varB->m_Key);
	return res;
}

//////////////////////////////////////////////////////////////////////////
// Single Return Block pass
//
typedef struct jir_func_pass_single_ret_block_t
{
	// Dynamic array to hold all basic blocks with a 
	// ret terminator instruction.
	jx_ir_basic_block_t** m_RetBBArr;
} jir_func_pass_single_ret_block_t;

static void jir_funcPass_singleRetBlockDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_singleRetBlockRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

bool jx_ir_funcPassCreate_singleRetBlock(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_single_ret_block_t* inst = (jir_func_pass_single_ret_block_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_single_ret_block_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_single_ret_block_t));
	inst->m_RetBBArr = (jx_ir_basic_block_t**)jx_array_create(allocator);
	if (!inst->m_RetBBArr) {
		JX_FREE(allocator, inst);
		return false;
	}

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_singleRetBlockRun;
	pass->destroy = jir_funcPass_singleRetBlockDestroy;

	return true;
}

static void jir_funcPass_singleRetBlockDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_single_ret_block_t* pass = (jir_func_pass_single_ret_block_t*)inst;
	jx_array_free(pass->m_RetBBArr);
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_singleRetBlockRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Single Ret Block", 1);
	jir_func_pass_single_ret_block_t* pass = (jir_func_pass_single_ret_block_t*)inst;
	
	jx_array_resize(pass->m_RetBBArr, 0);

	// Collect all basic blocks with a return instruction as their terminator.
	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_ir_instruction_t* lastInstr = jx_ir_bbGetLastInstr(ctx, bb);
		if (lastInstr->m_OpCode == JIR_OP_RET) {
			jx_array_push_back(pass->m_RetBBArr, bb);
		}

		bb = bb->m_Next;
	}

	// If there is only 1 such block we are done.
	const uint32_t numRetBlocks = (uint32_t)jx_array_sizeu(pass->m_RetBBArr);
	if (numRetBlocks <= 1) {
		TracyCZoneEnd(tracyCtx);
		return false;
	}

	// If the function returns a value, create a phi instruction 
	// for the return block
	jx_ir_type_function_t* funcType = jx_ir_funcGetType(ctx, func);
	jx_ir_type_t* funcRetType = funcType->m_RetType;

	jx_ir_basic_block_t* newRetBB = jx_ir_bbAlloc(ctx, "exit");
	
	jx_ir_instruction_t* phiInstr = NULL;
	if (funcRetType->m_Kind != JIR_TYPE_VOID) {
		phiInstr = jx_ir_instrPhi(ctx, funcRetType);
		jx_ir_bbAppendInstr(ctx, newRetBB, phiInstr);
	}

	jx_ir_bbAppendInstr(ctx, newRetBB, jx_ir_instrRet(ctx, phiInstr ? jx_ir_instrToValue(phiInstr) : NULL));

	// Replace all ret instructions with unconditional jumps to the new 
	// return block. If there is a phi instruction in the return block
	// add the returned value to the phi.
	for (uint32_t iBB = 0; iBB < numRetBlocks; ++iBB) {
		jx_ir_basic_block_t* bb = pass->m_RetBBArr[iBB];

		jx_ir_instruction_t* lastInstr = jx_ir_bbGetLastInstr(ctx, bb);

		if (phiInstr) {
			jx_ir_value_t* retVal = lastInstr->super.m_OperandArr[0]->m_Value;
			jx_ir_instrPhiAddValue(ctx, phiInstr, bb, retVal);
		}

		jx_ir_bbRemoveInstr(ctx, bb, lastInstr);
		jx_ir_instrFree(ctx, lastInstr);
		jx_ir_bbAppendInstr(ctx, bb, jx_ir_instrBranch(ctx, newRetBB));
	}

	jx_ir_funcAppendBasicBlock(ctx, func, newRetBB);

	TracyCZoneEnd(tracyCtx);

	return true;
}

//////////////////////////////////////////////////////////////////////////
// Simplify CFG
//
typedef struct jir_func_pass_simplify_cfg_t
{
	// Dynamic array to hold all phi values before
	// removing a basic block from the function. Those
	// values are inserted again to each phi instruction
	// after the eliminated basic block is replaced.
	jx_ir_value_t** m_PhiValueArr;
} jir_func_pass_simplify_cfg_t;

static void jir_funcPass_simplifyCFGDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_simplifyCFGRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);
static bool jir_bbCanMergeWithSinglePredecessor(jx_ir_basic_block_t* bb);
static bool jir_bbIsUnconditionalJump(jx_ir_basic_block_t* bb);

bool jx_ir_funcPassCreate_simplifyCFG(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_simplify_cfg_t* inst = (jir_func_pass_simplify_cfg_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_simplify_cfg_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_simplify_cfg_t));
	inst->m_PhiValueArr = (jx_ir_value_t**)jx_array_create(allocator);
	if (!inst->m_PhiValueArr) {
		JX_FREE(allocator, inst);
		return false;
	}

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_simplifyCFGRun;
	pass->destroy = jir_funcPass_simplifyCFGDestroy;

	return true;
}

static void jir_funcPass_simplifyCFGDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_simplify_cfg_t* pass = (jir_func_pass_simplify_cfg_t*)inst;
	jx_array_free(pass->m_PhiValueArr);
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_simplifyCFGRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Simplify CFG", 1);
	jir_func_pass_simplify_cfg_t* pass = (jir_func_pass_simplify_cfg_t*)inst;

	uint32_t numBasicBlocksChanged = 0;

	bool cfgChanged = true;
	while (cfgChanged) {
		cfgChanged = false;

		// Always skip the entry block
		// NOTE: I'm skipping the entry block because all merges are performed with 
		// a predecessor. Entry blocks don't have predecessors by definition...
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead->m_Next;
		while (bb && !cfgChanged) {
			jx_ir_basic_block_t* bbNext = bb->m_Next;

			const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
			if (numPred == 0 || (numPred == 1 && bb->m_PredArr[0] == bb)) {
				// Remove the block if it has no predecessors
				jx_ir_funcRemoveBasicBlock(ctx, func, bb);
				jx_ir_bbFree(ctx, bb);

				numBasicBlocksChanged++;
				cfgChanged = true;
			} else if (jir_bbCanMergeWithSinglePredecessor(bb)) {
				JX_CHECK(numPred == 1, "Something went really wrong!");
				JX_CHECK(bb->m_InstrListHead && bb->m_InstrListHead->m_OpCode != JIR_OP_PHI, "Something went really wrong!");
				JX_CHECK(jx_array_sizeu(bb->m_PredArr[0]->m_SuccArr) == 1 && bb->m_PredArr[0]->m_SuccArr[0] == bb, "Something went really wrong!");

				jx_ir_basic_block_t* pred = bb->m_PredArr[0];

				// Remove the terminator instruction from the predecessor.
				jx_ir_instruction_t* lastInstr = jx_ir_bbGetLastInstr(ctx, pred);
				jx_ir_bbRemoveInstr(ctx, pred, lastInstr);
				jx_ir_instrFree(ctx, lastInstr);

				// Remove the basic block from the function
				jx_ir_funcRemoveBasicBlock(ctx, func, bb);

				// Patch all phi instructions to point to the new block.
				// Note that there should be no jumps referencing this block because we
				// made sure that this block had only 1 predecessor and we deleted the branch
				// to this block.
				jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_bbToValue(bb), jx_ir_bbToValue(pred));

				// Add all basic block instructions to the predecessor.
				// Note that this includes the terminator instruction so the 
				// predecessor will end up as a valid basic block again.
				jx_ir_instruction_t* bbInstr = bb->m_InstrListHead;
				while (bbInstr) {
					jx_ir_instruction_t* bbInstrNext = bbInstr->m_Next;

					jx_ir_bbRemoveInstr(ctx, bb, bbInstr);
					jx_ir_bbAppendInstr(ctx, pred, bbInstr);

					bbInstr = bbInstrNext;
				}

				// Free the basic block
				jx_ir_bbFree(ctx, bb);
				numBasicBlocksChanged++;
				cfgChanged = true;
			} else if (jir_bbIsUnconditionalJump(bb)) {
#if 1
				// TODO: 
#else
				// The code below does not work correctly when phi instructions are included in the target 
				// block. There are cases where both targets of a conditional jump in a predecessor end
				// up pointing to the same block. This messes up the phi instructions. Ideally, the phis
				// should be moved to the predecessor. But this requires some kind of analysis to determine
				// correctly.
				jx_ir_instruction_t* instr = bb->m_InstrListHead;
				JX_CHECK(instr && instr->m_OpCode == JIR_OP_BRANCH, "Something went really wrong!");
				JX_CHECK(jx_array_sizeu(instr->super.m_OperandArr) == 1, "Something went really wrong!");
				jx_ir_value_t* targetBBVal = instr->super.m_OperandArr[0]->m_Value;
				jx_ir_basic_block_t* targetBB = jx_ir_valueToBasicBlock(targetBBVal);
				JX_CHECK(targetBB, "Unconditional branch operand expected to be a basic block!");

				// Remove this basic block from all phi instructions in the target basic block.
				jx_array_resize(pass->m_PhiValueArr, 0);

				jx_ir_instruction_t* targetInstr = targetBB->m_InstrListHead;
				while (targetInstr && targetInstr->m_OpCode == JIR_OP_PHI) {
					jx_ir_value_t* bbVal = jx_ir_instrPhiRemoveValue(ctx, targetInstr, bb);

					jx_array_push_back(pass->m_PhiValueArr, bbVal);

					targetInstr = targetInstr->m_Next;
				}
				
				// Replace the terminator instructions of all predecessors in order to point to the target block.
				for (uint32_t iPred = 0; iPred < numPred; ++iPred) {
					jx_ir_basic_block_t* pred = bb->m_PredArr[iPred];
					jx_ir_instruction_t* lastInstr = jx_ir_bbGetLastInstr(ctx, pred);
					JX_CHECK(lastInstr->m_OpCode == JIR_OP_BRANCH, "Expected branch instruction!");
					const uint32_t numBranchOperands = (uint32_t)jx_array_sizeu(lastInstr->super.m_OperandArr);
					if (numBranchOperands == 1) {
						// Unconditional branch to this basic block.
						JX_CHECK(jx_ir_valueToBasicBlock(lastInstr->super.m_OperandArr[0]->m_Value) == bb, "Branch target expected to be the current basic block.");
						jx_ir_bbRemoveInstr(ctx, pred, lastInstr);
						jx_ir_instrFree(ctx, lastInstr);

						jx_ir_bbAppendInstr(ctx, pred, jx_ir_instrBranch(ctx, targetBB));
					} else if (numBranchOperands == 3) {
						// Conditional branch to this basic block.
						jx_ir_value_t* condVal = lastInstr->super.m_OperandArr[0]->m_Value;
						jx_ir_basic_block_t* trueBB = jx_ir_valueToBasicBlock(lastInstr->super.m_OperandArr[1]->m_Value);
						jx_ir_basic_block_t* falseBB = jx_ir_valueToBasicBlock(lastInstr->super.m_OperandArr[2]->m_Value);
						JX_CHECK(trueBB && falseBB, "Expected basic blocks as condition branch operands.");
						JX_CHECK(trueBB == bb || falseBB == bb, "One of the branch targets expected to be the current basic block.");
						jx_ir_bbRemoveInstr(ctx, pred, lastInstr);
						jx_ir_instrFree(ctx, lastInstr);

						jx_ir_instruction_t* branchInstr = trueBB == bb
							? jx_ir_instrBranchIf(ctx, condVal, targetBB, falseBB)
							: jx_ir_instrBranchIf(ctx, condVal, trueBB, targetBB)
							;
						jx_ir_bbAppendInstr(ctx, pred, branchInstr);
					} else {
						JX_CHECK(false, "Invalid branch instruction?");
					}
				}

				// Add all phi values again in the target block, one for each predecessor.
				uint32_t curPhiValueID = 0;
				targetInstr = targetBB->m_InstrListHead;
				while (targetInstr && targetInstr->m_OpCode == JIR_OP_PHI) {
					JX_CHECK(curPhiValueID < jx_array_sizeu(pass->m_PhiValueArr), "Phi value is missing!");
					jx_ir_value_t* phiVal = pass->m_PhiValueArr[curPhiValueID++];

					for (uint32_t iPred = 0; iPred < numPred; ++iPred) {
						jx_ir_instrPhiAddValue(ctx, targetInstr, bb->m_PredArr[iPred], phiVal);
					}

					targetInstr = targetInstr->m_Next;
				}

				jx_ir_funcRemoveBasicBlock(ctx, func, bb);
				jx_ir_bbFree(ctx, bb);
				numBasicBlocksChanged++;
				cfgChanged = true;
#endif
			}

			bb = bbNext;
		}
	}

	TracyCZoneEnd(tracyCtx);

	return numBasicBlocksChanged != 0;
}

// If the block has only 1 predecessor and the predecessor has only 1 successor
// (i.e. this block) and this block does not have any phi instructions,
// merge this block to the predecessor.
static bool jir_bbCanMergeWithSinglePredecessor(jx_ir_basic_block_t* bb)
{
	const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
	if (numPred != 1) {
		return false;
	}

	jx_ir_instruction_t* firstInstr = bb->m_InstrListHead;
	if (firstInstr->m_OpCode == JIR_OP_PHI) {
		return false;
	}

	jx_ir_basic_block_t* pred = bb->m_PredArr[0];
	const uint32_t numPredSucc = (uint32_t)jx_array_sizeu(pred->m_SuccArr);
	if (numPredSucc != 1) {
		return false;
	}

	JX_CHECK(pred->m_SuccArr[0] == bb, "Invalid CFG?");

	return true;
}

// Check if this basic block includes only 1 instruction which is an 
// unconditional jump to another basic block.
// NOTE: The code assumes that the basic block is well-formed, i.e.
// if the first instruction is an unconditional branch, there will be 
// no other instruction following the branch.
static bool jir_bbIsUnconditionalJump(jx_ir_basic_block_t* bb)
{
	JX_CHECK(bb->m_InstrListHead, "Empty basic block?");
	return true
		&& bb->m_InstrListHead->m_OpCode == JIR_OP_BRANCH
		&& jx_array_sizeu(bb->m_InstrListHead->super.m_OperandArr) == 1
		;
}

//////////////////////////////////////////////////////////////////////////
// Simple and Efficient SSA Construction
//
#define JIR_SIMPLESSA_BB_FLAGS_IS_SEALED_Pos 30
#define JIR_SIMPLESSA_BB_FLAGS_IS_SEALED_Msk (1u << JIR_SIMPLESSA_BB_FLAGS_IS_SEALED_Pos)
#define JIR_SIMPLESSA_BB_FLAGS_IS_FILLED_Pos 31
#define JIR_SIMPLESSA_BB_FLAGS_IS_FILLED_Msk (1u << JIR_SIMPLESSA_BB_FLAGS_IS_FILLED_Pos)

#define JIR_SIMPLESSA_CONFIG_MAX_ITERATIONS  50

typedef struct jir_bb_var_map_item_t
{
	jx_ir_basic_block_t* m_BasicBlock;
	jx_ir_value_t* m_Addr;
	jx_ir_value_t* m_Value;
} jir_bb_var_map_item_t;

typedef struct jir_func_pass_simple_ssa_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_context_t* m_Ctx;
	jx_hashmap_t* m_DefMap;
	jx_hashmap_t* m_ReplacementMap;
	jir_bb_var_map_item_t* m_IncompletePhis;
	jx_ir_instruction_t** m_PromotableAllocas; // Array to hold all promotable allocas 
} jir_func_pass_simple_ssa_t;

static void jir_funcPass_simpleSSADestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_simpleSSARun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);
static bool jir_simpleSSA_isAllocaPromotable(jx_ir_instruction_t* instr);
static bool jir_simpleSSA_canPromoteAlloca(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* instr);
static void jir_simpleSSA_writeVariable(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr, jx_ir_value_t* val);
static jx_ir_value_t* jir_simpleSSA_readVariable(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr);
static jx_ir_value_t* jir_simpleSSA_readVariable_r(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr);
static void jir_simpleSSA_replaceVariable(jir_func_pass_simple_ssa_t* pass, jx_ir_value_t* var, jx_ir_value_t* newVal);
static jx_ir_value_t* jir_simpleSSA_addPhiOperands(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* phiInstr, jx_ir_value_t* addr);
static jx_ir_value_t* jir_simpleSSA_tryRemoveTrivialPhi(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* phiInstr);
static void jir_simpleSSA_tryRemoveDeadAlloca(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* allocaInstr);
static bool jir_simpleSSA_bbIsFilled(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb);
static void jir_simpleSSA_fillBlock(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb);
static bool jir_simpleSSA_bbIsSealed(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb);
static bool jir_simpleSSA_trySealBlock(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb);
static void jir_simpleSSA_addIncompletePhi(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr, jx_ir_value_t* val);
static jx_ir_value_t* jir_simpleSSA_getReplacementValue(jir_func_pass_simple_ssa_t* pass, jx_ir_value_t* val);
static uint64_t jir_bbVarMapItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_bbVarMapItemCompare(const void* a, const void* b, void* udata);

bool jx_ir_funcPassCreate_simpleSSA(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_simple_ssa_t* inst = (jir_func_pass_simple_ssa_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_simple_ssa_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_simple_ssa_t));
	inst->m_Allocator = allocator;

	inst->m_PromotableAllocas = (jx_ir_instruction_t**)jx_array_create(allocator);
	if (!inst->m_PromotableAllocas) {
		jir_funcPass_simpleSSADestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

	inst->m_IncompletePhis = (jir_bb_var_map_item_t*)jx_array_create(allocator);
	if (!inst->m_IncompletePhis) {
		jir_funcPass_simpleSSADestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

	inst->m_DefMap = jx_hashmapCreate(allocator, sizeof(jir_bb_var_map_item_t), 64, 0, 0, jir_bbVarMapItemHash, jir_bbVarMapItemCompare, NULL, NULL);
	if (!inst->m_DefMap) {
		jir_funcPass_simpleSSADestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

	inst->m_ReplacementMap = jx_hashmapCreate(allocator, sizeof(jir_value_map_item_t), 64, 0, 0, jir_valueMapItemHash, jir_valueMapItemCompare, NULL, NULL);
	if (!inst->m_ReplacementMap) {
		jir_funcPass_simpleSSADestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_simpleSSARun;
	pass->destroy = jir_funcPass_simpleSSADestroy;

	return true;
}

static void jir_funcPass_simpleSSADestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_simple_ssa_t* pass = (jir_func_pass_simple_ssa_t*)inst;
	jx_array_free(pass->m_PromotableAllocas);
	jx_array_free(pass->m_IncompletePhis);
	jx_hashmapDestroy(pass->m_ReplacementMap);
	jx_hashmapDestroy(pass->m_DefMap);
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_simpleSSARun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Simple SSA", 1);
	jir_func_pass_simple_ssa_t* pass = (jir_func_pass_simple_ssa_t*)inst;
	pass->m_Ctx = ctx;

	// Mark all blocks as unfilled and unsealed
	{
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_value_t* bbVal = jx_ir_bbToValue(bb);
			bbVal->m_Flags &= ~(JIR_SIMPLESSA_BB_FLAGS_IS_SEALED_Msk | JIR_SIMPLESSA_BB_FLAGS_IS_FILLED_Msk);

			bb = bb->m_Next;
		}
	}

	// Promote allocas iteratively. One promotion might end up making another alloca promotable.
	// Example: c-testsuite/00020.c
	// 
	// NOTE: Just in case something goes terrible wrong, put a limit to the maximum number 
	// of iterations.
	uint32_t iter = 0;
	while (iter < JIR_SIMPLESSA_CONFIG_MAX_ITERATIONS) {
		// Find all promotable allocas
		jx_array_resize(pass->m_PromotableAllocas, 0);

		{
			jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				if (instr && instr->m_OpCode == JIR_OP_ALLOCA && jir_simpleSSA_isAllocaPromotable(instr)) {
					jx_array_push_back(pass->m_PromotableAllocas, instr);
				}

				instr = instr->m_Next;
			}
		}

		if (!jx_array_sizeu(pass->m_PromotableAllocas)) {
			break;
		}

		jx_array_resize(pass->m_IncompletePhis, 0);
		jx_hashmapClear(pass->m_DefMap, false);
		jx_hashmapClear(pass->m_ReplacementMap, false);

		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jir_simpleSSA_trySealBlock(pass, bb);

			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_ir_instruction_t* nextInstr = instr->m_Next;

				if (instr->m_OpCode == JIR_OP_STORE) {
					jx_ir_value_t* addr = instr->super.m_OperandArr[0]->m_Value;
					jx_ir_value_t* val = jx_ir_instrGetOperandVal(instr, 1);

					// Only process stores to alloca'd pointers.
					if (jir_simpleSSA_canPromoteAlloca(pass, jx_ir_valueToInstr(addr))) {
						jir_simpleSSA_writeVariable(pass, bb, addr, val);
					}
				} else if (instr->m_OpCode == JIR_OP_LOAD) {
					jx_ir_value_t* addr = instr->super.m_OperandArr[0]->m_Value;

					// Only process loads from alloca'd pointers.
					if (jir_simpleSSA_canPromoteAlloca(pass, jx_ir_valueToInstr(addr))) {
						jx_ir_value_t* val = jir_simpleSSA_readVariable(pass, bb, addr);

						jir_simpleSSA_replaceVariable(pass, jx_ir_instrToValue(instr), val);

						// Instruction is now dead. Remove it.
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				}

				instr = nextInstr;
			}

			jir_simpleSSA_fillBlock(pass, bb);

			bb = bb->m_Next;
		}

		// Check if there are any remaining incomplete phis
		// NOTE: What should I do with them? What does it mean to have incomplete phis 
		// at this point? If the basic block was unreachable there would not have been
		// any phis generated. Do I just remove them?
		JX_CHECK(jx_array_sizeu(pass->m_IncompletePhis) == 0, "There are unprocessed incomplete phis!");

		// Remove allocas and stores to to those allocas 
		// if the only remaining users of the alloca are stores.
		const uint32_t numPromotableAllocas = (uint32_t)jx_array_sizeu(pass->m_PromotableAllocas);
		for (uint32_t iAlloca = 0; iAlloca < numPromotableAllocas; ++iAlloca) {
			jir_simpleSSA_tryRemoveDeadAlloca(pass, pass->m_PromotableAllocas[iAlloca]);
		}

		++iter;
	}

	// Remove any used flags from all basic blocks for other passes.
	{
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_value_t* bbVal = jx_ir_bbToValue(bb);
			bbVal->m_Flags &= ~(JIR_SIMPLESSA_BB_FLAGS_IS_SEALED_Msk | JIR_SIMPLESSA_BB_FLAGS_IS_FILLED_Msk);

			bb = bb->m_Next;
		}
	}

	pass->m_Ctx = NULL;

	TracyCZoneEnd(tracyCtx);

	return true;
}

static bool jir_simpleSSA_isAllocaPromotable(jx_ir_instruction_t* instr)
{
	if (!instr->m_OpCode == JIR_OP_ALLOCA) {
		JX_CHECK(false, "Should only be called on alloca instructions");
		return false;
	}

	bool isPromotable = true;

	jx_ir_value_t* val = jx_ir_instrToValue(instr);
	jx_ir_use_t* use = val->m_UsesListHead;
	while (use) {
		jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(jx_ir_userToValue(use->m_User));
		if (!userInstr) {
			isPromotable = false;
			break;
		} else {
			if (userInstr->m_OpCode == JIR_OP_LOAD) {
				// Noop
			} else if (userInstr->m_OpCode == JIR_OP_STORE) {
				if (userInstr->super.m_OperandArr[1]->m_Value == val) {
					// Don't allow stores of the alloca pointer. 
					// Only stores to the alloca pointer.
					isPromotable = false;
					break;
				}
			} else {
				isPromotable = false;
				break;
			}
		}

		use = use->m_Next;
	}

	return isPromotable;
}

static bool jir_simpleSSA_canPromoteAlloca(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* instr)
{
	if (!instr || instr->m_OpCode != JIR_OP_ALLOCA) {
		return false;
	}

	const uint32_t numPromotableAllocas = (uint32_t)jx_array_sizeu(pass->m_PromotableAllocas);
	for (uint32_t iAlloca = 0; iAlloca < numPromotableAllocas; ++iAlloca) {
		if (pass->m_PromotableAllocas[iAlloca] == instr) {
			return true;
		}
	}

	return false;
}

static void jir_simpleSSA_writeVariable(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr, jx_ir_value_t* val)
{
	jx_hashmapSet(pass->m_DefMap, &(jir_bb_var_map_item_t){ .m_BasicBlock = bb, .m_Addr = addr, .m_Value = val });
}

static jx_ir_value_t* jir_simpleSSA_readVariable(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr)
{
	jir_bb_var_map_item_t* hashItem = (jir_bb_var_map_item_t*)jx_hashmapGet(pass->m_DefMap, &(jir_bb_var_map_item_t){ .m_BasicBlock = bb, .m_Addr = addr });
	if (hashItem) {
		return jir_simpleSSA_getReplacementValue(pass, hashItem->m_Value);
	}

	return jir_simpleSSA_readVariable_r(pass, bb, addr);
}

static jx_ir_value_t* jir_simpleSSA_readVariable_r(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr)
{
	jx_ir_value_t* val = NULL;

	if (!jir_simpleSSA_bbIsSealed(pass, bb)) {
		jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(addr->m_Type);
		JX_CHECK(ptrType, "Expected pointer type!");
		jx_ir_type_t* valType = ptrType->m_BaseType;
		jx_ir_instruction_t* phiInstr = jx_ir_instrPhi(pass->m_Ctx, valType);
		jx_ir_bbPrependInstr(pass->m_Ctx, bb, phiInstr);
		val = jx_ir_instrToValue(phiInstr);

		jir_simpleSSA_addIncompletePhi(pass, bb, addr, val);
	} else if (jx_array_sizeu(bb->m_PredArr) == 1 && bb->m_PredArr[0] != bb) {
		// Optimize the common case of 1 predecessor. No phi needed.
		val = jir_simpleSSA_readVariable(pass, bb->m_PredArr[0], addr);
	} else {
		jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(addr->m_Type);
		JX_CHECK(ptrType, "Expected pointer type!");
		jx_ir_type_t* valType = ptrType->m_BaseType;
		jx_ir_instruction_t* phiInstr = jx_ir_instrPhi(pass->m_Ctx, valType);
		jx_ir_bbPrependInstr(pass->m_Ctx, bb, phiInstr);

		val = jx_ir_instrToValue(phiInstr);
		jir_simpleSSA_writeVariable(pass, bb, addr, val);

		val = jir_simpleSSA_addPhiOperands(pass, phiInstr, addr);
	}

	JX_CHECK(val, "Failed to read variable value!");

	jir_simpleSSA_writeVariable(pass, bb, addr, val);

	return val;
}

static void jir_simpleSSA_replaceVariable(jir_func_pass_simple_ssa_t* pass, jx_ir_value_t* var, jx_ir_value_t* newVal)
{
	JX_CHECK(var->m_Type == newVal->m_Type, "Replacement values should have the same type!");
	newVal = jir_simpleSSA_getReplacementValue(pass, newVal);
	jx_ir_valueReplaceAllUsesWith(pass->m_Ctx, var, newVal);
	jx_hashmapSet(pass->m_ReplacementMap, &(jir_value_map_item_t){.m_Key = var, .m_Value = newVal});
}

static jx_ir_value_t* jir_simpleSSA_addPhiOperands(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* phiInstr, jx_ir_value_t* addr)
{
	JX_CHECK(phiInstr->m_OpCode == JIR_OP_PHI, "Expected phi instruction!");
	JX_CHECK(phiInstr->m_ParentBB, "Expected phi instruction to be part of a basic block!");

	jx_ir_basic_block_t* phiBlock = phiInstr->m_ParentBB;
	const uint32_t numPreds = (uint32_t)jx_array_sizeu(phiBlock->m_PredArr);
	for (uint32_t iPred = 0; iPred < numPreds; ++iPred) {
		jx_ir_basic_block_t* bbPred = phiBlock->m_PredArr[iPred];
		jx_ir_value_t* predVal = jir_simpleSSA_readVariable(pass, bbPred, addr);
		jx_ir_instrPhiAddValue(pass->m_Ctx, phiInstr, bbPred, predVal);
	}

	return jir_simpleSSA_tryRemoveTrivialPhi(pass, phiInstr);
}

static jx_ir_value_t* jir_simpleSSA_tryRemoveTrivialPhi(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* phiInstr)
{
	JX_CHECK(phiInstr->m_OpCode == JIR_OP_PHI, "Expected phi instruction!");

	jx_ir_value_t* phiInstrVal = jx_ir_instrToValue(phiInstr);
	jx_ir_value_t* same = NULL;

	const uint32_t numOperands = (uint32_t)jx_array_sizeu(phiInstr->super.m_OperandArr);
	JX_CHECK(numOperands == 2 * jx_array_sizeu(phiInstr->m_ParentBB->m_PredArr), "Invalid number of phi operands");
	for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
		jx_ir_value_t* bbVal = phiInstr->super.m_OperandArr[iOperand + 0]->m_Value;
		if (bbVal == same || bbVal == phiInstrVal) {
			continue;
		}
		if (same) {
			return phiInstrVal;
		}

		same = bbVal;
	}

	if (!same) {
		// NOTE: This can happen when using uninitialized variables.
		// E.g. c-testsuite/00141.c
		// 
		// Proper handling requires Undef() but this will complicate things more
		// down the line. Simply replace with 0 for now.
		// TODO: Warn the user?
		same = jx_ir_constToValue(jx_ir_constGetZero(pass->m_Ctx, phiInstrVal->m_Type));
	}

	// Remove self-reference in phi
	for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
		jx_ir_value_t* bbVal = phiInstr->super.m_OperandArr[iOperand + 0]->m_Value;
		if (bbVal == phiInstrVal) {
			jx_ir_basic_block_t* bb = jx_ir_valueToBasicBlock(phiInstr->super.m_OperandArr[iOperand + 1]->m_Value);
			JX_CHECK(bb, "Expected basic block!");
			jx_ir_instrPhiRemoveValue(pass->m_Ctx, phiInstr, bb);
			JX_CHECK(!jx_ir_instrPhiHasValue(pass->m_Ctx, phiInstr, bb), "Multiple uses of the same operand?");
			break;
		}
	}

	// Keep all phi users
	// NOTE: The code below needs only phi instructions. So don't keep all 
	// users, just phi instructions.
	jx_ir_instruction_t** phiUsersArr = (jx_ir_instruction_t**)jx_array_create(pass->m_Allocator);
	if (!phiUsersArr) {
		return NULL;
	}

	jx_ir_use_t* phiUse = phiInstrVal->m_UsesListHead;
	while (phiUse) {
		jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(jx_ir_userToValue(phiUse->m_User));
		if (userInstr && userInstr->m_OpCode == JIR_OP_PHI) {
			// Make sure each user/instruction is added only once into the array.
			bool found = false;
			const uint32_t numUsers = (uint32_t)jx_array_sizeu(phiUsersArr);
			for (uint32_t iUser = 0; iUser < numUsers; ++iUser) {
				if (phiUsersArr[iUser] == userInstr) {
					found = true;
					break;
				}
			}

			if (!found) {
				jx_array_push_back(phiUsersArr, userInstr);
			}
		}
		phiUse = phiUse->m_Next;
	}

	jir_simpleSSA_replaceVariable(pass, phiInstrVal, same);

	// Remove phi
	jx_ir_bbRemoveInstr(pass->m_Ctx, phiInstr->m_ParentBB, phiInstr);
	jx_ir_instrFree(pass->m_Ctx, phiInstr);

	// Try to recursively remove all phi users which might have become trivial
	const uint32_t numUsers = (uint32_t)jx_array_sizeu(phiUsersArr);
	for (uint32_t iUser = 0; iUser < numUsers; ++iUser) {
		jx_ir_instruction_t* userInstr = phiUsersArr[iUser];
		JX_CHECK(userInstr && userInstr->m_OpCode == JIR_OP_PHI, "Expected phi instruction");
		jir_simpleSSA_tryRemoveTrivialPhi(pass, userInstr);
	}

	jx_array_free(phiUsersArr);

	// NOTE: This is not shown in the original paper.
	// When removing trivial phis recursively, the returned value of this function (same) must
	// change if it was the value that was removed. E.g. c-testsuite/00181.c
	// Otherwise, the value that was just replaced ends up referencing values which are not
	// part of the current function (they are already replaced by the recursive call to 
	// tryRemoveTrivialPhi())
	return jir_simpleSSA_getReplacementValue(pass, same);
}

static void jir_simpleSSA_tryRemoveDeadAlloca(jir_func_pass_simple_ssa_t* pass, jx_ir_instruction_t* allocaInstr)
{
	JX_CHECK(allocaInstr->m_OpCode == JIR_OP_ALLOCA, "Expected alloca instruction");

	jx_ir_value_t* allocaVal = jx_ir_instrToValue(allocaInstr);

	bool hasOnlyStores = true;
	jx_ir_use_t* use = allocaVal->m_UsesListHead;
	while (use) {
		jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(jx_ir_userToValue(use->m_User));
		if (!userInstr || userInstr->m_OpCode != JIR_OP_STORE) {
			hasOnlyStores = false;
			break;
		}

		use = use->m_Next;
	}

	if (hasOnlyStores) {
		// Remove all stores
		use = allocaVal->m_UsesListHead;
		while (use) {
			jx_ir_use_t* nextUse = use->m_Next;

			jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(jx_ir_userToValue(use->m_User));
			JX_CHECK(userInstr && userInstr->m_OpCode == JIR_OP_STORE, "Shouldn't land here. Something went terribly wrong!");
			jx_ir_bbRemoveInstr(pass->m_Ctx, userInstr->m_ParentBB, userInstr);
			jx_ir_instrFree(pass->m_Ctx, userInstr);

			use = nextUse;
		}

		// Remove the alloca
		jx_ir_bbRemoveInstr(pass->m_Ctx, allocaInstr->m_ParentBB, allocaInstr);
		jx_ir_instrFree(pass->m_Ctx, allocaInstr);
	}
}

static bool jir_simpleSSA_bbIsFilled(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb)
{
	return (jx_ir_bbToValue(bb)->m_Flags & JIR_SIMPLESSA_BB_FLAGS_IS_FILLED_Msk) != 0;
}

static void jir_simpleSSA_fillBlock(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb)
{
	jx_ir_value_t* bbVal = jx_ir_bbToValue(bb);
	bbVal->m_Flags |= JIR_SIMPLESSA_BB_FLAGS_IS_FILLED_Msk;

	// Try to seal all successor 
	const uint32_t numSucc = (uint32_t)jx_array_sizeu(bb->m_SuccArr);
	for (uint32_t iSucc = 0; iSucc < numSucc; ++iSucc) {
		jx_ir_basic_block_t* succ = bb->m_SuccArr[iSucc];
		jir_simpleSSA_trySealBlock(pass, succ);
	}
}

static bool jir_simpleSSA_bbIsSealed(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb)
{
	return (jx_ir_bbToValue(bb)->m_Flags & JIR_SIMPLESSA_BB_FLAGS_IS_SEALED_Msk) != 0;
}

static bool jir_simpleSSA_trySealBlock(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb)
{
	if (jir_simpleSSA_bbIsSealed(pass, bb)) {
		return true;
	}

	const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
	uint32_t numPredFilled = 0;
	for (uint32_t iPred = 0; iPred < numPred; ++iPred) {
		numPredFilled += jir_simpleSSA_bbIsFilled(pass, bb->m_PredArr[iPred]) 
			? 1 
			: 0
			;
	}

	if (numPredFilled == numPred) {
		const uint32_t numIncompletePhis = (uint32_t)jx_array_sizeu(pass->m_IncompletePhis);
		// Process in reverse order in order to be able to remove processed phis without extra bookkeeping.
		for (uint32_t iPhi = numIncompletePhis; iPhi > 0; --iPhi) {
			jir_bb_var_map_item_t* incompletePhi = &pass->m_IncompletePhis[iPhi - 1];
			if (incompletePhi->m_BasicBlock == bb) {
				jir_simpleSSA_addPhiOperands(pass, jx_ir_valueToInstr(incompletePhi->m_Value), incompletePhi->m_Addr);
				jx_array_del(pass->m_IncompletePhis, (iPhi - 1));
			}
		}

		jx_ir_value_t* bbVal = jx_ir_bbToValue(bb);
		bbVal->m_Flags |= JIR_SIMPLESSA_BB_FLAGS_IS_SEALED_Msk;
		return true;
	}

	return false;
}

static void jir_simpleSSA_addIncompletePhi(jir_func_pass_simple_ssa_t* pass, jx_ir_basic_block_t* bb, jx_ir_value_t* addr, jx_ir_value_t* val)
{
	jx_array_push_back(pass->m_IncompletePhis, (jir_bb_var_map_item_t) { .m_BasicBlock = bb, .m_Addr = addr, .m_Value = val });
}

static jx_ir_value_t* jir_simpleSSA_getReplacementValue(jir_func_pass_simple_ssa_t* pass, jx_ir_value_t* val)
{
	jir_value_map_item_t* replacementValItem = jx_hashmapGet(pass->m_ReplacementMap, &(jir_value_map_item_t){ .m_Key = val });
	while (replacementValItem) {
		val = replacementValItem->m_Value;
		replacementValItem = jx_hashmapGet(pass->m_ReplacementMap, &(jir_value_map_item_t){ .m_Key = val });
	}

	return val;
}

static uint64_t jir_bbVarMapItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jir_bb_var_map_item_t* var = (const jir_bb_var_map_item_t*)item;
	uint64_t hash = jx_hashFNV1a(&var->m_BasicBlock, sizeof(jx_ir_basic_block_t*), seed0, seed1);
	hash = jx_hashFNV1a(&var->m_Addr, sizeof(jx_ir_value_t*), hash, seed1);
	return hash;
}

static int32_t jir_bbVarMapItemCompare(const void* a, const void* b, void* udata)
{
	const jir_bb_var_map_item_t* varA = (const jir_bb_var_map_item_t*)a;
	const jir_bb_var_map_item_t* varB = (const jir_bb_var_map_item_t*)b;
	int32_t res = jir_comparePtrs(varA->m_BasicBlock, varB->m_BasicBlock);
	if (res == 0) {
		res = jir_comparePtrs(varA->m_Addr, varB->m_Addr);
	}

	return res;
}

static int32_t jir_comparePtrs(void* a, void* b)
{
	return (uintptr_t)a < (uintptr_t)b
		? -1
		: (uintptr_t)a > (uintptr_t)b ? 1 : 0
		;
}

//////////////////////////////////////////////////////////////////////////
// Constant folding
//
typedef struct jir_func_pass_const_folding_t
{
	jx_allocator_i* m_Allocator;
} jir_func_pass_const_folding_t;

static void jir_funcPass_constantFoldingDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_constantFoldingRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);
static jx_ir_constant_t* jir_constFold_cmpConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs, jx_ir_condition_code cc);
static jx_ir_constant_t* jir_constFold_addConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_subConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_mulConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_divConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_remConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_andConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_orConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_xorConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_shlConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_shrConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs);
static jx_ir_constant_t* jir_constFold_truncConst(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_zextConst(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_sextConst(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_fp2i(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_i2fp(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_fptrunc(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_fpext(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_bitcast(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_inttoptr(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_ptrtoint(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type);
static jx_ir_constant_t* jir_constFold_gep(jx_ir_context_t* ctx, jx_ir_instruction_t* gep);

bool jx_ir_funcPassCreate_constantFolding(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_const_folding_t* inst = (jir_func_pass_const_folding_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_const_folding_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_const_folding_t));
	inst->m_Allocator = allocator;

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_constantFoldingRun;
	pass->destroy = jir_funcPass_constantFoldingDestroy;

	return true;
}

static void jir_funcPass_constantFoldingDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_const_folding_t* pass = (jir_func_pass_const_folding_t*)inst;

	JX_FREE(allocator, pass);
}

static bool jir_funcPass_constantFoldingRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Constant Folding", 1);
	jir_func_pass_const_folding_t* pass = (jir_func_pass_const_folding_t*)inst;

	uint32_t numFolds = 0;

	bool changed = true;
	while (changed) {
		changed = false;
		
		uint32_t prevIterNumFolds = numFolds;
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_ir_instruction_t* instrNext = instr->m_Next;

				jx_ir_constant_t* resConst = NULL;
				switch (instr->m_OpCode) {
				case JIR_OP_BRANCH: {
					const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
					if (numOperands == 1) {
						// Unconditional branch. No op
					} else if (numOperands == 3) {
						// Conditional branch
						jx_ir_constant_t* cond = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
						if (cond) {
							JX_CHECK(jx_ir_constToValue(cond)->m_Type->m_Kind == JIR_TYPE_BOOL, "Expected boolean conditional as 1st branch operand.");
							jx_ir_bbConvertCondBranch(ctx, bb, cond->u.m_Bool);

							++numFolds;
						}
					} else {
						JX_CHECK(false, "Unknown branch instruction");
					}
				} break;
				case JIR_OP_ADD: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_addConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_SUB: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_subConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_MUL: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_mulConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_DIV: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_divConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_REM: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_remConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_AND: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_andConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_OR: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_orConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_XOR: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_xorConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_SET_LE:
				case JIR_OP_SET_GE:
				case JIR_OP_SET_LT:
				case JIR_OP_SET_GT:
				case JIR_OP_SET_EQ:
				case JIR_OP_SET_NE: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_cmpConst(ctx, lhs, rhs, (jx_ir_condition_code)(instr->m_OpCode - JIR_OP_SET_CC_BASE));
					}
				} break;
				case JIR_OP_SHL: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_shlConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_SHR: {
					jx_ir_constant_t* lhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					jx_ir_constant_t* rhs = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
					if (lhs && rhs) {
						resConst = jir_constFold_shrConst(ctx, lhs, rhs);
					}
				} break;
				case JIR_OP_TRUNC: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_truncConst(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_ZEXT: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_zextConst(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_SEXT: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_sextConst(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_FP2SI:
				case JIR_OP_FP2UI: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_fp2i(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_SI2FP: 
				case JIR_OP_UI2FP: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_i2fp(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_FPTRUNC: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_fptrunc(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_FPEXT: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_fpext(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_BITCAST: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_bitcast(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_INT_TO_PTR: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_inttoptr(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_PTR_TO_INT: {
					jx_ir_constant_t* op = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
					if (op) {
						resConst = jir_constFold_ptrtoint(ctx, op, jx_ir_instrToValue(instr)->m_Type);
					}
				} break;
				case JIR_OP_GET_ELEMENT_PTR: {
					resConst = jir_constFold_gep(ctx, instr);
				} break;
				case JIR_OP_PHI:
				case JIR_OP_CALL:
				case JIR_OP_RET:
				case JIR_OP_ALLOCA:
				case JIR_OP_LOAD:
				case JIR_OP_STORE: {
					// No op
				} break;
				default:
					JX_CHECK(false, "Unknown IR op");
					break;
				}

				if (resConst) {
					jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);
					++numFolds;
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}

		changed = prevIterNumFolds != numFolds;
	}

	TracyCZoneEnd(tracyCtx);

	return numFolds != 0;
}

static jx_ir_constant_t* jir_constFold_cmpConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs, jx_ir_condition_code cc)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;
	bool res = false;

	switch (cc) {
	case JIR_CC_LE: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL: { res = lhs->u.m_Bool          <= rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:   { res = (uint8_t)lhs->u.m_U64  <= (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:   { res = (int8_t)lhs->u.m_I64   <= (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:  { res = (uint16_t)lhs->u.m_U64 <= (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:  { res = (int16_t)lhs->u.m_I64  <= (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:  { res = (uint32_t)lhs->u.m_U64 <= (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:  { res = (int32_t)lhs->u.m_I64  <= (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:  { res = lhs->u.m_U64           <= rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:  { res = lhs->u.m_I64           <= rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:
		case JIR_TYPE_F64:  { res = lhs->u.m_F64           <= rhs->u.m_F64;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	case JIR_CC_GE: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL: { res = lhs->u.m_Bool          >= rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:   { res = (uint8_t)lhs->u.m_U64  >= (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:   { res = (int8_t)lhs->u.m_I64   >= (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:  { res = (uint16_t)lhs->u.m_U64 >= (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:  { res = (int16_t)lhs->u.m_I64  >= (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:  { res = (uint32_t)lhs->u.m_U64 >= (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:  { res = (int32_t)lhs->u.m_I64  >= (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:  { res = lhs->u.m_U64           >= rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:  { res = lhs->u.m_I64           >= rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:
		case JIR_TYPE_F64:  { res = lhs->u.m_F64           >= rhs->u.m_F64;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	case JIR_CC_LT: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL: { res = lhs->u.m_Bool          < rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:   { res = (uint8_t)lhs->u.m_U64  < (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:   { res = (int8_t)lhs->u.m_I64   < (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:  { res = (uint16_t)lhs->u.m_U64 < (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:  { res = (int16_t)lhs->u.m_I64  < (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:  { res = (uint32_t)lhs->u.m_U64 < (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:  { res = (int32_t)lhs->u.m_I64  < (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:  { res = lhs->u.m_U64           < rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:  { res = lhs->u.m_I64           < rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:
		case JIR_TYPE_F64:  { res = lhs->u.m_F64           < rhs->u.m_F64;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	case JIR_CC_GT: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL: { res = lhs->u.m_Bool          > rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:   { res = (uint8_t)lhs->u.m_U64  > (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:   { res = (int8_t)lhs->u.m_I64   > (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:  { res = (uint16_t)lhs->u.m_U64 > (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:  { res = (int16_t)lhs->u.m_I64  > (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:  { res = (uint32_t)lhs->u.m_U64 > (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:  { res = (int32_t)lhs->u.m_I64  > (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:  { res = lhs->u.m_U64           > rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:  { res = lhs->u.m_I64           > rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:
		case JIR_TYPE_F64:  { res = lhs->u.m_F64           > rhs->u.m_F64;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	case JIR_CC_EQ: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL:    { res = lhs->u.m_Bool          == rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:      { res = (uint8_t)lhs->u.m_U64  == (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:      { res = (int8_t)lhs->u.m_I64   == (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:     { res = (uint16_t)lhs->u.m_U64 == (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:     { res = (int16_t)lhs->u.m_I64  == (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:     { res = (uint32_t)lhs->u.m_U64 == (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:     { res = (int32_t)lhs->u.m_I64  == (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:     { res = lhs->u.m_U64           == rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:     { res = lhs->u.m_I64           == rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:
		case JIR_TYPE_F64:     { res = lhs->u.m_F64           == rhs->u.m_F64;           } break;
		case JIR_TYPE_POINTER: { res = lhs->u.m_Ptr           == rhs->u.m_Ptr;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	case JIR_CC_NE: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL:    { res = lhs->u.m_Bool          != rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:      { res = (uint8_t)lhs->u.m_U64  != (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:      { res = (int8_t)lhs->u.m_I64   != (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:     { res = (uint16_t)lhs->u.m_U64 != (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:     { res = (int16_t)lhs->u.m_I64  != (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:     { res = (uint32_t)lhs->u.m_U64 != (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:     { res = (int32_t)lhs->u.m_I64  != (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:     { res = lhs->u.m_U64           != rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:     { res = lhs->u.m_I64           != rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:
		case JIR_TYPE_F64:     { res = lhs->u.m_F64           != rhs->u.m_F64;           } break;
		case JIR_TYPE_POINTER: { res = lhs->u.m_Ptr           != rhs->u.m_Ptr;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	default:
		JX_CHECK(false, "Unknown IR condition code.");
		break;
	}

	return jx_ir_constGetBool(ctx, res);
}

static jx_ir_constant_t* jir_constFold_addConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   + rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    + rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 + rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  + rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 + rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  + rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            + rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            + rhs->u.m_I64);  } break;
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, (float)(lhs->u.m_F64    + rhs->u.m_F64)); } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            + rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_subConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   - rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    - rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 - rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  - rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 - rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  - rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            - rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            - rhs->u.m_I64);  } break;
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, (float)(lhs->u.m_F64    - rhs->u.m_F64));  } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            - rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_mulConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   * rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    * rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 * rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  * rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 * rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  * rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            * rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            * rhs->u.m_I64);  } break;
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, (float)(lhs->u.m_F64    * rhs->u.m_F64)); } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            * rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_divConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   / rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    / rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 / rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  / rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 / rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  / rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            / rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            / rhs->u.m_I64);  } break;
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, (float)(lhs->u.m_F64    / rhs->u.m_F64)); } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            / rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_remConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   % rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    % rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 % rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  % rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 % rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  % rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            % rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            % rhs->u.m_I64);  } break;
#if 0 // TODO: fmod?
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, lhs->u.m_F32            % rhs->u.m_F32);  } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            % rhs->u.m_F64);  } break;
#endif
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_andConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   & rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    & rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 & rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  & rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 & rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  & rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            & rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            & rhs->u.m_I64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_orConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   | rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    | rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 | rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  | rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 | rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  | rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            | rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            | rhs->u.m_I64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_xorConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	JX_CHECK(jx_ir_constToValue(lhs)->m_Type == jx_ir_constToValue(rhs)->m_Type, "Expected operands of the same type");
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_BOOL: { res = jx_ir_constGetBool(ctx, lhs->u.m_Bool          ^ rhs->u.m_Bool); } break;
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   ^ rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    ^ rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 ^ rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  ^ rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 ^ rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  ^ rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            ^ rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            ^ rhs->u.m_I64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_shlConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   << rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    << rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 << rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  << rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 << rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  << rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            << rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            << rhs->u.m_I64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_shrConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8:   { res = jx_ir_constGetU8(ctx, (uint8_t)(lhs->u.m_U64   >> rhs->u.m_U64)); } break;
	case JIR_TYPE_I8:   { res = jx_ir_constGetI8(ctx, (int8_t)(lhs->u.m_I64    >> rhs->u.m_I64)); } break;
	case JIR_TYPE_U16:  { res = jx_ir_constGetU16(ctx, (uint16_t)(lhs->u.m_U64 >> rhs->u.m_U64)); } break;
	case JIR_TYPE_I16:  { res = jx_ir_constGetI16(ctx, (int16_t)(lhs->u.m_I64  >> rhs->u.m_I64)); } break;
	case JIR_TYPE_U32:  { res = jx_ir_constGetU32(ctx, (uint32_t)(lhs->u.m_U64 >> rhs->u.m_U64)); } break;
	case JIR_TYPE_I32:  { res = jx_ir_constGetI32(ctx, (int32_t)(lhs->u.m_I64  >> rhs->u.m_I64)); } break;
	case JIR_TYPE_U64:  { res = jx_ir_constGetU64(ctx, lhs->u.m_U64            >> rhs->u.m_U64);  } break;
	case JIR_TYPE_I64:  { res = jx_ir_constGetI64(ctx, lhs->u.m_I64            >> rhs->u.m_I64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_truncConst(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;

	JX_CHECK(operandType->m_Kind > type->m_Kind, "Expected smaller type");

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_U8: 
	case JIR_TYPE_I8:
	case JIR_TYPE_U16:
	case JIR_TYPE_I16:
	case JIR_TYPE_U32:
	case JIR_TYPE_I32:
	case JIR_TYPE_U64: 
	case JIR_TYPE_I64: {
		JX_CHECK(jx_ir_typeIsInteger(type), "Expected integer trunc type");
		res = jx_ir_constGetInteger(ctx, type->m_Kind, op->u.m_I64); 
	} break;
	case JIR_TYPE_F32: {
		JX_CHECK(false, "Don't know how to trunc f32.");
	} break;
	case JIR_TYPE_F64: {
		JX_CHECK(type->m_Kind == JIR_TYPE_F32, "Can only trunc f64 to f32.");
		res = jx_ir_constGetF32(ctx, (float)op->u.m_F64);
	} break;
	default:
		JX_CHECK(false, "Invalid trunc type?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_zextConst(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;

	JX_CHECK(operandType->m_Kind < type->m_Kind, "Expected larger type");

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_BOOL: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, op->u.m_Bool ? 1 : 0);
	} break;
	case JIR_TYPE_U8: 
	case JIR_TYPE_I8: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)((uint8_t)op->u.m_U64));
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)((uint16_t)op->u.m_U64));
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)((uint32_t)op->u.m_U64));
	} break;
	case JIR_TYPE_U64: 
	case JIR_TYPE_I64: {
		JX_CHECK(false, "Don't how to zext 64-bit integers!");
	} break;
	default:
		JX_CHECK(false, "Invalid trunc type?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_sextConst(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;

	JX_CHECK(operandType->m_Kind < type->m_Kind, "Expected larger type");

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_BOOL: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, op->u.m_Bool ? 1 : 0);
	} break;
	case JIR_TYPE_U8: 
	case JIR_TYPE_I8: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)((int8_t)op->u.m_I64));
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)((int16_t)op->u.m_I64));
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)((int32_t)op->u.m_I64));
	} break;
	case JIR_TYPE_U64: 
	case JIR_TYPE_I64: {
		JX_CHECK(false, "Don't how to sext 64-bit integers!");
	} break;
	default:
		JX_CHECK(false, "Invalid trunc type?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_fp2i(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;
	JX_CHECK(jx_ir_typeIsFloatingPoint(operandType), "Expected floating point type");

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_F32:
	case JIR_TYPE_F64: {
		res = jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)(op->u.m_F64));
	} break;
	default:
		JX_CHECK(false, "Unknown floating point type");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_i2fp(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;
	JX_CHECK(jx_ir_typeIsInteger(operandType), "Expected signed integer type");

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
	case JIR_TYPE_BOOL: {
		res = jx_ir_constGetFloat(ctx, type->m_Kind, op->u.m_Bool ? 1.0 : 0.0);
	} break;
	case JIR_TYPE_U8: 
	case JIR_TYPE_I8: {
		res = jx_ir_constGetFloat(ctx, type->m_Kind, (double)((int8_t)op->u.m_I64));
	} break;
	case JIR_TYPE_U16:
	case JIR_TYPE_I16: {
		res = jx_ir_constGetFloat(ctx, type->m_Kind, (double)((int16_t)op->u.m_I64));
	} break;
	case JIR_TYPE_U32:
	case JIR_TYPE_I32: {
		res = jx_ir_constGetFloat(ctx, type->m_Kind, (double)((int32_t)op->u.m_I64));
	} break;
	case JIR_TYPE_U64: 
	case JIR_TYPE_I64: {
		res = jx_ir_constGetFloat(ctx, type->m_Kind, (double)op->u.m_I64);
	} break;
	default:
		JX_CHECK(false, "Unknown floating point type");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_fptrunc(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;
	JX_CHECK(jx_ir_typeIsFloatingPoint(operandType), "Expected floating point type");
	return jx_ir_constGetFloat(ctx, type->m_Kind, op->u.m_F64);
}

static jx_ir_constant_t* jir_constFold_fpext(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;
	JX_CHECK(jx_ir_typeIsFloatingPoint(operandType), "Expected floating point type");
	return jx_ir_constGetFloat(ctx, type->m_Kind, op->u.m_F64);
}

static jx_ir_constant_t* jir_constFold_bitcast(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;
	if (jx_ir_typeIsInteger(operandType)) {
		JX_CHECK(jx_ir_typeIsInteger(type), "Expected integer type");
		return jx_ir_constGetInteger(ctx, type->m_Kind, op->u.m_I64);
	}

	JX_CHECK(operandType->m_Kind == JIR_TYPE_POINTER && type->m_Kind == JIR_TYPE_POINTER, "Expected pointer types.");
	return jx_ir_constPointer(ctx, type, (void*)op->u.m_Ptr);
}

static jx_ir_constant_t* jir_constFold_inttoptr(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;
	JX_CHECK(jx_ir_typeIsInteger(operandType), "Expected integer type");
	return jx_ir_constPointer(ctx, type, (void*)op->u.m_Ptr);
}

static jx_ir_constant_t* jir_constFold_ptrtoint(jx_ir_context_t* ctx, jx_ir_constant_t* op, jx_ir_type_t* type)
{
	jx_ir_type_t* operandType = jx_ir_constToValue(op)->m_Type;
	JX_CHECK(operandType->m_Kind == JIR_TYPE_POINTER, "Expected pointer type");
	return jx_ir_constGetInteger(ctx, type->m_Kind, (int64_t)op->u.m_Ptr);
}

static jx_ir_constant_t* jir_constFold_gep(jx_ir_context_t* ctx, jx_ir_instruction_t* gep)
{
	jx_ir_constant_t* constPtr = jx_ir_valueToConst(jx_ir_instrGetOperandVal(gep, 0));
	if (!constPtr) {
		return NULL;
	}

	jx_ir_type_t* basePtrType = constPtr->super.super.m_Type;

	int64_t offset = 0;

	const uint32_t numOperands = (uint32_t)jx_array_sizeu(gep->super.m_OperandArr);
	for (uint32_t iOperand = 1; iOperand < numOperands; ++iOperand) {
		jx_ir_constant_t* constIndex = jx_ir_valueToConst(jx_ir_instrGetOperandVal(gep, iOperand));
		if (!constIndex) {
			return NULL;
		}

		if (basePtrType->m_Kind == JIR_TYPE_POINTER) {
			JX_CHECK(iOperand == 1, "Only first index can be on a pointer type.");
			jx_ir_type_pointer_t* ptrType = jx_ir_typeToPointer(basePtrType);
			const size_t itemSize = jx_ir_typeGetSize(ptrType->m_BaseType);
			const int64_t displacement = constIndex->u.m_I64 * (int64_t)itemSize;
			offset += displacement;
			basePtrType = ptrType->m_BaseType;
		} else if (basePtrType->m_Kind == JIR_TYPE_ARRAY) {
			jx_ir_type_array_t* arrType = jx_ir_typeToArray(basePtrType);
			const size_t itemSize = jx_ir_typeGetSize(arrType->m_BaseType);
			const int64_t displacement = constIndex->u.m_I64 * (int64_t)itemSize;
			offset += displacement;
			basePtrType = arrType->m_BaseType;
		} else if (basePtrType->m_Kind == JIR_TYPE_STRUCT) {
			jx_ir_type_struct_t* structType = jx_ir_typeToStruct(basePtrType);
			JX_CHECK(constIndex->u.m_I64 < structType->m_NumMembers, "Invalid struct member index!");
			jx_ir_struct_member_t* member = &structType->m_Members[constIndex->u.m_I64];
			const uint32_t memberOffset = (uint32_t)jx_ir_typeStructGetMemberOffset(structType, (uint32_t)constIndex->u.m_I64);
			offset += memberOffset;
			basePtrType = member->m_Type;
		} else {
			JX_CHECK(false, "Unexpected type in GEP index list");
		}
	}

	return jx_ir_constPointer(ctx, gep->super.super.m_Type, (void*)offset);
}

//////////////////////////////////////////////////////////////////////////
// Peephole optimizations
//
typedef struct jir_func_pass_peephole_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_context_t* m_Ctx;
	jx_ir_function_t* m_Func;
} jir_func_pass_peephole_t;

static void jir_funcPass_peepholeDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_peepholeRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

static bool jir_peephole_setcc(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_div(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_mul(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_add(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_sub(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_and(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_or(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_xor(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_getElementPtr(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_phi(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);
static bool jir_peephole_branch(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr);

bool jx_ir_funcPassCreate_peephole(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_peephole_t* inst = (jir_func_pass_peephole_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_peephole_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_peephole_t));
	inst->m_Allocator = allocator;

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_peepholeRun;
	pass->destroy = jir_funcPass_peepholeDestroy;

	return true;
}

static void jir_funcPass_peepholeDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_peephole_t* pass = (jir_func_pass_peephole_t*)inst;
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_peepholeRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Peephole", 1);

	jir_func_pass_peephole_t* pass = (jir_func_pass_peephole_t*)inst;

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	uint32_t numOpts = 0;
	bool changed = true;
	while (changed) {
		changed = false;

		const uint32_t prevIterNumOpts = numOpts;

		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_ir_instruction_t* instrNext = instr->m_Next;

				if (jx_ir_opcodeIsSetcc(instr->m_OpCode)) {
					numOpts += jir_peephole_setcc(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_DIV) {
					numOpts += jir_peephole_div(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_MUL) {
					numOpts += jir_peephole_mul(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_ADD) {
					numOpts += jir_peephole_add(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_SUB) {
					numOpts += jir_peephole_sub(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_AND) {
					numOpts += jir_peephole_and(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_OR) {
					numOpts += jir_peephole_or(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_XOR) {
					numOpts += jir_peephole_xor(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_GET_ELEMENT_PTR) {
					numOpts += jir_peephole_getElementPtr(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_PHI) {
					numOpts += jir_peephole_phi(pass, instr) ? 1 : 0;
				} else if (instr->m_OpCode == JIR_OP_BRANCH) {
					numOpts += jir_peephole_branch(pass, instr) ? 1 : 0;
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}

		changed = prevIterNumOpts != numOpts;
	}

	// Remove dead instructions...
	// TODO: Turn into a function?
	{
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_ir_instruction_t* instrNext = instr->m_Next;

				if (jx_ir_instrIsDead(instr)) {
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);
					++numOpts;
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}
	}

	TracyCZoneEnd(tracyCtx);

	return numOpts != 0;
}

static bool jir_peephole_setcc(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
	if (!constOp1) {
		return false;
	}

	jx_ir_value_t* valOp0 = jx_ir_instrGetOperandVal(instr, 0);

	bool res = false;
	if (instr->m_OpCode == JIR_OP_SET_EQ || instr->m_OpCode == JIR_OP_SET_NE) {
		jx_ir_instruction_t* instrOp0 = jx_ir_valueToInstr(valOp0);
		if (valOp0->m_Type->m_Kind == JIR_TYPE_BOOL) {
			if ((instr->m_OpCode == JIR_OP_SET_EQ && constOp1->u.m_Bool) || (instr->m_OpCode == JIR_OP_SET_NE && !constOp1->u.m_Bool)) {
				// %cmp = seteq bool, %bool_val, true 
				//  or
				// %cmp = setne bool, %bool_val, false
				//  => 
				// replace %cmp with %bool_val
				jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), valOp0);
				jx_ir_bbRemoveInstr(ctx, bb, instr);
				jx_ir_instrFree(ctx, instr);
			} else {
				// TODO: 
				// %cmp = seteq bool, %bool_val, false 
				//  or
				// %cmp = setne bool, %bool_val, true
				//  => 
				// if %bool_val is a setcc instruction, invert the cc, otherwise xor with true to invert the boolean.
				JX_NOT_IMPLEMENTED();
			}
		} else if (instrOp0 && constOp1) {
#if 0
			if (jx_ir_opcodeIsSetcc(instrOp0->m_OpCode)) {
				// TODO: Remove. The case above handles this as well.
				// %res = setcc bool, %a, %b
				// %cmp = seteq/setne bool, %res, true/false
				//   =>
				// %cmp = setcc bool, %a, %b // cc might be different than original cc
				if (instr->m_OpCode == JIR_OP_SET_EQ) {
					if (constOp1->u.m_Bool) {
						JX_CHECK(false, "Should be the same as SetNE/false. Implement when assert is hit.");
					} else {
						JX_CHECK(false, "Should probably invert cc. Implement when assert is hit.");
					}
				} else if (instr->m_OpCode == JIR_OP_SET_NE) {
					if (constOp1->u.m_Bool) {
						JX_CHECK(false, "Should probably invert cc. Implement when assert is hit.");
					} else {
						// %res = setcc bool, %a, %b
						// %cmp = setne bool %res, false
						//  =>
						// %cmp = setcc bool, %a, %b
						// 
						// Create new setcc instruction with the same opcode as the 
						// original instruction.
						jx_ir_condition_code cc = instrOp0->m_OpCode - JIR_OP_SET_CC_BASE;
						jx_ir_instruction_t* newSetcc = jx_ir_instrSetCC(ctx, cc, jx_ir_instrGetOperandVal(instrOp0, 0), jx_ir_instrGetOperandVal(instrOp0, 1));
						jx_ir_bbInsertInstrBefore(ctx, bb, instr, newSetcc);

						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(newSetcc));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);

						res = true;
					}
				}
			} else 
#endif
			if (instr->m_OpCode == JIR_OP_SET_NE && (instrOp0->m_OpCode == JIR_OP_SEXT || instrOp0->m_OpCode == JIR_OP_ZEXT) && jx_ir_instrGetOperandVal(instrOp0, 0)->m_Type->m_Kind == JIR_TYPE_BOOL && jx_ir_constIsZero(constOp1)) {
				// %extVal = zext i32, bool %val
				// %res = setne bool, i32 %extVal, i32 0
				//  =>
				// replace %res with %val and remove instruction
				jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instrOp0, 0));
				jx_ir_bbRemoveInstr(ctx, bb, instr);
				jx_ir_instrFree(ctx, instr);

				res = true;
			}
		}
	} else {
		// TODO: setlt unsigned, 0 => false, setge unsigned, 0 => true
	}

	return res;
}

static bool jir_peephole_div(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	jx_ir_value_t* valOp1 = jx_ir_instrGetOperandVal(instr, 1);
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(valOp1);
	const bool isInteger = jx_ir_typeIsInteger(valOp1->m_Type);
	if (constOp1) {
		if ((isInteger && constOp1->u.m_I64 == 1) || (!isInteger && constOp1->u.m_F64 == 1.0)) {
			// %res = div %val, 1 
			//  => 
			// replace %res with %val and remove instruction
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		} else if (isInteger && constOp1->u.m_I64 > 0 && jx_isPow2_u32((uint32_t)constOp1->u.m_I64)) {
			// %res = div %val, imm_pow2
			//  =>
			// %res = shr %val, log2(imm_pow2)
			jx_ir_constant_t* constI8 = jx_ir_constGetI8(ctx, jx_log2_u32((uint32_t)constOp1->u.m_I64));
			jx_ir_instruction_t* shrInstr = jx_ir_instrShr(ctx, jx_ir_instrGetOperandVal(instr, 0), jx_ir_constToValue(constI8));
			jx_ir_bbInsertInstrBefore(ctx, bb, instr, shrInstr);
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(shrInstr));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		}
	}

	return res;
}

static bool jir_peephole_mul(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	jx_ir_value_t* valOp1 = jx_ir_instrGetOperandVal(instr, 1);
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(valOp1);
	const bool isInteger = jx_ir_typeIsInteger(valOp1->m_Type);
	if (constOp1) {
		if ((isInteger && constOp1->u.m_I64 == 1) || (!isInteger && constOp1->u.m_F64 == 1.0)) {
			// %res = mul %val, 1 
			//  => 
			// replace %res with %val and remove instruction
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		} else if (isInteger && constOp1->u.m_I64 > 0 && jx_isPow2_u32((uint32_t)constOp1->u.m_I64)) {
			// %res = mul %val, imm_pow2
			//  =>
			// %res = shl %val, log2(imm_pow2)
			jx_ir_constant_t* constI8 = jx_ir_constGetI8(ctx, jx_log2_u32((uint32_t)constOp1->u.m_I64));
			jx_ir_instruction_t* shlInstr = jx_ir_instrShl(ctx, jx_ir_instrGetOperandVal(instr, 0), jx_ir_constToValue(constI8));
			jx_ir_bbInsertInstrBefore(ctx, bb, instr, shlInstr);
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(shlInstr));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		}
	}

	return res;
}

static bool jir_peephole_add(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	jx_ir_value_t* valOp1 = jx_ir_instrGetOperandVal(instr, 1);
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(valOp1);
	if (constOp1) {
		if (jx_ir_constIsZero(constOp1)) {
			// %res = add %val, 0
			//  => 
			// replace %res with %val and remove instruction
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		} else if (jx_ir_typeIsInteger(valOp1->m_Type)) {
			// Check if we are subtracting the same constant we added in a previous instruction.
			// E.g. 
			//   %344 = add i32, i32 %1566, i32 1
			//   %346 = add i32, i32 %344, i32 -1
			// => 
			// replace %346 with %1566
			//
			// NOTE: Only do this for integer addition.
			jx_ir_instruction_t* instrOp0 = jx_ir_valueToInstr(jx_ir_instrGetOperandVal(instr, 0));
			if (instrOp0 && instrOp0->m_OpCode == JIR_OP_ADD) {
				jx_ir_constant_t* instrOp0_constOp1 = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instrOp0, 1));
				if (instrOp0_constOp1 && instrOp0_constOp1->u.m_I64 == -constOp1->u.m_I64) {
					jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instrOp0, 0));
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);
				}
			}
		}
	}

	return res;
}

static bool jir_peephole_sub(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	jx_ir_value_t* valOp1 = jx_ir_instrGetOperandVal(instr, 1);
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(valOp1);
	const bool isInteger = jx_ir_typeIsInteger(valOp1->m_Type);
	if (constOp1) {
		if ((isInteger && constOp1->u.m_I64 == 0) || (!isInteger && constOp1->u.m_F64 == 0.0)) {
			// %res = sub %val, 0
			//  => 
			// replace %res with %val and remove instruction
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		}
	}

	return res;
}

static bool jir_peephole_and(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	jx_ir_value_t* valOp1 = jx_ir_instrGetOperandVal(instr, 1);
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(valOp1);
	JX_CHECK(jx_ir_typeIsIntegral(valOp1->m_Type), "Expected integer type");
	if (constOp1) {
		if (jx_ir_constIsZero(constOp1)) {
			// %res = and %val, 0
			//  =>
			// replace %res with 0 and remove instruction
			jx_ir_constant_t* zero = jx_ir_constGetZero(ctx, instr->super.super.m_Type);
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(zero));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		} else if (jx_ir_constIsOnes(constOp1)) {
			// %res = and %val, all_ones 
			//  => 
			// replace %res with %val and remove instruction
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		}
	} else if (jx_ir_instrGetOperandVal(instr, 0) == jx_ir_instrGetOperandVal(instr, 1)) {
		// %res = and %x, %x => %res = %x
		JX_NOT_IMPLEMENTED();
	}

	return res;
}

static bool jir_peephole_or(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	jx_ir_value_t* valOp1 = jx_ir_instrGetOperandVal(instr, 1);
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(valOp1);
	JX_CHECK(jx_ir_typeIsIntegral(valOp1->m_Type), "Expected integer type");
	if (constOp1) {
		if (jx_ir_constIsZero(constOp1)) {
			// %res = or %val, 0
			//  =>
			// replace %res with %val and remove instruction
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		} else if (jx_ir_constIsOnes(constOp1)) {
			// %res = or %val, all_ones 
			//  => 
			// replace %res with all ones and remove instruction
			jx_ir_constant_t* ones = jx_ir_constGetOnes(ctx, instr->super.super.m_Type);
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(ones));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);
			
			res = true;
		}
	} else if (jx_ir_instrGetOperandVal(instr, 0) == jx_ir_instrGetOperandVal(instr, 1)) {
		// %res = or %x, %x => %res = %x
		JX_NOT_IMPLEMENTED();
	}

	return res;
}

static bool jir_peephole_xor(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;
	jx_ir_value_t* valOp1 = jx_ir_instrGetOperandVal(instr, 1);
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(valOp1);
	JX_CHECK(jx_ir_typeIsIntegral(valOp1->m_Type), "Expected integer type");
	if (constOp1) {
		if (jx_ir_constIsZero(constOp1)) {
			// %res = xor %val, 0
			//  =>
			// replace %res with %val and remove instruction
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			res = true;
		} else if (jx_ir_constIsOnes(constOp1)) {
			// If it's a boolean check if the conditional that generated the boolean can be inverted.
			if (jx_ir_instrToValue(instr)->m_Type->m_Kind == JIR_TYPE_BOOL) {
				jx_ir_value_t* valOp0 = jx_ir_instrGetOperandVal(instr, 0);
				jx_ir_instruction_t* instrOp0 = jx_ir_valueToInstr(valOp0);
				if (jx_ir_opcodeIsSetcc(instrOp0->m_OpCode)) {
					// Insert a new setcc instruction before the xor with inverted cc and remove the xor.
					// Dead Instruction Elimination below will remove the original setcc if it's unused.
					jx_ir_condition_code cc = instrOp0->m_OpCode - JIR_OP_SET_CC_BASE;
					jx_ir_instruction_t* newSetcc = jx_ir_instrSetCC(ctx, jx_ir_ccInvert(cc), jx_ir_instrGetOperandVal(instrOp0, 0), jx_ir_instrGetOperandVal(instrOp0, 1));
					jx_ir_bbInsertInstrBefore(ctx, bb, instr, newSetcc);

					// Replace existing value with new instruction
					jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(newSetcc));

					// Remove old instruction
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);

					res = true;
				}
			}
		}
	}

	return res;
}

static bool jir_peephole_getElementPtr(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	jx_ir_instruction_t* instrOp0 = jx_ir_valueToInstr(jx_ir_instrGetOperandVal(instr, 0));
	jx_ir_constant_t* constOp1 = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));

	bool res = false;
	if (jx_array_sizeu(instr->super.m_OperandArr) == 2 && constOp1) {
		if (constOp1->u.m_I64 < 0) {
			// %addr1 = getelementptr %ptr, const
			// %addr2 = getelementptr %addr1, -const
			//  =>
			// Replace %addr2 with %ptr
			// 
			// Check if the first operand (the pointer) is the result of another GEP with the same
			// positive constant.
			if (instrOp0 && instrOp0->m_OpCode == JIR_OP_GET_ELEMENT_PTR && jx_array_sizeu(instrOp0->super.m_OperandArr) == 2) {
				jx_ir_constant_t* instrOp0_constOp1 = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instrOp0, 1));
				if (instrOp0_constOp1 && instrOp0_constOp1->u.m_I64 == -constOp1->u.m_I64) {
					jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instrOp0, 0));
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);
					res = true;
				}
			}
		} else if (constOp1->u.m_I64 == 0) {
			// TODO: Can I do the same if there are more than 1 indices and they are all 0?
			// %addr = getelementptr %ptr, 0
			//  =>
			// Replace %addr with %ptr
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrGetOperandVal(instr, 0));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);
			res = true;
		}
	}

#if 0
	if (!res) {
		// Try to merge getelementptrs 
		// TODO: If constant is not 0 it might be beneficial to insert an add instruction to the first 
		// index and merge GEPs either way.
		if (constOp1 && constOp1->u.m_I64 == 0 && instrOp0 && instrOp0->m_OpCode == JIR_OP_GET_ELEMENT_PTR) {
			jx_ir_value_t* ptr = jx_ir_instrGetOperandVal(instrOp0, 0);

			const uint32_t instrOp0_numOperands = (uint32_t)jx_array_sizeu(instrOp0->super.m_OperandArr);
			const uint32_t instr_numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
			uint32_t numIndices = 0
				+ (instrOp0_numOperands - 1)
				+ (instr_numOperands - 2)
				;
			jx_ir_value_t** indices = (jx_ir_value_t**)JX_ALLOC(pass->m_Allocator, sizeof(jx_ir_value_t*) * numIndices);
			if (!indices) {
				return false;
			}

			for (uint32_t iOperand = 1; iOperand < instrOp0_numOperands; ++iOperand) {
				indices[iOperand - 1] = jx_ir_instrGetOperandVal(instrOp0, iOperand);
			}
			for (uint32_t iOperand = 2; iOperand < instr_numOperands; ++iOperand) {
				indices[instrOp0_numOperands + iOperand - 3] = jx_ir_instrGetOperandVal(instr, iOperand);
			}

			jx_ir_instruction_t* newGEPInstr = jx_ir_instrGetElementPtr(ctx, ptr, numIndices, indices);
			jx_ir_bbInsertInstrBefore(ctx, bb, instr, newGEPInstr);
			jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(newGEPInstr));
			jx_ir_bbRemoveInstr(ctx, bb, instr);
			jx_ir_instrFree(ctx, instr);

			JX_FREE(pass->m_Allocator, indices);

			res = true;
		}
	}
#endif

	return res;
}

static bool jir_peephole_isOnlyPhiInBlock(jx_ir_basic_block_t* bb, jx_ir_instruction_t* instr)
{
	JX_CHECK(instr->m_OpCode == JIR_OP_PHI, "Not a phi instruction.");
	bool hasOtherPhi = false;
	jx_ir_instruction_t* bbInstr = bb->m_InstrListHead;
	while (bbInstr->m_OpCode == JIR_OP_PHI) {
		if (bbInstr != instr) {
			hasOtherPhi = true;
			break;
		}
		bbInstr = bbInstr->m_Next;
	}

	return !hasOtherPhi;
}

static bool jir_peephole_phi(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_function_t* func = pass->m_Func;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;

	bool res = false;

	if (jx_ir_instrToValue(instr)->m_Type->m_Kind == JIR_TYPE_BOOL && (uint32_t)jx_array_sizeu(instr->super.m_OperandArr) == 4 && jir_peephole_isOnlyPhiInBlock(bb, instr)) {
		// Try to simplify the logic around 
		//   %cmp = phi bool [true, %BB1], [false, %BB2]
		// by rewriting branches on predecessors.
		JX_CHECK(jx_array_sizeu(bb->m_PredArr) == 2, "Invalid phi instruction?");
		jx_ir_constant_t* constTrue = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0));
		jx_ir_constant_t* constFalse = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 2));
		jx_ir_basic_block_t* bbTrue = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(instr, 1));
		jx_ir_basic_block_t* bbFalse = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(instr, 3));

		// NOTE: If both constants are the same it will be handled by the constant folding pass
		if (constTrue && constFalse && constTrue->u.m_Bool != constFalse->u.m_Bool) {
			JX_CHECK(bbTrue && bbFalse, "Invalid phi operands?");
			if (constFalse->u.m_Bool) {
				// Swap consts and basic blocks so that cTrue/bbTrue is always the true case
				// and cFalse/bbFalse is the false case.
				{ jx_ir_constant_t* tmp = constFalse; constFalse = constTrue; constTrue = tmp; }
				{ jx_ir_basic_block_t* tmp = bbFalse; bbFalse = bbTrue; bbTrue = tmp; }
			}

			// Both basic blocks should have a single unconditional jump instruction.
			if (jx_ir_instrIsUncondBranch(bbTrue->m_InstrListHead) && jx_ir_instrIsUncondBranch(bbFalse->m_InstrListHead)) {
				JX_CHECK(!bbTrue->m_InstrListHead->m_Next && !bbFalse->m_InstrListHead->m_Next, "Expected branch to be the last bb instruction");

				jx_ir_basic_block_t* trueBlockSources[2] = { 0 };
				jx_ir_basic_block_t* falseBlockSources[2] = { 0 };
				uint32_t numTrueBlockSources = 0;
				uint32_t numFalseBlockSources = 0;

				jx_ir_use_t* bbTrueUse = bbTrue->super.m_UsesListHead;
				while (bbTrueUse) {
					jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(jx_ir_userToValue(bbTrueUse->m_User));
					if (userInstr && jx_ir_instrIsCondBranch(userInstr)) {
						if (numTrueBlockSources < 2) {
							trueBlockSources[numTrueBlockSources++] = userInstr->m_ParentBB;
						} else {
							++numTrueBlockSources;
							break;
						}
					}

					bbTrueUse = bbTrueUse->m_Next;
				}

				jx_ir_use_t* bbFalseUse = bbFalse->super.m_UsesListHead;
				while (bbFalseUse) {
					jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(jx_ir_userToValue(bbFalseUse->m_User));
					if (userInstr && jx_ir_instrIsCondBranch(userInstr)) {
						if (numFalseBlockSources < 2) {
							falseBlockSources[numFalseBlockSources++] = userInstr->m_ParentBB;
						} else {
							++numFalseBlockSources;
							break;
						}
					}

					bbFalseUse = bbFalseUse->m_Next;
				}

				if (numTrueBlockSources == 2 && numFalseBlockSources == 1) {
					// bb0:
					//   ...
					//   %cmp1 = setne bool, ...
					//   br bool %cmp1, label %bbTrue, label %bb1
					// bb1:
					//   ...
					//   %cmp2 = setne bool, ...
					//   br bool %cmp2, label %bbTrue, label %bbFalse
					// bbFalse:
					//   br label %bbJoin
					// bbTrue:
					//   br label %bbJoin
					// bbJoin:
					//   %cmpOr = phi bool [true, %bbTrue], [false, %bbFalse]
					//   ...
					//   br bool %cmpOr, ...
					// 
					//  =>
					// 
					// bb0:
					//   ...
					//   %cmp1 = setne bool, ...
					//   br bool %cmp1, label %bbJoin, label %bb1
					// bb1:
					//   ...
					//   %cmp2 = setne bool, ...
					//   br label %bbJoin
					// bbJoin:
					//   %cmpOr = phi bool [true, %bb0], [%cmp2, %bb1]
					//   ...
					//   br bool %cmpOr, ...
					// 
					jx_ir_basic_block_t* secondBB = falseBlockSources[0];
					jx_ir_basic_block_t* firstBB = trueBlockSources[0] == secondBB
						? trueBlockSources[1]
						: trueBlockSources[0]
						;

					jx_ir_instruction_t* firstBBLastInstr = jx_ir_bbGetLastInstr(ctx, firstBB);
					JX_CHECK(jx_ir_instrIsCondBranch(firstBBLastInstr), "!!!");
					jx_ir_instruction_t* secondBBLastInstr = jx_ir_bbGetLastInstr(ctx, secondBB);
					JX_CHECK(jx_ir_instrIsCondBranch(secondBBLastInstr), "!!!");

					const bool operandsOK = true
						&& jx_ir_instrGetOperandVal(firstBBLastInstr, 1) == jx_ir_bbToValue(bbTrue)
						&& jx_ir_instrGetOperandVal(secondBBLastInstr, 1) == jx_ir_bbToValue(bbTrue)
						&& jx_ir_instrGetOperandVal(secondBBLastInstr, 2) == jx_ir_bbToValue(bbFalse)
						;
					if (operandsOK) {
						// Replace conditional branch of first block
						jx_ir_instruction_t* firstBBNewBranch = jx_ir_instrBranchIf(ctx, jx_ir_instrGetOperandVal(firstBBLastInstr, 0), bb, jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(firstBBLastInstr, 2)));
						jx_ir_bbRemoveInstr(ctx, firstBB, firstBBLastInstr);
						jx_ir_bbAppendInstr(ctx, firstBB, firstBBNewBranch);
						jx_ir_instrFree(ctx, firstBBLastInstr);

						// Replace conditional branch of second block.
						jx_ir_value_t* secondBBCondVal = jx_ir_instrGetOperandVal(secondBBLastInstr, 0);
						jx_ir_instruction_t* secondBBNewBranch = jx_ir_instrBranch(ctx, bb);
						jx_ir_bbRemoveInstr(ctx, secondBB, secondBBLastInstr);
						jx_ir_bbAppendInstr(ctx, secondBB, secondBBNewBranch);
						jx_ir_instrFree(ctx, secondBBLastInstr);

						// Replace phi with new phi
						jx_ir_instruction_t* newPhi = jx_ir_instrPhi(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_BOOL));
						jx_ir_instrPhiAddValue(ctx, newPhi, firstBB, jx_ir_constToValue(jx_ir_constGetBool(ctx, true)));
						jx_ir_instrPhiAddValue(ctx, newPhi, secondBB, secondBBCondVal);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(newPhi));
						jx_ir_bbPrependInstr(ctx, bb, newPhi);
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);

						// Remove basic true/false blocks in order to update the CFG
						JX_CHECK(!bbTrue->super.m_UsesListHead, "!!!");
						JX_CHECK(!bbFalse->super.m_UsesListHead, "!!!");
						jx_ir_funcRemoveBasicBlock(ctx, func, bbTrue);
						jx_ir_funcRemoveBasicBlock(ctx, func, bbFalse);
						jx_ir_bbFree(ctx, bbTrue);
						jx_ir_bbFree(ctx, bbFalse);

						res = true;
					}
				} else if (numTrueBlockSources == 1 && numFalseBlockSources == 2) {
					// bb0:
					//   ...
					//   %cmp1 = setlt bool, ...
					//   br bool %cmp1, label %bb1, label %bbFalse
					// bb1:
					//   ...
					//   %cmp2 = setlt bool, ...
					//   br bool %cmp2, label %bbTrue, label %bbFalse
					// bbFalse:
					//   br label %bbJoin
					// bbTrue:
					//   br label %bbJoin
					// bbJoin:
					//   %cmpAnd = phi bool [true, %bbTrue], [false, %bbFalse]
					//   ...
					//   br bool %cmpAnd, ...
					// 
					//  =>
					// 
					// bb0:
					//   ...
					//   %cmp1 = setlt bool, ...
					//   br bool %cmp1, label %bb1, label %bbJoin
					// bb1:
					//   ...
					//   %cmp2 = setlt bool, ...
					//   br label %bbJoin
					// bbJoin:
					//   %cmpAnd = phi bool [%cmp2, %bb1], [false, %bb0]
					//   ...
					//   br bool %cmpAnd, ...
					// 
					jx_ir_basic_block_t* secondBB = trueBlockSources[0];
					jx_ir_basic_block_t* firstBB = falseBlockSources[0] == secondBB
						? falseBlockSources[1]
						: falseBlockSources[0]
						;

					jx_ir_instruction_t* firstBBLastInstr = jx_ir_bbGetLastInstr(ctx, firstBB);
					JX_CHECK(jx_ir_instrIsCondBranch(firstBBLastInstr), "!!!");
					jx_ir_instruction_t* secondBBLastInstr = jx_ir_bbGetLastInstr(ctx, secondBB);
					JX_CHECK(jx_ir_instrIsCondBranch(secondBBLastInstr), "!!!");

					const bool operandsOK = true
						&& jx_ir_instrGetOperandVal(firstBBLastInstr, 2) == jx_ir_bbToValue(bbFalse)
						&& jx_ir_instrGetOperandVal(secondBBLastInstr, 1) == jx_ir_bbToValue(bbTrue)
						&& jx_ir_instrGetOperandVal(secondBBLastInstr, 2) == jx_ir_bbToValue(bbFalse)
						;
					if (operandsOK) {
						// Replace conditional branch of first block
						jx_ir_instruction_t* firstBBNewBranch = jx_ir_instrBranchIf(ctx, jx_ir_instrGetOperandVal(firstBBLastInstr, 0), jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(firstBBLastInstr, 1)), bb);
						jx_ir_bbRemoveInstr(ctx, firstBB, firstBBLastInstr);
						jx_ir_bbAppendInstr(ctx, firstBB, firstBBNewBranch);
						jx_ir_instrFree(ctx, firstBBLastInstr);

						// Replace conditional branch of second block.
						jx_ir_value_t* secondBBCondVal = jx_ir_instrGetOperandVal(secondBBLastInstr, 0);
						jx_ir_instruction_t* secondBBNewBranch = jx_ir_instrBranch(ctx, bb);
						jx_ir_bbRemoveInstr(ctx, secondBB, secondBBLastInstr);
						jx_ir_bbAppendInstr(ctx, secondBB, secondBBNewBranch);
						jx_ir_instrFree(ctx, secondBBLastInstr);

						// Replace phi with new phi
						jx_ir_instruction_t* newPhi = jx_ir_instrPhi(ctx, jx_ir_typeGetPrimitive(ctx, JIR_TYPE_BOOL));
						jx_ir_instrPhiAddValue(ctx, newPhi, firstBB, jx_ir_constToValue(jx_ir_constGetBool(ctx, false)));
						jx_ir_instrPhiAddValue(ctx, newPhi, secondBB, secondBBCondVal);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(newPhi));
						jx_ir_bbPrependInstr(ctx, bb, newPhi);
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);

						// Remove basic true/false blocks in order to update the CFG
						JX_CHECK(!bbTrue->super.m_UsesListHead, "!!!");
						JX_CHECK(!bbFalse->super.m_UsesListHead, "!!!");
						jx_ir_funcRemoveBasicBlock(ctx, func, bbTrue);
						jx_ir_funcRemoveBasicBlock(ctx, func, bbFalse);
						jx_ir_bbFree(ctx, bbTrue);
						jx_ir_bbFree(ctx, bbFalse);

						res = true;
					}
				} else {
					// Don't know what to do.
					// TODO: DEBUG: Triggers on c-testsuite/00033.c
//					JX_NOT_IMPLEMENTED();
				}
			}
		}
	}

	// TODO: 
	// 2. phi type, [val, BB1], [val, BB2], [val, BB3], ..., [other_val, BBN] => Simplify conditionals

	return res;
}

static bool jir_peephole_branch(jir_func_pass_peephole_t* pass, jx_ir_instruction_t* instr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;
	jx_ir_basic_block_t* bb = instr->m_ParentBB;
	
	bool res = false;
	if (jx_ir_instrIsCondBranch(instr)) {
		// Try to remove unneeded conditional branches (usually appear from dead code, like asserts in release builds).
		// E.g. 
		//   br %bool_val, %bb1, %bb2;
		// bb1:
		//   br %bbJoin;
		// bb2:
		//   br %bbJoin;
		// bbJoin:
		//   No phi instructions here.
		//
		// In the example above the conditional branch on %bool_val can safely be turned into an unconditional
		// branch to bbJoin, since there are no phi instructions which depend on the control flow.
		jx_ir_value_t* condVal = jx_ir_instrGetOperandVal(instr, 0);
		jx_ir_basic_block_t* bbTrue = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(instr, 1));
		jx_ir_basic_block_t* bbFalse = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(instr, 2));
		if (jx_ir_instrIsUncondBranch(bbTrue->m_InstrListHead) && jx_ir_instrIsUncondBranch(bbFalse->m_InstrListHead)) {
			jx_ir_basic_block_t* bbTrueTarget = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(bbTrue->m_InstrListHead, 0));
			jx_ir_basic_block_t* bbFalseTarget = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(bbFalse->m_InstrListHead, 0));
			if (bbTrueTarget == bbFalseTarget) {
				jx_ir_basic_block_t* bbJoin = bbTrueTarget;
				if (bbJoin->m_InstrListHead->m_OpCode != JIR_OP_PHI) {
					jx_ir_instruction_t* newBranchInstr = jx_ir_instrBranch(ctx, bbJoin);
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);
					jx_ir_bbAppendInstr(ctx, bb, newBranchInstr);

					// If the conditional val was an instruction and it's now dead, remove it
					// in order to allow further such simplifications in the next pass.
					jx_ir_instruction_t* condValInstr = jx_ir_valueToInstr(condVal);
					if (condValInstr && jx_ir_instrIsDead(condValInstr)) {
						jx_ir_bbRemoveInstr(ctx, bb, condValInstr);
						jx_ir_instrFree(ctx, condValInstr);
					}
				}
			}
		}
	}

	return res;
}

//////////////////////////////////////////////////////////////////////////
// Canonicalize operand order
//
static void jir_funcPass_canonicalizeOperandsDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_canonicalizeOperandsRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

bool jx_ir_funcPassCreate_canonicalizeOperands(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	pass->m_Inst = NULL;
	pass->run = jir_funcPass_canonicalizeOperandsRun;
	pass->destroy = jir_funcPass_canonicalizeOperandsDestroy;

	return true;
}

static void jir_funcPass_canonicalizeOperandsDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
}

static bool jir_funcPass_canonicalizeOperandsRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Canonicalize Operands", 1);

	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_ir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_ir_instruction_t* instrNext = instr->m_Next;

			const bool isCommutativeBinaryOp = false
				|| instr->m_OpCode == JIR_OP_ADD
				|| instr->m_OpCode == JIR_OP_MUL
				|| instr->m_OpCode == JIR_OP_AND
				|| instr->m_OpCode == JIR_OP_OR
				|| instr->m_OpCode == JIR_OP_XOR
				;

			const bool isSetCC = false
				|| instr->m_OpCode == JIR_OP_SET_LE
				|| instr->m_OpCode == JIR_OP_SET_GE
				|| instr->m_OpCode == JIR_OP_SET_LT
				|| instr->m_OpCode == JIR_OP_SET_GT
				|| instr->m_OpCode == JIR_OP_SET_EQ
				|| instr->m_OpCode == JIR_OP_SET_NE
				;

			if (isCommutativeBinaryOp && jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0))) {
				// binop const, %x => binop %x, const
				jx_ir_instrSwapOperands(instr, 0, 1);
			} else if (isSetCC && jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 0))) {
				// setcc const, %x => setcc %x, const with swapped cc
				jx_ir_instrSwapOperands(instr, 0, 1);
				instr->m_OpCode = JIR_OP_SET_CC_BASE + jx_ir_ccSwapOperands(instr->m_OpCode - JIR_OP_SET_CC_BASE);
			} else if (instr->m_OpCode == JIR_OP_SUB) {
				jx_ir_constant_t* constOp1 = jx_ir_valueToConst(jx_ir_instrGetOperandVal(instr, 1));
				if (constOp1) {
					// sub %x, const => add %x, -const
					// 
					// only for signed integer and floating point constants
					jx_ir_type_t* type = constOp1->super.super.m_Type;
					if (jx_ir_typeIsInteger(type) && jx_ir_typeIsSigned(type)) {
						jx_ir_instruction_t* addInstr = jx_ir_instrAdd(ctx, jx_ir_instrGetOperandVal(instr, 0), jx_ir_constToValue(jx_ir_constGetInteger(ctx, type->m_Kind, -constOp1->u.m_I64)));
						jx_ir_bbInsertInstrBefore(ctx, bb, instr, addInstr);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(addInstr));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					} else if (jx_ir_typeIsFloatingPoint(type)) {
						jx_ir_instruction_t* addInstr = jx_ir_instrAdd(ctx, jx_ir_instrGetOperandVal(instr, 0), jx_ir_constToValue(jx_ir_constGetFloat(ctx, type->m_Kind, -constOp1->u.m_F64)));
						jx_ir_bbInsertInstrBefore(ctx, bb, instr, addInstr);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_instrToValue(addInstr));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				}
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

	TracyCZoneEnd(tracyCtx);

	return false;
}

//////////////////////////////////////////////////////////////////////////
// Reorder Basic Blocks
//
#if 1
// Simpler version which calls jx_ir_funcUpdateDomTree() and uses the RPO 
// index of each basic block to sort them and rebuild the linked list.
typedef struct jir_func_pass_reorder_bb_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_basic_block_t** m_BBArr;
} jir_func_pass_reorder_bb_t;

static void jir_funcPass_reorderBasicBlocksDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_reorderBasicBlocksRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

bool jx_ir_funcPassCreate_reorderBasicBlocks(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_reorder_bb_t* inst = (jir_func_pass_reorder_bb_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_reorder_bb_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_reorder_bb_t));
	inst->m_Allocator = allocator;

	inst->m_BBArr = (jx_ir_basic_block_t**)jx_array_create(allocator);
	if (!inst->m_BBArr) {
		jir_funcPass_reorderBasicBlocksDestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_reorderBasicBlocksRun;
	pass->destroy = jir_funcPass_reorderBasicBlocksDestroy;

	return true;
}

static void jir_funcPass_reorderBasicBlocksDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_reorder_bb_t* pass = (jir_func_pass_reorder_bb_t*)inst;
	jx_array_free(pass->m_BBArr);
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_reorderBasicBlocksRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Reorder Basic Blocks", 1);

	jir_func_pass_reorder_bb_t* pass = (jir_func_pass_reorder_bb_t*)inst;

	if (!jx_ir_funcUpdateDomTree(ctx, func)) {
		TracyCZoneEnd(tracyCtx);
		return false;
	}

	const uint32_t numBasicBlocks = jx_ir_funcCountBasicBlocks(ctx, func);
	jx_array_resize(pass->m_BBArr, numBasicBlocks);
	jx_memset(pass->m_BBArr, 0, sizeof(jx_ir_basic_block_t*) * numBasicBlocks);

	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		if (bb->m_RevPostOrderID != 0) {
			JX_CHECK(bb->m_RevPostOrderID <= numBasicBlocks, "Invalid RPO index!");
			pass->m_BBArr[bb->m_RevPostOrderID - 1] = bb;
		}

		bb = bb->m_Next;
	}

	JX_CHECK(pass->m_BBArr[0] == func->m_BasicBlockListHead, "Entry basic block changed?");

	bool orderChanged = false;
	jx_ir_basic_block_t* prev = NULL;
	for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
		jx_ir_basic_block_t* bb = pass->m_BBArr[iBB];
		if (!bb) {
			JX_CHECK(false, "Debug!");
			break;
		}

		if (bb->m_Prev != prev) {
			bb->m_Prev = prev;
			orderChanged = true;
		}

		if (prev && prev->m_Next != bb) {
			prev->m_Next = bb;
			orderChanged = true;
		}

		bb->m_Next = NULL;

		prev = bb;
	}

	TracyCZoneEnd(tracyCtx);
	
	return orderChanged;
}
#else
// More complex version which iteratively builds SCCs and identifies loops.
// It's not needed for this pass.
typedef struct jir_cfg_node_t jir_cfg_node_t;
typedef struct jir_cfg_scc_t jir_cfg_scc_t;

typedef struct jir_cfg_node_t
{
	jx_ir_basic_block_t* m_BasicBlock;
	jir_cfg_scc_t* m_ParentSCC;
	jir_cfg_node_t* m_Succ[2];
	uint32_t m_NumSucc;
	uint32_t m_ID;
	uint32_t m_LowLink;
	bool m_IsOnStack; // TODO: Use a bit from the ID?
	JX_PAD(3);
} jir_cfg_node_t;

typedef struct jir_cfg_scc_list_t
{
	jir_cfg_scc_t* m_Head;
	jir_cfg_scc_t* m_Tail;
} jir_cfg_scc_list_t;

typedef struct jir_cfg_scc_t
{
	jir_cfg_scc_t* m_Next;
	jir_cfg_scc_t* m_Prev;

	jir_cfg_node_t** m_NodesArr;
	jir_cfg_node_t* m_EntryNode;
	jir_cfg_scc_list_t m_SubSCCList;
} jir_cfg_scc_t;

typedef struct jir_bb_cfgnode_item_t
{
	jx_ir_basic_block_t* m_BB;
	jir_cfg_node_t* m_Node;
} jir_bb_cfgnode_item_t;

typedef struct jir_cfg_t
{
	jx_allocator_i* m_Allocator;
	jir_cfg_node_t** m_NodesArr;
	jx_hashmap_t* m_BBToNodeMap;
	jir_cfg_scc_list_t m_SCCList;
} jir_cfg_t;

typedef struct jir_cfg_scc_tarjan_state_t
{
	jx_allocator_i* m_Allocator;
	jir_cfg_node_t** m_Stack;
	jir_cfg_scc_list_t m_List;
	uint32_t m_NextIndex;
} jir_cfg_scc_tarjan_state_t;

typedef struct jir_func_pass_reorder_bb_t
{
	jx_allocator_i* m_Allocator;
	jir_cfg_scc_list_t m_List;
	jx_ir_basic_block_t** m_BBArr;
} jir_func_pass_reorder_bb_t;

static void jir_funcPass_reorderBasicBlocksDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_reorderBasicBlocksRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

static void jir_reorderBB_walkSCCTree(jir_func_pass_reorder_bb_t* pass, jir_cfg_scc_list_t* sccList);

static jir_cfg_t* jir_cfgCreate(jx_allocator_i* allocator);
static void jir_cfgDestroy(jir_cfg_t* cfg);
static jir_cfg_node_t* jir_cfgAddBasicBlock(jir_cfg_t* cfg, jx_ir_basic_block_t* bb);
static bool jir_cfgBuild(jir_cfg_t* cfg, jx_ir_context_t* ctx);
static jir_cfg_node_t* jir_cfgGetNodeForBB(jir_cfg_t* cfg, jx_ir_basic_block_t* bb);
static jir_cfg_scc_list_t jir_cfgFindSCCs(jir_cfg_t* cfg, jir_cfg_node_t** nodes, uint32_t numNodes, jx_allocator_i* allocator);
static bool jir_cfgStrongConnect(jir_cfg_scc_tarjan_state_t* sccState, jir_cfg_node_t* node);
static jir_cfg_node_t* jir_cfgNodeAlloc(jir_cfg_t* cfg, jx_ir_basic_block_t* func);
static void jir_cfgNodeFree(jir_cfg_t* cfg, jir_cfg_node_t* node);
static jir_cfg_scc_t* jir_cfgSCCAlloc(jx_allocator_i* allocator);
static void jir_cfgSCCFree(jir_cfg_scc_t* scc, jx_allocator_i* allocator);
static uint64_t jir_bbCFGNodeItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_bbCFGNodeItemCompare(const void* a, const void* b, void* udata);
static void jir_cfgSCCPrint(jir_cfg_scc_t* scc, jx_string_buffer_t* sb);

bool jx_ir_funcPassCreate_reorderBasicBlocks(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_reorder_bb_t* inst = (jir_func_pass_reorder_bb_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_reorder_bb_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_reorder_bb_t));
	inst->m_Allocator = allocator;

	inst->m_BBArr = (jx_ir_basic_block_t**)jx_array_create(allocator);
	if (!inst->m_BBArr) {
		jir_funcPass_reorderBasicBlocksDestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_reorderBasicBlocksRun;
	pass->destroy = jir_funcPass_reorderBasicBlocksDestroy;

	return true;
}

static void jir_funcPass_reorderBasicBlocksDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_reorder_bb_t* pass = (jir_func_pass_reorder_bb_t*)inst;
	jx_array_free(pass->m_BBArr);
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_reorderBasicBlocksRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	jir_func_pass_reorder_bb_t* pass = (jir_func_pass_reorder_bb_t*)inst;

	jir_cfg_t* cfg = jir_cfgCreate(pass->m_Allocator);

	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jir_cfgAddBasicBlock(cfg, bb);
		bb = bb->m_Next;
	}

	jir_cfgBuild(cfg, ctx);

	// Rebuild basic block order by walking the SCC tree backwards
	jx_array_resize(pass->m_BBArr, 0);
	jir_reorderBB_walkSCCTree(pass, &cfg->m_SCCList);

	JX_CHECK(pass->m_BBArr[0] == func->m_BasicBlockListHead, "Changed entry basic block?");
	const uint32_t numBasicBlocks = (uint32_t)jx_array_sizeu(pass->m_BBArr);
	jx_ir_basic_block_t* prevBlock = func->m_BasicBlockListHead;
	for (uint32_t iBB = 1; iBB < numBasicBlocks; ++iBB) {
		jx_ir_basic_block_t* bb = pass->m_BBArr[iBB];
		prevBlock->m_Next = bb;
		bb->m_Prev = prevBlock;
		prevBlock = bb;
	}
	prevBlock->m_Next = NULL;

	jir_cfgDestroy(cfg);

	return true;
}

static void jir_reorderBB_walkSCCTree(jir_func_pass_reorder_bb_t* pass, jir_cfg_scc_list_t* sccList)
{
	jir_cfg_scc_t* scc = sccList->m_Tail;
	while (scc) {
		const uint32_t numNodes = (uint32_t)jx_array_sizeu(scc->m_NodesArr);
		if (numNodes == 1 && !scc->m_EntryNode) {
			// Single-node scc.
			jir_cfg_node_t* node = scc->m_NodesArr[0];
			jx_array_push_back(pass->m_BBArr, node->m_BasicBlock);
		} else {
			if (scc->m_EntryNode) {
				// Natural loop. Insert entry node and walk the sub-tree
				jir_cfg_node_t* entry = scc->m_EntryNode;
				jx_array_push_back(pass->m_BBArr, entry->m_BasicBlock);
				jir_reorderBB_walkSCCTree(pass, &scc->m_SubSCCList);
			} else {
				// Cycle with many entry points.
				// TODO: This hits with c-testsuite/00143.c with SSA disabled and it seems to work.
				// TODO: It does not work on stb_sprintf_test.c with loop rotation enabled during codegen.
				JX_CHECK(false, "This is untested. If it comes up it might fail down the line. Debug if necessary and remove this assert if everything is fine.");
				for (uint32_t iNode = numNodes; iNode > 0; --iNode) {
					jx_array_push_back(pass->m_BBArr, scc->m_NodesArr[iNode - 1]->m_BasicBlock);
				}
			}
		}

		scc = scc->m_Prev;
	}
}

static jir_cfg_t* jir_cfgCreate(jx_allocator_i* allocator)
{
	jir_cfg_t* cfg = (jir_cfg_t*)JX_ALLOC(allocator, sizeof(jir_cfg_t));
	if (!cfg) {
		return NULL;
	}

	jx_memset(cfg, 0, sizeof(jir_cfg_t));
	cfg->m_Allocator = allocator;
	cfg->m_NodesArr = (jir_cfg_node_t**)jx_array_create(allocator);
	if (!cfg->m_NodesArr) {
		jir_cfgDestroy(cfg);
		return NULL;
	}

	cfg->m_BBToNodeMap = jx_hashmapCreate(allocator, sizeof(jir_bb_cfgnode_item_t), 64, 0, 0, jir_bbCFGNodeItemHash, jir_bbCFGNodeItemCompare, NULL, NULL);
	if (!cfg->m_BBToNodeMap) {
		jir_cfgDestroy(cfg);
		return NULL;
	}

	return cfg;
}

static void jir_cfgDestroy(jir_cfg_t* cfg)
{
	if (cfg->m_BBToNodeMap) {
		jx_hashmapDestroy(cfg->m_BBToNodeMap);
		cfg->m_BBToNodeMap = NULL;
	}

	jir_cfg_scc_t* scc = cfg->m_SCCList.m_Head;
	while (scc) {
		jir_cfg_scc_t* sccNext = scc->m_Next;
		jir_cfgSCCFree(scc, cfg->m_Allocator);
		scc = sccNext;
	}

	const uint32_t numNodes = (uint32_t)jx_array_sizeu(cfg->m_NodesArr);
	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		jir_cfgNodeFree(cfg, cfg->m_NodesArr[iNode]);
	}
	jx_array_free(cfg->m_NodesArr);
	JX_FREE(cfg->m_Allocator, cfg);
}

static jir_cfg_node_t* jir_cfgAddBasicBlock(jir_cfg_t* cfg, jx_ir_basic_block_t* bb)
{
	jir_cfg_node_t* node = jir_cfgNodeAlloc(cfg, bb);
	if (!node) {
		return NULL;
	}

	jx_array_push_back(cfg->m_NodesArr, node);
	jx_hashmapSet(cfg->m_BBToNodeMap, &(jir_bb_cfgnode_item_t){ .m_BB= bb, .m_Node = node });

	return node;
}

static bool jir_cfgBuild(jir_cfg_t* cfg, jx_ir_context_t* ctx)
{
	const uint32_t numNodes = (uint32_t)jx_array_sizeu(cfg->m_NodesArr);
	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		jir_cfg_node_t* node = cfg->m_NodesArr[iNode];
		jx_ir_basic_block_t* bb = node->m_BasicBlock;
		jx_ir_instruction_t* termInstr = jx_ir_bbGetLastInstr(ctx, bb);
		if (termInstr->m_OpCode == JIR_OP_BRANCH) {
			const uint32_t numOperands = (uint32_t)jx_array_sizeu(termInstr->super.m_OperandArr);
			if (numOperands == 1) {
				node->m_Succ[0] = jir_cfgGetNodeForBB(cfg, jx_ir_valueToBasicBlock(termInstr->super.m_OperandArr[0]->m_Value));
				node->m_NumSucc = 1;
			} else if (numOperands == 3) {
				node->m_Succ[0] = jir_cfgGetNodeForBB(cfg, jx_ir_valueToBasicBlock(termInstr->super.m_OperandArr[1]->m_Value));
				node->m_Succ[1] = jir_cfgGetNodeForBB(cfg, jx_ir_valueToBasicBlock(termInstr->super.m_OperandArr[2]->m_Value));
				node->m_NumSucc = 2;
			} else {
				JX_CHECK(false, "Unknown branch instruction");
			}
		} else {
			JX_CHECK(termInstr->m_OpCode == JIR_OP_RET, "Unknown terminator instruction!");
			node->m_NumSucc = 0;
		}
	}

	cfg->m_SCCList = jir_cfgFindSCCs(cfg, cfg->m_NodesArr, jx_array_sizeu(cfg->m_NodesArr), cfg->m_Allocator);

	return true;
}

static jir_cfg_node_t* jir_cfgGetNodeForBB(jir_cfg_t* cfg, jx_ir_basic_block_t* bb)
{
	jir_bb_cfgnode_item_t* item = jx_hashmapGet(cfg->m_BBToNodeMap, &(jir_bb_cfgnode_item_t){.m_BB = bb});
	if (!item) {
		JX_CHECK(false, "Basic block not found in map");
		return NULL;
	}

	return item->m_Node;
}

static jir_cfg_scc_list_t jir_cfgFindSCCs(jir_cfg_t* cfg, jir_cfg_node_t** nodes, uint32_t numNodes, jx_allocator_i* allocator)
{
	// Reset all nodes' SCC state
	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		nodes[iNode]->m_ID = UINT32_MAX;
		nodes[iNode]->m_LowLink = UINT32_MAX;
		nodes[iNode]->m_IsOnStack = false;
		nodes[iNode]->m_ParentSCC = NULL;
	}

	// https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
	jir_cfg_scc_tarjan_state_t* sccState = &(jir_cfg_scc_tarjan_state_t) { 0 };
	sccState->m_Allocator = allocator;
	sccState->m_Stack = (jir_cfg_node_t**)jx_array_create(allocator);
	jx_array_reserve(sccState->m_Stack, numNodes);

	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		jir_cfg_node_t* node = nodes[iNode];
		if (node->m_ID == UINT32_MAX) {
			jir_cfgStrongConnect(sccState, node);
		}
	}

	jx_array_free(sccState->m_Stack);

	jir_cfg_scc_t* scc = sccState->m_List.m_Head;
	while (scc) {
		const uint32_t numSCCNodes = (uint32_t)jx_array_sizeu(scc->m_NodesArr);
		if (numSCCNodes != 1) {
			// Check if this a natural loop (single entry into the SCC).
			// If it is "remove" the entry node from the node list and recursively
			// find all sub-SCCs.
			// 
			// For a node to be the entry node it must be the only node with a predecessor
			// from another SCC.
			uint32_t entryNodeID = UINT32_MAX;
			uint32_t numEntries = 0;
			for (uint32_t iNode = 0; iNode < numSCCNodes && numEntries <= 1; ++iNode) {
				jir_cfg_node_t* node = scc->m_NodesArr[iNode];
				jx_ir_basic_block_t* bb = node->m_BasicBlock;
				const uint32_t numPreds = (uint32_t)jx_array_sizeu(bb->m_PredArr);
				for (uint32_t iPred = 0; iPred < numPreds; ++iPred) {
					jir_cfg_node_t* predNode = jir_cfgGetNodeForBB(cfg, bb->m_PredArr[iPred]);
					JX_CHECK(predNode, "Predecessor node not found in the CFG!");
					if (predNode->m_ParentSCC != scc) {
						entryNodeID = iNode;
						++numEntries;
					}
				}
			}

			JX_CHECK(numEntries != 0 && entryNodeID != UINT32_MAX, "Unconnected SCC?");
			if (numEntries == 1) {
				// This is a single-entry SCC.
				scc->m_EntryNode = scc->m_NodesArr[entryNodeID];
				jx_array_del(scc->m_NodesArr, entryNodeID);

				scc->m_SubSCCList = jir_cfgFindSCCs(cfg, scc->m_NodesArr, jx_array_sizeu(scc->m_NodesArr), cfg->m_Allocator);
			} else {
				// This is an irreducible SCC. Cannot dive deeper.
			}
		}

		scc = scc->m_Next;
	}

	return sccState->m_List;
}

static bool jir_cfgStrongConnect(jir_cfg_scc_tarjan_state_t* sccState, jir_cfg_node_t* node)
{
	const uint32_t id = sccState->m_NextIndex++;
	node->m_ID = id;
	node->m_LowLink = id;

	jx_array_push_back(sccState->m_Stack, node);
	node->m_IsOnStack = true;

	const uint32_t numSucc = node->m_NumSucc;
	for (uint32_t iSucc = 0; iSucc < numSucc; ++iSucc) {
		jir_cfg_node_t* succNode = node->m_Succ[iSucc];
		if (succNode->m_ID == UINT32_MAX) {
			// Successor w has not yet been visited; recurse on it
			jir_cfgStrongConnect(sccState, succNode);
			node->m_LowLink = jx_min_u32(node->m_LowLink, succNode->m_LowLink);
		} else if (succNode->m_IsOnStack) {
			// Successor w is in stack S and hence in the current SCC
			node->m_LowLink = jx_min_u32(node->m_LowLink, succNode->m_ID);
		} else {
			// If w is not on stack, then (v, w) is an edge pointing to 
			// an SCC already found and must be ignored
		}
	}

	// If v is a root node, pop the stack and generate an SCC
	if (node->m_LowLink == node->m_ID) {
		jir_cfg_scc_t* scc = jir_cfgSCCAlloc(sccState->m_Allocator);
		if (!scc) {
			return false;
		}

		jir_cfg_node_t* stackNode = NULL;
		do {
			stackNode = jx_array_pop_back(sccState->m_Stack);
			stackNode->m_IsOnStack = false;
			stackNode->m_ParentSCC = scc;
			jx_array_push_back(scc->m_NodesArr, stackNode);
		} while (stackNode != node);

		if (!sccState->m_List.m_Head) {
			sccState->m_List.m_Head = scc;
			sccState->m_List.m_Tail = scc;
		} else {
			scc->m_Prev = sccState->m_List.m_Tail;
			sccState->m_List.m_Tail->m_Next = scc;
			sccState->m_List.m_Tail = scc;
		}
	}

	return true;
}

static jir_cfg_node_t* jir_cfgNodeAlloc(jir_cfg_t* cfg, jx_ir_basic_block_t* bb)
{
	jir_cfg_node_t* node = (jir_cfg_node_t*)JX_ALLOC(cfg->m_Allocator, sizeof(jir_cfg_node_t));
	if (!node) {
		return NULL;
	}

	jx_memset(node, 0, sizeof(jir_cfg_node_t));
	node->m_BasicBlock = bb;
	node->m_ID = UINT32_MAX;
	node->m_LowLink = UINT32_MAX;
	node->m_IsOnStack = false;
	
	return node;
}

static void jir_cfgNodeFree(jir_cfg_t* cfg, jir_cfg_node_t* node)
{
	JX_FREE(cfg->m_Allocator, node);
}

static jir_cfg_scc_t* jir_cfgSCCAlloc(jx_allocator_i* allocator)
{
	jir_cfg_scc_t* scc = (jir_cfg_scc_t*)JX_ALLOC(allocator, sizeof(jir_cfg_scc_t));
	if (!scc) {
		return NULL;
	}

	jx_memset(scc, 0, sizeof(jir_cfg_scc_t));
	scc->m_NodesArr = (jir_cfg_node_t**)jx_array_create(allocator);
	if (!scc->m_NodesArr) {
		jir_cfgSCCFree(scc, allocator);
		return NULL;
	}

	return scc;
}

static void jir_cfgSCCFree(jir_cfg_scc_t* scc, jx_allocator_i* allocator)
{
	jir_cfg_scc_t* sub = scc->m_SubSCCList.m_Head;
	while (sub) {
		jir_cfg_scc_t* subNext = sub->m_Next;
		jir_cfgSCCFree(sub, allocator);
		sub = subNext;
	}
	jx_array_free(scc->m_NodesArr);
	JX_FREE(allocator, scc);
}

static uint64_t jir_bbCFGNodeItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jir_bb_cfgnode_item_t* var = (const jir_bb_cfgnode_item_t*)item;
	uint64_t hash = jx_hashFNV1a(&var->m_BB, sizeof(jx_ir_basic_block_t*), seed0, seed1);
	return hash;
}

static int32_t jir_bbCFGNodeItemCompare(const void* a, const void* b, void* udata)
{
	const jir_bb_cfgnode_item_t* varA = (const jir_bb_cfgnode_item_t*)a;
	const jir_bb_cfgnode_item_t* varB = (const jir_bb_cfgnode_item_t*)b;
	int32_t res = jir_comparePtrs(varA->m_BB, varB->m_BB);
	return res;
}

static void jir_cfgSCCPrint(jir_cfg_scc_t* scc, jx_string_buffer_t* sb)
{
	const uint32_t numNodes = (uint32_t)jx_array_sizeu(scc->m_NodesArr);
	if (numNodes == 1 && !scc->m_EntryNode) {
		JX_CHECK(!scc->m_EntryNode && !scc->m_SubSCCList.m_Head, "?");
		jir_cfg_node_t* node = scc->m_NodesArr[0];
		jx_strbuf_printf(sb, "single: %s\n", node->m_BasicBlock->super.m_Name);
	} else {
		if (scc->m_EntryNode) {
			jx_strbuf_printf(sb, "loop: %s\n", scc->m_EntryNode->m_BasicBlock->super.m_Name);

			jir_cfg_scc_t* sub = scc->m_SubSCCList.m_Head;
			while (sub) {
				jir_cfgSCCPrint(sub, sb);
				sub = sub->m_Next;
			}
			jx_strbuf_printf(sb, "endloop\n");
		} else {
			jx_strbuf_printf(sb, "cycle:");
			for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
				jir_cfg_node_t* node = scc->m_NodesArr[iNode];

				if (iNode != 0) {
					jx_strbuf_pushCStr(sb, ", ");
				}
				jx_strbuf_printf(sb, "%s", node->m_BasicBlock->super.m_Name);
			}
			jx_strbuf_pushCStr(sb, "\n");
		}
	}
}
#endif

//////////////////////////////////////////////////////////////////////////
// Remove Redundant Phis
//
typedef struct jir_func_pass_remove_redundant_phis_t
{
	jx_allocator_i* m_Allocator;
} jir_func_pass_remove_redundant_phis_t;

static void jir_funcPass_removeRedundantPhisDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_removeRedundantPhisRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

bool jx_ir_funcPassCreate_removeRedundantPhis(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_remove_redundant_phis_t* inst = (jir_func_pass_remove_redundant_phis_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_remove_redundant_phis_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_remove_redundant_phis_t));
	inst->m_Allocator = allocator;

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_removeRedundantPhisRun;
	pass->destroy = jir_funcPass_removeRedundantPhisDestroy;

	return true;
}

static void jir_funcPass_removeRedundantPhisDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_remove_redundant_phis_t* pass = (jir_func_pass_remove_redundant_phis_t*)inst;
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_removeRedundantPhisRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: Remove Redundant Phis", 1);

	jir_func_pass_remove_redundant_phis_t* pass = (jir_func_pass_remove_redundant_phis_t*)inst;

#if 0
	{
		jx_string_buffer_t* sb = jx_strbuf_create(pass->m_Allocator);
		jx_strbuf_printf(sb, "Pre-redundantPhiElimination\n");
		jx_ir_funcPrint(ctx, func, sb);
		jx_strbuf_nullTerminate(sb);
		JX_SYS_LOG_INFO(NULL, "%s", jx_strbuf_getString(sb, NULL));
		jx_strbuf_destroy(sb);
	}
#endif

	JX_CHECK(jx_ir_funcCheck(ctx, func), "Func is in invalid state!");

	uint32_t numRemovals = 0;

	bool changed = true;
	while (changed) {
		changed = false;

		const uint32_t numPrevRemovals = numRemovals;
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr->m_OpCode == JIR_OP_PHI) {
				jx_ir_instruction_t* instrNext = instr->m_Next;

				jx_ir_value_t* uniqueVal = NULL;

				const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
				JX_CHECK(numOperands == 2 * jx_array_sizeu(instr->m_ParentBB->m_PredArr), "Invalid number of phi operands");
				for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
					jx_ir_value_t* bbVal = jx_ir_instrGetOperandVal(instr, iOperand + 0);
					if (bbVal == uniqueVal || bbVal == jx_ir_instrToValue(instr)) {
						continue;
					}

					if (uniqueVal) {
						uniqueVal = NULL;
						break;
					}

					uniqueVal = bbVal;
				}

				if (uniqueVal) {
					jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), uniqueVal);
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);

					++numRemovals;
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}

		changed = numPrevRemovals != numRemovals;
	}

	TracyCZoneEnd(tracyCtx);

	return numRemovals != 0;
}

//////////////////////////////////////////////////////////////////////////
// Dead Code Elimination
//
#define JIR_DCE_BB_FLAGS_VISITED_Pos 31
#define JIR_DCE_BB_FLAGS_VISITED_Msk (1u << JIR_DCE_BB_FLAGS_VISITED_Pos)

typedef struct jir_func_pass_dce_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_context_t* m_Ctx;
	jx_ir_function_t* m_Func;
} jir_func_pass_dce_t;

static void jir_funcPass_deadCodeEliminationDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_deadCodeEliminationRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

static uint32_t jir_dce_bbVisit(jir_func_pass_dce_t* pass, jx_ir_basic_block_t* bb);

bool jx_ir_funcPassCreate_deadCodeElimination(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_dce_t* inst = (jir_func_pass_dce_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_dce_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_dce_t));
	inst->m_Allocator = allocator;

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_deadCodeEliminationRun;
	pass->destroy = jir_funcPass_deadCodeEliminationDestroy;

	return true;
}

static void jir_funcPass_deadCodeEliminationDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_dce_t* pass = (jir_func_pass_dce_t*)inst;
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_deadCodeEliminationRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: DCE", 1);

	jir_func_pass_dce_t* pass = (jir_func_pass_dce_t*)inst;

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	// Reset flags for all basic blocks.
	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		bb->super.m_Flags &= ~JIR_DCE_BB_FLAGS_VISITED_Msk;
		bb = bb->m_Next;
	}

	uint32_t numRemovals = 0;
	bool changed = true;
	while (changed) {
		changed = false;

		const uint32_t numPrevRemovals = numRemovals;

		numRemovals += jir_dce_bbVisit(pass, func->m_BasicBlockListHead);

		changed = numPrevRemovals != numRemovals;
	}

	// Empty and remove all unvisited basic blocks.
	// NOTE: First all unvisited basic blocks are emptied in order to correctly
	// update the CFG and then they are removed from the function.
	{
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_basic_block_t* bbNext = bb->m_Next;
			if ((bb->super.m_Flags & JIR_DCE_BB_FLAGS_VISITED_Msk) == 0) {
				jx_ir_instruction_t* instr = bb->m_InstrListHead;
				while (instr) {
					jx_ir_instruction_t* instrNext = instr->m_Next;
					jx_ir_bbRemoveInstr(ctx, bb, instr);
					jx_ir_instrFree(ctx, instr);
					instr = instrNext;
				}
			}

			bb = bbNext;
		}

		bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_basic_block_t* bbNext = bb->m_Next;
			if ((bb->super.m_Flags & JIR_DCE_BB_FLAGS_VISITED_Msk) == 0) {
				jx_ir_funcRemoveBasicBlock(ctx, func, bb);
				jx_ir_bbFree(ctx, bb);
				++numRemovals;
			}

			bb = bbNext;
		}
	}

	TracyCZoneEnd(tracyCtx);

	return numRemovals != 0;
}

static uint32_t jir_dce_bbVisit(jir_func_pass_dce_t* pass, jx_ir_basic_block_t* bb)
{
	if ((bb->super.m_Flags & JIR_DCE_BB_FLAGS_VISITED_Msk) != 0) {
		return 0;
	}

	bb->super.m_Flags |= JIR_DCE_BB_FLAGS_VISITED_Msk;

	uint32_t numInstrRemoved = 0;

	// Loop until terminator instruction
	jx_ir_instruction_t* instr = bb->m_InstrListHead;
	while (instr->m_Next) {
		jx_ir_instruction_t* instrNext = instr->m_Next;

		if (jx_ir_instrIsDead(instr)) {
			jx_ir_bbRemoveInstr(pass->m_Ctx, bb, instr);
			jx_ir_instrFree(pass->m_Ctx, instr);
			++numInstrRemoved;
		}

		instr = instrNext;
	}

	JX_CHECK(instr && jx_ir_opcodeIsTerminator(instr->m_OpCode), "Expected terminator instruction");
	if (instr->m_OpCode == JIR_OP_BRANCH) {
		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		if (numOperands == 1) {
			jx_ir_basic_block_t* targetBB = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(instr, 0));
			JX_CHECK(targetBB, "Expected basic block as unconditional branch target.");
			numInstrRemoved += jir_dce_bbVisit(pass, targetBB);
		} else if (numOperands == 3) {
			jx_ir_basic_block_t* trueBB = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(instr, 1));
			JX_CHECK(trueBB, "Expected basic block as conditional branch target.");
			numInstrRemoved += jir_dce_bbVisit(pass, trueBB);

			jx_ir_basic_block_t* falseBB = jx_ir_valueToBasicBlock(jx_ir_instrGetOperandVal(instr, 2));
			JX_CHECK(falseBB, "Expected basic block as conditional branch target.");
			numInstrRemoved += jir_dce_bbVisit(pass, falseBB);
		} else {
			JX_CHECK(false, "Unknown branch instruction.");
		}
	} else if (instr->m_OpCode == JIR_OP_RET) {
		// Nothing to do in this case.
	} else {
		JX_CHECK(false, "Unknown terminator instruction!");
	}

	return numInstrRemoved;
}

//////////////////////////////////////////////////////////////////////////
// Local Value Numbering
//
#define JIR_LVN_CONFIG_VALUE_NUMBER_LOADS 1

typedef struct jir_hash_value_map_item_t
{
	uint64_t m_Hash;
	jx_ir_value_t* m_Value;
} jir_hash_value_map_item_t;

typedef struct jir_func_pass_lvn_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_context_t* m_Ctx;
	jx_ir_function_t* m_Func;
	jx_hashmap_t* m_ValueMap;
} jir_func_pass_lvn_t;

static void jir_funcPass_localValueNumberingDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_localValueNumberingRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func);

static bool jir_lvn_instrCalcHash(jir_func_pass_lvn_t* pass, jx_ir_instruction_t* instr, uint64_t* hash);
static uint64_t jir_lvn_typeHash(const jx_ir_type_t* type, uint64_t seed0, uint64_t seed1);
static uint64_t jir_hashValueMapItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_hashValueMapItemCompare(const void* a, const void* b, void* udata);

bool jx_ir_funcPassCreate_localValueNumbering(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_lvn_t* inst = (jir_func_pass_lvn_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_lvn_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_lvn_t));
	inst->m_Allocator = allocator;

	inst->m_ValueMap = jx_hashmapCreate(allocator, sizeof(jir_hash_value_map_item_t), 64, 0, 0, jir_hashValueMapItemHash, jir_hashValueMapItemCompare, NULL, NULL);
	if (!inst->m_ValueMap) {
		jir_funcPass_localValueNumberingDestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_ir_function_pass_o*)inst;
	pass->run = jir_funcPass_localValueNumberingRun;
	pass->destroy = jir_funcPass_localValueNumberingDestroy;

	return true;
}

static void jir_funcPass_localValueNumberingDestroy(jx_ir_function_pass_o* inst, jx_allocator_i* allocator)
{
	jir_func_pass_lvn_t* pass = (jir_func_pass_lvn_t*)inst;
	if (pass->m_ValueMap) {
		jx_hashmapDestroy(pass->m_ValueMap);
		pass->m_ValueMap = NULL;
	}
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_localValueNumberingRun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
	TracyCZoneN(tracyCtx, "ir: LVN", 1);

	jir_func_pass_lvn_t* pass = (jir_func_pass_lvn_t*)inst;

	pass->m_Ctx = ctx;
	pass->m_Func = func;

	jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
	while (bb) {
		jx_hashmapClear(pass->m_ValueMap, false);

		jx_ir_instruction_t* instr = bb->m_InstrListHead;
		while (instr) {
			jx_ir_instruction_t* instrNext = instr->m_Next;

#if JIR_LVN_CONFIG_VALUE_NUMBER_LOADS
			if (instr->m_OpCode == JIR_OP_CALL || instr->m_OpCode == JIR_OP_STORE) {
				// NOTE: Calls and stores invalidate all loads. Anything more complicated requires
				// alias analysis. 
				// 
				// TODO: In case of stores to alloca'd values I might be able to 
				// map the stored value to the load (is that store-to-load forwarding?) but I don't
				// know if it's safe without alias analysis.
				uint32_t itemID = 0;
				jir_hash_value_map_item_t* itemPtr = NULL;
				while (jx_hashmapIter(pass->m_ValueMap, &itemID, (void**)&itemPtr)) {
					jx_ir_instruction_t* instr = jx_ir_valueToInstr(itemPtr->m_Value);
					if (instr && instr->m_OpCode == JIR_OP_LOAD) {
						jx_hashmapDelete(pass->m_ValueMap, itemPtr);
						itemID = 0;
					}
				}
			} else 
#endif
			{
				uint64_t hash = 0ull;
				if (jir_lvn_instrCalcHash(pass, instr, &hash)) {
					jir_hash_value_map_item_t* hashItem = jx_hashmapGet(pass->m_ValueMap, &(jir_hash_value_map_item_t){ .m_Hash = hash });
					if (hashItem) {
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), hashItem->m_Value);
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					} else {
						jx_hashmapSet(pass->m_ValueMap, &(jir_hash_value_map_item_t){ .m_Hash = hash, .m_Value = jx_ir_instrToValue(instr) });
					}
				}
			}

			instr = instrNext;
		}

		bb = bb->m_Next;
	}

	TracyCZoneEnd(tracyCtx);

	return false;
}

static bool jir_lvn_instrCalcHash(jir_func_pass_lvn_t* pass, jx_ir_instruction_t* instr, uint64_t* hashPtr)
{
	bool res = false;

	uint64_t hash = jx_hashFNV1a(&instr->m_OpCode, sizeof(uint32_t), 0, 0);
	hash = jir_lvn_typeHash(instr->super.super.m_Type, hash, 0);

	switch (instr->m_OpCode) {
	case JIR_OP_ADD:
	case JIR_OP_MUL:
	case JIR_OP_AND:
	case JIR_OP_OR:
	case JIR_OP_XOR: 
	case JIR_OP_SET_EQ:
	case JIR_OP_SET_NE: {
		// Commutative binary operations. Sort operands in some predefined order and calculate hash based on those.
		jx_ir_value_t* op0 = jx_ir_instrGetOperandVal(instr, 0);
		jx_ir_value_t* op1 = jx_ir_instrGetOperandVal(instr, 1);
		if (jir_comparePtrs(op0, op1) < 0) {
			jx_ir_value_t* tmp = op0;
			op0 = op1;
			op1 = tmp;
		}

		hash = jx_hashFNV1a(&op0, sizeof(jx_ir_value_t**), hash, 0);
		hash = jx_hashFNV1a(&op1, sizeof(jx_ir_value_t**), hash, 0);
		res = true;
	} break;
	case JIR_OP_SUB:
	case JIR_OP_DIV:
	case JIR_OP_REM:
	case JIR_OP_SET_LE:
	case JIR_OP_SET_GE:
	case JIR_OP_SET_LT:
	case JIR_OP_SET_GT: 
	case JIR_OP_SHL:
	case JIR_OP_SHR: {
		jx_ir_value_t* op0 = jx_ir_instrGetOperandVal(instr, 0);
		jx_ir_value_t* op1 = jx_ir_instrGetOperandVal(instr, 1);
		hash = jx_hashFNV1a(&op0, sizeof(jx_ir_value_t**), hash, 0);
		hash = jx_hashFNV1a(&op1, sizeof(jx_ir_value_t**), hash, 0);
		res = true;
	} break;
	case JIR_OP_GET_ELEMENT_PTR:
	case JIR_OP_PHI: {
		const uint32_t numOperands = (uint32_t)jx_array_sizeu(instr->super.m_OperandArr);
		hash = jx_hashFNV1a(&numOperands, sizeof(uint32_t), hash, 0);
		for (uint32_t iOperand = 0; iOperand < numOperands; ++iOperand) {
			jx_ir_value_t* op = jx_ir_instrGetOperandVal(instr, iOperand);
			hash = jx_hashFNV1a(&op, sizeof(jx_ir_value_t**), hash, 0);
		}
		res = true;
	} break;
	case JIR_OP_TRUNC:
	case JIR_OP_ZEXT:
	case JIR_OP_SEXT:
	case JIR_OP_PTR_TO_INT:
	case JIR_OP_INT_TO_PTR:
	case JIR_OP_BITCAST:
	case JIR_OP_FPEXT:
	case JIR_OP_FPTRUNC:
	case JIR_OP_FP2UI:
	case JIR_OP_FP2SI:
	case JIR_OP_UI2FP:
	case JIR_OP_SI2FP: {
		jx_ir_value_t* op0 = jx_ir_instrGetOperandVal(instr, 0);
		hash = jx_hashFNV1a(&op0, sizeof(jx_ir_value_t**), hash, 0);
		res = true;
	} break;
	case JIR_OP_LOAD: {
#if JIR_LVN_CONFIG_VALUE_NUMBER_LOADS
		jx_ir_value_t* ptr = jx_ir_instrGetOperandVal(instr, 0);
		hash = jx_hashFNV1a(&ptr, sizeof(jx_ir_value_t**), hash, 0);
		res = true;
#endif
	} break;
	case JIR_OP_RET:
	case JIR_OP_BRANCH:
	case JIR_OP_ALLOCA:{
		// Don't replace those.
	} break;
	case JIR_OP_STORE:
	case JIR_OP_CALL: {
#if JIR_LVN_CONFIG_VALUE_NUMBER_LOADS
		JX_CHECK(false, "Cannot calculate hash for calls and stores!");
#endif
	} break;
	default: {
		JX_CHECK(false, "Unknown opcode");
	} break;
	}

	*hashPtr = hash;
	
	return res;
}

static uint64_t jir_lvn_typeHash(const jx_ir_type_t* type, uint64_t seed0, uint64_t seed1)
{
	uint64_t hash = jx_hashFNV1a(&type->m_Kind, sizeof(jx_ir_type_kind), seed0, seed1);
	hash = jx_hashFNV1a(&type->m_Flags, sizeof(uint32_t), hash, seed1);
	switch (type->m_Kind) {
	case JIR_TYPE_VOID:
	case JIR_TYPE_BOOL:
	case JIR_TYPE_U8:
	case JIR_TYPE_I8:
	case JIR_TYPE_U16:
	case JIR_TYPE_I16:
	case JIR_TYPE_U32:
	case JIR_TYPE_I32:
	case JIR_TYPE_U64:
	case JIR_TYPE_I64:
	case JIR_TYPE_F32:
	case JIR_TYPE_F64:
	case JIR_TYPE_TYPE:
	case JIR_TYPE_LABEL: {
		// Nothing else to hash.
	} break;
	case JIR_TYPE_FUNCTION: {
		const jx_ir_type_function_t* funcType = (const jx_ir_type_function_t*)type;
		hash = jir_lvn_typeHash(funcType->m_RetType, hash, seed1);

		const uint32_t numArgs = funcType->m_NumArgs;
		hash = jx_hashFNV1a(&numArgs, sizeof(uint32_t), hash, seed1);
		for (uint32_t iArg = 0; iArg < numArgs; ++iArg) {
			hash = jir_lvn_typeHash(funcType->m_Args[iArg], hash, seed1);
		}

		hash = jx_hashFNV1a(&funcType->m_IsVarArg, sizeof(bool), hash, seed1);
	} break;
	case JIR_TYPE_STRUCT: {
		const jx_ir_type_struct_t* structType = (const jx_ir_type_struct_t*)type;
		hash = jx_hashFNV1a(&structType->m_UniqueID, sizeof(uint64_t), hash, seed1);
	} break;
	case JIR_TYPE_ARRAY: {
		const jx_ir_type_array_t* arrType = (const jx_ir_type_array_t*)type;
		hash = jir_lvn_typeHash(arrType->m_BaseType, hash, seed1);
		hash = jx_hashFNV1a(&arrType->m_Size, sizeof(uint32_t), hash, seed1);
	} break;
	case JIR_TYPE_POINTER: {
		const jx_ir_type_pointer_t* ptrType = (const jx_ir_type_pointer_t*)type;
		hash = jir_lvn_typeHash(ptrType->m_BaseType, hash, seed1);
	} break;
	default:
		JX_CHECK(false, "Unknown type kind!");
		break;
	}

	return hash;
}

static uint64_t jir_hashValueMapItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jir_hash_value_map_item_t* var = (const jir_hash_value_map_item_t*)item;
	return var->m_Hash;
}

static int32_t jir_hashValueMapItemCompare(const void* a, const void* b, void* udata)
{
	const jir_hash_value_map_item_t* varA = (const jir_hash_value_map_item_t*)a;
	const jir_hash_value_map_item_t* varB = (const jir_hash_value_map_item_t*)b;
	int32_t res = varA->m_Hash < varB->m_Hash
		? -1
		: (varA->m_Hash > varB->m_Hash ? 1 : 0)
		;
	return res;
}

//////////////////////////////////////////////////////////////////////////
// Simple Function inliner
//
typedef struct jir_call_graph_node_t jir_call_graph_node_t;
typedef struct jir_call_graph_scc_t jir_call_graph_scc_t;

typedef struct jir_func_cgnode_item_t
{
	jx_ir_function_t* m_Func;
	jir_call_graph_node_t* m_Node;
} jir_func_cgnode_item_t;

typedef struct jir_call_graph_node_t
{
	jx_ir_function_t* m_Func;
	jir_call_graph_node_t** m_CalledFuncsArr;
	jx_ir_instruction_t** m_CallInstrArr;
	uint32_t m_ID;
	uint32_t m_LowLink;
	bool m_IsOnStack; // TODO: Use a bit from the ID?
	JX_PAD(7);
} jir_call_graph_node_t;

typedef struct jir_call_graph_scc_t
{
	jir_call_graph_scc_t* m_Next;
	jir_call_graph_scc_t* m_Prev;
	jir_call_graph_node_t** m_NodesArr;
} jir_call_graph_scc_t;

typedef struct jir_call_graph_t
{
	jx_allocator_i* m_Allocator;
	jir_call_graph_node_t** m_NodesArr;
	jx_hashmap_t* m_FuncToNodeMap;
	jir_call_graph_scc_t* m_SCCListHead;
	jir_call_graph_scc_t* m_SCCListTail;
} jir_call_graph_t;

typedef struct jir_call_graph_scc_tarjan_state_t
{
	jir_call_graph_node_t** m_Stack;
	uint32_t m_NextIndex;
} jir_call_graph_scc_tarjan_state_t;

typedef struct jir_module_pass_inliner_t
{
	jx_allocator_i* m_Allocator;
	jx_ir_context_t* m_Ctx;
	jx_hashmap_t* m_ValueMap;
} jir_module_pass_inliner_t;

static void jir_funcPass_inlineFuncsDestroy(jx_ir_module_pass_o* inst, jx_allocator_i* allocator);
static bool jir_funcPass_inlineFuncsRun(jx_ir_module_pass_o* inst, jx_ir_context_t* ctx, jx_ir_module_t* func);

static bool jir_inliner_inlineCall(jir_module_pass_inliner_t* pass, jx_ir_instruction_t* callInstr);

static jir_call_graph_t* jir_callGraphCreate(jx_allocator_i* allocator);
static void jir_callGraphDestroy(jir_call_graph_t* cg);
static jir_call_graph_node_t* jir_callGraphAddFunc(jir_call_graph_t* cg, jx_ir_function_t* func);
static bool jir_callGraphBuild(jir_call_graph_t* cg);
static bool jir_callGraphNodeCallsFunc(jir_call_graph_node_t* node, jx_ir_function_t* func);
static void jir_callGraphStrongConnect(jir_call_graph_t* cg, jir_call_graph_node_t* node, jir_call_graph_scc_tarjan_state_t* sccState);
static void jir_callGraphAppendSCC(jir_call_graph_t* cg, jir_call_graph_scc_t* scc);
static jir_call_graph_node_t* jir_callGraphNodeAlloc(jir_call_graph_t* cg, jx_ir_function_t* func);
static void jir_callGraphNodeFree(jir_call_graph_t* cg, jir_call_graph_node_t* node);
static jir_call_graph_scc_t* jir_callGraphSCCAlloc(jir_call_graph_t* cg);
static void jir_callGraphSCCFree(jir_call_graph_t* cg, jir_call_graph_scc_t* scc);

static uint64_t jir_funcCGNodeItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_funcCGNodeItemCompare(const void* a, const void* b, void* udata);
static uint64_t jir_valueMapItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_valueMapItemCompare(const void* a, const void* b, void* udata);

bool jx_ir_modulePassCreate_inlineFuncs(jx_ir_module_pass_t* pass, jx_allocator_i* allocator)
{
	jir_module_pass_inliner_t* inst = (jir_module_pass_inliner_t*)JX_ALLOC(allocator, sizeof(jir_module_pass_inliner_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_module_pass_inliner_t));
	inst->m_Allocator = allocator;

	inst->m_ValueMap = jx_hashmapCreate(allocator, sizeof(jir_value_map_item_t), 64, 0, 0, jir_valueMapItemHash, jir_valueMapItemCompare, NULL, NULL);
	if (!inst->m_ValueMap) {
		jir_funcPass_inlineFuncsDestroy((jx_ir_module_pass_o*)inst, allocator);
		return false;
	}

	pass->m_Inst = (jx_ir_module_pass_o*)inst;
	pass->run = jir_funcPass_inlineFuncsRun;
	pass->destroy = jir_funcPass_inlineFuncsDestroy;

	return true;
}

static void jir_funcPass_inlineFuncsDestroy(jx_ir_module_pass_o* inst, jx_allocator_i* allocator)
{
	jir_module_pass_inliner_t* pass = (jir_module_pass_inliner_t*)inst;

	if (pass->m_ValueMap) {
		jx_hashmapDestroy(pass->m_ValueMap);
		pass->m_ValueMap = NULL;
	}

	JX_FREE(allocator, pass);
}

static bool jir_funcPass_inlineFuncsRun(jx_ir_module_pass_o* inst, jx_ir_context_t* ctx, jx_ir_module_t* mod)
{
	TracyCZoneN(tracyCtx, "ir: Inline Functions", 1);

	jir_module_pass_inliner_t* pass = (jir_module_pass_inliner_t*)inst;
	pass->m_Ctx = ctx;

	// Create the call graph.
	jir_call_graph_t* callGraph = jir_callGraphCreate(pass->m_Allocator);

	jx_ir_function_t* func = mod->m_FunctionListHead;
	while (func) {
		if (func->m_BasicBlockListHead) {
			jir_callGraphAddFunc(callGraph, func);
		}

		func = func->m_Next;
	}

	jir_callGraphBuild(callGraph);

	uint32_t numCallsInlined = 0;

	jir_call_graph_scc_t* scc = callGraph->m_SCCListHead;
	while (scc) {
		// Inline any function which is it's own SCC (i.e. it's not part of a cycle).
		const uint32_t numSCCNodes = (uint32_t)jx_array_sizeu(scc->m_NodesArr);
		if (numSCCNodes == 1) {
			jir_call_graph_node_t* node = scc->m_NodesArr[0];

			const uint32_t numCalls = (uint32_t)jx_array_sizeu(node->m_CallInstrArr);
			for (uint32_t iCall = 0; iCall < numCalls; ++iCall) {
				jx_ir_instruction_t* callInstr = node->m_CallInstrArr[iCall];
				JX_CHECK(node->m_Func == callInstr->m_ParentBB->m_ParentFunc, "Invalid call instruction!");

				jx_ir_function_t* calleeFunc = jx_ir_valueToFunc(callInstr->super.m_OperandArr[0]->m_Value);

				// Avoid recursive calls
				if (calleeFunc == node->m_Func) {
					continue;
				}

				// TODO: Better heuristic
				const bool shouldInline = (calleeFunc->m_Flags & JIR_FUNC_FLAGS_INLINE_Msk) != 0;
				if (shouldInline) {
					if (jir_inliner_inlineCall(pass, callInstr)) {
						++numCallsInlined;
					}
				}
			}
		}

		scc = scc->m_Next;
	}

	jir_callGraphDestroy(callGraph);

	TracyCZoneEnd(tracyCtx);

	return numCallsInlined != 0;
}

static bool jir_inliner_inlineCall(jir_module_pass_inliner_t* pass, jx_ir_instruction_t* callInstr)
{
	jx_ir_context_t* ctx = pass->m_Ctx;

	jx_ir_basic_block_t* callBB = callInstr->m_ParentBB;
	jx_ir_function_t* callerFunc = callBB->m_ParentFunc;
	jx_ir_function_t* calleeFunc = jx_ir_valueToFunc(callInstr->super.m_OperandArr[0]->m_Value);

	JX_CHECK(callerFunc != calleeFunc, "Cannot self-inline!");
	JX_CHECK(jx_ir_funcCheck(ctx, callerFunc), "Caller function is not valid!");

	jx_hashmapClear(pass->m_ValueMap, false);

	// Insert all callee arguments to the map
	uint32_t argID = 0;
	jx_ir_argument_t* calleeArg = calleeFunc->m_ArgListHead;
	while (calleeArg) {
		jx_ir_value_t* operandVal = callInstr->super.m_OperandArr[1 + argID]->m_Value;
		
		jx_hashmapSet(pass->m_ValueMap, &(jir_value_map_item_t){ .m_Key = jx_ir_argToValue(calleeArg), .m_Value = operandVal });

		++argID;
		calleeArg = calleeArg->m_Next;
	}

	// Clone callee basic blocks and instructions, WITHOUT adding any of them 
	// to the caller yet (not even the cloned instructions to the cloned basic blocks).
	// This is needed because the bbs/instructions have to patched to use the new operands
	// before adding them to a func/bb otherwise the CFG will be invalid.
	{
		jx_ir_basic_block_t* calleeBB = calleeFunc->m_BasicBlockListHead;
		while (calleeBB) {
			jx_ir_basic_block_t* clonedBB = jx_ir_bbAlloc(ctx, NULL);
			jx_hashmapSet(pass->m_ValueMap, &(jir_value_map_item_t){ .m_Key = jx_ir_bbToValue(calleeBB), .m_Value = jx_ir_bbToValue(clonedBB) });

			jx_ir_instruction_t* calleeInstr = calleeBB->m_InstrListHead;
			while (calleeInstr) {
				jx_ir_instruction_t* clonedInstr = jx_ir_instrClone(ctx, calleeInstr);
				jx_hashmapSet(pass->m_ValueMap, &(jir_value_map_item_t){ .m_Key = jx_ir_instrToValue(calleeInstr), jx_ir_instrToValue(clonedInstr) });
		
				calleeInstr = calleeInstr->m_Next;
			}

			calleeBB = calleeBB->m_Next;
		}
	}

	// Patch all cloned instructions to use the new values.
	jx_ir_basic_block_t* firstClonedBB = NULL;
	{
		jx_ir_basic_block_t* originalBB = calleeFunc->m_BasicBlockListHead;
		while (originalBB) {
			jir_value_map_item_t* bbItem = (jir_value_map_item_t*)jx_hashmapGet(pass->m_ValueMap, &(jir_value_map_item_t){.m_Key = jx_ir_bbToValue(originalBB)});
			JX_CHECK(bbItem, "Basic block not found in map!");
			jx_ir_basic_block_t* clonedBB = jx_ir_valueToBasicBlock(bbItem->m_Value);

			jx_ir_instruction_t* originalInstr = originalBB->m_InstrListHead;
			while (originalInstr) {
				jir_value_map_item_t* instrItem = (jir_value_map_item_t*)jx_hashmapGet(pass->m_ValueMap, &(jir_value_map_item_t){.m_Key = jx_ir_instrToValue(originalInstr)});
				JX_CHECK(instrItem, "Instruction not found in map!");
				jx_ir_instruction_t* clonedInstr = jx_ir_valueToInstr(instrItem->m_Value);

				const uint32_t numOperands = (uint32_t)jx_array_sizeu(clonedInstr->super.m_OperandArr);
				for (uint32_t iOp = 0; iOp < numOperands; ++iOp) {
					jx_ir_value_t* operandVal = clonedInstr->super.m_OperandArr[iOp]->m_Value;
					jir_value_map_item_t* item = (jir_value_map_item_t*)jx_hashmapGet(pass->m_ValueMap, &(jir_value_map_item_t){ .m_Key = operandVal });
					if (item) {
						jx_ir_instrReplaceOperand(ctx, clonedInstr, iOp, item->m_Value);
					} else {
						JX_CHECK(operandVal->m_Kind == JIR_VALUE_CONSTANT || operandVal->m_Kind == JIR_VALUE_FUNCTION || operandVal->m_Kind == JIR_VALUE_GLOBAL_VARIABLE, "Operand should have been found in the map!");
					}
				}

				jx_ir_bbAppendInstr(ctx, clonedBB, clonedInstr);

				originalInstr = originalInstr->m_Next;
			}

			jx_ir_funcAppendBasicBlock(ctx, callerFunc, clonedBB);

			if (!firstClonedBB) {
				firstClonedBB = clonedBB;
			}

			originalBB = originalBB->m_Next;
		}
	}

	// Move all allocas to the caller's entry block.
	{
		jx_ir_basic_block_t* callerEntryBB = callerFunc->m_BasicBlockListHead;
		jx_ir_instruction_t* instr = firstClonedBB->m_InstrListHead;
		while (instr && instr->m_OpCode == JIR_OP_ALLOCA) {
			jx_ir_bbRemoveInstr(ctx, instr->m_ParentBB, instr);
			jx_ir_bbPrependInstr(ctx, callerEntryBB, instr);

			instr = instr->m_Next;
		}
	}

	// Split call's basic block at the call instruction.
	jx_ir_basic_block_t* contBB = jx_ir_bbSplitAt(ctx, callBB, callInstr);
	JX_CHECK(contBB, "Failed to split basic block!");
	
	JX_CHECK(jx_ir_funcCheck(ctx, callerFunc), "Caller function is not valid!");

	// Now, callBB ends in an unconditional branch to contBB. 
	// Patch the unconditional branch to point to the cloned BB and 
	// remove the actual call instruction from callBB.
	{
		jx_ir_instruction_t* termInstr = jx_ir_bbGetLastInstr(ctx, callBB);
		JX_CHECK(termInstr && termInstr->m_OpCode == JIR_OP_BRANCH && jx_array_sizeu(termInstr->super.m_OperandArr) == 1 && termInstr->super.m_OperandArr[0]->m_Value == jx_ir_bbToValue(contBB), "Unexpected instruction!");

		jx_ir_bbRemoveInstr(ctx, callBB, termInstr);
		jx_ir_instrReplaceOperand(ctx, termInstr, 0, jx_ir_bbToValue(firstClonedBB));
		jx_ir_bbAppendInstr(ctx, callBB, termInstr);
	}

	// Replace all ret instructions in the cloned blocks with unconditional branches to the contBB.
	// If the function returned a value, add a phi instruction in contBB with all the values collected.
	{
		jx_ir_type_function_t* calleeFuncType = jx_ir_funcGetType(ctx, calleeFunc);
		jx_ir_instruction_t* phiInstr = calleeFuncType->m_RetType->m_Kind != JIR_TYPE_VOID
			? jx_ir_instrPhi(ctx, calleeFuncType->m_RetType)
			: NULL
			;
		
		jx_ir_basic_block_t* bb = firstClonedBB;
		while (bb) {
			jx_ir_instruction_t* lastInstr = jx_ir_bbGetLastInstr(ctx, bb);
			if (lastInstr->m_OpCode == JIR_OP_RET) {
				jx_ir_bbRemoveInstr(ctx, bb, lastInstr);

				jx_ir_instruction_t* branchInstr = jx_ir_instrBranch(ctx, contBB);
				jx_ir_bbAppendInstr(ctx, bb, branchInstr);

				if (phiInstr) {
					JX_CHECK(jx_array_sizeu(lastInstr->super.m_OperandArr) == 1, "ret instruction expected to have an operand");
					jx_ir_instrPhiAddValue(ctx, phiInstr, bb, lastInstr->super.m_OperandArr[0]->m_Value);
				}

				jx_ir_instrFree(ctx, lastInstr);
			}

			bb = bb->m_Next;
		}

		if (phiInstr) {
			// NOTE: Currently due to the way mirgen walks the function's basic block list
			// I cannot eliminate single operand phis at this point. Once this is fixed,
			// I can uncomment the following check.
#if 0
			if (jx_array_sizeu(phiInstr->super.m_OperandArr) == 2) {
				jx_ir_value_t* phiVal = phiInstr->super.m_OperandArr[0]->m_Value;
				jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(callInstr), phiVal);
				jx_ir_instrFree(ctx, phiInstr);
			} else 
#endif
			{
				jx_ir_bbPrependInstr(ctx, contBB, phiInstr);
				jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(callInstr), jx_ir_instrToValue(phiInstr));
			}
		}

		jx_ir_bbRemoveInstr(ctx, callBB, callInstr);
		jx_ir_instrFree(ctx, callInstr);
	}

	// At this point the caller must be valid!
	JX_CHECK(jx_ir_funcCheck(ctx, callerFunc), "Caller function is not valid!");

	return true;
}

static jir_call_graph_t* jir_callGraphCreate(jx_allocator_i* allocator)
{
	jir_call_graph_t* cg = (jir_call_graph_t*)JX_ALLOC(allocator, sizeof(jir_call_graph_t));
	if (!cg) {
		return NULL;
	}

	jx_memset(cg, 0, sizeof(jir_call_graph_t));
	cg->m_Allocator = allocator;
	cg->m_FuncToNodeMap = jx_hashmapCreate(allocator, sizeof(jir_func_cgnode_item_t), 64, 0, 0, jir_funcCGNodeItemHash, jir_funcCGNodeItemCompare, NULL, NULL);
	if (!cg->m_FuncToNodeMap) {
		jir_callGraphDestroy(cg);
		return NULL;
	}

	cg->m_NodesArr = (jir_call_graph_node_t**)jx_array_create(allocator);
	if (!cg->m_NodesArr) {
		jir_callGraphDestroy(cg);
		return NULL;
	}

	return cg;
}

static void jir_callGraphDestroy(jir_call_graph_t* cg)
{
	jir_call_graph_scc_t* scc = cg->m_SCCListHead;
	while (scc) {
		jir_call_graph_scc_t* sccNext = scc->m_Next;
		jir_callGraphSCCFree(cg, scc);
		scc = sccNext;
	}

	if (cg->m_FuncToNodeMap) {
		jx_hashmapDestroy(cg->m_FuncToNodeMap);
		cg->m_FuncToNodeMap = NULL;
	}

	const uint32_t numNodes = (uint32_t)jx_array_sizeu(cg->m_NodesArr);
	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		jir_callGraphNodeFree(cg, cg->m_NodesArr[iNode]);
	}
	jx_array_free(cg->m_NodesArr);
	cg->m_NodesArr = NULL;

	JX_FREE(cg->m_Allocator, cg);
}

static jir_call_graph_node_t* jir_callGraphAddFunc(jir_call_graph_t* cg, jx_ir_function_t* func)
{
	jir_call_graph_node_t* node = jir_callGraphNodeAlloc(cg, func);
	if (!node) {
		return NULL;
	}

	jx_array_push_back(cg->m_NodesArr, node);
	jx_hashmapSet(cg->m_FuncToNodeMap, &(jir_func_cgnode_item_t){ .m_Func = func, .m_Node = node });

	return node;
}

static bool jir_callGraphBuild(jir_call_graph_t* cg)
{
	// Find all call instructions and create edges between nodes.
	// TODO: Avoid scanning the whole function and just use the uses list of each function.
	// If a use is a call instruction, create an edge between the caller (parent of the call instruction
	// and the current function). Otherwise, ignore use.
	const uint32_t numNodes = (uint32_t)jx_array_sizeu(cg->m_NodesArr);
	for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
		jir_call_graph_node_t* node = cg->m_NodesArr[iNode];
		jx_ir_function_t* func = node->m_Func;

		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				if (instr->m_OpCode == JIR_OP_CALL) {
					// Check if this is a direct call to an internal function.
					jx_ir_function_t* calleeFunc = jx_ir_valueToFunc(instr->super.m_OperandArr[0]->m_Value);
					if (calleeFunc && calleeFunc->m_BasicBlockListHead) {
						jir_func_cgnode_item_t* item = (jir_func_cgnode_item_t*)jx_hashmapGet(cg->m_FuncToNodeMap, &(jir_func_cgnode_item_t){ .m_Func = calleeFunc });
						if (item) {
							jx_array_push_back(node->m_CallInstrArr, instr);

							if (!jir_callGraphNodeCallsFunc(node, calleeFunc)) {
								jx_array_push_back(node->m_CalledFuncsArr, item->m_Node);
							}
						}
					}
				}

				instr = instr->m_Next;
			}

			bb = bb->m_Next;
		}
	}

	// Find strongly-connected components
	// https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm
	{
		jir_call_graph_scc_tarjan_state_t* sccState = &(jir_call_graph_scc_tarjan_state_t){ 0 };

		sccState->m_Stack = (jir_call_graph_node_t**)jx_array_create(cg->m_Allocator);
		jx_array_reserve(sccState->m_Stack, numNodes);

		for (uint32_t iNode = 0; iNode < numNodes; ++iNode) {
			jir_call_graph_node_t* node = cg->m_NodesArr[iNode];
			if (node->m_ID == UINT32_MAX) {
				jir_callGraphStrongConnect(cg, node, sccState);
			}
		}

		jx_array_free(sccState->m_Stack);
	}

	return true;
}

static bool jir_callGraphNodeCallsFunc(jir_call_graph_node_t* node, jx_ir_function_t* func)
{
	const uint32_t numCalledFuncs = (uint32_t)jx_array_sizeu(node->m_CalledFuncsArr);
	for (uint32_t iFunc = 0; iFunc < numCalledFuncs; ++iFunc) {
		if (node->m_CalledFuncsArr[iFunc]->m_Func == func) {
			return true;
		}
	}

	return false;
}

static void jir_callGraphStrongConnect(jir_call_graph_t* cg, jir_call_graph_node_t* node, jir_call_graph_scc_tarjan_state_t* sccState)
{
	const uint32_t id = sccState->m_NextIndex++;
	node->m_ID = id;
	node->m_LowLink = id;

	jx_array_push_back(sccState->m_Stack, node);
	node->m_IsOnStack = true;

	const uint32_t numEdges = (uint32_t)jx_array_sizeu(node->m_CalledFuncsArr);
	for (uint32_t iEdge = 0; iEdge < numEdges; ++iEdge) {
		jir_call_graph_node_t* calledNode = node->m_CalledFuncsArr[iEdge];
		if (calledNode->m_ID == UINT32_MAX) {
			// Successor w has not yet been visited; recurse on it
			jir_callGraphStrongConnect(cg, calledNode, sccState);
			node->m_LowLink = jx_min_u32(node->m_LowLink, calledNode->m_LowLink);
		} else if (calledNode->m_IsOnStack) {
			// Successor w is in stack S and hence in the current SCC
			node->m_LowLink = jx_min_u32(node->m_LowLink, calledNode->m_ID);
		} else {
			// If w is not on stack, then (v, w) is an edge pointing to 
			// an SCC already found and must be ignored
		}
	}

	// If v is a root node, pop the stack and generate an SCC
	if (node->m_LowLink == node->m_ID) {
		jir_call_graph_scc_t* scc = jir_callGraphSCCAlloc(cg);
		
		jir_call_graph_node_t* stackNode = NULL;
		do {
			stackNode = jx_array_pop_back(sccState->m_Stack);
			stackNode->m_IsOnStack = false;
			jx_array_push_back(scc->m_NodesArr, stackNode);
		} while (stackNode != node);

		jir_callGraphAppendSCC(cg, scc);
	}
}

static void jir_callGraphAppendSCC(jir_call_graph_t* cg, jir_call_graph_scc_t* scc)
{
	if (!cg->m_SCCListHead) {
		JX_CHECK(!cg->m_SCCListTail, "Invalid linked-list state");
		cg->m_SCCListHead = scc;
		cg->m_SCCListTail = scc;
	} else {
		JX_CHECK(cg->m_SCCListTail, "Invalid linked-list state");
		cg->m_SCCListTail->m_Next = scc;
		scc->m_Prev = cg->m_SCCListTail;
		cg->m_SCCListTail = scc;
	}
}

static jir_call_graph_node_t* jir_callGraphNodeAlloc(jir_call_graph_t* cg, jx_ir_function_t* func)
{
	jir_call_graph_node_t* node = (jir_call_graph_node_t*)JX_ALLOC(cg->m_Allocator, sizeof(jir_call_graph_node_t));
	if (!node) {
		return NULL;
	}

	jx_memset(node, 0, sizeof(jir_call_graph_node_t));
	node->m_Func = func;
	node->m_ID = UINT32_MAX;
	node->m_LowLink = 0;
	node->m_IsOnStack = false;
	node->m_CallInstrArr = (jx_ir_instruction_t**)jx_array_create(cg->m_Allocator);
	if (!node->m_CallInstrArr) {
		jir_callGraphNodeFree(cg, node);
		return NULL;
	}

	node->m_CalledFuncsArr = (jir_call_graph_node_t**)jx_array_create(cg->m_Allocator);
	if (!node->m_CalledFuncsArr) {
		jir_callGraphNodeFree(cg, node);
		return NULL;
	}

	return node;
}

static void jir_callGraphNodeFree(jir_call_graph_t* cg, jir_call_graph_node_t* node)
{
	jx_array_free(node->m_CallInstrArr);
	jx_array_free(node->m_CalledFuncsArr);
	JX_FREE(cg->m_Allocator, node);
}

static jir_call_graph_scc_t* jir_callGraphSCCAlloc(jir_call_graph_t* cg)
{
	jir_call_graph_scc_t* scc = (jir_call_graph_scc_t*)JX_ALLOC(cg->m_Allocator, sizeof(jir_call_graph_scc_t));
	if (!scc) {
		return NULL;
	}

	jx_memset(scc, 0, sizeof(jir_call_graph_scc_t));
	scc->m_NodesArr = (jir_call_graph_node_t**)jx_array_create(cg->m_Allocator);
	if (!scc->m_NodesArr) {
		jir_callGraphSCCFree(cg, scc);
		return NULL;
	}

	return scc;
}

static void jir_callGraphSCCFree(jir_call_graph_t* cg, jir_call_graph_scc_t* scc)
{
	jx_array_free(scc->m_NodesArr);
	JX_FREE(cg->m_Allocator, scc);
}

static uint64_t jir_funcCGNodeItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata)
{
	const jir_func_cgnode_item_t* var = (const jir_func_cgnode_item_t*)item;
	uint64_t hash = jx_hashFNV1a(&var->m_Func, sizeof(jx_ir_function_t*), seed0, seed1);
	return hash;
}

static int32_t jir_funcCGNodeItemCompare(const void* a, const void* b, void* udata)
{
	const jir_func_cgnode_item_t* varA = (const jir_func_cgnode_item_t*)a;
	const jir_func_cgnode_item_t* varB = (const jir_func_cgnode_item_t*)b;
	int32_t res = jir_comparePtrs(varA->m_Func, varB->m_Func);
	return res;
}
