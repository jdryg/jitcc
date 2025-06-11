#ifndef JX_BITSET_H
#error "Must be included from jlib/bitset.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

static inline uint64_t jx_bitsetCalcBufferSize(uint32_t numBits)
{
	return sizeof(uint64_t) * ((numBits + 63) / 64);
}

static inline void jx_bitsetSetBit(jx_bitset_t* bs, uint32_t bit)
{
	JX_CHECK(bit < bs->m_NumBits, "Invalid bit index");
	bs->m_Bits[bit / 64] |= (1ull << (bit & 63));
}

static inline void jx_bitsetResetBit(jx_bitset_t* bs, uint32_t bit)
{
	JX_CHECK(bit < bs->m_NumBits, "Invalid bit index");
	bs->m_Bits[bit / 64] &= ~(1ull << (bit & 63));
}

static inline bool jx_bitsetIsBitSet(const jx_bitset_t* bs, uint32_t bit)
{
	JX_CHECK(bit < bs->m_NumBits, "Invalid bit index");
	return (bs->m_Bits[bit / 64] & (1ull << (bit & 63))) != 0;
}

#ifdef __cplusplus
}
#endif
