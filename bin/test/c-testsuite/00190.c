//#include <stdio.h>
int printf(const char* str, ...);

void fred(void)
{
   printf("yo\n");
}

int main()
{
   fred();

   return 0;
}

/* vim: set expandtab ts=4 sw=3 sts=3 tw=80 :*/
