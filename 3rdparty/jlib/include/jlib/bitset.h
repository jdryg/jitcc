#ifndef JX_BITSET_H
#define JX_BITSET_H

#include <stdint.h>
#include <jlib/dbg.h>
#include <jlib/macros.h> // JX_PAD()

#ifdef __cpluscplus
extern "C" {
#endif

typedef struct jx_allocator_i jx_allocator_i;

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
	uint32_t m_BitCapacity;
} jx_bitset_t;

jx_bitset_t* jx_bitsetCreate(uint32_t numBits, jx_allocator_i* allocator);
void jx_bitsetDestroy(jx_bitset_t* bs, jx_allocator_i* allocator);
bool jx_bitsetResize(jx_bitset_t* bs, uint32_t numBits, jx_allocator_i* allocator);
uint64_t jx_bitsetCalcBufferSize(uint32_t numBits);
void jx_bitsetSetBit(jx_bitset_t* bs, uint32_t bit);
void jx_bitsetResetBit(jx_bitset_t* bs, uint32_t bit);
bool jx_bitsetIsBitSet(const jx_bitset_t* bs, uint32_t bit);
bool jx_bitsetUnion(jx_bitset_t* dst, const jx_bitset_t* src);         // dst = dst | src
bool jx_bitsetIntersection(jx_bitset_t* dst, const jx_bitset_t* src);  // dst = dst & src
bool jx_bitsetCopy(jx_bitset_t* dst, const jx_bitset_t* src);          // dst = src
bool jx_bitsetSub(jx_bitset_t* dst, const jx_bitset_t* src);           // dst = dst - src
bool jx_bitsetEqual(const jx_bitset_t* a, const jx_bitset_t* b);       // dst == src
void jx_bitsetClear(jx_bitset_t* bs);
void jx_bitsetIterBegin(const jx_bitset_t* bs, jx_bitset_iterator_t* iter, uint32_t firstBit);
uint32_t jx_bitsetIterNext(const jx_bitset_t* bs, jx_bitset_iterator_t* iter);

#ifdef __cpluscplus
}
#endif

#include "inline/bitset.inl"

#endif // JX_BITSET_H
