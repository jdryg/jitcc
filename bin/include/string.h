#ifndef _STRING
#define _STRING

#include <stddef.h> // size_t

void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t count);
int memcmp(const void* ptr1, const void* ptr2, size_t num);
void* memset(void* dest, int ch, size_t count);
void* memchr(const void* _Buf, int _Val, size_t _MaxCount);

char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t count);
int strcmp(const char* lhs, const char* rhs);
size_t strlen(const char* str);
char* strcat(char* dest, const char* src);
int strncmp(const char* s1, const char* s2, size_t n);
char* strchr(const char* str, int ch);
char* strrchr(const char* str, int ch);

size_t strspn(const char* _Str, const char* _Control);
size_t strcspn(const char* _Str, const char* _Control);
char* strstr(char* str1, const char* str2);

#endif // _STRING
