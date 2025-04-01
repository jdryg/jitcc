#ifndef JX_SORT_H
#define JX_SORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t (*jsortComparisonCallback)(const void* elemA, const void* elemB, void* userData);

typedef struct jx_sort_api
{
	void (*quickSort)(void* base, uint64_t totalElems, uint64_t elemSize, jsortComparisonCallback cmp, void* userData);
} jx_sort_api;

extern jx_sort_api* sort_api;

static void jx_quickSort(void* base, uint64_t totalElems, uint64_t elemSize, jsortComparisonCallback cmp, void* userData);
#ifdef __cplusplus
}
#endif

#include "inline/sort.inl"

#endif // JX_SORT_H
