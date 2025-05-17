#include <stdint.h>

#define bool _Bool
#define false 0
#define true 1

#define JX_COUNTOF(arr) (sizeof(arr) / sizeof((arr)[0]))

#define JX64_REX(W, R, X, B)         (0x40 | ((W) << 3) | ((R) << 2) | ((X) << 1) | ((B) << 0))
#define JX64_MODRM(mod, reg, rm)     (((mod) & 0b11) << 6) | (((reg) & 0b111) << 3) | (((rm) & 0b111) << 0)
#define JX64_SIB(scale, index, base) (((scale) & 0b11) << 6) | (((index) & 0b111) << 3) | (((base) & 0b111) << 0)

#define JX64_OPERAND_SIZE_PREFIX 0x66
#define JX64_ADDRESS_SIZE_PREFIX 0x67

void* memcpy(void* dest, const void* src, uint64_t sz);
int printf(const char* str, ...);

typedef struct jx_x64_instr_encoding_t
{
	uint8_t m_SegmentOverride : 3; // jx_x64_segment_prefix
	uint8_t m_LockRepeat : 2; // jx_x64_lock_repeat_prefix
	uint8_t m_OperandSizeOverride : 1;
	uint8_t m_AddressSizeOverride : 1;
	uint8_t : 1;

	uint32_t m_HasREX : 1;
	uint32_t m_HasModRM : 1;
	uint32_t m_HasSIB : 1;
	uint32_t m_HasDisp : 1;
	uint32_t m_HasImm : 1;
	uint32_t m_OpcodeSize : 2; // 0 - 3, valid values are 1, 2, 3
	uint32_t : 1;

	uint8_t m_REX_W : 1;
	uint8_t m_REX_R : 1;
	uint8_t m_REX_X : 1;
	uint8_t m_REX_B : 1;
	uint8_t m_DispSize : 2; // jx_x64_size
	uint8_t m_ImmSize : 2; // jx_x64_size

	uint8_t m_ModRM_Mod : 2;
	uint8_t m_ModRM_Reg : 3;
	uint8_t m_ModRM_RM : 3;

	uint8_t m_SIB_Scale : 2;
	uint8_t m_SIB_Index : 3;
	uint8_t m_SIB_Base : 3;

	uint8_t m_Opcode[3];

	int32_t m_Disp;
	uint8_t _padding[4];

	int64_t m_ImmI64;
} jx_x64_instr_encoding_t;

typedef struct jx_x64_instr_buffer_t
{
	uint8_t m_Buffer[16];
	uint32_t m_Size;
} jx_x64_instr_buffer_t;

typedef enum jx_x64_segment_prefix
{
	JX64_SEGMENT_NONE = 0,
	JX64_SEGMENT_STACK = 1, // SS, 0x2E, Pointer to 0x0; unused.
	JX64_SEGMENT_CODE = 2, // CS, 0x36, Pointer to 0x0; unused.
	JX64_SEGMENT_DATA = 3, // DS, 0x3E, Pointer to 0x0; unused.
	JX64_SEGMENT_EXTRA = 4, // ES, 0x26, Pointer to 0x0; unused.
	JX64_SEGMENT_F = 5, // FS, 0x64, Pointer to thread-local process data.
	JX64_SEGMENT_G = 6, // GS, 0x65, Pointer to thread-local process data.
} jx_x64_segment_prefix;

typedef enum jx_x64_lock_repeat_prefix
{
	JX64_LOCK_REPEAT_NONE = 0,
	JX64_LOCK_REPEAT_LOCK = 1, // LOCK, 0xF0, 
	JX64_LOCK_REPEAT_REP = 2, // REP, 0xF2
	JX64_LOCK_REPEAT_REPNZ = 3, // REPNX, 0xF3
} jx_x64_lock_repeat_prefix;

typedef enum jx_x64_size
{
	JX64_SIZE_8 = 0,
	JX64_SIZE_16 = 1,
	JX64_SIZE_32 = 2,
	JX64_SIZE_64 = 3,
} jx_x64_size;

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
//		JX_CHECK(false, "Invalid opcode");
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

static inline void jx64_instrEnc_disp(jx_x64_instr_encoding_t* enc, bool hasDisp, jx_x64_size dispSize, int32_t disp)
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

uint32_t jx64_instrEnc_calcInstrSize(const jx_x64_instr_encoding_t* encoding)
{
	const bool invalidEncoding = false
		|| encoding->m_OpcodeSize == 0
		;
	if (invalidEncoding) {
		return 0u;
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

static inline bool jx64_instrBuf_push8(jx_x64_instr_buffer_t* ib, uint8_t b)
{
	if (ib->m_Size + 1 > JX_COUNTOF(ib->m_Buffer)) {
//		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	ib->m_Buffer[ib->m_Size++] = b;
	return true;
}

static inline bool jx64_instrBuf_push16(jx_x64_instr_buffer_t* ib, uint16_t w)
{
	if (ib->m_Size + 2 > JX_COUNTOF(ib->m_Buffer)) {
//		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	ib->m_Buffer[ib->m_Size++] = (uint8_t)((w & 0x00FF) >> 0);
	ib->m_Buffer[ib->m_Size++] = (uint8_t)((w & 0xFF00) >> 8);

	return true;
}

static inline bool jx64_instrBuf_push32(jx_x64_instr_buffer_t* ib, uint32_t dw)
{
	if (ib->m_Size + 4 > JX_COUNTOF(ib->m_Buffer)) {
//		JX_CHECK(false, "Too many instruction bytes!");
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
//		JX_CHECK(false, "Too many instruction bytes!");
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
//		JX_CHECK(false, "Too many instruction bytes!");
		return false;
	}

	memcpy(&ib->m_Buffer[ib->m_Size], buffer, n);
	ib->m_Size += n;

	return true;
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
//		JX_NOT_IMPLEMENTED();
	}

	if (encoding->m_LockRepeat != JX64_LOCK_REPEAT_NONE) {
//		JX_NOT_IMPLEMENTED();
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

int main(void)
{
	jx_x64_instr_encoding_t* enc = &(jx_x64_instr_encoding_t){ 0 };

#if 1
	jx64_instrEnc_rex(enc, true, 1, 0, 0, 0); // TODO: Too many args
#else
	enc->m_HasREX = 1;
	enc->m_REX_W = 1;
	enc->m_REX_R = 0;
	enc->m_REX_X = 0;
	enc->m_REX_B = 0;
#endif
	jx64_instrEnc_opcode1(enc, 0xC7);
	jx64_instrEnc_modrm(enc, 0b11, 0b000, 0b000);
	jx64_instrEnc_imm(enc, true, JX64_SIZE_32, 0x12345678);

	if (jx64_instrEnc_calcInstrSize(enc) != 7) {
		return 1;
	}

	jx_x64_instr_buffer_t* instr = &(jx_x64_instr_buffer_t){ 0 };
	jx64_encodeInstr(instr, enc);

	for (uint32_t i = 0; i < instr->m_Size; ++i) {
		printf("%02X ", instr->m_Buffer[i]);
	}
	printf("\n");

	if (instr->m_Size != 7) {
		return 2;
	}

	if (instr->m_Buffer[0] != 0x48 ||
		instr->m_Buffer[1] != 0xC7 ||
		instr->m_Buffer[2] != 0xC0 ||
		instr->m_Buffer[3] != 0x78 ||
		instr->m_Buffer[4] != 0x56 ||
		instr->m_Buffer[5] != 0x34 ||
		instr->m_Buffer[6] != 0x12) {
		return 3;
	}

	return 0;
}
