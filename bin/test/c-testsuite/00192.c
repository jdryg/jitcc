//#include <stdio.h>
int printf(const char* str, ...);

int main(void)
{
   int Count = 0;

   for (;;)
   {
      Count++;
      printf("%d\n", Count);
      if (Count >= 10)
         break;
   }

   return 0;
}

/* vim: set expandtab ts=4 sw=3 sts=3 tw=80 :*/
