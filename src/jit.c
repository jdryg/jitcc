// TODO
// - Bit test instructions
// - Convert 32-bit jumps to 8-bit jumps at function end
#include "jit.h"
#include <jlib/allocator.h>
#include <jlib/array.h>
#include <jlib/dbg.h>
#include <jlib/math.h>
#include <jlib/memory.h>
#include <jlib/string.h>

#define JX64_REX(W, R, X, B)         (0x40 | ((W) << 3) | ((R) << 2) | ((X) << 1) | ((B) << 0))
#define JX64_MODRM(mod, reg, rm)     (((mod) & 0b11) << 6) | (((reg) & 0b111) << 3) | (((rm) & 0b111) << 0)
#define JX64_SIB(scale, index, base) (((scale) & 0b11) << 6) | (((index) & 0b111) << 3) | (((base) & 0b111) << 0)

#define JX64_REG_LO(reg)         ((JX64_REG_GET_ID(reg) & 0b0111) >> 0)
#define JX64_REG_HI(reg)         ((JX64_REG_GET_ID(reg) & 0b1000) >> 3)
#define JX64_REG_IS_RBP_R13(reg) ((JX64_REG_GET_FLAG(reg) == 0) && (JX64_REG_LO(reg) == 5))
#define JX64_REG_IS_RIP(reg)     ((JX64_REG_GET_FLAG(reg) == 1) && (JX64_REG_LO(reg) == 5))
#define JX64_REG_IS_HI(reg)      (JX64_REG_HI(reg) != 0)

#define JX64_DISP_IS_8BIT(disp) ((disp) >= -128 && (disp) < 127)

#define JX64_OPERAND_SIZE_PREFIX 0x66
#define JX64_ADDRESS_SIZE_PREFIX 0x67

#define JX64_LABEL_OFFSET_UNBOUND 0x7FFFFFFFFFFFFFFF

typedef enum jx_x64_segment_prefix
{
	JX64_SEGMENT_NONE  = 0,
	JX64_SEGMENT_STACK = 1, // SS, 0x2E, Pointer to 0x0; unused.
	JX64_SEGMENT_CODE  = 2, // CS, 0x36, Pointer to 0x0; unused.
	JX64_SEGMENT_DATA  = 3, // DS, 0x3E, Pointer to 0x0; unused.
	JX64_SEGMENT_EXTRA = 4, // ES, 0x26, Pointer to 0x0; unused.
	JX64_SEGMENT_F     = 5, // FS, 0x64, Pointer to thread-local process data.
	JX64_SEGMENT_G     = 6, // GS, 0x65, Pointer to thread-local process data.
} jx_x64_segment_prefix;

typedef enum jx_x64_lock_repeat_prefix
{
	JX64_LOCK_REPEAT_NONE  = 0,
	JX64_LOCK_REPEAT_LOCK  = 1, // LOCK, 0xF0, 
	JX64_LOCK_REPEAT_REP   = 2, // REP, 0xF2
	JX64_LOCK_REPEAT_REPNZ = 3, // REPNX, 0xF3
} jx_x64_lock_repeat_prefix;

typedef struct jx_x64_instr_encoding_t
{
	uint8_t m_SegmentOverride     : 3; // jx_x64_segment_prefix
	uint8_t m_LockRepeat          : 2; // jx_x64_lock_repeat_prefix
	uint8_t m_OperandSizeOverride : 1;
	uint8_t m_AddressSizeOverride : 1;
	JX_PAD_BITFIELD(uint8_t, 1);

	uint8_t m_HasREX              : 1;
	uint8_t m_HasModRM            : 1;
	uint8_t m_HasSIB              : 1;
	uint8_t m_HasDisp             : 1;
	uint8_t m_HasImm              : 1;
	uint8_t m_OpcodeSize          : 2; // 0 - 3, valid values are 1, 2, 3
	JX_PAD_BITFIELD(uint8_t, 1);

	uint8_t m_REX_W               : 1;
	uint8_t m_REX_R               : 1;
	uint8_t m_REX_X               : 1;
	uint8_t m_REX_B               : 1;
	uint8_t m_DispSize            : 2; // jx_x64_size
	uint8_t m_ImmSize             : 2; // jx_x64_size

	uint8_t m_ModRM_Mod           : 2;
	uint8_t m_ModRM_Reg           : 3;
	uint8_t m_ModRM_RM            : 3;

	uint8_t m_SIB_Scale           : 2;
	uint8_t m_SIB_Index           : 3;
	uint8_t m_SIB_Base            : 3;

	uint8_t m_Opcode[3];
	
	uint32_t m_Disp;
	JX_PAD(4);

	int64_t m_ImmI64;
} jx_x64_instr_encoding_t;

typedef struct jx_x64_instr_buffer_t
{
	uint8_t m_Buffer[16];
	uint32_t m_Size;
} jx_x64_instr_buffer_t;

typedef struct jx_x64_label_ref_t
{
	uint32_t m_DispOffset;
	uint32_t m_NextInstrOffset;
} jx_x64_label_ref_t;

typedef struct jx_x64_label_t
{
	uint64_t m_Offset;
	jx_x64_label_ref_t* m_RefsArr;
	jx_x64_section_kind m_Section;
	JX_PAD(4);
} jx_x64_label_t;

typedef struct jx_x64_section_t
{
	uint8_t* m_Buffer;
	uint32_t m_Size;
	uint32_t m_Capacity;
	uint32_t m_CodeBufferOffset;
	JX_PAD(4);
} jx_x64_section_t;

typedef struct jx_x64_code_buffer_t
{
	uint8_t* m_Buffer;
	uint32_t m_Size;
	JX_PAD(4);
} jx_x64_code_buffer_t;

typedef struct jx_x64_context_t
{
	jx_allocator_i* m_Allocator;
	jx_x64_symbol_t** m_SymbolArr;
	jx_x64_symbol_t* m_CurFunc;
#if 0
	uint32_t* m_CurFuncJccArr;
#endif
	jx_x64_section_t m_Section[JX64_SECTION_COUNT];
	jx_x64_code_buffer_t m_CodeBuffer;
} jx_x64_context_t;

static jx_x64_symbol_t* jx64_symbolAlloc(jx_x64_context_t* ctx, jx_x64_symbol_kind kind, const char* name);
static void jx64_symbolFree(jx_x64_context_t* ctx, jx_x64_symbol_t* sym);

static bool jx64_stack_op_mem(jx_x64_instr_encoding_t* enc, uint8_t opcode, uint8_t modrm_reg, const jx_x64_mem_t* mem, jx_x64_size sz);
static bool jx64_stack_op_reg(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, jx_x64_reg reg);
static bool jx64_math_unary_op(jx_x64_context_t* ctx, uint8_t baseOpcode, uint8_t modrm_reg, jx_x64_operand_t op);
static bool jx64_math_binary_op(jx_x64_context_t* ctx, uint8_t opcode_imm, uint8_t modrm_reg, uint8_t opcode_rm, jx_x64_operand_t dst, jx_x64_operand_t src);
static bool jx64_jmp_call_op(jx_x64_context_t* ctx, uint8_t opcode_lbl, uint8_t opcode_rm, uint8_t modrm_reg, jx_x64_operand_t op);
static bool jx64_shift_rotate_op(jx_x64_context_t* ctx, uint8_t modrm_reg, jx_x64_operand_t op, jx_x64_operand_t shift);
static bool jx64_math_unary_op_reg(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, jx_x64_reg reg);
static bool jx64_math_unary_op_mem(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, const jx_x64_mem_t* mem, jx_x64_size sz);
static bool jx64_math_binary_op_reg_imm(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, jx_x64_reg reg, int64_t imm, jx_x64_size imm_sz);
static bool jx64_math_binary_op_reg_reg(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, jx_x64_reg dst, jx_x64_reg src);
static bool jx64_math_binary_op_reg_mem(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t isRegDst, jx_x64_reg reg, const jx_x64_mem_t* mem);
static bool jx64_math_binary_op_mem_imm(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, const jx_x64_mem_t* dst_m, jx_x64_size dst_m_sz, int64_t src_imm, jx_x64_size src_imm_sz);
static bool jx64_binary_op_reg_imm(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, uint8_t modrm_reg, jx_x64_reg reg, int64_t imm, jx_x64_size imm_sz);
static bool jx64_binary_op_mem_imm(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, uint8_t modrm_reg, const jx_x64_mem_t* dst_m, jx_x64_size dst_m_sz, int64_t src_imm, jx_x64_size src_imm_sz);
static bool jx64_binary_op_reg_reg(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, jx_x64_reg dst, jx_x64_reg src);
static bool jx64_binary_op_reg_mem(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, jx_x64_reg reg, const jx_x64_mem_t* mem);
static bool jx64_mov_reg_imm(jx_x64_instr_encoding_t* enc, jx_x64_reg dst_r, int64_t src_imm, jx_x64_size src_imm_sz);
static bool jx64_mov_mem_imm(jx_x64_instr_encoding_t* enc, const jx_x64_mem_t* dst_m, int64_t src_imm, jx_x64_size src_imm_sz);
static bool jx64_movsx_reg_reg(jx_x64_instr_encoding_t* enc, jx_x64_reg dst_r, jx_x64_reg src_r);
static bool jx64_movzx_reg_reg(jx_x64_instr_encoding_t* enc, jx_x64_reg dst_r, jx_x64_reg src_r);
static bool jx64_instrBuf_push8(jx_x64_instr_buffer_t* ib, uint8_t b);
static bool jx64_instrBuf_push16(jx_x64_instr_buffer_t* ib, uint16_t w);
static bool jx64_instrBuf_push32(jx_x64_instr_buffer_t* ib, uint32_t dw);
static bool jx64_instrBuf_push64(jx_x64_instr_buffer_t* ib, uint64_t qw);
static bool jx64_instrBuf_push_n(jx_x64_instr_buffer_t* ib, const uint8_t* buffer, uint32_t n);
static void jx64_instrEnc_opcode1(jx_x64_instr_encoding_t* enc, uint8_t opcode);
static void jx64_instrEnc_opcode2(jx_x64_instr_encoding_t* enc, uint8_t opcode0, uint8_t opcode1);
static void jx64_instrEnc_opcode3(jx_x64_instr_encoding_t* enc, uint8_t opcode0, uint8_t opcode1, uint8_t opcode2);
static void jx64_instrEnc_opcoden(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t n);
static void jx64_instrEnc_modrm(jx_x64_instr_encoding_t* enc, uint8_t mod, uint8_t reg, uint8_t rm);
static void jx64_instrEnc_sib(jx_x64_instr_encoding_t* enc, bool hasSIB, uint8_t scale, uint8_t index, uint8_t base);
static void jx64_instrEnc_rex(jx_x64_instr_encoding_t* enc, bool hasREX, uint8_t w, uint8_t r, uint8_t x, uint8_t b);
static void jx64_instrEnc_disp(jx_x64_instr_encoding_t* enc, bool hasDisp, jx_x64_size dispSize, uint32_t disp);
static void jx64_instrEnc_imm(jx_x64_instr_encoding_t* enc, bool hasImm, jx_x64_size immSize, int64_t imm);
static void jx64_instrEnc_segment(jx_x64_instr_encoding_t* enc, jx_x64_segment_prefix seg);
static void jx64_instrEnc_lock_rep(jx_x64_instr_encoding_t* enc, jx_x64_lock_repeat_prefix lockRepeat);
static void jx64_instrEnc_addrSize(jx_x64_instr_encoding_t* enc, bool override);
static void jx64_instrEnc_operandSize(jx_x64_instr_encoding_t* enc, bool override);
static uint32_t jx64_instrEnc_calcInstrSize(const jx_x64_instr_encoding_t* encoding);
static uint32_t jx64_instrEnc_calcDispOffset(const jx_x64_instr_encoding_t* enc);
static bool jx64_encodeInstr(jx_x64_instr_buffer_t* instr, const jx_x64_instr_encoding_t* encoding);

jx_x64_context_t* jx_x64_createContext(jx_allocator_i* allocator)
{
	jx_x64_context_t* ctx = (jx_x64_context_t*)JX_ALLOC(allocator, sizeof(jx_x64_context_t));
	if (!ctx) {
		return NULL;
	}

	jx_memset(ctx, 0, sizeof(jx_x64_context_t));
	ctx->m_Allocator = allocator;

	ctx->m_SymbolArr = (jx_x64_symbol_t**)jx_array_create(allocator);
	if (!ctx->m_SymbolArr) {
		jx_x64_destroyContext(ctx);
		return NULL;
	}

#if 0
	ctx->m_CurFuncJccArr = (uint32_t*)jx_array_create(allocator);
	if (!ctx->m_CurFuncJccArr) {
		jx_x64_destroyContext(ctx);
		return NULL;
	}
#endif

	return ctx;
}

void jx_x64_destroyContext(jx_x64_context_t* ctx)
{
	jx_allocator_i* allocator = ctx->m_Allocator;

	const uint32_t numSymbols = (uint32_t)jx_array_sizeu(ctx->m_SymbolArr);
	for (uint32_t iSym = 0; iSym < numSymbols; ++iSym) {
		jx_x64_symbol_t* sym = ctx->m_SymbolArr[iSym];
		jx64_symbolFree(ctx, sym);
	}
	jx_array_free(ctx->m_SymbolArr);

#if 0
	jx_array_free(ctx->m_CurFuncJccArr);
#endif
#if 0
	JX_FREE(allocator, ctx->m_Buffer);
#endif
	JX_FREE(allocator, ctx);
}

void jx64_resetBuffer(jx_x64_context_t* ctx)
{
#if 0
	ctx->m_Size = 0;
#endif
}

const uint8_t* jx64_getBuffer(jx_x64_context_t* ctx, uint32_t* sz)
{
#if 1
	* sz = ctx->m_CodeBuffer.m_Size;
	return ctx->m_CodeBuffer.m_Buffer;
#else
	*sz = ctx->m_Section[JX64_SECTION_TEXT].m_Size;
	return ctx->m_Section[JX64_SECTION_TEXT].m_Buffer;
#endif
}

// TODO: 
#include <Windows.h>

