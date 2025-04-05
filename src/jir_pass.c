// TODO:
// - Dead store elimination
// - Constant folding
// - Constant propagation
// - 
#include "jir_pass.h"
#include "jir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/hashmap.h>
#include <jlib/memory.h>
#include <jlib/string.h>

static int32_t jir_comparePtrs(void* a, void* b);

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
	jir_func_pass_simplify_cfg_t* pass = (jir_func_pass_simplify_cfg_t*)inst;

	uint32_t numBasicBlocksChanged = 0;

	bool cfgChanged = true;
	while (cfgChanged) {
		cfgChanged = false;

		// Always skip the entry block
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead->m_Next;
		while (bb) {
			jx_ir_basic_block_t* bbNext = bb->m_Next;

			const uint32_t numPred = (uint32_t)jx_array_sizeu(bb->m_PredArr);
			if (!numPred) {
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
#if 0
			} else if (jir_bbIsUnconditionalJump(bb)) {
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
	jx_ir_context_t* m_Ctx;
	jx_hashmap_t* m_DefMap;
	jx_ir_value_t** m_PhiUsersArr;             // Temporary array to hold all phi instruction users while trying to remove trivial phi.
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
static uint64_t jir_bbVarMapItemHash(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
static int32_t jir_bbVarMapItemCompare(const void* a, const void* b, void* udata);

bool jx_ir_funcPassCreate_simpleSSA(jx_ir_function_pass_t* pass, jx_allocator_i* allocator)
{
	jir_func_pass_simple_ssa_t* inst = (jir_func_pass_simple_ssa_t*)JX_ALLOC(allocator, sizeof(jir_func_pass_simple_ssa_t));
	if (!inst) {
		return false;
	}

	jx_memset(inst, 0, sizeof(jir_func_pass_simple_ssa_t));
	inst->m_PhiUsersArr = (jx_ir_value_t**)jx_array_create(allocator);
	if (!inst->m_PhiUsersArr) {
		jir_funcPass_simpleSSADestroy((jx_ir_function_pass_o*)inst, allocator);
		return false;
	}

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
	jx_array_free(pass->m_PhiUsersArr);
	jx_hashmapDestroy(pass->m_DefMap);
	JX_FREE(allocator, pass);
}

static bool jir_funcPass_simpleSSARun(jx_ir_function_pass_o* inst, jx_ir_context_t* ctx, jx_ir_function_t* func)
{
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

		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jir_simpleSSA_trySealBlock(pass, bb);

			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_ir_instruction_t* nextInstr = instr->m_Next;

				if (instr->m_OpCode == JIR_OP_STORE) {
					jx_ir_value_t* addr = instr->super.m_OperandArr[0]->m_Value;
					jx_ir_value_t* val = instr->super.m_OperandArr[1]->m_Value;

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

	pass->m_Ctx = NULL;

	return false;
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
	return hashItem
		? hashItem->m_Value
		: jir_simpleSSA_readVariable_r(pass, bb, addr)
		;
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
	} else if (jx_array_sizeu(bb->m_PredArr) == 1) {
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
	jx_ir_valueReplaceAllUsesWith(pass->m_Ctx, var, newVal);

	uint32_t iter = 0;
	jir_bb_var_map_item_t* hashItem = NULL;
	while (jx_hashmapIter(pass->m_DefMap, &iter, &hashItem)) {
		if (hashItem->m_Value == var) {
			hashItem->m_Value = newVal;
		}
	}
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
		JX_NOT_IMPLEMENTED(); // TODO: Requires Undef.
		return phiInstrVal;
	}

	// Remove self-reference in phi
	for (uint32_t iOperand = 0; iOperand < numOperands; iOperand += 2) {
		jx_ir_value_t* bbVal = phiInstr->super.m_OperandArr[iOperand + 0]->m_Value;
		if (bbVal == phiInstrVal) {
			jx_ir_basic_block_t* bb = jx_ir_valueToBasicBlock(phiInstr->super.m_OperandArr[iOperand + 1]->m_Value);
			JX_CHECK(bb, "Expected basic block!");
			jx_ir_instrPhiRemoveValue(pass->m_Ctx, phiInstr, bb);
			break;
		}
	}

	// Keep all phi users
	jx_array_resize(pass->m_PhiUsersArr, 0);
	jx_ir_use_t* phiUse = phiInstrVal->m_UsesListHead;
	while (phiUse) {
		jx_array_push_back(pass->m_PhiUsersArr, jx_ir_userToValue(phiUse->m_User));
		phiUse = phiUse->m_Next;
	}

	jir_simpleSSA_replaceVariable(pass, phiInstrVal, same);

	// Remove phi
	jx_ir_bbRemoveInstr(pass->m_Ctx, phiInstr->m_ParentBB, phiInstr);
	jx_ir_instrFree(pass->m_Ctx, phiInstr);

	// Try to recursively remove all phi users which might have become trivial
	const uint32_t numUsers = (uint32_t)jx_array_sizeu(pass->m_PhiUsersArr);
	for (uint32_t iUser = 0; iUser < numUsers; ++iUser) {
		jx_ir_value_t* user = pass->m_PhiUsersArr[iUser];
		jx_ir_instruction_t* userInstr = jx_ir_valueToInstr(user);
		if (userInstr && userInstr->m_OpCode == JIR_OP_PHI) {
			jir_simpleSSA_tryRemoveTrivialPhi(pass, userInstr);
		}
	}

	return same;
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
	jir_func_pass_const_folding_t* pass = (jir_func_pass_const_folding_t*)inst;

	bool changed = true;
	while (changed) {
		changed = false;
		
		jx_ir_basic_block_t* bb = func->m_BasicBlockListHead;
		while (bb) {
			jx_ir_instruction_t* instr = bb->m_InstrListHead;
			while (instr) {
				jx_ir_instruction_t* instrNext = instr->m_Next;

				switch (instr->m_OpCode) {
				case JIR_OP_BRANCH: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					const uint32_t numOperands = (uint32_t)jx_array_sizeu(instrUser->m_OperandArr);
					if (numOperands == 1) {
						// Unconditional branch. No op
					} else if (numOperands == 3) {
						// Conditional branch
						jx_ir_constant_t* cond = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
						if (cond) {
							JX_CHECK(jx_ir_constToValue(cond)->m_Type->m_Kind == JIR_TYPE_BOOL, "Expected boolean conditional as 1st branch operand.");
							jx_ir_value_t* targetVal = cond->u.m_Bool
								? instrUser->m_OperandArr[1]->m_Value
								: instrUser->m_OperandArr[2]->m_Value
								;
							jx_ir_basic_block_t* targetBB = jx_ir_valueToBasicBlock(targetVal);
							JX_CHECK(targetBB, "Expected basic block as branch targets");

							jx_ir_instruction_t* uncondBranch = jx_ir_instrBranch(ctx, targetBB);
							jx_ir_bbRemoveInstr(ctx, bb, instr);
							jx_ir_bbAppendInstr(ctx, bb, uncondBranch);
							jx_ir_instrFree(ctx, instr);
						}
					} else {
						JX_CHECK(false, "Unknown branch instruction");
					}
				} break;
				case JIR_OP_ADD: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_addConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_SUB: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_subConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_MUL: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_mulConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_DIV: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_divConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_REM: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_remConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_AND: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_andConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_OR: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_orConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_XOR: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_xorConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_SET_LE:
				case JIR_OP_SET_GE:
				case JIR_OP_SET_LT:
				case JIR_OP_SET_GT:
				case JIR_OP_SET_EQ:
				case JIR_OP_SET_NE: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_cmpConst(ctx, lhs, rhs, (jx_ir_condition_code)(instr->m_OpCode - JIR_OP_SET_CC_BASE));
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_GET_ELEMENT_PTR: {
//					JX_NOT_IMPLEMENTED();
				} break;
				case JIR_OP_PHI: {
					// TODO: If all phi values are the same constant replace phi with constant.
//					JX_NOT_IMPLEMENTED();
				} break;
				case JIR_OP_CALL: {
//					JX_NOT_IMPLEMENTED();
				} break;
				case JIR_OP_SHL: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_shlConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_SHR: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* lhs = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					jx_ir_constant_t* rhs = jx_ir_valueToConst(instrUser->m_OperandArr[1]->m_Value);
					if (lhs && rhs) {
						jx_ir_constant_t* resConst = jir_constFold_shrConst(ctx, lhs, rhs);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_TRUNC: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* op = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					if (op) {
						jx_ir_constant_t* resConst = jir_constFold_truncConst(ctx, op, jx_ir_instrToValue(instr)->m_Type);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_ZEXT: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* op = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					if (op) {
						jx_ir_constant_t* resConst = jir_constFold_zextConst(ctx, op, jx_ir_instrToValue(instr)->m_Type);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_SEXT: {
					jx_ir_user_t* instrUser = jx_ir_instrToUser(instr);
					jx_ir_constant_t* op = jx_ir_valueToConst(instrUser->m_OperandArr[0]->m_Value);
					if (op) {
						jx_ir_constant_t* resConst = jir_constFold_sextConst(ctx, op, jx_ir_instrToValue(instr)->m_Type);
						jx_ir_valueReplaceAllUsesWith(ctx, jx_ir_instrToValue(instr), jx_ir_constToValue(resConst));
						jx_ir_bbRemoveInstr(ctx, bb, instr);
						jx_ir_instrFree(ctx, instr);
					}
				} break;
				case JIR_OP_RET:
				case JIR_OP_ALLOCA:
				case JIR_OP_LOAD:
				case JIR_OP_STORE:
				case JIR_OP_PTR_TO_INT:
				case JIR_OP_INT_TO_PTR:
				case JIR_OP_BITCAST: {
					// No op
				} break;
				default:
					JX_CHECK(false, "Unknown IR op");
					break;
				}

				instr = instrNext;
			}

			bb = bb->m_Next;
		}
	}

	return false;
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
		case JIR_TYPE_F32:  { res = lhs->u.m_F32           <= rhs->u.m_F32;           } break;
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
		case JIR_TYPE_F32:  { res = lhs->u.m_F32           >= rhs->u.m_F32;           } break;
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
		case JIR_TYPE_F32:  { res = lhs->u.m_F32           < rhs->u.m_F32;           } break;
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
		case JIR_TYPE_F32:  { res = lhs->u.m_F32           > rhs->u.m_F32;           } break;
		case JIR_TYPE_F64:  { res = lhs->u.m_F64           > rhs->u.m_F64;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	case JIR_CC_EQ: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL: { res = lhs->u.m_Bool          == rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:   { res = (uint8_t)lhs->u.m_U64  == (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:   { res = (int8_t)lhs->u.m_I64   == (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:  { res = (uint16_t)lhs->u.m_U64 == (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:  { res = (int16_t)lhs->u.m_I64  == (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:  { res = (uint32_t)lhs->u.m_U64 == (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:  { res = (int32_t)lhs->u.m_I64  == (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:  { res = lhs->u.m_U64           == rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:  { res = lhs->u.m_I64           == rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:  { res = lhs->u.m_F32           == rhs->u.m_F32;           } break;
		case JIR_TYPE_F64:  { res = lhs->u.m_F64           == rhs->u.m_F64;           } break;
		default:
			JX_CHECK(false, "Invalid comparison types?");
			break;
		}
	} break;
	case JIR_CC_NE: {
		switch (operandType->m_Kind) {
		case JIR_TYPE_BOOL: { res = lhs->u.m_Bool          != rhs->u.m_Bool;          } break;
		case JIR_TYPE_U8:   { res = (uint8_t)lhs->u.m_U64  != (uint8_t)rhs->u.m_U64;  } break;
		case JIR_TYPE_I8:   { res = (int8_t)lhs->u.m_I64   != (int8_t)rhs->u.m_I64;   } break;
		case JIR_TYPE_U16:  { res = (uint16_t)lhs->u.m_U64 != (uint16_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I16:  { res = (int16_t)lhs->u.m_I64  != (int16_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U32:  { res = (uint32_t)lhs->u.m_U64 != (uint32_t)rhs->u.m_U64; } break;
		case JIR_TYPE_I32:  { res = (int32_t)lhs->u.m_I64  != (int32_t)rhs->u.m_I64;  } break;
		case JIR_TYPE_U64:  { res = lhs->u.m_U64           != rhs->u.m_U64;           } break;
		case JIR_TYPE_I64:  { res = lhs->u.m_I64           != rhs->u.m_I64;           } break;
		case JIR_TYPE_F32:  { res = lhs->u.m_F32           != rhs->u.m_F32;           } break;
		case JIR_TYPE_F64:  { res = lhs->u.m_F64           != rhs->u.m_F64;           } break;
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
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, lhs->u.m_F32            + rhs->u.m_F32);  } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            + rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_subConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
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
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, lhs->u.m_F32            - rhs->u.m_F32);  } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            - rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_mulConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
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
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, lhs->u.m_F32            * rhs->u.m_F32);  } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            * rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_divConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
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
	case JIR_TYPE_F32:  { res = jx_ir_constGetF32(ctx, lhs->u.m_F32            / rhs->u.m_F32);  } break;
	case JIR_TYPE_F64:  { res = jx_ir_constGetF64(ctx, lhs->u.m_F64            / rhs->u.m_F64);  } break;
	default:
		JX_CHECK(false, "Invalid types?");
		break;
	}

	return res;
}

static jx_ir_constant_t* jir_constFold_remConst(jx_ir_context_t* ctx, jx_ir_constant_t* lhs, jx_ir_constant_t* rhs)
{
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
	jx_ir_type_t* operandType = jx_ir_constToValue(lhs)->m_Type;

	jx_ir_constant_t* res = NULL;
	switch (operandType->m_Kind) {
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
