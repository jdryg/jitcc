#include <stdarg.h>

int sumi(int count, ...)
{
    va_list ap;
    va_start(ap, count);

    int sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += va_arg(ap, int);
    }
    va_end(ap);
    return sum;
}

int main(void)
{
    if (sumi(1, 1) != 1) {
        return 1;
    }
    if (sumi(2, 1, -1) != 0) {
        return 2;
    }
    if (sumi(5, 1, 2, 3, 4, 5) != 15) {
        return 3;
    }
    if (sumi(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) != 55) {
        return 4;
    }

    return 0;
}
