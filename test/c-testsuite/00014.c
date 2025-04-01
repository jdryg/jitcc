// TODO: Wrong output. Returns 1 (x) instead of 0 (p = &x). Memory aliasing.
int main(void)
{
	int x;
	int *p;
	
	x = 1;
	p = &x;
	p[0] = 0;
	return x;
}
