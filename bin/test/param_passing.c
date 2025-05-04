// a in RCX, b in RDX, c in R8, d in R9, f then e pushed on stack
int func1(int a, int b, int c, int d, int e, int f);

// a in XMM0, b in XMM1, c in XMM2, d in XMM3, f then e pushed on stack
int func2(float a, double b, float c, double d, float e, float f);

// a in RCX, b in XMM1, c in R8, d in XMM3, f then e pushed on stack
int func3(int a, double b, int c, float d, int e, float f);

// a in RCX, ptr to b in RDX, ptr to c in R8, d in XMM3,
// ptr to f pushed on stack, then ptr to e pushed on stack
//int func4(__m64 a, __m128 b, struct c, float d, __m128 e, __m128 f);

// Caller passes a in RCX, b in XMM1, c in R8, d in R9, e pushed on stack,
// callee returns __int64 result in RAX.
long long int func5(int a, float b, int c, int d, int e);

// Caller passes a in XMM0, b in XMM1, c in R8, d in R9,
// callee returns __m128 result in XMM0.
//__m128 func6(float a, double b, int c, __m64 d);

// Caller allocates memory for Struct1 returned and passes pointer in RCX,
// a in RDX, b in XMM2, c in R9, d pushed on the stack;
// callee returns pointer to Struct1 result in RAX.
typedef struct Struct1
{
	int j, k, l;    // Struct1 exceeds 64 bits.
} Struct1;
Struct1 func7(int a, double b, int c, float d);

// Caller passes a in RCX, b in XMM1, c in R8, and d in XMM3;
// callee returns Struct2 result by value in RAX.
typedef struct Struct2
{
	int j, k;    // Struct2 fits in 64 bits, and meets requirements for return by value.
} Struct2;
Struct2 func8(int a, double b, int c, float d);

int main(void)
{
	if (func1(1, 2, 3, 4, 5, 6)) {
		return 1;
	}

	if (func2(1.0f, 2.0, 3.0f, 4.0, 5.0f, 6.0f)) {
		return 1;
	}

	if (func3(1, 2.0, 3, 4.0f, 5, 6.0f)) {
		return 1;
	}

	if (func5(1, 2.0f, 3, 4, 5)) {
		return 1;
	}

	Struct1 tmp = func7(1, 2.0, 3, 4.0f);
	if (tmp.j + tmp.k + tmp.l != 0) {
		return 1;
	}

	Struct2 tmp2 = func8(1, 2.0, 3, 4.0f);
	if (tmp2.j + tmp2.k != 0) {
		return 1;
	}

	return 0;
}
