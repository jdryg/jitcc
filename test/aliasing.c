int printf(const char* format, ...);

int main(void)
{
	int* p;
	int n;
	p = &n;
	n = 4;
	printf("%d\n", *p);

	return 0;
}