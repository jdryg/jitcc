#ifndef _STDIO
#define _STDIO

#include <stddef.h>

#define EOF    (-1)

typedef struct _iobuf
{
    void* _Placeholder;
} FILE;

int printf(const char* format, ...);
int sprintf(char* buffer, const char* format, ...);
int fprintf(FILE* stream, const char* format, ...);

FILE* fopen(const char* filename, const char* mode);
int fclose(FILE* stream);
size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream);
size_t fread(void* buffer, size_t size, size_t count, FILE* stream);
int fgetc(FILE* stream);
int getc(FILE* stream);
char* fgets(char* str, int count, FILE* stream);

#endif // _STDIO
