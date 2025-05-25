#ifndef _STRING
#define _STRING

void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t count);
int memcmp(const void* ptr1, const void* ptr2, size_t num);
void* memset(void* dest, int ch, size_t count);

#endif // _STRING