bool jx64_finalize(jx_x64_context_t* ctx)
{
	// Combine all sections into a continuous buffer
	uint32_t totalSize = 0;
	for (uint32_t iSection = 0; iSection < JX64_SECTION_COUNT; ++iSection) {
		jx_x64_section_t* sec = &ctx->m_Section[iSection];
		sec->m_CodeBufferOffset = totalSize;
		totalSize += ((sec->m_Size + 4095) / 4096) * 4096;
	}

	jx_x64_code_buffer_t* cb = &ctx->m_CodeBuffer;
	cb->m_Buffer = VirtualAlloc(NULL, totalSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!cb->m_Buffer) {
		return false;
	}
	cb->m_Size = totalSize;

	jx_memset(cb->m_Buffer, 0, totalSize);

	for (uint32_t iSection = 0; iSection < JX64_SECTION_COUNT; ++iSection) {
		jx_x64_section_t* sec = &ctx->m_Section[iSection];
		jx_memcpy(&cb->m_Buffer[sec->m_CodeBufferOffset], sec->m_Buffer, sec->m_Size);
	}

	const uint32_t numSymbols = (uint32_t)jx_array_sizeu(ctx->m_SymbolArr);

#if 0
	// Emit stubs for all external functions
	for (uint32_t iSym = 0; iSym < numSymbols; ++iSym) {
		jx_x64_symbol_t* sym = ctx->m_SymbolArr[iSym];
		if (sym->m_Kind == JX64_SYMBOL_FUNCTION && sym->m_Size == 0) {
			jx_x64_label_t* iatEntry = jx64_labelAlloc(ctx);
			jx64_labelBind(ctx, iatEntry, JX64_SECTION_DATA);
			jx64_emitBytes(ctx, (uint8_t*)&sym->m_Label->m_Offset, sizeof(uint64_t));
			
			sym->m_Label->m_Offset = JX64_LABEL_OFFSET_UNBOUND;

			jx64_funcBegin(ctx, sym);
			const int32_t diff = (int32_t)iatEntry->m_Offset - (int32_t)ctx->m_Size;
			jx64_jmp(ctx, jx64_opMem(JX64_SIZE_64, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff));
			jx64_funcEnd(ctx);
		}
	}
#endif

	for (uint32_t iSym = 0; iSym < numSymbols; ++iSym) {
		jx_x64_symbol_t* sym = ctx->m_SymbolArr[iSym];
		const uint32_t numRelocs = (uint32_t)jx_array_sizeu(sym->m_RelocArr);
		if (sym->m_Kind == JX64_SYMBOL_GLOBAL_VARIABLE) {

		} else if (sym->m_Kind == JX64_SYMBOL_FUNCTION) {

		} else {
			JX_CHECK(false, "Unknown symbol kind.");
		}

		for (uint32_t iReloc = 0; iReloc < numRelocs; ++iReloc) {
			jx_x64_relocation_t* reloc = &sym->m_RelocArr[iReloc];
			jx_x64_symbol_t* refSym = jx64_symbolGetByName(ctx, reloc->m_SymbolName);
			if (!refSym || refSym->m_Label->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
				// Unresolved external symbol
				return false;
			}

			jx_x64_section_t* refSymSection = &ctx->m_Section[refSym->m_Label->m_Section];
			const uint32_t refSymOffset = refSymSection->m_CodeBufferOffset + (uint32_t)refSym->m_Label->m_Offset;

			// TODO: Once I split code and data into separate buffers, get the correct buffer using the
			// symbol kind.
			jx_x64_section_t* symSection = &ctx->m_Section[sym->m_Label->m_Section];
			const uint32_t patchOffset = symSection->m_CodeBufferOffset + (uint32_t)sym->m_Label->m_Offset + reloc->m_Offset;
			uint8_t* patchAddr = &cb->m_Buffer[patchOffset];

			switch (reloc->m_Kind) {
			case JX64_RELOC_ABSOLUTE: {
				JX_NOT_IMPLEMENTED();
			} break;
			case JX64_RELOC_ADDR64: {
				*(uintptr_t*)patchAddr = (uintptr_t)&cb->m_Buffer[refSymOffset];
			} break;
			case JX64_RELOC_REL32: {
				*(int32_t*)patchAddr = (int32_t)refSymOffset - (int32_t)(patchOffset + 4);
			} break;
			case JX64_RELOC_REL32_1: {
				*(int32_t*)patchAddr = (int32_t)refSymOffset - (int32_t)(patchOffset + 5);
			} break;
			case JX64_RELOC_REL32_2: {
				*(int32_t*)patchAddr = (int32_t)refSymOffset - (int32_t)(patchOffset + 6);
			} break;
			case JX64_RELOC_REL32_3: {
				*(int32_t*)patchAddr = (int32_t)refSymOffset - (int32_t)(patchOffset + 7);
			} break;
			case JX64_RELOC_REL32_4: {
				*(int32_t*)patchAddr = (int32_t)refSymOffset - (int32_t)(patchOffset + 8);
			} break;
			case JX64_RELOC_REL32_5: {
				*(int32_t*)patchAddr = (int32_t)refSymOffset - (int32_t)(patchOffset + 9);
			} break;
			default:
				JX_CHECK(false, "Unknown relocation kind.");
				break;
			}
		}
	}

	DWORD oldProtect = 0;
	VirtualProtect(cb->m_Buffer, cb->m_Size, PAGE_EXECUTE_READWRITE, &oldProtect);

	return true;
}

jx_x64_label_t* jx64_labelAlloc(jx_x64_context_t* ctx, jx_x64_section_kind section)
{
	jx_x64_label_t* lbl = (jx_x64_label_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_x64_label_t));
	if (!lbl) {
		return NULL;
	}

	jx_memset(lbl, 0, sizeof(jx_x64_label_t));
	lbl->m_Section = section;
	lbl->m_Offset = JX64_LABEL_OFFSET_UNBOUND;
	lbl->m_RefsArr = (jx_x64_label_ref_t*)jx_array_create(ctx->m_Allocator);

	return lbl;
}

void jx64_labelFree(jx_x64_context_t* ctx, jx_x64_label_t* lbl)
{
	jx_array_free(lbl->m_RefsArr);
	JX_FREE(ctx->m_Allocator, lbl);
}

void jx64_labelBind(jx_x64_context_t* ctx, jx_x64_label_t* lbl)
{
	JX_CHECK(lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND, "Label already bound!");
	lbl->m_Offset = ctx->m_Section[lbl->m_Section].m_Size;

	const uint32_t numRefs = (uint32_t)jx_array_sizeu(lbl->m_RefsArr);
	for (uint32_t i = 0; i < numRefs; ++i) {
		const jx_x64_label_ref_t* ref = &lbl->m_RefsArr[i];

		uint8_t* instrPtr = &ctx->m_Section[lbl->m_Section].m_Buffer[ref->m_DispOffset];

		const int32_t diff = (int32_t)lbl->m_Offset - (int32_t)ref->m_NextInstrOffset;
		*(int32_t*)instrPtr = diff;
	}
}

uint32_t jx64_labelGetOffset(jx_x64_context_t* ctx, jx_x64_label_t* lbl)
{
	return (uint32_t)lbl->m_Offset;
}

jx_x64_symbol_t* jx64_globalVarDeclare(jx_x64_context_t* ctx, const char* name)
{
	jx_x64_symbol_t* gv = jx64_symbolAlloc(ctx, JX64_SYMBOL_GLOBAL_VARIABLE, name);
	if (!gv) {
		return NULL;
	}

	jx_array_push_back(ctx->m_SymbolArr, gv);

	return gv;
}

bool jx64_globalVarDefine(jx_x64_context_t* ctx, jx_x64_symbol_t* gv, const uint8_t* data, uint32_t sz, uint32_t alignment)
{
	if (gv->m_Kind != JX64_SYMBOL_GLOBAL_VARIABLE) {
		JX_CHECK(false, "Expected global variable symbol.");
		return false;
	}

	JX_CHECK(jx_isPow2_u32(alignment), "Alignment expected to be a power of 2.");

	const uint32_t curPos = ctx->m_Section[JX64_SECTION_DATA].m_Size;
	const uint32_t alignedPos = ((curPos + (alignment - 1)) / alignment) * alignment;
	const uint32_t alignmentSize = alignedPos - curPos;

	if (alignmentSize && !jx64_nop(ctx, alignmentSize)) {
		return false;
	}

	jx64_labelBind(ctx, gv->m_Label);

	return jx64_emitBytes(ctx, JX64_SECTION_DATA, data, sz);
}

jx_x64_symbol_t* jx64_funcDeclare(jx_x64_context_t* ctx, const char* name)
{
	jx_x64_symbol_t* func = jx64_symbolAlloc(ctx, JX64_SYMBOL_FUNCTION, name);
	if (!func) {
		return NULL;
	}

	jx_array_push_back(ctx->m_SymbolArr, func);

	return func;
}

bool jx64_funcBegin(jx_x64_context_t* ctx, jx_x64_symbol_t* func)
{
	if (func->m_Kind != JX64_SYMBOL_FUNCTION) {
		JX_CHECK(false, "Expected function symbol");
		return false;
	}

	if (ctx->m_CurFunc) {
		return false;
	}

	jx64_labelBind(ctx, func->m_Label);
#if 0
	jx_array_resize(ctx->m_CurFuncJccArr, 0);
#endif

	ctx->m_CurFunc = func;

	return true;
}

void jx64_funcEnd(jx_x64_context_t* ctx)
{
	if (!ctx->m_CurFunc) {
		return;
	}

	jx_x64_symbol_t* func = ctx->m_CurFunc;
	func->m_Size = ctx->m_Section[JX64_SECTION_TEXT].m_Size - (uint32_t)func->m_Label->m_Offset;

	ctx->m_CurFunc = NULL;

	// TODO: Patch all jumps inside the function body in order to turn them into 1-byte rel jumps
	// and pad the rest of the function with nops in order to keep the other function addresses 
	// intact.
	// NOTE: I have to keep all jumps and not only conditional jumps in order to replace 
	// their displacements if at least 1 of them changes
#if 0
	const uint32_t numJumps = (uint32_t)jx_array_sizeu(ctx->m_CurFuncJccArr);
	for (uint32_t iJmp = numJumps; iJmp > 0; --iJmp) {
		const uint32_t codeOffset = ctx->m_CurFuncJccArr[iJmp - 1];

		uint8_t* code = &ctx->m_Buffer[codeOffset];
		if (code[0] == 0x0F && (code[1] & 0xF0) == 0x80) {
			// Conditional jump with 32-bit displacement
			// Check if displacement can fit into an 8-bit signed integer
			// and convert instruction.
			int32_t disp32 = *(int32_t*)&code[2];
			if (JX64_DISP_IS_8BIT(disp32)) {
				const uint8_t cc = (code[1] & 0x0F);
				code[0] = 0x90; // nop
				code[1] = 0x90;
				code[2] = 0x90;
				code[3] = 0x90;
				code[4] = 0x70 | cc;
				code[5] = (uint8_t)disp32;
			}
		}
	}
#endif
}

jx_x64_symbol_t* jx64_symbolGetByName(jx_x64_context_t* ctx, const char* name)
{
	const uint32_t numSymbols = (uint32_t)jx_array_sizeu(ctx->m_SymbolArr);
	for (uint32_t iSym = 0; iSym < numSymbols; ++iSym) {
		jx_x64_symbol_t* sym = ctx->m_SymbolArr[iSym];
		if (!jx_strcmp(sym->m_Name, name)) {
			return sym;
		}
	}

	return NULL;
}

void jx64_symbolAddRelocation(jx_x64_context_t* ctx, jx_x64_symbol_t* sym, jx_x64_relocation_kind kind, uint32_t offset, const char* symbolName)
{
	jx_array_push_back(sym->m_RelocArr, (jx_x64_relocation_t){
		.m_Kind = kind,
		.m_Offset = offset,
		.m_SymbolName = jx_strdup(symbolName, ctx->m_Allocator)
	});
}

bool jx64_symbolSetExternalAddress(jx_x64_context_t* ctx, jx_x64_symbol_t* sym, void* ptr)
{
	sym->m_Label->m_Offset = (uint64_t)ptr;
	return true;
}

bool jx64_emitBytes(jx_x64_context_t* ctx, jx_x64_section_kind section, const uint8_t* bytes, uint32_t n)
{
	jx_x64_section_t* sec = &ctx->m_Section[section];
	if (sec->m_Size + n > sec->m_Capacity) {
		const uint32_t oldCapacity = sec->m_Capacity;
		const uint32_t newCapacity = oldCapacity + 4096;

		uint8_t* newBuffer = (uint8_t*)JX_ALLOC(ctx->m_Allocator, newCapacity);
		if (!newBuffer) {
			return false;
		}

		jx_memcpy(newBuffer, sec->m_Buffer, oldCapacity);
		jx_memset(&newBuffer[oldCapacity], 0, newCapacity - oldCapacity);

		JX_FREE(ctx->m_Allocator, sec->m_Buffer);
		sec->m_Buffer = newBuffer;
		sec->m_Capacity = newCapacity;
	}

	jx_memcpy(&sec->m_Buffer[sec->m_Size], bytes, n);
	sec->m_Size += n;

	return true;
}

bool jx64_retn(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0xC3 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, &instr[0], JX_COUNTOF(instr));
}

