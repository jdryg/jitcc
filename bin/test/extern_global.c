#include <stdio.h>

extern int g_ExternalVar;
extern int g_ExternalArr[10];

static int readExternalVar(void)
{
	return g_ExternalVar;
}

static int* getExternalVarPtr(void)
{
	return &g_ExternalVar;
}

static void writeExternalVar(int val)
{
	g_ExternalVar = val;
}

static int readExternalArr(int id)
{
	return g_ExternalArr[id];
}

static int* getExternalArrPtr(int id)
{
	return &g_ExternalArr[id];
}

static void writeExternalArr(int id, int val)
{
	g_ExternalArr[id] = val;
}

int main(void)
{
	printf("ExternalVar: %p\n", getExternalVarPtr());
	printf("ExternalArr: %p\n", getExternalArrPtr(0));

	if (readExternalVar() != 1000) {
		return 1;
	}
	writeExternalVar(123456);
	if (readExternalVar() != 123456) {
		return 2;
	}

	if (readExternalArr(5) != 5) {
		return 3;
	}
	writeExternalArr(5, 100);
	if (readExternalArr(5) != 100) {
		return 4;
	}

	return 0;
}
