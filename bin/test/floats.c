int printf(const char* str, ...);

float fadd(float a, float b)
{
	return a + b;
}

float fsub(float a, float b)
{
	return a - b;
}

float fmul(float a, float b)
{
	return a * b;
}

float fdiv(float a, float b)
{
	return a / b;
}

double dadd(double a, double b)
{
	return a + b;
}

double dsub(double a, double b)
{
	return a - b;
}

double dmul(double a, double b)
{
	return a * b;
}

double ddiv(double a, double b)
{
	return a / b;
}

int main(void)
{
	float f = fdiv(fadd(fmul(2.0f, 4.0f), fsub(16.0f, 8.0f)), 16.0f);
	if (f != 1.0f) {
		return 1;
	}

	double d = ddiv(dadd(dmul(2.0, 4.0), dsub(16.0, 8.0)), 16.0);
	if (d != 1.0) {
		return 1;
	}

	return 0;
}
