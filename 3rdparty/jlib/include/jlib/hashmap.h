#ifndef JX_HASHMAP_H
#define JX_HASHMAP_H

// https://github.com/tidwall/hashmap.c
// Original code is:
// Copyright 2020 Joshua J Baker. All rights reserved.
// Use of this source code is governed by an MIT-style
// license that can be found in the LICENSE file.
//
// Adapted to support allocators and to match jlib's coding style.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cpluscplus
extern "C" {
#endif

typedef struct jx_allocator_i jx_allocator_i;

// An "item" is a structure of your design that contains a key and a value. You load your structure 
// with key and value and you set it in the table, which copies the contents of your structure into 
// a bucket in the table. When you get an item out of the table, you load your structure with the 
// key data and call "hashmap_get()". This looks up the key and returns a pointer to the item stored 
// in the bucket. The passed-in item is not modified.
//
// Since the hashmap code doesn't know anything about your item structure, you must provide "compare" 
// and "hash" functions which access the structure's key properly.If you want to use the "hashmap_scan()" 
// function, you must also provide an "iter" function.For your hash function, you are welcome to call 
// one of the supplied hash functions, passing the key in your structure.
//
// Note that if your element structure contains pointers, those pointer values will be copied into 
// the buckets.I.e.it is a "shallow" copy of the item, not a "deep" copy.Therefore, anything your 
// entry points to must be maintained for the lifetime of the item in the table.
//
// The functions "hashmap_get()", "hashmap_set()", and "hashmap_delete()" all return a pointer to 
// an item if found.In all cases, the pointer is not guaranteed to continue to point to that same item 
// after subsequent calls to the hashmap.I.e.the hashmap can be rearranged by a subsequent call, which 
// can render previously - returned pointers invalid, possibly even pointing into freed heap space.
// DO NOT RETAIN POINTERS RETURNED BY HASHMAP CALLS! It is common to copy the contents of the item into 
// your storage immediately following a call that returns an item pointer.
typedef struct jx_hashmap_t jx_hashmap_t;

typedef uint64_t (*jxHashCallback)(const void* item, uint64_t seed0, uint64_t seed1, void* udata);
typedef int32_t (*jxCompareCallback)(const void* a, const void* b, void* udata);
typedef void (*jxItemFreeCallback)(void* item, void* udata);
typedef bool (*jxIterCallback)(const void* item, void* udata);

jx_hashmap_t* jx_hashmapCreate(jx_allocator_i* allocator, uint32_t elsize, uint32_t cap, uint64_t seed0, uint64_t seed1, jxHashCallback hash, jxCompareCallback compare, jxItemFreeCallback elfree, void* udata);
void jx_hashmapDestroy(jx_hashmap_t* map);
void jx_hashmapClear(jx_hashmap_t* map, bool update_cap);
uint32_t jx_hashmapCount(jx_hashmap_t* map);
void* jx_hashmapGet(jx_hashmap_t* map, const void* item);
void* jx_hashmapSet(jx_hashmap_t* map, const void* item);
void* jx_hashmapDelete(jx_hashmap_t* map, void* item);
bool jx_hashmapScan(jx_hashmap_t* map, jxIterCallback iter, void* udata);
bool jx_hashmapIter(jx_hashmap_t* map, uint32_t* i, void** item);

// Hash functions
uint64_t jx_hashSip(const void* data, size_t len, uint64_t seed0, uint64_t seed1);
uint64_t jx_hashMurmur3(const void* data, size_t len, uint64_t seed0, uint64_t seed1);
uint64_t jx_hashFNV1a(const void* data, size_t len, uint64_t seed0, uint64_t seed1);
uint64_t jx_hashFNV1a_cstr(const void* data, size_t len, uint64_t seed0, uint64_t seed1);
#ifdef __cpluscplus
}
#endif

#endif // JX_HASHMAP_H
