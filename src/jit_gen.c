#include "jit_gen.h"
#include "jit.h"
#include "jmir.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/memory.h>
#include <jlib/string.h>

static jx_x64_operand_t jx_x64gen_convertMIROperand(jx_x64_context_t* jitCtx, const jx_mir_operand_t* mirOp);
static jx_x64_size jx_x64gen_convertMIRTypeToSize(jx_mir_type_kind type);
static jx_x64_reg jx_x64gen_convertMIRReg(uint32_t mirRegID, jx_x64_size sz);
static jx_x64_scale jx_x64gen_convertMIRScale(uint32_t mirScale);

bool jx_x64_emitCode(jx_x64_context_t* jitCtx, jx_mir_context_t* mirCtx, jx_allocator_i* allocator)
{
	// Declare global variables.
	const uint32_t numGlobalVars = jx_mir_getNumGlobalVars(mirCtx);
	jx_x64_global_var_t** jitGVs = (jx_x64_global_var_t**)JX_ALLOC(allocator, sizeof(jx_x64_global_var_t*) * numGlobalVars);
	if (!jitGVs) {
		return false;
	}

	jx_memset(jitGVs, 0, sizeof(jx_x64_global_var_t*) * numGlobalVars);
	for (uint32_t iGV = 0; iGV < numGlobalVars; ++iGV) {
		jx_mir_global_variable_t* mirGV = jx_mir_getGlobalVarByID(mirCtx, iGV);

		const char* gvName = jx_strrchr(mirGV->m_Name, ':');
		if (gvName) {
			++gvName;
		} else {
			gvName = mirGV->m_Name;
		}

		jitGVs[iGV] = jx64_globalVarDeclare(jitCtx, gvName);
	}

	// Declare functions
	const uint32_t numFunctions = jx_mir_getNumFunctions(mirCtx);
	jx_x64_func_t** jitFuncs = (jx_x64_func_t**)JX_ALLOC(allocator, sizeof(jx_x64_func_t*) * numFunctions);
	if (!jitFuncs) {
		return false;
	}

	jx_memset(jitFuncs, 0, sizeof(jx_x64_func_t*) * numFunctions);
	for (uint32_t iFunc = 0; iFunc < numFunctions; ++iFunc) {
		jx_mir_function_t* mirFunc = jx_mir_getFunctionByID(mirCtx, iFunc);

		const char* funcName = jx_strrchr(mirFunc->m_Name, ':');
		if (funcName) {
			++funcName;
		} else {
			funcName = mirFunc->m_Name;
		}

		jitFuncs[iFunc] = jx64_funcDeclare(jitCtx, funcName);
	}

	// Emit functions
	for (uint32_t iFunc = 0; iFunc < numFunctions; ++iFunc) {
		jx_mir_function_t* mirFunc = jx_mir_getFunctionByID(mirCtx, iFunc);

		if (mirFunc->m_BasicBlockListHead) {
			const uint32_t numBasicBlocks = mirFunc->m_NextBasicBlockID;
			jx_x64_label_t** bbLabels = (jx_x64_label_t**)JX_ALLOC(allocator, sizeof(jx_x64_label_t*) * numBasicBlocks);
			if (!bbLabels) {
				return false;
			}

			jx_memset(bbLabels, 0, sizeof(jx_x64_label_t*) * numBasicBlocks);
			for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
				bbLabels[iBB] = jx64_labelAlloc(jitCtx);
			}

			jx64_funcBegin(jitCtx, jitFuncs[iFunc]);

			jx_mir_basic_block_t* mirBB = mirFunc->m_BasicBlockListHead;
			while (mirBB) {
				jx64_labelBind(jitCtx, bbLabels[mirBB->m_ID]);

				jx_mir_instruction_t* mirInstr = mirBB->m_InstrListHead;
				while (mirInstr) {
					switch (mirInstr->m_OpCode) {
					case JMIR_OP_RET: {
						jx64_retn(jitCtx);
					} break;
					case JMIR_OP_CMP: {
						jx_x64_operand_t op1 = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t op2 = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_cmp(jitCtx, op1, op2);
					} break;
					case JMIR_OP_TEST: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_test(jitCtx, dst, src);
					} break;
					case JMIR_OP_JMP: {
						jx_mir_operand_t* mirOp = mirInstr->m_Operands[0];
						JX_CHECK(mirOp->m_Kind == JMIR_OPERAND_BASIC_BLOCK, "jmp expected basic block operand");
						jx_x64_operand_t lblOp = jx64_opLbl(JX64_SIZE_64, bbLabels[mirOp->u.m_BB->m_ID]);
						jx64_jmp(jitCtx, lblOp);
					} break;
					case JMIR_OP_PHI: {
						JX_NOT_IMPLEMENTED();
					} break;
					case JMIR_OP_MOV: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_mov(jitCtx, dst, src);
					} break;
					case JMIR_OP_MOVSX: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_movsx(jitCtx, dst, src);
					} break;
					case JMIR_OP_MOVZX: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_movzx(jitCtx, dst, src);
					} break;
					case JMIR_OP_IMUL: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_imul(jitCtx, dst, src);
					} break;
					case JMIR_OP_IDIV: {
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx64_idiv(jitCtx, src);
					} break;
					case JMIR_OP_DIV: {
						JX_NOT_IMPLEMENTED();
					} break;
					case JMIR_OP_ADD: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_add(jitCtx, dst, src);
					} break;
					case JMIR_OP_SUB: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_sub(jitCtx, dst, src);
					} break;
					case JMIR_OP_LEA: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_lea(jitCtx, dst, src);
					} break;
					case JMIR_OP_XOR: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_xor(jitCtx, dst, src);
					} break;
					case JMIR_OP_AND: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_and(jitCtx, dst, src);
					} break;
					case JMIR_OP_OR: {
						jx_x64_operand_t dst = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_operand_t src = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[1]);
						jx64_or(jitCtx, dst, src);
					} break;
					case JMIR_OP_SAR: {
						JX_NOT_IMPLEMENTED();
					} break;
					case JMIR_OP_SHR: {
						JX_NOT_IMPLEMENTED();
					} break;
					case JMIR_OP_SHL: {
						JX_NOT_IMPLEMENTED();
					} break;
					case JMIR_OP_CALL: {
						jx_mir_operand_t* targetOp = mirInstr->m_Operands[0];
						const char* funcName = targetOp->u.m_ExternalSymbolName;
						jx_x64_label_t* funcLbl = jx64_funcGetLabelByName(jitCtx, funcName);
						JX_CHECK(funcLbl, "Function not found!");
						jx64_call(jitCtx, jx64_opLbl(JX64_SIZE_64, funcLbl));
					} break;
					case JMIR_OP_PUSH: {
						jx_x64_operand_t op = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx64_push(jitCtx, op);
					} break;
					case JMIR_OP_POP: {
						jx_x64_operand_t op = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx64_pop(jitCtx, op);
					} break;
					case JMIR_OP_CDQ: {
						jx64_cdq(jitCtx);
					} break;
					case JMIR_OP_CQO: {
						jx64_cqo(jitCtx);
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
						jx_x64_operand_t op = jx_x64gen_convertMIROperand(jitCtx, mirInstr->m_Operands[0]);
						jx_x64_condition_code cc = (jx_x64_condition_code)(mirInstr->m_OpCode - JMIR_OP_SETCC_BASE);
						jx64_setcc(jitCtx, cc, op);
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
						jx_mir_operand_t* mirOp = mirInstr->m_Operands[0];
						JX_CHECK(mirOp->m_Kind == JMIR_OPERAND_BASIC_BLOCK, "jcc expected basic block operand");
						jx_x64_condition_code cc = (jx_x64_condition_code)(mirInstr->m_OpCode - JMIR_OP_JCC_BASE);
						jx64_jcc(jitCtx, cc, bbLabels[mirOp->u.m_BB->m_ID]);
					} break;
					default:
						JX_CHECK(false, "Unknown mir instruction opcode");
						break;
					}

					mirInstr = mirInstr->m_Next;
				}

				mirBB = mirBB->m_Next;
			}

			jx64_funcEnd(jitCtx);

			for (uint32_t iBB = 0; iBB < numBasicBlocks; ++iBB) {
				jx64_labelFree(jitCtx, bbLabels[iBB]);
			}
			JX_FREE(allocator, bbLabels);
		} else {
			// TODO: How to handle external functions?
		}
	}

	// Emit global variables
	for (uint32_t iGV = 0; iGV < numGlobalVars; ++iGV) {
		jx_mir_global_variable_t* mirGV = jx_mir_getGlobalVarByID(mirCtx, iGV);

		const uint32_t dataSize = (uint32_t)jx_array_sizeu(mirGV->m_DataArr);
		jx64_globalVarDefine(jitCtx, jitGVs[iGV], mirGV->m_DataArr, dataSize);
	}

	JX_FREE(allocator, jitFuncs);
	JX_FREE(allocator, jitGVs);

	return true;
}

