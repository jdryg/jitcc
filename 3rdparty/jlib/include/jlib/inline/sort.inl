#ifndef JX_SORT_H
#error "Must be included from jx/sort.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

static void jx_quickSort(void* base, uint64_t totalElems, uint64_t elemSize, jsortComparisonCallback cmp, void* userData)
{
	sort_api->quickSort(base, totalElems, elemSize, cmp, userData);
}

#ifdef __cplusplus
}
#endif
