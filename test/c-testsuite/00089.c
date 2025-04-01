int zero(void)
{
	return 0;
}

struct S
{
	int (*zerofunc)();
} s = { &zero };

struct S *anon(void)
{
	return &s;
}

typedef struct S * (*fty)();

fty go(void)
{
	return &anon;
}

int main(void)
{
	return go()()->zerofunc();
}
