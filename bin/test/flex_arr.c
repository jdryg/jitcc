#include <stdlib.h>
#include <string.h>

typedef struct test_flex_t
{
    int a;
    int b;
    int c[];
} test_flex_t;

int getFlexElement(test_flex_t* s, int i)
{
    return s->c[i];
}

long long getFlexStructSize(void)
{
    return sizeof(test_flex_t);
}

int main(void)
{
    if (getFlexStructSize() != 8) {
        return 1;
    }

    test_flex_t* test = (test_flex_t*)malloc(sizeof(test_flex_t) + sizeof(int) * 10);
    memset(test, 0, sizeof(test_flex_t) + sizeof(int) * 10);

    for (int i = 0; i < 10; ++i) {
        test->c[i] = i;
    }

    if (getFlexElement(test, 5) != 5) {
        return 2;
    }

    return 0;
}