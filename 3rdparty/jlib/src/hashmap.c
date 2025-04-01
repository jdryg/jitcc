// Original code is:
// Copyright 2020 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.
//
// Adapted to support allocators and to match jlib's coding style.
#include <jlib/hashmap.h>
#include <jlib/allocator.h>
#include <jlib/memory.h>
#include <jlib/math.h>

typedef struct _jx_hash_bucket
{
	uint64_t hash : 48;
	uint64_t dib : 16;
} _jx_hash_bucket;

// hashmap is an open addressed hash map using robinhood hashing.
typedef struct jx_hashmap_t
{
	jx_allocator_i* m_Allocator;
	jxHashCallback m_HashFunc;
	jxCompareCallback m_CompareFunc;
	jxItemFreeCallback m_ItemFreeFunc;
	uint8_t* m_Buckets;
	uint8_t* m_SpareItem;
	uint8_t* m_EntryData;
	void* m_UserData;
	uint64_t m_Seed0;
	uint64_t m_Seed1;
	uint32_t m_ItemSize;
	uint32_t m_Capacity;
	uint32_t m_BucketSize;
	uint32_t m_NumBuckets;
	uint32_t m_NumItems;
	uint32_t m_Mask;
	uint32_t m_GrowAt;
	uint32_t m_ShrinkAt;
} jx_hashmap_t;

static inline _jx_hash_bucket* _jx_hashmapBucketAt(struct jx_hashmap_t* map, uint32_t index);
static inline void* _jx_hashmapBucketItem(_jx_hash_bucket* entry);
static inline uint64_t _jx_hashmapGetHash(jx_hashmap_t* map, const void* key);
static void _jx_hashmapFreeItems(jx_hashmap_t* map);
static bool _jx_hashmapResize(jx_hashmap_t* map, uint32_t new_cap);
static uint64_t SIP64(const uint8_t* in, const size_t inlen, uint64_t seed0, uint64_t seed1);
static void MM86128(const void* key, const size_t len, uint32_t seed, void* out);

jx_hashmap_t* jx_hashmapCreate(jx_allocator_i* allocator, uint32_t elsize, uint32_t cap, uint64_t seed0, uint64_t seed1, jxHashCallback hash, jxCompareCallback compare, jxItemFreeCallback elfree, void* udata)
{
	// 'cap' should be a power of 2 greater than or equal to 16
	cap = jx_max_u32(jx_nextPowerOf2_u32(cap), 16);

	// Make sure 'bucketsz' is a multiply of void*
	uint32_t bucketsz = (uint32_t)(sizeof(_jx_hash_bucket) + elsize);
	while (bucketsz & (sizeof(uintptr_t) - 1)) {
		bucketsz++;
	}

	// hashmap + spare + edata
	uint32_t size = sizeof(jx_hashmap_t) + bucketsz * 2;
	jx_hashmap_t* map = JX_ALLOC(allocator, size);
	if (!map) {
		return NULL;
	}

	jx_memset(map, 0, sizeof(jx_hashmap_t));
	map->m_Allocator = allocator;
	map->m_ItemSize = elsize;
	map->m_BucketSize = bucketsz;
	map->m_Seed0 = seed0;
	map->m_Seed1 = seed1;
	map->m_HashFunc = hash;
	map->m_CompareFunc = compare;
	map->m_ItemFreeFunc = elfree;
	map->m_UserData = udata;
	map->m_SpareItem = ((uint8_t*)map) + sizeof(jx_hashmap_t);
	map->m_EntryData = (uint8_t*)map->m_SpareItem + bucketsz;
	map->m_Capacity = cap;
	map->m_NumBuckets = cap;
	map->m_Mask = map->m_NumBuckets - 1;
	map->m_Buckets = (uint8_t*)JX_ALLOC(allocator, map->m_BucketSize * map->m_NumBuckets);
	if (!map->m_Buckets) {
		JX_FREE(allocator, map);
		return NULL;
	}
	jx_memset(map->m_Buckets, 0, map->m_BucketSize * map->m_NumBuckets);
	map->m_GrowAt = (map->m_NumBuckets * 3) >> 2;
	map->m_ShrinkAt = map->m_NumBuckets / 10;
	return map;
}

