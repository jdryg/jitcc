#include <stdio.h>

static int compute(int n)
{
	int a = 1, b = 1, c = 2, d = 3, e = 5;
	int f = 8, g = 13, h = 21, i = 34, j = 55;

	for (int k = 0; k < n; ++k) {
		int next = a + b + c + d + e + f + g + h + i + j;
		a = b + c;
		b = c + d;
		c = d + e;
		d = e + f;
		e = f + g;
		f = g + h;
		g = h + i;
		h = i + j;
		i = j + next;
		j = next;
	}

	return a + b + c + d + e + f + g + h + i + j;
}

int main(void)
{
	int res = compute(10);
	printf("res = %d\n", res);
	return res == 141143873 ? 0 : 1;
}
