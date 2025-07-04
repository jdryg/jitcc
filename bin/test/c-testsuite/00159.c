//#include <stdio.h>
int printf(const char* str, ...);

int myfunc(int x)
{
   return x * x;
}

void vfunc(int a)
{
   printf("a=%d\n", a);
}

void qfunc()
{
   printf("qfunc()\n");
}

void zfunc()
{
   ((void (*)(void))0) ();
}

int main()
{
   printf("%d\n", myfunc(3));
   printf("%d\n", myfunc(4));

   vfunc(1234);

   qfunc();

   return 0;
}

// vim: set expandtab ts=4 sw=3 sts=3 tw=80 :