void jx_hashmapClear(jx_hashmap_t* map, bool update_cap)
{
	map->m_NumItems = 0;
	_jx_hashmapFreeItems(map);

	if (update_cap) {
		map->m_Capacity = map->m_NumBuckets;
	} else if (map->m_NumBuckets != map->m_Capacity) {
		uint8_t* new_buckets = (uint8_t*)JX_ALLOC(map->m_Allocator, map->m_BucketSize * map->m_Capacity);
		if (new_buckets) {
			JX_FREE(map->m_Allocator, map->m_Buckets);
			map->m_Buckets = new_buckets;
			map->m_NumBuckets = map->m_Capacity;
		}
	}

	jx_memset(map->m_Buckets, 0, map->m_BucketSize * map->m_NumBuckets);

	map->m_Mask = map->m_NumBuckets - 1;
	map->m_GrowAt = (map->m_NumBuckets * 3) >> 2;
	map->m_ShrinkAt = map->m_NumBuckets / 10;
}

void* jx_hashmapSet(jx_hashmap_t* map, const void* item)
{
	if (map->m_NumItems == map->m_GrowAt) {
		if (!_jx_hashmapResize(map, map->m_NumBuckets * 2)) {
			return NULL;
		}
	}

	_jx_hash_bucket* entry = (_jx_hash_bucket*)map->m_EntryData;
	entry->hash = _jx_hashmapGetHash(map, item);
	entry->dib = 1;
	jx_memcpy(_jx_hashmapBucketItem(entry), item, map->m_ItemSize);

	uint32_t i = (uint32_t)(entry->hash & (uint64_t)map->m_Mask);
	for (;;) {
		_jx_hash_bucket* bucket = _jx_hashmapBucketAt(map, i);
		if (bucket->dib == 0) {
			jx_memcpy(bucket, entry, map->m_BucketSize);
			++map->m_NumItems;
			return NULL;
		}

		if (entry->hash == bucket->hash && map->m_CompareFunc(_jx_hashmapBucketItem(entry), _jx_hashmapBucketItem(bucket), map->m_UserData) == 0) {
			jx_memcpy(map->m_SpareItem, _jx_hashmapBucketItem(bucket), map->m_ItemSize);
			jx_memcpy(_jx_hashmapBucketItem(bucket), _jx_hashmapBucketItem(entry), map->m_ItemSize);
			return map->m_SpareItem;
		}

		if (bucket->dib < entry->dib) {
			jx_memcpy(map->m_SpareItem, bucket, map->m_BucketSize);
			jx_memcpy(bucket, entry, map->m_BucketSize);
			jx_memcpy(entry, map->m_SpareItem, map->m_BucketSize);
		}

		i = (uint32_t)(((uint64_t)i + 1) & (uint64_t)map->m_Mask);
		entry->dib += 1;
	}
}

void* jx_hashmapGet(jx_hashmap_t* map, const void* key)
{
	uint64_t hash = _jx_hashmapGetHash(map, key);
	uint32_t i = (uint32_t)(hash & (uint64_t)map->m_Mask);
	for (;;) {
		_jx_hash_bucket* bucket = _jx_hashmapBucketAt(map, i);
		if (!bucket->dib) {
			return NULL;
		}

		if (bucket->hash == hash && map->m_CompareFunc(key, _jx_hashmapBucketItem(bucket), map->m_UserData) == 0) {
			return _jx_hashmapBucketItem(bucket);
		}

		i = (uint32_t)(((uint64_t)i + 1) & (uint64_t)map->m_Mask);
	}
}

