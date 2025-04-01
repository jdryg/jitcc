// Changed struct definition to make it compile.
int main(void)
{
	typedef struct S { int x; int y; } S;
	S s;
	S *p;

	p = &s;	
	s.x = 1;
	p->y = 2;
	return p->y + p->x - 3; 
}