bool jx64_mov(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	if (dst.m_Type == JX64_OPERAND_SYM && src.m_Type == JX64_OPERAND_SYM) {
		JX_CHECK(false, "Invalid operands.");
		return false;
	}

	jx_x64_symbol_t* sym = NULL;
	if (src.m_Type == JX64_OPERAND_SYM) {
		sym = src.u.m_Sym;
		src = jx64_opMem(src.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, 0);
	} else if (dst.m_Type == JX64_OPERAND_SYM) {
		sym = dst.u.m_Sym;
		dst = jx64_opMem(dst.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, 0);
	}

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };
	if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_IMM) {
		if (!jx64_mov_reg_imm(enc, dst.u.m_Reg, src.u.m_ImmI64, src.m_Size)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_math_binary_op_reg_reg(enc, 0x88, dst.u.m_Reg, src.u.m_Reg)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_MEM) {
		if (!jx64_math_binary_op_reg_mem(enc, 0x88, 1, dst.u.m_Reg, &src.u.m_Mem)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_MEM && src.m_Type == JX64_OPERAND_IMM) {
		if (!jx64_mov_mem_imm(enc, &dst.u.m_Mem, src.u.m_ImmI64, src.m_Size)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_MEM && src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_math_binary_op_reg_mem(enc, 0x88, 0, src.u.m_Reg, &dst.u.m_Mem)) {
			return false;
		}
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

	if (sym) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);

		const uint32_t relocDelta = (instrSize - dispOffset) - sizeof(int32_t);
		JX_CHECK(relocDelta <= 5, "Invalid relocation type");
		const uint32_t curFuncOffset = (uint32_t)ctx->m_CurFunc->m_Label->m_Offset;
		const uint32_t relocOffset = ctx->m_Section[JX64_SECTION_TEXT].m_Size - curFuncOffset + dispOffset;
		jx64_symbolAddRelocation(ctx, ctx->m_CurFunc, JX64_RELOC_REL32 + relocDelta, relocOffset, sym->m_Name);
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_movsx(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	JX_CHECK(dst.m_Type != JX64_OPERAND_SYM && src.m_Type != JX64_OPERAND_SYM, "TODO");
	const bool invalidOperands = false
		|| dst.m_Type != JX64_OPERAND_REG // Destination operand should always be a register
		|| dst.m_Size <= src.m_Size
		;
	if (invalidOperands) {
		return false;
	}

#if 0
	// RIP-relative addressing. Calculate offset to the specified label 
	// and turn operand into mem operand. 
	// NOTE: Since, at this point, we don't know how long the instruction
	// will end up being we cannot calculate the correct offset. We calculate
	// the offset to the start of this instruction and jx64_binary_op_reg_mem() should
	// take care of fixing it.
	jx_x64_label_t* lbl = NULL;
	if (src.m_Type == JX64_OPERAND_LBL) {
		lbl = src.u.m_Lbl;
		const int32_t diff = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)ctx->m_Size
			;
		src = jx64_opMem(src.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff);
	}
#endif

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	if (src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_movsx_reg_reg(enc, dst.u.m_Reg, src.u.m_Reg)) {
			return false;
		}
	} else if (src.m_Type == JX64_OPERAND_MEM) {
		JX_CHECK(false, "Untested code below! If this assert hits check if it's correct. Otherwise implemement jx64_movsx_reg_mem.");
		if (src.m_Size == JX64_SIZE_8) {
			const uint8_t opcode[] = { 0x0F, 0xBE };
			if (!jx64_binary_op_reg_mem(enc, opcode, JX_COUNTOF(opcode), dst.u.m_Reg, &src.u.m_Mem)) {
				return false;
			}
		} else if (src.m_Size == JX64_SIZE_16) {
			const uint8_t opcode[] = { 0x0F, 0xBF };
			if (!jx64_binary_op_reg_mem(enc, opcode, JX_COUNTOF(opcode), dst.u.m_Reg, &src.u.m_Mem)) {
				return false;
			}
		} else if (src.m_Size == JX64_SIZE_32) {
			if (!jx64_math_binary_op_reg_mem(enc, 0x60, 1, dst.u.m_Reg, &src.u.m_Mem)) {
				return false;
			}
		}
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

#if 0
	if (lbl && lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = ctx->m_Size + dispOffset, .m_NextInstrOffset = ctx->m_Size + instrSize });
	}
#endif

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_movzx(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	JX_CHECK(dst.m_Type != JX64_OPERAND_SYM && src.m_Type != JX64_OPERAND_SYM, "TODO");
	const bool invalidOperands = false
		|| dst.m_Type != JX64_OPERAND_REG // Destination operand should always be a register
		|| dst.m_Size < src.m_Size
		;
	if (invalidOperands) {
		return false;
	}

	JX_CHECK(!(dst.m_Size == JX64_SIZE_64 && src.m_Size == JX64_SIZE_32), "Use mov!");

#if 0
	// RIP-relative addressing. Calculate offset to the specified label 
	// and turn operand into mem operand. 
	// NOTE: Since, at this point, we don't know how long the instruction
	// will end up being we cannot calculate the correct offset. We calculate
	// the offset to the start of this instruction and jx64_binary_op_reg_mem() should
	// take care of fixing it.
	jx_x64_label_t* lbl = NULL;
	if (src.m_Type == JX64_OPERAND_LBL) {
		lbl = src.u.m_Lbl;
		const int32_t diff = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)ctx->m_Size
			;
		src = jx64_opMem(src.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff);
	}
#endif

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	if (src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_movzx_reg_reg(enc, dst.u.m_Reg, src.u.m_Reg)) {
			return false;
		}
	} else if (src.m_Type == JX64_OPERAND_MEM) {
		JX_CHECK(false, "Untested code below! If this assert hits check if it's correct. Otherwise implemement jx64_movzx_reg_mem.");
		if (src.m_Size == JX64_SIZE_8) {
			const uint8_t opcode[] = { 0x0F, 0xB6 };
			if (!jx64_binary_op_reg_mem(enc, opcode, JX_COUNTOF(opcode), dst.u.m_Reg, &src.u.m_Mem)) {
				return false;
			}
		} else if (src.m_Size == JX64_SIZE_16) {
			const uint8_t opcode[] = { 0x0F, 0xB7 };
			if (!jx64_binary_op_reg_mem(enc, opcode, JX_COUNTOF(opcode), dst.u.m_Reg, &src.u.m_Mem)) {
				return false;
			}
		} else if (src.m_Size == JX64_SIZE_32) {
			JX_NOT_IMPLEMENTED();
		}
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

#if 0
	if (lbl && lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = ctx->m_Size + dispOffset, .m_NextInstrOffset = ctx->m_Size + instrSize });
	}
#endif

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_nop(jx_x64_context_t* ctx, uint32_t n)
{
	if (n == 0) {
		return false;
	}

	static const uint8_t kNops[10][9] = {
		{ 0 },                                                    // Invalid
		{ 0x90 },                                                 // NOP
		{ 0x66, 0x90 },                                           // 0x66 NOP
		{ 0x0F, 0x1F, 0x00 },                                     // NOP dword ptr [EAX]
		{ 0x0F, 0x1F, 0x40, 0x00 },                               // NOP dword ptr [EAX + 0x00]
		{ 0x0F, 0x1F, 0x44, 0x00, 0x00 },                         // NOP dword ptr [EAX + EAX * 1 + 0x00]
		{ 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 },                   // 66 NOP dword ptr [EAX + EAX * 1 + 0x00]
		{ 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 },             // NOP dword ptr [EAX + 0x00000000]
		{ 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 },       // NOP dword ptr [EAX + EAX * 1 + 0x00000000]
		{ 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 66 NOP dword ptr [EAX + EAX * 1 + 0x00000000]
	};

	while (n > 9) {
		jx64_emitBytes(ctx, JX64_SECTION_TEXT, &kNops[9][0], 9);
		n -= 9;
	}

	jx64_emitBytes(ctx, JX64_SECTION_TEXT, &kNops[n][0], n);

	return true;
}

bool jx64_push(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	JX_CHECK(op.m_Type != JX64_OPERAND_SYM, "TODO");
	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t){ 0 };
	
#if 0
	jx_x64_label_t* lbl = NULL;
	if (op.m_Type == JX64_OPERAND_LBL) {
		lbl = op.u.m_Lbl;
		const int32_t diff = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)ctx->m_Size
			;
		op = jx64_opMem(op.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff);
	}
#endif

	if (op.m_Type == JX64_OPERAND_IMM) {
		const bool invalidOperand = false
			|| op.m_Size == JX64_SIZE_64 // Cannot push 64-bit immediate
			;
		if (invalidOperand) {
			return false;
		}

		jx64_instrEnc_opcode1(enc, 0x68 | (op.m_Size == JX64_SIZE_8 ? 0b10 : 0));
		jx64_instrEnc_imm(enc, true, (op.m_Size == JX64_SIZE_8) ? JX64_SIZE_8 : JX64_SIZE_32, op.u.m_ImmI64);
	} else if (op.m_Type == JX64_OPERAND_REG) {
		// 0x50+ r
		if (!jx64_stack_op_reg(enc, 0x50, op.u.m_Reg)) {
			return false;
		}
	} else if (op.m_Type == JX64_OPERAND_MEM) {
		// FF /6
		if (!jx64_stack_op_mem(enc, 0xFF, 0b110, &op.u.m_Mem, op.m_Size)) {
			return false;
		}
	}

#if 0
	if (lbl && lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = ctx->m_Size + dispOffset, .m_NextInstrOffset = ctx->m_Size + instrSize });
	}
#endif

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t){ 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_pop(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	JX_CHECK(op.m_Type != JX64_OPERAND_SYM, "TODO");
	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t){ 0 };

#if 0
	jx_x64_label_t* lbl = NULL;
	if (op.m_Type == JX64_OPERAND_LBL) {
		lbl = op.u.m_Lbl;
		const int32_t diff = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)ctx->m_Size
			;
		op = jx64_opMem(op.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff);
	}
#endif

	if (op.m_Type == JX64_OPERAND_REG) {
		// 0x58+ r
		if (!jx64_stack_op_reg(enc, 0x58, op.u.m_Reg)) {
			return false;
		}
	} else if (op.m_Type == JX64_OPERAND_MEM) {
		// 0x8F /0
		if (!jx64_stack_op_mem(enc, 0x8F, 0b000, &op.u.m_Mem, op.m_Size)) {
			return false;
		}
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

#if 0
	if (lbl && lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = ctx->m_Size + dispOffset, .m_NextInstrOffset = ctx->m_Size + instrSize });
	}
#endif

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t){ 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_add(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b000, 0x00, dst, src);
}

bool jx64_or(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b001, 0x08, dst, src);
}

bool jx64_adc(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b010, 0x10, dst, src);
}

bool jx64_sbb(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b011, 0x18, dst, src);
}

bool jx64_and(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b100, 0x20, dst, src);
}

bool jx64_sub(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b101, 0x28, dst, src);
}

bool jx64_xor(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b110, 0x30, dst, src);
}

bool jx64_cmp(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return jx64_math_binary_op(ctx, 0x80, 0b111, 0x38, dst, src);
}

bool jx64_not(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_math_unary_op(ctx, 0xF6, 0b010, op);
}

bool jx64_neg(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_math_unary_op(ctx, 0xF6, 0b011, op);
}

bool jx64_mul(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_math_unary_op(ctx, 0xF6, 0b100, op);
}

bool jx64_div(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_math_unary_op(ctx, 0xF6, 0b110, op);
}

bool jx64_idiv(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_math_unary_op(ctx, 0xF6, 0b111, op);
}

bool jx64_inc(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_math_unary_op(ctx, 0xFE, 0b000, op);
}

bool jx64_dec(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_math_unary_op(ctx, 0xFE, 0b001, op);
}

bool jx64_imul(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	JX_CHECK(dst.m_Type != JX64_OPERAND_SYM && src.m_Type != JX64_OPERAND_SYM, "TODO");
	const bool invalidOperands = false
		|| dst.m_Type != JX64_OPERAND_REG // Destination operand should always be a register
		;
	if (invalidOperands) {
		return false;
	}

#if 0
	if (JX64_REG_GET_ID(dst.u.m_Reg) == JX64_REG_ID_RAX) {
		return jx64_math_unary_op(ctx, 0xF6, 0b101, src);
	}
#endif

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	if (src.m_Type == JX64_OPERAND_MEM) {
		const uint8_t opcode[] = { 0x0F, 0xAF };
		if (!jx64_binary_op_reg_mem(enc, opcode, JX_COUNTOF(opcode), dst.u.m_Reg, &src.u.m_Mem)) {
			return false;
		}
	} else if (src.m_Type == JX64_OPERAND_REG) {
		// NOTE: Reverse the order of operands because jx64_binary_op_reg_reg assumes the instruction
		// is in the form "op r/m, r", but in this case it's actually "op r, r/m"
		const uint8_t opcode[] = { 0x0F, 0xAF };
		if (!jx64_binary_op_reg_reg(enc, opcode, JX_COUNTOF(opcode), src.u.m_Reg, dst.u.m_Reg)) {
			return false;
		}
	} else {
		return false;
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_lea(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	const bool invalidOperands = false
		|| dst.m_Type != JX64_OPERAND_REG
		|| (src.m_Type != JX64_OPERAND_MEM && src.m_Type != JX64_OPERAND_LBL && src.m_Type != JX64_OPERAND_SYM)
		;
	if (invalidOperands) {
		return false;
	}

	jx_x64_symbol_t* sym = NULL;
	if (src.m_Type == JX64_OPERAND_SYM) {
		sym = src.u.m_Sym;
		src = jx64_opMem(JX64_SIZE_64, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, 0);
	}

	const uint8_t opcode = 0x8D;
	if (!jx64_binary_op_reg_mem(enc, &opcode, 1, dst.u.m_Reg, &src.u.m_Mem)) {
		return false;
	}

	if (sym) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);

		const uint32_t relocDelta = (instrSize - dispOffset) - sizeof(int32_t);
		JX_CHECK(relocDelta <= 5, "Invalid relocation type");
		const uint32_t curFuncOffset = (uint32_t)ctx->m_CurFunc->m_Label->m_Offset;
		const uint32_t relocOffset = ctx->m_Section[JX64_SECTION_TEXT].m_Size - curFuncOffset + dispOffset;
		jx64_symbolAddRelocation(ctx, ctx->m_CurFunc, JX64_RELOC_REL32 + relocDelta, relocOffset, sym->m_Name);
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_test(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	JX_CHECK(dst.m_Type != JX64_OPERAND_SYM && src.m_Type != JX64_OPERAND_SYM, "TODO");
#if 0
	// RIP relative addressing
	jx_x64_label_t* lbl = NULL;
	if (dst.m_Type == JX64_OPERAND_LBL) {
		lbl = dst.u.m_Lbl;
		const int32_t diff = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)ctx->m_Size
			;
		dst = jx64_opMem(src.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff);
	}
#endif

	const bool invalidOperands = false
		|| !(dst.m_Size == src.m_Size || (dst.m_Size == JX64_SIZE_64 && src.m_Size == JX64_SIZE_32)) // only allow reg size == imm size or 64-bit with 32-bit imm
		;
	if (invalidOperands) {
		return false;
	}

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	const uint8_t opcode = (src.m_Type == JX64_OPERAND_IMM ? 0xF6 : 0x84)
		| ((dst.m_Size != JX64_SIZE_8) ? 1 : 0) // w bit
		;

	if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_IMM) {
		if (!jx64_binary_op_reg_imm(enc, &opcode, 1, 0b000, dst.u.m_Reg, src.u.m_ImmI64, src.m_Size)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_binary_op_reg_reg(enc, &opcode, 1, dst.u.m_Reg, src.u.m_Reg)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_MEM && src.m_Type == JX64_OPERAND_IMM) {
		if (!jx64_binary_op_mem_imm(enc, &opcode, 1, 0b000, &dst.u.m_Mem, dst.m_Size, src.u.m_ImmI64, src.m_Size)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_MEM && src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_binary_op_reg_mem(enc, &opcode, 1, src.u.m_Reg, &dst.u.m_Mem)) {
			return false;
		}
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

#if 0
	if (lbl && lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = ctx->m_Size + dispOffset, .m_NextInstrOffset = ctx->m_Size + instrSize });
	}
#endif

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_std(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0xFD };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, &instr[0], JX_COUNTOF(instr));
}

