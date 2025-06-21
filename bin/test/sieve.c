#include <stdlib.h> // malloc/free
#include <string.h> // memset
#include <stdint.h>
#include <stdio.h>

static void sieve(uint32_t n)
{
	uint8_t* status = (uint8_t*)malloc(n);
	if (!status) {
		return;
	}

	memset(status, 0, n);

	uint32_t p = 2;
	do {
		status[p] = 1;
		for (uint32_t i = p * p; i < n; i += p) {
			status[i] = 2;
		}

		uint32_t next = p + 1;
		while (next < n && status[next] != 0) {
			++next;
		}
		p = next;
	} while (p < n);

	for (uint32_t i = 0; i < n; ++i) {
		if (status[i] == 1) {
			printf("%u ", i);
		}
	}

	free(status);
}

int main(void)
{
	sieve(10000);
}
