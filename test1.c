int m[6];

int scale(int x) {
   int i;

   if (x == 0)
      return 0;
   for (i = 0; i < 6; i += 1)
      m[i] *= x;
   return 1;
}

main()
{
   int i;
   int z;
   m[0] = 0;
   m[1] = 1;
   m[2] = 2;
   m[3] = 3;
   m[4] = 4;
   m[5] = 5;
   z = 10;
   if (scale(z))
      for (i = 0; i < 6; i += 1) {
         printf("m[%d]", i);
         printf(" = %d\n", m[i]);
      }
   else
      printf("scale factor is zero\n");
}

