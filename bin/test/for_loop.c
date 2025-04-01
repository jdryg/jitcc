int main(void)
{
	int x;
	int w;

	w = 0;
	for (x = 0; x < 5; ++x) {
		w += 10;
	}

	w = 0;
	x = 0;
	for (; x < 5; ++x) {
		w += 10;
	}

	w = 0;
	x = 0;
	for (; x < 5;) {
		w += 10;
		x++;
	}

	w = 0;
	x = 0;
	for (;;) {
		w += 10;
		x++;
		if (x >= 5) {
			break;
		}
	}

	return 0;
}