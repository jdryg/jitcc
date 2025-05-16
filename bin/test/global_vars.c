int x = 5;

static int getGlobal(void)
{
	return x;
}

static int* getGlobalPtr(void)
{
	return &x;
}

static int* getPtr(int* x)
{
	return x;
}

int main(void)
{
	//if (getGlobal() != 5) {
	//	return 1;
	//}

	//if (getGlobalPtr() != &x) {
	//	return 2;
	//}

	int tmp = 5;
	if (getPtr(&tmp) != &tmp) {
		return 3;
	}

	return 0;
}