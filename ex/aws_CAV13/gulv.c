#include "../../pagai_assert.h"
int nondet();

int main(){
  int x = 0, y = 0, w = 0, z = 0;
while (nondet()) {
   if (nondet())
     {x = x+1; y = y+100;}
   else if (nondet()){
     if (x >= 4)
       {x = x+1; y = y+1;}
   }
 else if (y > 10*w && z >= 100*x)
     {y = -y;}
   w = w+1; z = z+10;
  x = x; /* work around VC gen bug */
 
}
if (x >= 4 && y <= 2)
  goto ERROR;

return 0;
ERROR:assert(0);
}