bool jx64_cld(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0xFC };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, &instr[0], JX_COUNTOF(instr));
}

bool jx64_stc(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0xF9 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, &instr[0], JX_COUNTOF(instr));
}

bool jx64_cmc(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0xF5 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, &instr[0], JX_COUNTOF(instr));
}

bool jx64_clc(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0xF8 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, &instr[0], JX_COUNTOF(instr));
}

bool jx64_setcc(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_operand_t dst)
{
	JX_CHECK(dst.m_Type != JX64_OPERAND_SYM, "TODO");
#if 0
	jx_x64_label_t* lbl = NULL;
	if (dst.m_Type == JX64_OPERAND_LBL) {
		// RIP-relative addressing. Calculate offset to the specified label 
		// and turn src operand into mem operand. 
		// NOTE: Since, at this point, we don't know how long the instruction
		// will end up being we cannot calculate the correct offset. We calculate
		// the offset to the start of this instruction and jx64_binary_op_reg_mem() should
		// take care of fixing it.
		lbl = dst.u.m_Lbl;
		const int32_t diff = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)ctx->m_Size
			;
		dst = jx64_opMem(dst.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff);
	}
#endif

	const bool invalidOperands = false
		|| (dst.m_Type != JX64_OPERAND_REG && dst.m_Type != JX64_OPERAND_MEM)
		|| dst.m_Size != JX64_SIZE_8
		;
	if (invalidOperands) {
		return false;
	}

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	if (dst.m_Type == JX64_OPERAND_REG) {
		const uint32_t reg = dst.u.m_Reg;
		const bool needsREX = false
			|| (JX64_REG_GET_ID(reg) >= JX64_REG_ID_RSP)
			|| JX64_REG_IS_HI(reg)
			;

		jx64_instrEnc_rex(enc, needsREX, 0, 0, 0, JX64_REG_HI(reg));
		jx64_instrEnc_opcode2(enc, 0x0F, 0x90 | cc);
		jx64_instrEnc_modrm(enc, 0b11, 0b000, JX64_REG_LO(reg));
	} else if (dst.m_Type == JX64_OPERAND_MEM) {
		JX_NOT_IMPLEMENTED();
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

#if 0
	if (lbl && lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = ctx->m_Size + dispOffset, .m_NextInstrOffset = ctx->m_Size + instrSize });
	}
#endif

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);

	return false;
}

bool jx64_sar(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b111, op, shift);
}

bool jx64_sal(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b100, op, shift);
}

bool jx64_shr(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b101, op, shift);
}

bool jx64_shl(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b100, op, shift);
}

bool jx64_rcr(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b011, op, shift);
}

bool jx64_rcl(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b010, op, shift);
}

bool jx64_ror(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b001, op, shift);
}

bool jx64_rol(jx_x64_context_t* ctx, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	return jx64_shift_rotate_op(ctx, 0b000, op, shift);
}

bool jx64_cmovcc(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return false;
}

bool jx64_bt(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return false;
}

bool jx64_btr(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return false;
}

bool jx64_bts(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return false;
}

bool jx64_btc(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return false;
}

bool jx64_bsr(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return false;
}

bool jx64_bsf(jx_x64_context_t* ctx, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	return false;
}

bool jx64_jcc(jx_x64_context_t* ctx, jx_x64_condition_code cc, jx_x64_label_t* lbl)
{
	if (lbl->m_Section != JX64_SECTION_TEXT) {
		JX_CHECK(false, "Can only jump to text section labels");
		return false;
	}

	jx_x64_section_t* sec = &ctx->m_Section[lbl->m_Section];

	const int32_t displacement = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
		? 0
		: (int32_t)lbl->m_Offset - (int32_t)sec->m_Size
		;

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t){ 0 };
	jx64_instrEnc_opcode2(enc, 0x0F, 0x80 | cc);
	jx64_instrEnc_disp(enc, 1, JX64_SIZE_32, (uint32_t)displacement);

	// Fix displacement
	enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	if (lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t){ .m_DispOffset = sec->m_Size + dispOffset, .m_NextInstrOffset = sec->m_Size + instrSize });
	}

#if 0
	jx_array_push_back(ctx->m_CurFuncJccArr, sec->m_Size);
#endif

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

bool jx64_jmp(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_jmp_call_op(ctx, 0xE9, 0xFF, 0b100, op);
}

bool jx64_call(jx_x64_context_t* ctx, jx_x64_operand_t op)
{
	return jx64_jmp_call_op(ctx, 0xE8, 0xFF, 0b010, op);
}

bool jx64_cwd(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { JX64_OPERAND_SIZE_PREFIX, 0x99 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr, JX_COUNTOF(instr));
}

bool jx64_cdq(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0x99 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr, JX_COUNTOF(instr));
}

bool jx64_cqo(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { JX64_REX(1, 0, 0, 0), 0x99};
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr, JX_COUNTOF(instr));
}

bool jx64_cbw(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { JX64_OPERAND_SIZE_PREFIX, 0x98 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr, JX_COUNTOF(instr));
}

bool jx64_cwde(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { 0x98 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr, JX_COUNTOF(instr));
}

bool jx64_cdqe(jx_x64_context_t* ctx)
{
	const uint8_t instr[] = { JX64_REX(1, 0, 0, 0), 0x98 };
	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr, JX_COUNTOF(instr));
}

static jx_x64_symbol_t* jx64_symbolAlloc(jx_x64_context_t* ctx, jx_x64_symbol_kind kind, const char* name)
{
	jx_x64_symbol_t* sym = (jx_x64_symbol_t*)JX_ALLOC(ctx->m_Allocator, sizeof(jx_x64_symbol_t));
	if (!sym) {
		return NULL;
	}

	jx_x64_section_kind section = kind == JX64_SYMBOL_FUNCTION
		? JX64_SECTION_TEXT
		: JX64_SECTION_DATA
		;

	jx_memset(sym, 0, sizeof(jx_x64_symbol_t));
	sym->m_Kind = kind;
	sym->m_Label = jx64_labelAlloc(ctx, section);
	sym->m_Name = jx_strdup(name, ctx->m_Allocator);
	sym->m_RelocArr = (jx_x64_relocation_t*)jx_array_create(ctx->m_Allocator);
	if (!sym->m_Label || !sym->m_Name || !sym->m_RelocArr) {
		jx64_symbolFree(ctx, sym);
		return NULL;
	}

	return sym;
}

static void jx64_symbolFree(jx_x64_context_t* ctx, jx_x64_symbol_t* sym)
{
	jx_array_free(sym->m_RelocArr);
	JX_FREE(ctx->m_Allocator, sym->m_Name);
	if (sym->m_Label) {
		jx64_labelFree(ctx, sym->m_Label);
		sym->m_Label = NULL;
	}
	JX_FREE(ctx->m_Allocator, sym);
}

// push, pop
static bool jx64_stack_op_mem(jx_x64_instr_encoding_t* enc, uint8_t opcode, uint8_t modrm_reg, const jx_x64_mem_t* mem, jx_x64_size sz)
{
	if (sz == JX64_SIZE_8 || sz == JX64_SIZE_32) {
		return false;
	}

	const jx_x64_reg base_r = mem->m_Base;
	const jx_x64_reg index_r = mem->m_Index;

	if (base_r != JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op (q)word ptr [base + index * scale + (disp)]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32                // Cannot use 16-bit or 8-bit base reg
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32               // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_SIZE(base_r) != JX64_REG_GET_SIZE(index_r) // Base and index registers must be the same size
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP             // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(base_r)                                 // Cannot use RIP as base register
			|| JX64_REG_IS_RIP(index_r)                                // Cannot use RIP as index register
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| JX64_REG_IS_HI(base_r)
			|| JX64_REG_IS_HI(index_r)
			;
		const bool needsDisplacement = false
			|| mem->m_Displacement != 0
			|| JX64_REG_IS_RBP_R13(base_r)
			;

		const bool forceMod01 = JX64_REG_IS_RBP_R13(base_r); // rBP/r13 requires SIB + disp8 with 0 disp
		const uint8_t mod = mem->m_Displacement != 0
			? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
			: (forceMod01 ? 0b01 : 0b00)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, 0, 0, JX64_REG_HI(index_r), JX64_REG_HI(base_r));
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, mod, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), JX64_REG_LO(base_r));
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);
	} else if (base_r == JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op (q)word ptr [index * scale + (disp)]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32    // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP  // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(index_r)                     // Cannot use RIP as index register
			;
		if (invalidOperands) {
			return false;
		}

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(index_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, JX64_REG_IS_HI(index_r), 0, 0, JX64_REG_HI(index_r), 0);
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement); // Displacement is always 32-bit
	} else if (base_r != JX64_REG_NONE && index_r == JX64_REG_NONE) {
		// op (q)word ptr [base + (disp)]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32 // Cannot use 16-bit or 8-bit base reg
			;
		if (invalidOperands) {
			return false;
		}

		// NOTE: Both can be true at the same time!
		// RIP is encoded as RBP with the MSB set to 1 (all other registers have the MSB set to 0)
		// in order to keep things a bit simpler (TODO: Find a better way!)
		// Since isBase_rbp checks only the 3 LSBs of the register ID, it becomes true for RBP, R13 and RIP.
		// isBase_rip can only be true if the register ID is RIP
		const bool isBase_rbp = JX64_REG_IS_RBP_R13(base_r);
		const bool isBase_rip = JX64_REG_IS_RIP(base_r);
		const bool isBase_rsp = JX64_REG_LO(base_r) == JX64_REG_ID_RSP;
		const bool needsDisplacement = false
			|| mem->m_Displacement != 0
			|| isBase_rbp
			|| isBase_rip
			;
		const uint8_t mod = isBase_rip
			? 0b00
			: (isBase_rbp && mem->m_Displacement == 0
				? 0b01
				: (mem->m_Displacement != 0
					? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
					: 0b00
					)
				)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, JX64_REG_IS_HI(base_r), 0, 0, 0, JX64_REG_HI(base_r));
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, mod, modrm_reg, JX64_REG_LO(base_r));
		jx64_instrEnc_sib(enc, isBase_rsp, 0b00, 0b100, 0b100);
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) && !isBase_rip ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);

#if 0
		// RIP-relative addressing: 
		// Fix displacement by the number of bytes of this instruction.
		// The caller doesn't and shouldn't know how many bytes the current instruction
		// will take and the rip-relative displacement is based on the start of the next
		// instruction. Since the user can only know the displacement to the start of the
		// current instruction, subtracting the size of the current instruction from the 
		// displacement should do the trick.
		if (isBase_rip) {
			enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);
		}
#endif
	} else {
		// op (q)word ptr [disp]
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, 0b00, 0b100, 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement);
	}

	return true;
}

// push, pop
static bool jx64_stack_op_reg(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, jx_x64_reg reg)
{
	const jx_x64_size reg_sz = JX64_REG_GET_SIZE(reg);

	const bool invalidOperand = false
		|| reg_sz == JX64_SIZE_8   // Cannot push 8-bit registers
		|| reg_sz == JX64_SIZE_32  // Cannot push 32-bit registers
		;
	if (invalidOperand) {
		return false;
	}

	jx64_instrEnc_operandSize(enc, reg_sz == JX64_SIZE_16);
	jx64_instrEnc_rex(enc, JX64_REG_IS_HI(reg), 0, 0, 0, JX64_REG_HI(reg));
	jx64_instrEnc_opcode1(enc, baseOpcode + JX64_REG_LO(reg));

	return true;
}

