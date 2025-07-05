typedef int (*funcPtr)(void);

static int add(int a, int b)
{
	return a + b;
}

static funcPtr getFunc(void)
{
	return (funcPtr)add;
}

int main(void)
{
	funcPtr test = getFunc();

	return ((int (*)(int, int))test)(1, 2) == 3 ? 0 : 1;
}
