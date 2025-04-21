//#include <stdio.h>
int sprintf(char* str, const char* format, ...);
int printf(const char* str, ...);

int main()
{
   char Buf[100];
   int Count;

   for (Count = 1; Count <= 20; Count++)
   {
      sprintf(Buf, "->%02d<-\n", Count);
      printf("%s", Buf);
   }

   return 0;
}

/* vim: set expandtab ts=4 sw=3 sts=3 tw=80 :*/
