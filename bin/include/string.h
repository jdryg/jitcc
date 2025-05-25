#ifndef _STRING
#define _STRING

void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t count);
int memcmp(const void* ptr1, const void* ptr2, size_t num);
void* memset(void* dest, int ch, size_t count);

char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t count);
int strcmp(const char* lhs, const char* rhs);
size_t strlen(const char* str);
char* strcat(char* dest, const char* src);
int strncmp(const char* s1, const char* s2, size_t n);
char* strchr(const char* str, int ch);
char* strrchr(const char* str, int ch);

#endif // _STRING