// not, neg, mul, imul eax, div, idiv
static bool jx64_math_unary_op(jx_x64_context_t* ctx, uint8_t baseOpcode, uint8_t modrm_reg, jx_x64_operand_t op)
{
	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	if (op.m_Type == JX64_OPERAND_REG) {
		if (!jx64_math_unary_op_reg(enc, baseOpcode, modrm_reg, op.u.m_Reg)) {
			return false;
		}
	} else if (op.m_Type == JX64_OPERAND_MEM) {
		if (!jx64_math_unary_op_mem(enc, baseOpcode, modrm_reg, &op.u.m_Mem, op.m_Size)) {
			return false;
		}
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

// adc, add, sub, sbb, or, xor, and, cmp
static bool jx64_math_binary_op(jx_x64_context_t* ctx, uint8_t opcode_imm, uint8_t modrm_reg, uint8_t opcode_rm, jx_x64_operand_t dst, jx_x64_operand_t src)
{
	if (dst.m_Type == JX64_OPERAND_SYM && src.m_Type == JX64_OPERAND_SYM) {
		return false;
	}

	jx_x64_symbol_t* sym = NULL;
	if (src.m_Type == JX64_OPERAND_SYM) {
		sym = src.u.m_Sym;
		src = jx64_opMem(src.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, 0);
	} else if (dst.m_Type == JX64_OPERAND_SYM) {
		sym = dst.u.m_Sym;
		dst = jx64_opMem(dst.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, 0);
	}

	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_IMM) {
		jx_x64_size immSize = src.m_Size;
		if (immSize == JX64_SIZE_64) {
			if (src.u.m_ImmI64 > INT32_MAX || src.u.m_ImmI64 < INT32_MIN) {
				JX_CHECK(false, "Immediate cannot be a 64-bit value!");
				return false;
			} else {
				immSize = JX64_SIZE_32;
			}
		}

		if (!jx64_math_binary_op_reg_imm(enc, opcode_imm, modrm_reg, dst.u.m_Reg, src.u.m_ImmI64, immSize)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_math_binary_op_reg_reg(enc, opcode_rm, dst.u.m_Reg, src.u.m_Reg)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_REG && src.m_Type == JX64_OPERAND_MEM) {
		if (!jx64_math_binary_op_reg_mem(enc, opcode_rm, 1, dst.u.m_Reg, &src.u.m_Mem)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_MEM && src.m_Type == JX64_OPERAND_IMM) {
		jx_x64_size immSize = src.m_Size;
		if (immSize == JX64_SIZE_64) {
			if (src.u.m_ImmI64 > INT32_MAX || src.u.m_ImmI64 < INT32_MIN) {
				JX_CHECK(false, "Immediate cannot be a 64-bit value!");
				return false;
			} else {
				immSize = JX64_SIZE_32;
			}
		}

		if (!jx64_math_binary_op_mem_imm(enc, opcode_imm, modrm_reg, &dst.u.m_Mem, dst.m_Size, src.u.m_ImmI64, immSize)) {
			return false;
		}
	} else if (dst.m_Type == JX64_OPERAND_MEM && src.m_Type == JX64_OPERAND_REG) {
		if (!jx64_math_binary_op_reg_mem(enc, opcode_rm, 0, src.u.m_Reg, &dst.u.m_Mem)) {
			return false;
		}
	} else {
		JX_CHECK(false, "Invalid operands?");
		return false;
	}

	if (sym) {
		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);

		const uint32_t relocDelta = (instrSize - dispOffset) - sizeof(int32_t);
		JX_CHECK(relocDelta <= 5, "Invalid relocation type");
		const uint32_t curFuncOffset = (uint32_t)ctx->m_CurFunc->m_Label->m_Offset;
		const uint32_t relocOffset = ctx->m_Section[JX64_SECTION_TEXT].m_Size - curFuncOffset + dispOffset;
		jx64_symbolAddRelocation(ctx, ctx->m_CurFunc, JX64_RELOC_REL32 + relocDelta, relocOffset, sym->m_Name);
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

// jmp, call
static bool jx64_jmp_call_op(jx_x64_context_t* ctx, uint8_t opcode_lbl, uint8_t opcode_rm, uint8_t modrm_reg, jx_x64_operand_t op)
{
	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

	if (op.m_Type == JX64_OPERAND_LBL) {
		jx_x64_label_t* lbl = op.u.m_Lbl;
		JX_CHECK(lbl->m_Section == JX64_SECTION_TEXT, "Only text section labels are allowed!");

		jx_x64_section_t* sec = &ctx->m_Section[lbl->m_Section];
		const int32_t displacement = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)sec->m_Size
			;

		jx64_instrEnc_opcode1(enc, opcode_lbl);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, (uint32_t)displacement);

		// Fix displacement
		enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);

		if (lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
			const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
			const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
			jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = sec->m_Size + dispOffset, .m_NextInstrOffset = sec->m_Size + instrSize });
		}
	} else if (op.m_Type == JX64_OPERAND_SYM) {
		jx_x64_symbol_t* sym = op.u.m_Sym;

		jx64_instrEnc_opcode1(enc, opcode_lbl);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, 0);

		const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
		const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
		const uint32_t relocDelta = (instrSize - dispOffset) - sizeof(int32_t);
		JX_CHECK(relocDelta <= 5, "Invalid relocation type");
		const uint32_t curFuncOffset = (uint32_t)ctx->m_CurFunc->m_Label->m_Offset;
		const uint32_t relocOffset = ctx->m_Section[JX64_SECTION_TEXT].m_Size - curFuncOffset + dispOffset;
		jx64_symbolAddRelocation(ctx, ctx->m_CurFunc, JX64_RELOC_REL32 + relocDelta, relocOffset, sym->m_Name);
	} else if (op.m_Type == JX64_OPERAND_REG) {
		const jx_x64_reg reg = op.u.m_Reg;

		const bool invalidOperands = false
			|| reg == JX64_REG_NONE
			|| JX64_REG_IS_RIP(reg)
			|| JX64_REG_GET_SIZE(reg) != JX64_SIZE_64
			;
		if (invalidOperands) {
			return false;
		}

		jx64_instrEnc_opcode1(enc, opcode_rm);
		jx64_instrEnc_modrm(enc, 0b11, modrm_reg, JX64_REG_LO(reg));
	} else {
		JX_CHECK(op.m_Type == JX64_OPERAND_MEM, "Expected memory operand.");
		const jx_x64_mem_t* mem = &op.u.m_Mem;

		const jx_x64_reg base_r = mem->m_Base;
		const jx_x64_reg index_r = mem->m_Index;

		if (base_r != JX64_REG_NONE && index_r != JX64_REG_NONE) {
			const bool invalidOperands = false
				|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32                // Cannot use 16-bit or 8-bit base reg
				|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32               // Cannot use 16-bit or 8-bit index reg
				|| JX64_REG_GET_SIZE(base_r) != JX64_REG_GET_SIZE(index_r) // Base and index registers must be the same size
				|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP             // Cannot use RSP as index register
				|| JX64_REG_IS_RIP(base_r)                                 // Cannot use RIP as base register
				|| JX64_REG_IS_RIP(index_r)                                // Cannot use RIP as index register
				;
			if (invalidOperands) {
				return false;
			}

			const bool needsDisplacement = false
				|| mem->m_Displacement != 0
				|| JX64_REG_IS_RBP_R13(base_r)
				;

			const bool forceMod01 = JX64_REG_IS_RBP_R13(base_r); // rBP/r13 requires SIB + disp8 with 0 disp
			const uint8_t mod = mem->m_Displacement != 0
				? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
				: (forceMod01 ? 0b01 : 0b00)
				;

			jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
			jx64_instrEnc_opcode1(enc, opcode_rm);
			jx64_instrEnc_modrm(enc, mod, modrm_reg, 0b100);
			jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), JX64_REG_LO(base_r));
			jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);
		} else if (base_r == JX64_REG_NONE && index_r != JX64_REG_NONE) {
			const bool invalidOperands = false
				|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32   // Cannot use 16-bit or 8-bit index reg
				|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP // Cannot use RSP as index register
				|| JX64_REG_IS_RIP(index_r)                    // Cannot use RIP as index register
				;
			if (invalidOperands) {
				return false;
			}

			jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(index_r) == JX64_SIZE_32);
			jx64_instrEnc_opcode1(enc, opcode_rm);
			jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
			jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), 0b101);
			jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement); // Displacement is always 32-bit
		} else if (base_r != JX64_REG_NONE && index_r == JX64_REG_NONE) {
			const bool invalidOperands = false
				|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32 // Cannot use 16-bit or 8-bit base reg
				;
			if (invalidOperands) {
				return false;
			}

			// NOTE: Both can be true at the same time!
			// RIP is encoded as RBP with the MSB set to 1 (all other registers have the MSB set to 0)
			// in order to keep things a bit simpler (TODO: Find a better way!)
			// Since isBase_rbp checks only the 3 LSBs of the register ID, it becomes true for RBP, R13 and RIP.
			// isBase_rip can only be true if the register ID is RIP
			const bool isBase_rbp = JX64_REG_IS_RBP_R13(base_r);
			const bool isBase_rip = JX64_REG_IS_RIP(base_r);
			const bool isBase_rsp = JX64_REG_LO(base_r) == JX64_REG_ID_RSP;
			const bool needsDisplacement = false
				|| mem->m_Displacement != 0
				|| isBase_rbp
				|| isBase_rip
				;

			const uint8_t mod = isBase_rip
				? 0b00
				: (isBase_rbp && mem->m_Displacement == 0
					? 0b01
					: (mem->m_Displacement != 0
						? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
						: 0b00
						)
					)
				;

			jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
			jx64_instrEnc_opcode1(enc, opcode_rm);
			jx64_instrEnc_modrm(enc, mod, modrm_reg, JX64_REG_LO(base_r));
			jx64_instrEnc_sib(enc, isBase_rsp, 0b00, 0b100, 0b100);
			jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) && !isBase_rip ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);

#if 0
			// RIP-relative addressing: 
			// Fix displacement by the number of bytes of this instruction.
			// The caller doesn't and shouldn't know how many bytes the current instruction
			// will take and the rip-relative displacement is based on the start of the next
			// instruction. Since the user can only know the displacement to the start of the
			// current instruction, subtracting the size of the current instruction from the 
			// displacement should do the trick.
			if (isBase_rip) {
				enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);
			}
#endif
		} else {
			jx64_instrEnc_opcode1(enc, opcode_rm);
			jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
			jx64_instrEnc_sib(enc, true, 0b00, 0b100, 0b101);
			jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement);
		}
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

// sal, sar, shl, shr, rcl, rcr, rol, ror
static bool jx64_shift_rotate_op(jx_x64_context_t* ctx, uint8_t modrm_reg, jx_x64_operand_t op, jx_x64_operand_t shift)
{
	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t) { 0 };

#if 0
	// RIP-relative addressing. 
	jx_x64_label_t* lbl = NULL;
	if (op.m_Type == JX64_OPERAND_LBL) {
		lbl = op.u.m_Lbl;
		const int32_t diff = lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND
			? 0
			: (int32_t)lbl->m_Offset - (int32_t)ctx->m_Size
			;
		op = jx64_opMem(op.m_Size, JX64_REG_RIP, JX64_REG_NONE, JX64_SCALE_1, diff);
	}
#endif

	if (op.m_Type == JX64_OPERAND_REG) {
		if (shift.m_Type == JX64_OPERAND_IMM) {
			const bool isImm_1 = shift.u.m_ImmI64 == 1;
			if (isImm_1) {
				if (!jx64_math_unary_op_reg(enc, 0xD0, modrm_reg, op.u.m_Reg)) {
					return false;
				}
			} else {
				const uint8_t opcode = 0xC0 | (op.m_Size != JX64_SIZE_8 ? 0x01 : 0x00);
				if (!jx64_binary_op_reg_imm(enc, &opcode, 1, modrm_reg, op.u.m_Reg, shift.u.m_ImmI64 & 0xFF, JX64_SIZE_8)) {
					return false;
				}
			}
		} else if (shift.m_Type == JX64_OPERAND_REG && shift.u.m_Reg == JX64_REG_CL) {
			if (!jx64_math_unary_op_reg(enc, 0xD2, modrm_reg, op.u.m_Reg)) {
				return false;
			}
		} else {
			return false;
		}
	} else if (op.m_Type == JX64_OPERAND_MEM) {
		if (shift.m_Type == JX64_OPERAND_IMM) {
			const bool isImm_1 = shift.u.m_ImmI64 == 1;
			if (isImm_1) {
				if (!jx64_math_unary_op_mem(enc, 0xD0, modrm_reg, &op.u.m_Mem, op.m_Size)) {
					return false;
				}
			} else {
				const uint8_t opcode = 0xC0 | (op.m_Size != JX64_SIZE_8 ? 0x01 : 0x00);
				if (!jx64_binary_op_mem_imm(enc, &opcode, 1, modrm_reg, &op.u.m_Mem, op.m_Size, shift.u.m_ImmI64 & 0xFF, JX64_SIZE_8)) {
					return false;
				}
			}
		} else if (shift.m_Type == JX64_OPERAND_REG && shift.u.m_Reg == JX64_REG_CL) {
			if (!jx64_math_unary_op_mem(enc, 0xD2, modrm_reg, &op.u.m_Mem, op.m_Size)) {
				return false;
			}
		} else {
			return false;
		}

#if 0
		if (lbl && lbl->m_Offset == JX64_LABEL_OFFSET_UNBOUND) {
			const uint32_t dispOffset = jx64_instrEnc_calcDispOffset(enc);
			const uint32_t instrSize = jx64_instrEnc_calcInstrSize(enc);
			jx_array_push_back(lbl->m_RefsArr, (jx_x64_label_ref_t) { .m_DispOffset = ctx->m_Size + dispOffset, .m_NextInstrOffset = ctx->m_Size + instrSize });
		}
#endif
	} else {
		return false;
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t) { 0 };
	if (!jx64_encodeInstr(instr, enc)) {
		return false;
	}

	return jx64_emitBytes(ctx, JX64_SECTION_TEXT, instr->m_Buffer, instr->m_Size);
}

static bool jx64_math_unary_op_reg(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, jx_x64_reg reg)
{
	JX_CHECK((baseOpcode & 0b00000001) == 0, "Lowest opcode bit is set by the code below!");

	const bool invalidOperands = false
		|| reg == JX64_REG_NONE
		|| JX64_REG_IS_RIP(reg)
		;
	if (invalidOperands) {
		return false;
	}

	const jx_x64_size reg_sz = JX64_REG_GET_SIZE(reg);

	const bool needsREX = false
		|| (reg_sz == JX64_SIZE_8 && JX64_REG_GET_ID(reg) >= JX64_REG_ID_RSP)
		|| reg_sz == JX64_SIZE_64
		|| JX64_REG_IS_HI(reg)
		;

	jx64_instrEnc_operandSize(enc, reg_sz == JX64_SIZE_16);
	jx64_instrEnc_rex(enc, needsREX, reg_sz == JX64_SIZE_64, 0, 0, JX64_REG_HI(reg));
	jx64_instrEnc_opcode1(enc, (baseOpcode & 0b11111110) | ((reg_sz != JX64_SIZE_8) ? 1 : 0));
	jx64_instrEnc_modrm(enc, 0b11, modrm_reg, JX64_REG_LO(reg));

	return true;
}

