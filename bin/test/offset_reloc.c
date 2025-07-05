static const int g_Arr[5] = {
	0, 1, 2, 3, 4
};
static const int* g_Arr3Ptr = &g_Arr[3];

int main(void)
{
	if (g_Arr3Ptr[0] != 3) {
		return 1;
	}

	return 0;
}