void* jx_hashmapDelete(jx_hashmap_t* map, void* key)
{
	uint64_t hash = _jx_hashmapGetHash(map, key);
	uint32_t i = (uint32_t)(hash & (uint64_t)map->m_Mask);
	for (;;) {
		_jx_hash_bucket* bucket = _jx_hashmapBucketAt(map, i);
		if (!bucket->dib) {
			return NULL;
		}

		if (bucket->hash == hash && map->m_CompareFunc(key, _jx_hashmapBucketItem(bucket), map->m_UserData) == 0) {
			jx_memcpy(map->m_SpareItem, _jx_hashmapBucketItem(bucket), map->m_ItemSize);

			bucket->dib = 0;
			for (;;) {
				_jx_hash_bucket* prev = bucket;
				i = (uint32_t)(((uint64_t)i + 1) & (uint64_t)map->m_Mask);
				bucket = _jx_hashmapBucketAt(map, i);
				if (bucket->dib <= 1) {
					prev->dib = 0;
					break;
				}
				jx_memcpy(prev, bucket, map->m_BucketSize);
				prev->dib--;
			}

			map->m_NumItems--;
			if (map->m_NumBuckets > map->m_Capacity && map->m_NumItems <= map->m_ShrinkAt) {
				// Ignore the return value. It's ok for the _jx_hashmapResize operation to
				// fail to allocate enough memory because a shrink operation
				// does not change the integrity of the data.
				_jx_hashmapResize(map, map->m_NumBuckets / 2);
			}

			return map->m_SpareItem;
		}

		i = (uint32_t)(((uint64_t)i + 1) & (uint64_t)map->m_Mask);
	}
}

uint32_t jx_hashmapCount(jx_hashmap_t* map)
{
	return map->m_NumItems;
}

void jx_hashmapDestroy(jx_hashmap_t* map)
{
	if (!map) {
		return;
	}

	_jx_hashmapFreeItems(map);
	JX_FREE(map->m_Allocator, map->m_Buckets);
	JX_FREE(map->m_Allocator, map);
}

bool jx_hashmapScan(jx_hashmap_t* map, jxIterCallback iter, void* udata)
{
	for (uint32_t i = 0; i < map->m_NumBuckets; ++i) {
		_jx_hash_bucket* bucket = _jx_hashmapBucketAt(map, i);
		if (bucket->dib) {
			if (!iter(_jx_hashmapBucketItem(bucket), udata)) {
				return false;
			}
		}
	}

	return true;
}

// hashmap_iter iterates one key at a time yielding a reference to an
// entry at each iteration. Useful to write simple loops and avoid writing
// dedicated callbacks and udata structures, as in hashmap_scan.
//
// map is a hash map handle. i is a pointer to a size_t cursor that
// should be initialized to 0 at the beginning of the loop. item is a void
// pointer pointer that is populated with the retrieved item. Note that this
// is NOT a copy of the item stored in the hash map and can be directly
// modified.
//
// Note that if hashmap_delete() is called on the hashmap being iterated,
// the buckets are rearranged and the iterator must be reset to 0, otherwise
// unexpected results may be returned after deletion.
//
// This function has not been tested for thread safety.
//
// The function returns true if an item was retrieved; false if the end of the
// iteration has been reached.
bool jx_hashmapIter(jx_hashmap_t* map, uint32_t* i, void** item)
{
	_jx_hash_bucket* bucket;

	do {
		if (*i >= map->m_NumBuckets) {
			return false;
		}

		bucket = _jx_hashmapBucketAt(map, *i);
		(*i)++;
	} while (!bucket->dib);

	*item = _jx_hashmapBucketItem(bucket);

	return true;
}

// SipHash-2-4.
uint64_t jx_hashSip(const void* data, size_t len, uint64_t seed0, uint64_t seed1)
{
	return SIP64((uint8_t*)data, len, seed0, seed1);
}

// Murmur3_86_128.
uint64_t jx_hashMurmur3(const void* data, size_t len, uint64_t seed0, uint64_t seed1)
{
	JX_UNUSED(seed1);
	char out[16];
	MM86128(data, len, (uint32_t)seed0, &out);
	return *(uint64_t*)out;
}

