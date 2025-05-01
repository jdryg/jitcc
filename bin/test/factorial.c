int factorial(int x)
{
	if (x <= 1) {
		return 1;
	}

	int res = 1;
	for (int c = 1; c <= x; ++c) {
		res *= c;
	}

	return res;
}

int main(void)
{
	if (factorial(5) != 120) {
		return 1;
	}

	return 0;
}