static bool jx64_math_unary_op_mem(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, const jx_x64_mem_t* mem, jx_x64_size sz)
{
	JX_CHECK((baseOpcode & 0b00000001) == 0, "Lowest opcode bit is set by the code below!");

	const jx_x64_reg base_r = mem->m_Base;
	const jx_x64_reg index_r = mem->m_Index;

	const uint8_t opcode = (baseOpcode & 0b11111110)
		| (sz == JX64_SIZE_8 ? 0b00 : 0b01)
		;

	if (base_r != JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op [base + index * scale + (disp)], reg
		// op reg, [base + index * scale + (disp)]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32                // Cannot use 16-bit or 8-bit base reg
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32               // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_SIZE(base_r) != JX64_REG_GET_SIZE(index_r) // Base and index registers must be the same size
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP             // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(base_r)                                 // Cannot use RIP as base register
			|| JX64_REG_IS_RIP(index_r)                                // Cannot use RIP as index register
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(base_r)
			|| JX64_REG_IS_HI(index_r)
			;
		const bool needsDisplacement = false
			|| mem->m_Displacement != 0
			|| JX64_REG_IS_RBP_R13(base_r)
			;
		const bool forceMod01 = JX64_REG_IS_RBP_R13(base_r); // rBP/r13 requires SIB + disp8 with 0 disp
		const uint8_t mod = mem->m_Displacement != 0
			? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
			: (forceMod01 ? 0b01 : 0b00)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, sz == JX64_SIZE_64, 0, JX64_REG_HI(index_r), JX64_REG_HI(base_r));
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, mod, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), JX64_REG_LO(base_r));
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);
	} else if (base_r == JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op [index * scale + disp], reg
		// op reg, [index * scale + disp]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32   // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(index_r)                    // Cannot use RIP as index register
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(index_r)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(index_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, sz == JX64_SIZE_64, 0, JX64_REG_HI(index_r), 0);
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement);
	} else if (base_r != JX64_REG_NONE && index_r == JX64_REG_NONE) {
		// op [base + (disp)], reg
		// op reg, [base + (disp)]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32 // Cannot use 16-bit or 8-bit base reg
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(base_r)
			;
		// NOTE: Both can be true at the same time!
		// RIP is encoded as RBP with the MSB set to 1 (all other registers have the MSB set to 0)
		// in order to keep things a bit simpler (TODO: Find a better way!)
		// Since isBase_rbp checks only the 3 LSBs of the register ID, it becomes true for RBP, R13 and RIP.
		// isBase_rip can only be true if the register ID is RIP
		const bool isBase_rbp = JX64_REG_IS_RBP_R13(base_r);
		const bool isBase_rip = JX64_REG_IS_RIP(base_r);
		const bool isBase_rsp = JX64_REG_LO(base_r) == JX64_REG_ID_RSP;
		const bool needsDisplacement = false
			|| mem->m_Displacement != 0
			|| isBase_rbp
			|| isBase_rip
			;
		const uint8_t mod = isBase_rip
			? 0b00
			: (isBase_rbp && mem->m_Displacement == 0
				? 0b01
				: (mem->m_Displacement != 0
					? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
					: 0b00
					)
				)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, sz == JX64_SIZE_64, 0, 0, JX64_REG_HI(base_r));
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, mod, modrm_reg, JX64_REG_LO(base_r));
		jx64_instrEnc_sib(enc, isBase_rsp, 0b00, 0b100, 0b100);
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) && !isBase_rip ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);

#if 0
		// RIP-relative addressing: 
		// Fix displacement by the number of bytes of this instruction.
		// The caller doesn't and shouldn't know how many bytes the current instruction
		// will take and the rip-relative displacement is based on the start of the next
		// instruction. Since the user can only know the displacement to the start of the
		// current instruction, subtracting the size of the current instruction from the 
		// displacement should do the trick.
		if (isBase_rip) {
			enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);
		}
#endif
	} else {
		// op [disp32], reg
		// op reg, [disp32]
		// mov reg, [disp]
		jx64_instrEnc_operandSize(enc, sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, sz == JX64_SIZE_64, sz == JX64_SIZE_64, 0, 0, 0);
		jx64_instrEnc_opcode1(enc, opcode);
		jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, 0b00, 0b100, 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement);
	}

	return true;
}

static bool jx64_math_binary_op_reg_imm(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, jx_x64_reg reg, int64_t imm, jx_x64_size imm_sz)
{
	JX_CHECK((baseOpcode & 0b00000011) == 0, "Lowest 2 opcode bits are set by the code below!");

	const jx_x64_size reg_sz = JX64_REG_GET_SIZE(reg);

	const uint8_t opcode = (baseOpcode & 0b11111100)
		| ((imm_sz == JX64_SIZE_8 && reg_sz != JX64_SIZE_8) ? 0b10 : 0b00) // s bit
		| ((reg_sz != JX64_SIZE_8) ? 0b01 : 0b00)                          // w bit
		;

	return jx64_binary_op_reg_imm(enc, &opcode, 1, modrm_reg, reg, imm, imm_sz);
}

static bool jx64_math_binary_op_reg_reg(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, jx_x64_reg dst, jx_x64_reg src)
{
	JX_CHECK((baseOpcode & 0b00000001) == 0, "Lowest opcode bit is set by the code below!");

	const uint8_t opcode = (baseOpcode & 0b11111110)
		| ((JX64_REG_GET_SIZE(dst) != JX64_SIZE_8) ? 1 : 0)
		;

	return jx64_binary_op_reg_reg(enc, &opcode, 1, dst, src);
}

static bool jx64_math_binary_op_reg_mem(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t isRegDst, jx_x64_reg reg, const jx_x64_mem_t* mem)
{
	JX_CHECK((baseOpcode & 0b00000011) == 0, "Lowest 2 opcode bits are set by the code below!");

	const uint8_t opcode = (baseOpcode & 0b11111100)
		| (JX64_REG_GET_SIZE(reg) == JX64_SIZE_8 ? 0b00 : 0b01)
		| (isRegDst ? 0b10 : 0b00)
		;

	return jx64_binary_op_reg_mem(enc, &opcode, 1, reg, mem);
}

static bool jx64_math_binary_op_mem_imm(jx_x64_instr_encoding_t* enc, uint8_t baseOpcode, uint8_t modrm_reg, const jx_x64_mem_t* dst_m, jx_x64_size dst_m_sz, int64_t src_imm, jx_x64_size src_imm_sz)
{
	JX_CHECK((baseOpcode & 0b00000011) == 0, "Lowest 2 opcode bits are set by the code below!");

	const uint8_t opcode = (baseOpcode & 0b11111100)
		| ((src_imm_sz == JX64_SIZE_8 && dst_m_sz != JX64_SIZE_8) ? 0b10 : 0b00) // s bit
		| ((dst_m_sz != JX64_SIZE_8) ? 0b01 : 0b00)                              // w bit
		;

	return jx64_binary_op_mem_imm(enc, &opcode, 1, modrm_reg, dst_m, dst_m_sz, src_imm, src_imm_sz);
}

static bool jx64_binary_op_reg_imm(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, uint8_t modrm_reg, jx_x64_reg reg, int64_t imm, jx_x64_size imm_sz)
{
	const jx_x64_size reg_sz = JX64_REG_GET_SIZE(reg);

	const bool invalidOperands = false
		|| opcodeSize > 3
		|| reg == JX64_REG_NONE                        // must be a valid register
		|| JX64_REG_IS_RIP(reg)                        // cannot add to RIP
		|| imm_sz == JX64_SIZE_64                      // 64-bit immediates not supported
		|| !(reg_sz == imm_sz || imm_sz == JX64_SIZE_8 || (reg_sz == JX64_SIZE_64 && imm_sz == JX64_SIZE_32)) // only allow reg size == imm size or 8-bit immediates or 64-bit with 32-bit imm
		;
	if (invalidOperands) {
		return false;
	}

	const bool needsREX = false
		|| (reg_sz == JX64_SIZE_8 && JX64_REG_GET_ID(reg) >= JX64_REG_ID_RSP)
		|| reg_sz == JX64_SIZE_64
		|| JX64_REG_IS_HI(reg)
		;

	jx64_instrEnc_operandSize(enc, reg_sz == JX64_SIZE_16);
	jx64_instrEnc_rex(enc, needsREX, reg_sz == JX64_SIZE_64, 0, 0, JX64_REG_HI(reg));
	jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
	jx64_instrEnc_modrm(enc, 0b11, modrm_reg, JX64_REG_LO(reg));
	jx64_instrEnc_imm(enc, true, imm_sz, imm);

	return true;
}

static bool jx64_binary_op_mem_imm(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, uint8_t modrm_reg, const jx_x64_mem_t* dst_m, jx_x64_size dst_m_sz, int64_t src_imm, jx_x64_size src_imm_sz)
{
	const jx_x64_reg base_r = dst_m->m_Base;
	const jx_x64_reg index_r = dst_m->m_Index;

	if (base_r != JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op [base + index * scale + (disp)], reg
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32                // Cannot use 16-bit or 8-bit base reg
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32               // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_SIZE(base_r) != JX64_REG_GET_SIZE(index_r) // Base and index registers must be the same size
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP             // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(base_r)                                 // Cannot use RIP as base register
			|| JX64_REG_IS_RIP(index_r)                                // Cannot use RIP as index register
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF)    // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| dst_m_sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(base_r)
			|| JX64_REG_IS_HI(index_r)
			;
		const bool needsDisplacement = false
			|| dst_m->m_Displacement != 0
			|| JX64_REG_IS_RBP_R13(base_r)
			;
		const bool forceMod01 = JX64_REG_IS_RBP_R13(base_r); // rBP/r13 requires SIB + disp8 with 0 disp
		const uint8_t mod = dst_m->m_Displacement != 0
			? (JX64_DISP_IS_8BIT(dst_m->m_Displacement) ? 0b01 : 0b10)
			: (forceMod01 ? 0b01 : 0b00)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, dst_m_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, dst_m_sz == JX64_SIZE_64, 0, JX64_REG_HI(index_r), JX64_REG_HI(base_r));
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, mod, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, dst_m->m_Scale, JX64_REG_LO(index_r), JX64_REG_LO(base_r));
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(dst_m->m_Displacement) ? JX64_SIZE_8 : JX64_SIZE_32, dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);
	} else if (base_r == JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op [index * scale + (disp)], reg
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32    // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP  // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(index_r)                     // Cannot use RIP as index register
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF) // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| dst_m_sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(index_r)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(index_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, dst_m_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, dst_m_sz == JX64_SIZE_64, 0, JX64_REG_HI(index_r), 0);
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, dst_m->m_Scale, JX64_REG_LO(index_r), 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);
	} else if (base_r != JX64_REG_NONE && index_r == JX64_REG_NONE) {
		// op [base + (disp)], reg
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32 // Cannot use 16-bit or 8-bit base reg
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF) // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		// NOTE: Both can be true at the same time!
		// RIP is encoded as RBP with the MSB set to 1 (all other registers have the MSB set to 0)
		// in order to keep things a bit simpler (TODO: Find a better way!)
		// Since isBase_rbp checks only the 3 LSBs of the register ID, it becomes true for RBP, R13 and RIP.
		// isBase_rip can only be true if the register ID is RIP
		const bool isBase_rbp = JX64_REG_IS_RBP_R13(base_r);
		const bool isBase_rip = JX64_REG_IS_RIP(base_r);
		const bool isBase_rsp = JX64_REG_LO(base_r) == JX64_REG_ID_RSP;
		const bool needsREX = false
			|| dst_m_sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(base_r)
			;
		const bool needsDisplacement = false
			|| dst_m->m_Displacement != 0
			|| isBase_rbp
			|| isBase_rip
			;
		const uint8_t mod = isBase_rip
			? 0b00
			: (isBase_rbp && dst_m->m_Displacement == 0
				? 0b01
				: (dst_m->m_Displacement != 0
					? (JX64_DISP_IS_8BIT(dst_m->m_Displacement) ? 0b01 : 0b10)
					: 0b00
					)
				)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, dst_m_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, dst_m_sz == JX64_SIZE_64, 0, 0, JX64_REG_HI(base_r));
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, mod, modrm_reg, JX64_REG_LO(base_r));
		jx64_instrEnc_sib(enc, isBase_rsp, 0b00, 0b100, 0b100);
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(dst_m->m_Displacement) && !isBase_rip ? JX64_SIZE_8 : JX64_SIZE_32, dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);

#if 0
		// RIP-relative addressing: 
		// Fix displacement by the number of bytes of this instruction.
		// The caller doesn't and shouldn't know how many bytes the current instruction
		// will take and the rip-relative displacement is based on the start of the next
		// instruction. Since the user can only know the displacement to the start of the
		// current instruction, subtracting the size of the current instruction from the 
		// displacement should do the trick.
		if (isBase_rip) {
			enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);
		}
#endif
	} else {
		// op [disp], imm
		const bool invalidOperands = false
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF) // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		jx64_instrEnc_operandSize(enc, dst_m_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, dst_m_sz == JX64_SIZE_64, dst_m_sz == JX64_SIZE_64, 0, 0, 0);
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, 0b00, modrm_reg, 0b100);
		jx64_instrEnc_sib(enc, true, 0b00, 0b100, 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);
	}

	return true;
}

static bool jx64_binary_op_reg_reg(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, jx_x64_reg dst, jx_x64_reg src)
{
	const bool invalidOperands = false
		|| opcodeSize > 3
		|| dst == JX64_REG_NONE
		|| src == JX64_REG_NONE
		|| JX64_REG_IS_RIP(dst)
		|| JX64_REG_IS_RIP(src)
		|| JX64_REG_GET_SIZE(dst) != JX64_REG_GET_SIZE(src)
		;
	if (invalidOperands) {
		return false;
	}

	const jx_x64_size reg_sz = JX64_REG_GET_SIZE(dst);

	const bool needsREX = false
		|| (reg_sz == JX64_SIZE_8 && (JX64_REG_GET_ID(dst) >= JX64_REG_ID_RSP || JX64_REG_GET_ID(src) >= JX64_REG_ID_RSP))
		|| reg_sz == JX64_SIZE_64
		|| JX64_REG_IS_HI(dst)
		|| JX64_REG_IS_HI(src)
		;

	jx64_instrEnc_operandSize(enc, reg_sz == JX64_SIZE_16);
	jx64_instrEnc_rex(enc, needsREX, reg_sz == JX64_SIZE_64, JX64_REG_HI(src), 0, JX64_REG_HI(dst));
	jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
	jx64_instrEnc_modrm(enc, 0b11, JX64_REG_LO(src), JX64_REG_LO(dst));

	return true;
}