// FNV1a 64-bit
// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
#define FNV1A_OFFSET_64 0xcbf29ce484222325ull
#define FNV1A_PRIME_64  0x00000100000001B3ull

uint64_t jx_hashFNV1a(const void* data, size_t len, uint64_t seed0, uint64_t seed1)
{
	JX_UNUSED(seed1);

	const uint8_t* src = (const uint8_t*)data;

	uint64_t hash = seed0;
	while (len-- > 0) {
		hash ^= (uint64_t)*src++;
		hash *= FNV1A_PRIME_64;
	}

	return hash;
}

uint64_t jx_hashFNV1a_cstr(const void* data, size_t len, uint64_t seed0, uint64_t seed1)
{
	JX_UNUSED(len, seed1);

	const uint8_t* src = (const uint8_t*)data;
	
	uint64_t hash = seed0;
	while (*src != 0 && len-- > 0) {
		hash ^= (uint64_t)*src++;
		hash *= FNV1A_PRIME_64;
	}

	return hash;
}

static inline _jx_hash_bucket* _jx_hashmapBucketAt(struct jx_hashmap_t* map, uint32_t index)
{
	return (_jx_hash_bucket*)(map->m_Buckets + (map->m_BucketSize * index));
}

static inline void* _jx_hashmapBucketItem(_jx_hash_bucket* entry)
{
	return ((uint8_t*)entry) + sizeof(_jx_hash_bucket);
}

static inline uint64_t _jx_hashmapGetHash(jx_hashmap_t* map, const void* key)
{
	return map->m_HashFunc(key, map->m_Seed0, map->m_Seed1, map->m_UserData) << 16 >> 16;
}

static void _jx_hashmapFreeItems(jx_hashmap_t* map)
{
	if (!map->m_ItemFreeFunc) {
		return;
	}

	for (uint32_t i = 0; i < map->m_NumBuckets; ++i) {
		_jx_hash_bucket* bucket = _jx_hashmapBucketAt(map, i);
		if (bucket->dib) {
			map->m_ItemFreeFunc(_jx_hashmapBucketItem(bucket), map->m_UserData);
		}
	}
}

static bool _jx_hashmapResize(jx_hashmap_t* map, uint32_t new_cap)
{
	jx_hashmap_t* map2 = jx_hashmapCreate(map->m_Allocator, map->m_ItemSize, new_cap, map->m_Seed0, map->m_Seed1, map->m_HashFunc, map->m_CompareFunc, map->m_ItemFreeFunc, map->m_UserData);
	if (!map2) {
		return false;
	}

	for (uint32_t i = 0; i < map->m_NumBuckets; ++i) {
		_jx_hash_bucket* entry = _jx_hashmapBucketAt(map, i);
		if (!entry->dib) {
			continue;
		}

		entry->dib = 1;
		uint32_t j = (uint32_t)(entry->hash & (uint64_t)map2->m_Mask);
		for (;;) {
			_jx_hash_bucket* bucket = _jx_hashmapBucketAt(map2, j);
			if (bucket->dib == 0) {
				jx_memcpy(bucket, entry, map->m_BucketSize);
				break;
			}

			if (bucket->dib < entry->dib) {
				jx_memcpy(map2->m_SpareItem, bucket, map->m_BucketSize);
				jx_memcpy(bucket, entry, map->m_BucketSize);
				jx_memcpy(entry, map2->m_SpareItem, map->m_BucketSize);
			}

			j = (uint32_t)(((uint64_t)j + 1) & (uint64_t)map2->m_Mask);
			entry->dib += 1;
		}
	}

	JX_FREE(map->m_Allocator, map->m_Buckets);
	map->m_Buckets = map2->m_Buckets;
	map->m_NumBuckets = map2->m_NumBuckets;
	map->m_Mask = map2->m_Mask;
	map->m_GrowAt = map2->m_GrowAt;
	map->m_ShrinkAt = map2->m_ShrinkAt;
	JX_FREE(map->m_Allocator, map2);

	return true;
}

