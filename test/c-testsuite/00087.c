struct S
{
	int	(*fptr)();
};

int foo(void)
{
	return 0;
}

int main(void)
{
	struct S v;
	
	v.fptr = foo;
	return v.fptr();
}

