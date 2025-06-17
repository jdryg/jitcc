#include <jlib/allocator.h>
#include <jlib/bitset.h>
#include <jlib/dbg.h>
#include <jlib/math.h>
#include <jlib/memory.h>

jx_bitset_t* jx_bitsetCreate(uint32_t numBits, jx_allocator_i* allocator)
{
	jx_bitset_t* bs = (jx_bitset_t*)JX_ALLOC(allocator, sizeof(jx_bitset_t));
	if (!bs) {
		return NULL;
	}

	jx_memset(bs, 0, sizeof(jx_bitset_t));

	const uint64_t bufferSize = jx_bitsetCalcBufferSize(jx_max_u32(numBits, 1));
	bs->m_Bits = (uint64_t*)JX_ALLOC(allocator, bufferSize);
	if (!bs->m_Bits) {
		JX_FREE(allocator, bs);
		return NULL;
	}

	jx_memset(bs->m_Bits, 0, bufferSize);
	bs->m_NumBits = numBits;
	bs->m_BitCapacity = (uint32_t)(bufferSize * 8);

	return bs;
}

void jx_bitsetDestroy(jx_bitset_t* bs, jx_allocator_i* allocator)
{
	JX_FREE(allocator, bs->m_Bits);
	JX_FREE(allocator, bs);
}

void jx_bitsetFree(jx_bitset_t* bs, jx_allocator_i* allocator)
{
	JX_FREE(allocator, bs->m_Bits);
	jx_memset(bs, 0, sizeof(jx_bitset_t));
}

bool jx_bitsetResize(jx_bitset_t* bs, uint32_t numBits, jx_allocator_i* allocator)
{
	if (numBits > bs->m_BitCapacity) {
		const uint64_t bufferSize = jx_bitsetCalcBufferSize(numBits);
		uint8_t* newBits = (uint8_t*)JX_ALLOC(allocator, bufferSize);
		if (!newBits) {
			return false;
		}

		const uint32_t oldCapacity = bs->m_BitCapacity / 8;
		jx_memcpy(&newBits[0], bs->m_Bits, oldCapacity);
		jx_memset(&newBits[oldCapacity], 0, bufferSize - oldCapacity);

		JX_FREE(allocator, bs->m_Bits);
		bs->m_Bits = (uint64_t*)newBits;
		bs->m_BitCapacity = (uint32_t)(bufferSize * 8);
	}

	bs->m_NumBits = numBits;

	return true;
}

void jx_bitsetUnion(jx_bitset_t* dst, const jx_bitset_t* src)
{
	JX_CHECK(dst->m_NumBits == src->m_NumBits, "Can only perform bitset operation on identically sized sets.");

	const uint32_t numBits = dst->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		dst->m_Bits[iWord] |= src->m_Bits[iWord];
	}
}

void jx_bitsetIntersection(jx_bitset_t* dst, const jx_bitset_t* src)
{
	JX_CHECK(dst->m_NumBits == src->m_NumBits, "Can only perform bitset operation on identically sized sets.");

	const uint32_t numBits = dst->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		dst->m_Bits[iWord] &= src->m_Bits[iWord];
	}
}

void jx_bitsetCopy(jx_bitset_t* dst, const jx_bitset_t* src)
{
	JX_CHECK(dst->m_NumBits == src->m_NumBits, "Can only perform bitset operation on identically sized sets.");

	const uint32_t numBits = dst->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
#if 1
	uint64_t* dstPtr = dst->m_Bits;
	const uint64_t* srcPtr = src->m_Bits;
	uint32_t remaining = numWords;
	while (remaining >= 4) {
		*dstPtr++ = *srcPtr++;
		*dstPtr++ = *srcPtr++;
		*dstPtr++ = *srcPtr++;
		*dstPtr++ = *srcPtr++;
		remaining -= 4;
	}
	switch (remaining) {
	case 3:
		*dstPtr++ = *srcPtr++;
	case 2:
		*dstPtr++ = *srcPtr++;
	case 1:
		*dstPtr++ = *srcPtr++;
		break;
	}
#else
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		dst->m_Bits[iWord] = src->m_Bits[iWord];
	}
#endif
}

void jx_bitsetSub(jx_bitset_t* dst, const jx_bitset_t* src)
{
	JX_CHECK(dst->m_NumBits == src->m_NumBits, "Can only perform bitset operation on identically sized sets.");

	jx_bitset_iterator_t iter;
	jx_bitsetIterBegin(dst, &iter, 0);

	uint32_t nextSetBit = jx_bitsetIterNext(dst, &iter);
	while (nextSetBit != UINT32_MAX) {
		if (jx_bitsetIsBitSet(src, nextSetBit)) {
			jx_bitsetResetBit(dst, nextSetBit);
		}

		nextSetBit = jx_bitsetIterNext(dst, &iter);
	}
}

bool jx_bitsetEqual(const jx_bitset_t* a, const jx_bitset_t* b)
{
	JX_CHECK(a->m_NumBits == b->m_NumBits, "Can only perform bitset operation on identically sized sets.");

	const uint32_t numBits = a->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	for (uint32_t iWord = 0; iWord < numWords; ++iWord) {
		if (a->m_Bits[iWord] != b->m_Bits[iWord]) {
			return false;
		}
	}

	return true;
}

void jx_bitsetClear(jx_bitset_t* bs)
{
	const uint32_t numBits = bs->m_NumBits;
	const uint32_t numWords = (numBits + 63) / 64;
	jx_memset(bs->m_Bits, 0, sizeof(uint64_t) * numWords);
}

void jx_bitsetIterBegin(const jx_bitset_t* bs, jx_bitset_iterator_t* iter, uint32_t firstBit)
{
	JX_CHECK(firstBit < bs->m_NumBits, "Invalid bit index");
	const uint32_t wordID = firstBit / 64;
	const uint32_t bitID = firstBit & 63;
	iter->m_WordID = wordID;

	const uint64_t word = bs->m_Bits[wordID];
	iter->m_Bits = word & (~((1ull << bitID) - 1));
}

uint32_t jx_bitsetIterNext(const jx_bitset_t* bs, jx_bitset_iterator_t* iter)
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