//-----------------------------------------------------------------------------
// SipHash reference C implementation
//
// Copyright (c) 2012-2016 Jean-Philippe Aumasson
// <jeanphilippe.aumasson@gmail.com>
// Copyright (c) 2012-2014 Daniel J. Bernstein <djb@cr.yp.to>
//
// To the extent possible under law, the author(s) have dedicated all copyright
// and related and neighboring rights to this software to the public domain
// worldwide. This software is distributed without any warranty.
//
// You should have received a copy of the CC0 Public Domain Dedication along
// with this software. If not, see
// <http://creativecommons.org/publicdomain/zero/1.0/>.
//
// default: SipHash-2-4
//-----------------------------------------------------------------------------
static uint64_t SIP64(const uint8_t* in, const size_t inlen, uint64_t seed0, uint64_t seed1)
{
#define U8TO64_LE(p) \
	{  (((uint64_t)((p)[0])) | ((uint64_t)((p)[1]) << 8) | \
		((uint64_t)((p)[2]) << 16) | ((uint64_t)((p)[3]) << 24) | \
		((uint64_t)((p)[4]) << 32) | ((uint64_t)((p)[5]) << 40) | \
		((uint64_t)((p)[6]) << 48) | ((uint64_t)((p)[7]) << 56)) }
#define U64TO8_LE(p, v) \
	{ U32TO8_LE((p), (uint32_t)((v))); \
	  U32TO8_LE((p) + 4, (uint32_t)((v) >> 32)); }
#define U32TO8_LE(p, v) \
	{ (p)[0] = (uint8_t)((v)); \
	  (p)[1] = (uint8_t)((v) >> 8); \
	  (p)[2] = (uint8_t)((v) >> 16); \
	  (p)[3] = (uint8_t)((v) >> 24); }
#define ROTL(x, b) (uint64_t)(((x) << (b)) | ((x) >> (64 - (b))))
#define SIPROUND \
	{ v0 += v1; v1 = ROTL(v1, 13); \
	  v1 ^= v0; v0 = ROTL(v0, 32); \
	  v2 += v3; v3 = ROTL(v3, 16); \
	  v3 ^= v2; \
	  v0 += v3; v3 = ROTL(v3, 21); \
	  v3 ^= v0; \
	  v2 += v1; v1 = ROTL(v1, 17); \
	  v1 ^= v2; v2 = ROTL(v2, 32); }

	uint64_t k0 = U8TO64_LE((uint8_t*)&seed0);
	uint64_t k1 = U8TO64_LE((uint8_t*)&seed1);
	uint64_t v3 = UINT64_C(0x7465646279746573) ^ k1;
	uint64_t v2 = UINT64_C(0x6c7967656e657261) ^ k0;
	uint64_t v1 = UINT64_C(0x646f72616e646f6d) ^ k1;
	uint64_t v0 = UINT64_C(0x736f6d6570736575) ^ k0;
	const uint8_t* end = in + inlen - (inlen % sizeof(uint64_t));
	for (; in != end; in += 8) {
		uint64_t m = U8TO64_LE(in);
		v3 ^= m;
		SIPROUND; SIPROUND;
		v0 ^= m;
	}
	const int left = inlen & 7;
	uint64_t b = ((uint64_t)inlen) << 56;
	switch (left) {
	case 7: b |= ((uint64_t)in[6]) << 48;
	case 6: b |= ((uint64_t)in[5]) << 40;
	case 5: b |= ((uint64_t)in[4]) << 32;
	case 4: b |= ((uint64_t)in[3]) << 24;
	case 3: b |= ((uint64_t)in[2]) << 16;
	case 2: b |= ((uint64_t)in[1]) << 8;
	case 1: b |= ((uint64_t)in[0]); break;
	case 0: break;
	}
	v3 ^= b;
	SIPROUND; SIPROUND;
	v0 ^= b;
	v2 ^= 0xff;
	SIPROUND; SIPROUND; SIPROUND; SIPROUND;
	b = v0 ^ v1 ^ v2 ^ v3;
	uint64_t out = 0;
	U64TO8_LE((uint8_t*)&out, b);
	return out;
}

