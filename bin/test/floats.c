int printf(const char* str, ...);

float fadd(float a, float b)
{
	return a + b;
}

float fmul(float a, float b)
{
	return a * b;
}

int main(void)
{
	float f = fadd(fmul(2.0f, 4.0f), 8.0f);
	printf("res: %f\n", f);
	return 0;
}
