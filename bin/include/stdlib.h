#ifndef _STDLIB
#define _STDLIB

#include <stddef.h>

void* malloc(size_t size);
void free(void* ptr);
void* realloc(void* ptr, size_t new_size);

int abs(int n);

#endif // _STDLIB