//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.
//
// Murmur3_86_128
//-----------------------------------------------------------------------------
static void MM86128(const void* key, const size_t len, uint32_t seed, void* out)
{
#define	ROTL32(x, r) ((x << r) | (x >> (32 - r)))
#define FMIX32(h) h^=h>>16; h*=0x85ebca6b; h^=h>>13; h*=0xc2b2ae35; h^=h>>16;

	const uint8_t* data = (const uint8_t*)key;
	const size_t nblocks = len / 16;
	uint32_t h1 = seed;
	uint32_t h2 = seed;
	uint32_t h3 = seed;
	uint32_t h4 = seed;
	uint32_t c1 = 0x239b961b;
	uint32_t c2 = 0xab0e9789;
	uint32_t c3 = 0x38b34ae5;
	uint32_t c4 = 0xa1e38b93;
	const uint32_t* blocks = (const uint32_t*)(data + nblocks * 16);
	for (int32_t i = -(int32_t)nblocks; i; i++) {
		uint32_t k1 = blocks[i * 4 + 0];
		uint32_t k2 = blocks[i * 4 + 1];
		uint32_t k3 = blocks[i * 4 + 2];
		uint32_t k4 = blocks[i * 4 + 3];
		k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
		h1 = ROTL32(h1, 19); h1 += h2; h1 = h1 * 5 + 0x561ccd1b;
		k2 *= c2; k2 = ROTL32(k2, 16); k2 *= c3; h2 ^= k2;
		h2 = ROTL32(h2, 17); h2 += h3; h2 = h2 * 5 + 0x0bcaa747;
		k3 *= c3; k3 = ROTL32(k3, 17); k3 *= c4; h3 ^= k3;
		h3 = ROTL32(h3, 15); h3 += h4; h3 = h3 * 5 + 0x96cd1c35;
		k4 *= c4; k4 = ROTL32(k4, 18); k4 *= c1; h4 ^= k4;
		h4 = ROTL32(h4, 13); h4 += h1; h4 = h4 * 5 + 0x32ac3b17;
	}
	const uint8_t* tail = (const uint8_t*)(data + nblocks * 16);
	uint32_t k1 = 0;
	uint32_t k2 = 0;
	uint32_t k3 = 0;
	uint32_t k4 = 0;
	switch (len & 15) {
	case 15: k4 ^= tail[14] << 16;
	case 14: k4 ^= tail[13] << 8;
	case 13: k4 ^= tail[12] << 0;
		k4 *= c4; k4 = ROTL32(k4, 18); k4 *= c1; h4 ^= k4;
	case 12: k3 ^= tail[11] << 24;
	case 11: k3 ^= tail[10] << 16;
	case 10: k3 ^= tail[9] << 8;
	case  9: k3 ^= tail[8] << 0;
		k3 *= c3; k3 = ROTL32(k3, 17); k3 *= c4; h3 ^= k3;
	case  8: k2 ^= tail[7] << 24;
	case  7: k2 ^= tail[6] << 16;
	case  6: k2 ^= tail[5] << 8;
	case  5: k2 ^= tail[4] << 0;
		k2 *= c2; k2 = ROTL32(k2, 16); k2 *= c3; h2 ^= k2;
	case  4: k1 ^= tail[3] << 24;
	case  3: k1 ^= tail[2] << 16;
	case  2: k1 ^= tail[1] << 8;
	case  1: k1 ^= tail[0] << 0;
		k1 *= c1; k1 = ROTL32(k1, 15); k1 *= c2; h1 ^= k1;
	};
	h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;
	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;
	FMIX32(h1); FMIX32(h2); FMIX32(h3); FMIX32(h4);
	h1 += h2; h1 += h3; h1 += h4;
	h2 += h1; h3 += h1; h4 += h1;
	((uint32_t*)out)[0] = h1;
	((uint32_t*)out)[1] = h2;
	((uint32_t*)out)[2] = h3;
	((uint32_t*)out)[3] = h4;
}
