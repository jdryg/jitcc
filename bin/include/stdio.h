#ifndef _STDIO
#define _STDIO

#include <stddef.h>

#define EOF    (-1)

#define SEEK_CUR    1
#define SEEK_END    2
#define SEEK_SET    0

#define FILENAME_MAX 260

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
int fflush(FILE* stream);
int fgetc(FILE* stream);
int getc(FILE* stream);
char* fgets(char* str, int count, FILE* stream);
int fseek(FILE* stream, long offset, int origin);
long ftell(FILE* stream);

int putchar(int ch);

FILE* __iob_func(unsigned _Ix);

#define stdin  (__iob_func(0))
#define stdout (__iob_func(1))
#define stderr (__iob_func(2))

#endif // _STDIO