static bool jx64_binary_op_reg_mem(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t opcodeSize, jx_x64_reg reg, const jx_x64_mem_t* mem)
{
	const jx_x64_reg base_r = mem->m_Base;
	const jx_x64_reg index_r = mem->m_Index;

	if (base_r != JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op [base + index * scale + (disp)], reg
		// op reg, [base + index * scale + (disp)]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32                // Cannot use 16-bit or 8-bit base reg
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32               // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_SIZE(base_r) != JX64_REG_GET_SIZE(index_r) // Base and index registers must be the same size
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP             // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(base_r)                                 // Cannot use RIP as base register
			|| JX64_REG_IS_RIP(index_r)                                // Cannot use RIP as index register
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| JX64_REG_GET_SIZE(reg) == JX64_SIZE_64
			|| JX64_REG_IS_HI(reg)
			|| (JX64_REG_GET_SIZE(reg) == JX64_SIZE_8 && JX64_REG_GET_ID(reg) >= JX64_REG_ID_RSP)
			|| JX64_REG_IS_HI(base_r)
			|| JX64_REG_IS_HI(index_r)
			;
		const bool needsDisplacement = false
			|| mem->m_Displacement != 0
			|| JX64_REG_IS_RBP_R13(base_r)
			;
		const bool forceMod01 = JX64_REG_IS_RBP_R13(base_r); // rBP/r13 requires SIB + disp8 with 0 disp
		const uint8_t mod = mem->m_Displacement != 0
			? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
			: (forceMod01 ? 0b01 : 0b00)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, JX64_REG_GET_SIZE(reg) == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, JX64_REG_GET_SIZE(reg) == JX64_SIZE_64, JX64_REG_HI(reg), JX64_REG_HI(index_r), JX64_REG_HI(base_r));
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, mod, JX64_REG_LO(reg), 0b100);
		jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), JX64_REG_LO(base_r));
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);
	} else if (base_r == JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// op [index * scale + disp], reg
		// op reg, [index * scale + disp]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32   // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(index_r)                    // Cannot use RIP as index register
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| JX64_REG_GET_SIZE(reg) == JX64_SIZE_64
			|| JX64_REG_IS_HI(reg)
			|| (JX64_REG_GET_SIZE(reg) == JX64_SIZE_8 && JX64_REG_GET_ID(reg) >= JX64_REG_ID_RSP)
			|| JX64_REG_IS_HI(index_r)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(index_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, JX64_REG_GET_SIZE(reg) == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, JX64_REG_GET_SIZE(reg) == JX64_SIZE_64, JX64_REG_HI(reg), JX64_REG_HI(index_r), 0);
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, 0b00, JX64_REG_LO(reg), 0b100);
		jx64_instrEnc_sib(enc, true, mem->m_Scale, JX64_REG_LO(index_r), 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement); // Displacement is always 32-bit
	} else if (base_r != JX64_REG_NONE && index_r == JX64_REG_NONE) {
		// op [base + (disp)], reg
		// op reg, [base + (disp)]
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32 // Cannot use 16-bit or 8-bit base reg
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| JX64_REG_GET_SIZE(reg) == JX64_SIZE_64
			|| JX64_REG_IS_HI(reg)
			|| (JX64_REG_GET_SIZE(reg) == JX64_SIZE_8 && JX64_REG_GET_ID(reg) >= JX64_REG_ID_RSP)
			|| JX64_REG_IS_HI(base_r)
			;
		// NOTE: Both can be true at the same time!
		// RIP is encoded as RBP with the MSB set to 1 (all other registers have the MSB set to 0)
		// in order to keep things a bit simpler (TODO: Find a better way!)
		// Since isBase_rbp checks only the 3 LSBs of the register ID, it becomes true for RBP, R13 and RIP.
		// isBase_rip can only be true if the register ID is RIP
		const bool isBase_rbp = JX64_REG_IS_RBP_R13(base_r);
		const bool isBase_rip = JX64_REG_IS_RIP(base_r);
		const bool isBase_rsp = JX64_REG_LO(base_r) == JX64_REG_ID_RSP;
		const bool needsDisplacement = false
			|| mem->m_Displacement != 0
			|| isBase_rbp
			|| isBase_rip
			;
		const uint8_t mod = isBase_rip
			? 0b00
			: (isBase_rbp && mem->m_Displacement == 0
				? 0b01
				: (mem->m_Displacement != 0
					? (JX64_DISP_IS_8BIT(mem->m_Displacement) ? 0b01 : 0b10)
					: 0b00
					)
				)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, JX64_REG_GET_SIZE(reg) == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, JX64_REG_GET_SIZE(reg) == JX64_SIZE_64, JX64_REG_HI(reg), 0, JX64_REG_HI(base_r));
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, mod, JX64_REG_LO(reg), JX64_REG_LO(base_r));
		jx64_instrEnc_sib(enc, isBase_rsp, 0b00, 0b100, 0b100);
		jx64_instrEnc_disp(enc, needsDisplacement, JX64_DISP_IS_8BIT(mem->m_Displacement) && !isBase_rip ? JX64_SIZE_8 : JX64_SIZE_32, mem->m_Displacement);

#if 0
		// RIP-relative addressing: 
		// Fix displacement by the number of bytes of this instruction.
		// The caller doesn't and shouldn't know how many bytes the current instruction
		// will take and the rip-relative displacement is based on the start of the next
		// instruction. Since the user can only know the displacement to the start of the
		// current instruction, subtracting the size of the current instruction from the 
		// displacement should do the trick.
		if (isBase_rip) {
			enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);
		}
#endif
	} else {
		// op [disp32], reg
		// op reg, [disp32]
		const bool needsREX = false
			|| JX64_REG_GET_SIZE(reg) == JX64_SIZE_64
			|| (JX64_REG_GET_SIZE(reg) == JX64_SIZE_8 && JX64_REG_GET_ID(reg) >= JX64_REG_ID_RSP)
			|| JX64_REG_IS_HI(reg)
			;

		jx64_instrEnc_operandSize(enc, JX64_REG_GET_SIZE(reg) == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, JX64_REG_GET_SIZE(reg) == JX64_SIZE_64, JX64_REG_HI(reg), 0, 0);
		jx64_instrEnc_opcoden(enc, opcode, opcodeSize);
		jx64_instrEnc_modrm(enc, 0b00, JX64_REG_LO(reg), 0b100);
		jx64_instrEnc_sib(enc, true, 0b00, 0b100, 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, mem->m_Displacement);
	}

	return true;
}

static bool jx64_mov_reg_imm(jx_x64_instr_encoding_t* enc, jx_x64_reg dst_r, int64_t src_imm, jx_x64_size src_imm_sz)
{
	const jx_x64_size dst_r_sz = JX64_REG_GET_SIZE(dst_r);

	const bool invalidOperands = false
		|| dst_r == JX64_REG_NONE
		|| JX64_REG_IS_RIP(dst_r)
		|| (dst_r_sz != src_imm_sz && !(dst_r_sz == JX64_SIZE_64 && src_imm_sz == JX64_SIZE_32))
		;
	if (invalidOperands) {
		return false;
	}

	if (dst_r_sz == JX64_SIZE_64 && src_imm_sz == JX64_SIZE_32) {
		// Sign extend 32-bit immediate into 64-bit register
		jx64_instrEnc_rex(enc, true, 1, 0, 0, JX64_REG_HI(dst_r));
		jx64_instrEnc_opcode1(enc, 0xC7);
		jx64_instrEnc_modrm(enc, 0b11, 0b000, JX64_REG_LO(dst_r));
		jx64_instrEnc_imm(enc, true, JX64_SIZE_32, src_imm);
	} else {
		JX_CHECK(dst_r_sz == src_imm_sz, "Both operands must be the same size!");

		const bool needsREX = false
			|| (dst_r_sz == JX64_SIZE_64)
			|| (dst_r_sz == JX64_SIZE_8 && JX64_REG_GET_ID(dst_r) >= JX64_REG_ID_RSP)
			|| JX64_REG_IS_HI(dst_r)
			;

		jx64_instrEnc_operandSize(enc, dst_r_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, dst_r_sz == JX64_SIZE_64, 0, 0, JX64_REG_HI(dst_r));
		jx64_instrEnc_opcode1(enc, (dst_r_sz == JX64_SIZE_8 ? 0xB0 : 0xB8) + JX64_REG_LO(dst_r));
		jx64_instrEnc_imm(enc, true, dst_r_sz, src_imm);
	}

	return true;
}

static bool jx64_mov_mem_imm(jx_x64_instr_encoding_t* enc, const jx_x64_mem_t* dst_m, int64_t src_imm, jx_x64_size src_imm_sz)
{
	const jx_x64_reg base_r = dst_m->m_Base;
	const jx_x64_reg index_r = dst_m->m_Index;

	if (base_r != JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// mov [base + index * scale + (disp)], reg
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32                // Cannot use 16-bit or 8-bit base reg
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32               // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_SIZE(base_r) != JX64_REG_GET_SIZE(index_r) // Base and index registers must be the same size
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP             // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(base_r)                                 // Cannot use RIP as base register
			|| JX64_REG_IS_RIP(index_r)                                // Cannot use RIP as index register
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF)    // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| src_imm_sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(base_r)
			|| JX64_REG_IS_HI(index_r)
			;

		const bool needsDisplacement = false
			|| dst_m->m_Displacement != 0
			|| JX64_REG_IS_RBP_R13(base_r)
			;

		const uint8_t mod = dst_m->m_Displacement != 0
			? (JX64_DISP_IS_8BIT(dst_m->m_Displacement) ? 0b01 : 0b10)
			: (JX64_REG_IS_RBP_R13(base_r) ? 0b01 : 0b00)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, src_imm_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, src_imm_sz == JX64_SIZE_64, 0, JX64_REG_HI(index_r), JX64_REG_HI(base_r));
		jx64_instrEnc_opcode1(enc, 0xC6 | (src_imm_sz == JX64_SIZE_8 ? 0 : 1));
		jx64_instrEnc_modrm(enc, mod, 0b000, 0b100);
		jx64_instrEnc_sib(enc, true, dst_m->m_Scale, JX64_REG_LO(index_r), JX64_REG_LO(base_r));
		jx64_instrEnc_disp(enc, needsDisplacement, (JX64_DISP_IS_8BIT(dst_m->m_Displacement) ? JX64_SIZE_8 : JX64_SIZE_32), dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);
	} else if (base_r == JX64_REG_NONE && index_r != JX64_REG_NONE) {
		// mov [index * scale + (disp)], reg
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(index_r) < JX64_SIZE_32    // Cannot use 16-bit or 8-bit index reg
			|| JX64_REG_GET_ID(index_r) == JX64_REG_ID_RSP  // Cannot use RSP as index register
			|| JX64_REG_IS_RIP(index_r)                     // Cannot use RIP as index register
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF) // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		const bool needsREX = false
			|| src_imm_sz == JX64_SIZE_64 
			|| JX64_REG_IS_HI(index_r)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(index_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, src_imm_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, src_imm_sz == JX64_SIZE_64, 0, JX64_REG_HI(index_r), 0);
		jx64_instrEnc_opcode1(enc, 0xC6 | (src_imm_sz == JX64_SIZE_8 ? 0 : 1));
		jx64_instrEnc_modrm(enc, 0b00, 0b000, 0b100);
		jx64_instrEnc_sib(enc, true, dst_m->m_Scale, JX64_REG_LO(index_r), 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);
	} else if (base_r != JX64_REG_NONE && index_r == JX64_REG_NONE) {
		// mov [base + (disp)], reg
		const bool invalidOperands = false
			|| JX64_REG_GET_SIZE(base_r) < JX64_SIZE_32 // Cannot use 16-bit or 8-bit base reg
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF) // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		// NOTE: Both can be true at the same time!
		// RIP is encoded as RBP with the MSB set to 1 (all other registers have the MSB set to 0)
		// in order to keep things a bit simpler (TODO: Find a better way!)
		// Since isBase_rbp checks only the 3 LSBs of the register ID, it becomes true for RBP, R13 and RIP.
		// isBase_rip can only be true if the register ID is RIP
		const bool isBase_rbp = JX64_REG_IS_RBP_R13(base_r);
		const bool isBase_rip = JX64_REG_IS_RIP(base_r);
		const bool isBase_rsp = JX64_REG_LO(base_r) == JX64_REG_ID_RSP;
		
		const bool needsREX = false
			|| src_imm_sz == JX64_SIZE_64
			|| JX64_REG_IS_HI(base_r)
			;

		const bool needsDisplacement = false
			|| dst_m->m_Displacement != 0
			|| isBase_rbp
			|| isBase_rip
			;

		const uint8_t mod = isBase_rip
			? 0b00
			: (isBase_rbp && dst_m->m_Displacement == 0
				? 0b01
				: (dst_m->m_Displacement != 0
					? (JX64_DISP_IS_8BIT(dst_m->m_Displacement) ? 0b01 : 0b10)
					: 0b00
					)
				)
			;

		jx64_instrEnc_addrSize(enc, JX64_REG_GET_SIZE(base_r) == JX64_SIZE_32);
		jx64_instrEnc_operandSize(enc, src_imm_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, needsREX, src_imm_sz == JX64_SIZE_64, 0, 0, JX64_REG_HI(base_r));
		jx64_instrEnc_opcode1(enc, 0xC6 | (src_imm_sz == JX64_SIZE_8 ? 0 : 1));
		jx64_instrEnc_modrm(enc, mod, 0b000, JX64_REG_LO(base_r));
		jx64_instrEnc_sib(enc, isBase_rsp, 0b00, 0b100, 0b100);
		jx64_instrEnc_disp(enc, needsDisplacement, (JX64_DISP_IS_8BIT(dst_m->m_Displacement) && !isBase_rip) ? JX64_SIZE_8 : JX64_SIZE_32, dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);

#if 0
		// RIP-relative addressing: 
		// Fix displacement by the number of bytes of this instruction.
		// The caller doesn't and shouldn't know how many bytes the current instruction
		// will take and the rip-relative displacement is based on the start of the next
		// instruction. Since the user can only know the displacement to the start of the
		// current instruction, subtracting the size of the current instruction from the 
		// displacement should do the trick.
		if (isBase_rip) {
			enc->m_Disp -= jx64_instrEnc_calcInstrSize(enc);
		}
#endif
	} else {
		// mov [disp], imm
		const bool invalidOperands = false
			|| (src_imm_sz == JX64_SIZE_64 && src_imm > 0xFFFFFFFF) // Only 32-bit immediates are supported
			;
		if (invalidOperands) {
			return false;
		}

		jx64_instrEnc_operandSize(enc, src_imm_sz == JX64_SIZE_16);
		jx64_instrEnc_rex(enc, src_imm_sz == JX64_SIZE_64, src_imm_sz == JX64_SIZE_64, 0, 0, 0);
		jx64_instrEnc_opcode1(enc, 0xC6 | (src_imm_sz == JX64_SIZE_8 ? 0 : 1));
		jx64_instrEnc_modrm(enc, 0b00, 0b000, 0b100);
		jx64_instrEnc_sib(enc, true, 0b00, 0b100, 0b101);
		jx64_instrEnc_disp(enc, true, JX64_SIZE_32, dst_m->m_Displacement);
		jx64_instrEnc_imm(enc, true, src_imm_sz == JX64_SIZE_64 ? JX64_SIZE_32 : src_imm_sz, src_imm);
	}

	return true;
}

