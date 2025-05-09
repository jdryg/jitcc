typedef char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

typedef struct s1_t
{
	uint8_t x;
} s1_t;

typedef struct s2_t
{
	uint8_t x[2];
} s2_t;

typedef struct s3_t
{
	uint8_t x[3];
} s3_t;

typedef struct s4_t
{
	uint8_t x[4];
} s4_t;

typedef struct s5_t
{
	uint8_t x[5];
} s5_t;

typedef struct s8_t
{
	uint8_t x[8];
} s8_t;

typedef struct s12_t
{
	uint8_t x[12];
} s12_t;

uint8_t f1(s1_t a)
{
	return a.x;
}

uint8_t f2(s2_t a)
{
	return a.x[0];
}

uint8_t f3(s3_t a)
{
	return a.x[0];
}

uint8_t f4(s4_t a)
{
	return a.x[0];
}

uint8_t f5(s5_t a)
{
	return a.x[0];
}

uint8_t f8(s8_t a)
{
	return a.x[0];
}

uint8_t f12(s12_t a)
{
	return a.x[0];
}

int main(void)
{
	s1_t s1 = { .x = 1 };
	s2_t s2 = { .x[0] = 2 };
	s3_t s3 = { .x[0] = 3 };
	s4_t s4 = { .x[0] = 4 };
	s5_t s5 = { .x[0] = 5 };
	s8_t s8 = { .x[0] = 8 };
	s12_t s12 = { .x[0] = 12 };

	if (f1(s1) != 1) {
		return 1;
	}
	if (f2(s2) != 2) {
		return 2;
	}
	if (f3(s3) != 3) {
		return 3;
	}
	if (f4(s4) != 4) {
		return 4;
	}
	if (f5(s5) != 5) {
		return 5;
	}
	if (f8(s8) != 8) {
		return 8;
	}
	if (f12(s12) != 12) {
		return 12;
	}

	return 0;
}