static jx_x64_operand_t jx_x64gen_convertMIROperand(jx_x64_context_t* jitCtx, const jx_mir_operand_t* mirOp)
{
	jx_x64_operand_t op = jx64_opReg(JX64_REG_NONE);

	switch (mirOp->m_Kind) {
	case JMIR_OPERAND_REGISTER: {
		JX_CHECK(mirOp->u.m_RegID < JMIR_FIRST_VIRTUAL_REGISTER, "Expected hardware register!");
		switch (mirOp->m_Type) {
		case JMIR_TYPE_VOID: {
			JX_CHECK(false, "Unexpected void register!");
		} break;
		case JMIR_TYPE_I8: {
			op = jx64_opReg((jx_x64_reg)(JX64_REG_AL + mirOp->u.m_RegID));
		} break;
		case JMIR_TYPE_I16: {
			op = jx64_opReg((jx_x64_reg)(JX64_REG_AX + mirOp->u.m_RegID));
		} break;
		case JMIR_TYPE_I32: {
			op = jx64_opReg((jx_x64_reg)(JX64_REG_EAX + mirOp->u.m_RegID));
		} break;
		case JMIR_TYPE_I64:
		case JMIR_TYPE_PTR: {
			op = jx64_opReg((jx_x64_reg)(JX64_REG_RAX + mirOp->u.m_RegID));
		} break;
		case JMIR_TYPE_F32: {
			JX_NOT_IMPLEMENTED();
		} break;
		case JMIR_TYPE_F64: {
			JX_NOT_IMPLEMENTED();
		} break;
		default:
			JX_CHECK(false, "Unknown mir type");
			break;
		}
	} break;
	case JMIR_OPERAND_CONST: {
		switch (mirOp->m_Type) {
		case JMIR_TYPE_VOID: {
			JX_CHECK(false, "Unexpected void constant!");
		} break;
		case JMIR_TYPE_I8: {
			op = jx64_opImm8((uint8_t)mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I16: {
			op = jx64_opImm16((uint16_t)mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I32: {
			op = jx64_opImm32((uint32_t)mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_I64:
		case JMIR_TYPE_PTR: {
			op = jx64_opImm64((uint64_t)mirOp->u.m_ConstI64);
		} break;
		case JMIR_TYPE_F32: {
			JX_NOT_IMPLEMENTED();
		} break;
		case JMIR_TYPE_F64: {
			JX_NOT_IMPLEMENTED();
		} break;
		default:
			JX_CHECK(false, "Unknown mir type");
			break;
		}
	} break;
	case JMIR_OPERAND_BASIC_BLOCK: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OPERAND_STACK_OBJECT: {
		jx_x64_size size = jx_x64gen_convertMIRTypeToSize(mirOp->m_Type);
		int32_t disp = mirOp->u.m_StackObj->m_SPOffset;
		op = jx64_opMem(size, JX64_REG_RSP, JX64_REG_NONE, JX64_SCALE_1, disp);
	} break;
	case JMIR_OPERAND_GLOBAL_VARIABLE: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_OPERAND_EXTERNAL_SYMBOL: {
		const char* name = mirOp->u.m_ExternalSymbolName;
		jx_x64_label_t* lbl = jx64_globalVarGetLabelByName(jitCtx, name);
		if (!lbl) {
			lbl = jx64_funcGetLabelByName(jitCtx, name);
		}
		JX_CHECK(lbl, "External symbol not found!");
		op = jx64_opLbl(JX64_SIZE_64, lbl);
	} break;
	case JMIR_OPERAND_MEMORY_REF: {
		jx_x64_size size = jx_x64gen_convertMIRTypeToSize(mirOp->m_Type);
		jx_x64_reg baseReg = jx_x64gen_convertMIRReg(mirOp->u.m_MemRef.m_BaseRegID, JX64_SIZE_64);
		jx_x64_reg indexReg = jx_x64gen_convertMIRReg(mirOp->u.m_MemRef.m_IndexRegID, JX64_SIZE_64);
		jx_x64_scale scale = jx_x64gen_convertMIRScale(mirOp->u.m_MemRef.m_Scale);
		int32_t disp = mirOp->u.m_MemRef.m_Displacement;
		op = jx64_opMem(size, baseReg, indexReg, scale, disp);
	} break;
	default:
		JX_CHECK(false, "Unknown kind of mir operand");
		break;
	}

	return op;
}

static jx_x64_size jx_x64gen_convertMIRTypeToSize(jx_mir_type_kind type)
{
	jx_x64_size sz = JX64_SIZE_8;

	switch (type) {
	case JMIR_TYPE_VOID : {
		JX_CHECK(false, "void does not have a size!");
	} break;
	case JMIR_TYPE_I8: {
		sz = JX64_SIZE_8;
	} break;
	case JMIR_TYPE_I16: {
		sz = JX64_SIZE_16;
	} break;
	case JMIR_TYPE_I32: {
		sz = JX64_SIZE_32;
	} break;
	case JMIR_TYPE_I64:
	case JMIR_TYPE_PTR: {
		sz = JX64_SIZE_64;
	} break;
	case JMIR_TYPE_F32: {
		JX_NOT_IMPLEMENTED();
	} break;
	case JMIR_TYPE_F64: {
		JX_NOT_IMPLEMENTED();
	} break;
	default:
		JX_CHECK(false, "Unknown mir type");
		break;
	}

	return sz;
}

static jx_x64_reg jx_x64gen_convertMIRReg(uint32_t mirRegID, jx_x64_size sz)
{
	if (mirRegID == JMIR_MEMORY_REG_NONE) {
		return JX64_REG_NONE;
	}

	JX_CHECK(mirRegID < JMIR_FIRST_VIRTUAL_REGISTER, "Expected hardware register!");

	jx_x64_reg reg = JX64_REG_NONE;
	switch (sz) {
	case JX64_SIZE_8: {
		reg = (jx_x64_reg)(JX64_REG_AL + mirRegID);
	} break;
	case JX64_SIZE_16: {
		reg = (jx_x64_reg)(JX64_REG_AX + mirRegID);
	} break;
	case JX64_SIZE_32: {
		reg = (jx_x64_reg)(JX64_REG_EAX + mirRegID);
	} break;
	case JX64_SIZE_64: {
		reg = (jx_x64_reg)(JX64_REG_RAX + mirRegID);
	} break;
	default:
		JX_CHECK(false, "Unknown size!");
		break;
	}

	return reg;
}

static jx_x64_scale jx_x64gen_convertMIRScale(uint32_t mirScale)
{
	jx_x64_scale scale = JX64_SCALE_1;

	switch (mirScale) {
	case 1:
		scale = JX64_SCALE_1;
		break;
	case 2:
		scale = JX64_SCALE_2;
		break;
	case 4:
		scale = JX64_SCALE_4;
		break;
	case 8:
		scale = JX64_SCALE_8;
		break;
	default:
		JX_CHECK(false, "Invalid scale value");
		break;
	}

	return scale;
}