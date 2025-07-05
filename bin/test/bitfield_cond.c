#include <stdio.h>

typedef unsigned int yDbMask;

# define DbMaskTest(M,I)    (((M)&(((yDbMask)1)<<(I)))!=0)

static int test(yDbMask cookieMask, int iDb)
{
	if (DbMaskTest(cookieMask, iDb) == 0) {
		return 0;
	}
	return 1;
}

int main(void)
{
	return test(0, 0);
}