// TODO: Generates phi instuctions with 1 bb
int main(void)
{
	start:
		goto next;
		return 1;
	success:
		return 0;
	next:
	foo:
		goto success;
		return 1;
}
