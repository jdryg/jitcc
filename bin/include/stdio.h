#ifndef _STDIO
#define _STDIO

#include <stddef.h>

typedef struct _iobuf
{
    void* _Placeholder;
} FILE;

int sprintf(char* buffer, const char* format, ...);

FILE* fopen(const char* filename, const char* mode);
int fclose(FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);

#endif // _STDIO