static bool jx64_movsx_reg_reg(jx_x64_instr_encoding_t* enc, jx_x64_reg dst_r, jx_x64_reg src_r)
{
	jx_x64_size src_r_sz = JX64_REG_GET_SIZE(src_r);
	jx_x64_size dst_r_sz = JX64_REG_GET_SIZE(dst_r);

	const bool invalidOperands = false
		|| dst_r == JX64_REG_NONE
		|| src_r == JX64_REG_NONE
		|| JX64_REG_IS_RIP(dst_r)
		|| JX64_REG_IS_RIP(src_r)
		|| src_r_sz == JX64_SIZE_64
		;
	if (invalidOperands) {
		return false;
	}

	const bool needsREX = false
		|| (dst_r_sz == JX64_SIZE_8 && (JX64_REG_GET_ID(dst_r) >= JX64_REG_ID_RSP || JX64_REG_GET_ID(src_r) >= JX64_REG_ID_RSP))
		|| dst_r_sz == JX64_SIZE_64
		|| JX64_REG_IS_HI(dst_r)
		|| JX64_REG_IS_HI(src_r)
		;

	jx64_instrEnc_operandSize(enc, dst_r_sz == JX64_SIZE_16);
	jx64_instrEnc_rex(enc, needsREX, dst_r_sz == JX64_SIZE_64, JX64_REG_HI(src_r), 0, JX64_REG_HI(dst_r));
	if (src_r_sz == JX64_SIZE_8) {
		jx64_instrEnc_opcode2(enc, 0x0F, 0xBE);
	} else if (src_r_sz == JX64_SIZE_16) {
		jx64_instrEnc_opcode2(enc, 0x0F, 0xBF);
	} else if (src_r_sz == JX64_SIZE_32) {
		jx64_instrEnc_opcode1(enc, 0x63);
	}
	jx64_instrEnc_modrm(enc, 0b11, JX64_REG_LO(src_r), JX64_REG_LO(dst_r));

	return true;
}

static bool jx64_movzx_reg_reg(jx_x64_instr_encoding_t* enc, jx_x64_reg dst_r, jx_x64_reg src_r)
{
	jx_x64_size src_r_sz = JX64_REG_GET_SIZE(src_r);
	jx_x64_size dst_r_sz = JX64_REG_GET_SIZE(dst_r);

	const bool invalidOperands = false
		|| dst_r == JX64_REG_NONE
		|| src_r == JX64_REG_NONE
		|| JX64_REG_IS_RIP(dst_r)
		|| JX64_REG_IS_RIP(src_r)
		|| src_r_sz == JX64_SIZE_64 
		;
	if (invalidOperands) {
		return false;
	}

	const bool needsREX = false
		|| (dst_r_sz == JX64_SIZE_8 && (JX64_REG_GET_ID(dst_r) >= JX64_REG_ID_RSP || JX64_REG_GET_ID(src_r) >= JX64_REG_ID_RSP))
		|| dst_r_sz == JX64_SIZE_64
		|| JX64_REG_IS_HI(dst_r)
		|| JX64_REG_IS_HI(src_r)
		;

	jx64_instrEnc_operandSize(enc, dst_r_sz == JX64_SIZE_16);
	jx64_instrEnc_rex(enc, needsREX, dst_r_sz == JX64_SIZE_64, JX64_REG_HI(src_r), 0, JX64_REG_HI(dst_r));
	if (src_r_sz == JX64_SIZE_8) {
		jx64_instrEnc_opcode2(enc, 0x0F, 0xB6);
	} else if (src_r_sz == JX64_SIZE_16) {
		jx64_instrEnc_opcode2(enc, 0x0F, 0xB7);
	} else if (src_r_sz == JX64_SIZE_32) {
		JX_CHECK(false, "Use mov reg, reg instead!");
	}
	jx64_instrEnc_modrm(enc, 0b11, JX64_REG_LO(src_r), JX64_REG_LO(dst_r));

	return true;
}

static inline bool jx64_instrBuf_push8(jx_x64_instr_buffer_t* ib, uint8_t b)
{
	if (ib->m_Size + 1 > JX_COUNTOF(ib->m_Buffer)) {
		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	ib->m_Buffer[ib->m_Size++] = b;
	return true;
}

static inline bool jx64_instrBuf_push16(jx_x64_instr_buffer_t* ib, uint16_t w)
{
	if (ib->m_Size + 2 > JX_COUNTOF(ib->m_Buffer)) {
		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	ib->m_Buffer[ib->m_Size++] = (uint8_t)((w & 0x00FF) >> 0);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((w & 0xFF00) >> 8);

	return true;
}

static inline bool jx64_instrBuf_push32(jx_x64_instr_buffer_t* ib, uint32_t dw)
{
	if (ib->m_Size + 4 > JX_COUNTOF(ib->m_Buffer)) {
		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	ib->m_Buffer[ib->m_Size++] = (uint8_t)((dw & 0x000000FFu) >> 0);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((dw & 0x0000FF00u) >> 8);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((dw & 0x00FF0000u) >> 16);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((dw & 0xFF000000u) >> 24);

	return true;
}

static inline bool jx64_instrBuf_push64(jx_x64_instr_buffer_t* ib, uint64_t qw)
{
	if (ib->m_Size + 8 > JX_COUNTOF(ib->m_Buffer)) {
		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0x00000000000000FFull) >> 0);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0x000000000000FF00ull) >> 8);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0x0000000000FF0000ull) >> 16);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0x00000000FF000000ull) >> 24);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0x000000FF00000000ull) >> 32);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0x0000FF0000000000ull) >> 40);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0x00FF000000000000ull) >> 48);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((qw & 0xFF00000000000000ull) >> 56);

	return true;
}

static inline bool jx64_instrBuf_push_n(jx_x64_instr_buffer_t* ib, const uint8_t* buffer, uint32_t n)
{
	if (ib->m_Size + n > JX_COUNTOF(ib->m_Buffer)) {
		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	jx_memcpy(&ib->m_Buffer[ib->m_Size], buffer, n);
	ib->m_Size += n;

	return true;
}

static inline void jx64_instrEnc_opcode1(jx_x64_instr_encoding_t* enc, uint8_t opcode)
{
	enc->m_OpcodeSize = 1;
	enc->m_Opcode[0] = opcode;
}

static inline void jx64_instrEnc_opcode2(jx_x64_instr_encoding_t* enc, uint8_t opcode0, uint8_t opcode1)
{
	enc->m_OpcodeSize = 2;
	enc->m_Opcode[0] = opcode0;
	enc->m_Opcode[1] = opcode1;
}

static inline void jx64_instrEnc_opcode3(jx_x64_instr_encoding_t* enc, uint8_t opcode0, uint8_t opcode1, uint8_t opcode2)
{
	enc->m_OpcodeSize = 3;
	enc->m_Opcode[0] = opcode0;
	enc->m_Opcode[1] = opcode1;
	enc->m_Opcode[2] = opcode2;
}

static inline void jx64_instrEnc_opcoden(jx_x64_instr_encoding_t* enc, const uint8_t* opcode, uint32_t n)
{
	if (n > 3) {
		JX_CHECK(false, "Invalid opcode");
		return;
	}

	enc->m_OpcodeSize = n;
	switch (n) {
	case 3:
		enc->m_Opcode[2] = opcode[2];
	case 2:
		enc->m_Opcode[1] = opcode[1];
	case 1:
		enc->m_Opcode[0] = opcode[0];
	default:
		break;
	}
}

static inline void jx64_instrEnc_modrm(jx_x64_instr_encoding_t* enc, uint8_t mod, uint8_t reg, uint8_t rm)
{
	enc->m_HasModRM = 1;
	enc->m_ModRM_Mod = mod;
	enc->m_ModRM_Reg = reg;
	enc->m_ModRM_RM = rm;
}

static inline void jx64_instrEnc_sib(jx_x64_instr_encoding_t* enc, bool hasSIB, uint8_t scale, uint8_t index, uint8_t base)
{
	enc->m_HasSIB = hasSIB ? 1 : 0;
	enc->m_SIB_Scale = scale;
	enc->m_SIB_Index = index;
	enc->m_SIB_Base = base;
}

static inline void jx64_instrEnc_rex(jx_x64_instr_encoding_t* enc, bool hasREX, uint8_t w, uint8_t r, uint8_t x, uint8_t b)
{
	enc->m_HasREX = hasREX ? 1 : 0;
	enc->m_REX_W = w;
	enc->m_REX_R = r;
	enc->m_REX_X = x;
	enc->m_REX_B = b;
}

static inline void jx64_instrEnc_disp(jx_x64_instr_encoding_t* enc, bool hasDisp, jx_x64_size dispSize, uint32_t disp)
{
	enc->m_HasDisp = hasDisp ? 1 : 0;
	enc->m_DispSize = dispSize;
	enc->m_Disp = disp;
}

static inline void jx64_instrEnc_imm(jx_x64_instr_encoding_t* enc, bool hasImm, jx_x64_size immSize, int64_t imm)
{
	enc->m_HasImm = hasImm ? 1 : 0;
	enc->m_ImmSize = immSize;
	enc->m_ImmI64 = imm;
}

static inline void jx64_instrEnc_segment(jx_x64_instr_encoding_t* enc, jx_x64_segment_prefix seg)
{
	enc->m_SegmentOverride = seg;
}

static inline void jx64_instrEnc_lock_rep(jx_x64_instr_encoding_t* enc, jx_x64_lock_repeat_prefix lockRepeat)
{
	enc->m_LockRepeat = lockRepeat;
}

static inline void jx64_instrEnc_addrSize(jx_x64_instr_encoding_t* enc, bool override)
{
	enc->m_AddressSizeOverride = override;
}

static inline void jx64_instrEnc_operandSize(jx_x64_instr_encoding_t* enc, bool override)
{
	enc->m_OperandSizeOverride = override;
}

static uint32_t jx64_instrEnc_calcInstrSize(const jx_x64_instr_encoding_t* encoding)
{
	const bool invalidEncoding = false
		|| encoding->m_OpcodeSize == 0
		;
	if (invalidEncoding) {
		return 0;
	}

	uint32_t sz = 0;
	sz += (encoding->m_SegmentOverride != JX64_SEGMENT_NONE) ? 1 : 0;
	sz += (encoding->m_LockRepeat != JX64_LOCK_REPEAT_NONE) ? 1 : 0;
	sz += (encoding->m_AddressSizeOverride != 0) ? 1 : 0;
	sz += (encoding->m_OperandSizeOverride != 0) ? 1 : 0;
	sz += encoding->m_HasREX ? 1 : 0;
	sz += encoding->m_OpcodeSize;
	sz += encoding->m_HasModRM ? 1 : 0;
	sz += encoding->m_HasSIB ? 1 : 0;
	sz += encoding->m_HasDisp ? (1u << encoding->m_DispSize) : 0;
	sz += encoding->m_HasImm ? (1u << encoding->m_ImmSize) : 0;

	return sz;
}

static uint32_t jx64_instrEnc_calcDispOffset(const jx_x64_instr_encoding_t* encoding)
{
	const bool invalidEncoding = false
		|| encoding->m_OpcodeSize == 0
		|| encoding->m_HasDisp == 0
		;
	if (invalidEncoding) {
		return UINT32_MAX;
	}

	uint32_t offset = 0;
	offset += (encoding->m_SegmentOverride != JX64_SEGMENT_NONE) ? 1 : 0;
	offset += (encoding->m_LockRepeat != JX64_LOCK_REPEAT_NONE) ? 1 : 0;
	offset += (encoding->m_AddressSizeOverride != 0) ? 1 : 0;
	offset += (encoding->m_OperandSizeOverride != 0) ? 1 : 0;
	offset += encoding->m_HasREX ? 1 : 0;
	offset += encoding->m_OpcodeSize;
	offset += encoding->m_HasModRM ? 1 : 0;
	offset += encoding->m_HasSIB ? 1 : 0;
	
	return offset;
}

static bool jx64_encodeInstr(jx_x64_instr_buffer_t* instr, const jx_x64_instr_encoding_t* encoding)
{
	const bool invalidEncoding = false
		|| encoding->m_OpcodeSize == 0
		;
	if (invalidEncoding) {
		return false;
	}

	if (encoding->m_SegmentOverride != JX64_SEGMENT_NONE) {
		JX_NOT_IMPLEMENTED();
	}

	if (encoding->m_LockRepeat != JX64_LOCK_REPEAT_NONE) {
		JX_NOT_IMPLEMENTED();
	}

	if (encoding->m_AddressSizeOverride) {
		jx64_instrBuf_push8(instr, JX64_ADDRESS_SIZE_PREFIX);
	}

	if (encoding->m_OperandSizeOverride) {
		jx64_instrBuf_push8(instr, JX64_OPERAND_SIZE_PREFIX);
	}

	if (encoding->m_HasREX) {
		jx64_instrBuf_push8(instr, JX64_REX(encoding->m_REX_W, encoding->m_REX_R, encoding->m_REX_X, encoding->m_REX_B));
	}

	jx64_instrBuf_push_n(instr, encoding->m_Opcode, encoding->m_OpcodeSize);

	if (encoding->m_HasModRM) {
		jx64_instrBuf_push8(instr, JX64_MODRM(encoding->m_ModRM_Mod, encoding->m_ModRM_Reg, encoding->m_ModRM_RM));
	}

	if (encoding->m_HasSIB) {
		jx64_instrBuf_push8(instr, JX64_SIB(encoding->m_SIB_Scale, encoding->m_SIB_Index, encoding->m_SIB_Base));
	}

	if (encoding->m_HasDisp) {
		const uint32_t sz = 1u << encoding->m_DispSize;
		jx64_instrBuf_push_n(instr, (const uint8_t*)&encoding->m_Disp, sz);
	}
	
	if (encoding->m_HasImm) {
		const uint32_t sz = 1u << encoding->m_ImmSize;
		jx64_instrBuf_push_n(instr, (const uint8_t*)&encoding->m_ImmI64, sz);
	}

	return instr->m_Size != 0;
}
