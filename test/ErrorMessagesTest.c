#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "../main/ErrorMessages.h"

static void assertEqual(char const * actual, char const * expected, char const * description) {
   if (strcmp(actual, expected) != 0) {
      printf("ERROR: %s\n", description);
      printf("\texpected: %s\n", expected);
      printf("\tactual  : %s\n\n", actual);
   }
}

int main(int argc, char* argv[]) {  

   char *expected = "";
   const char *message  = getErrorMessages();
   assertEqual(message, expected, "getErrorMessages returns empty string when no error added");
   

   addErrorMessage("test");
   expected = "test";
   message  = getErrorMessages();
   assertEqual(message, expected, "getErrorMessages returns added error");
   

   clearErrorMessages();
   for(int i = 0; i < 5; i++) {
      char *error = malloc(i + 1);
      for(int j = 0; j < (i + 1); j++) {
         error[j] = 'a' + i;
      }
      error[i + 1] = 0;
      addErrorMessage(error);
   }
   expected = "a,bb,ccc,dddd,eeeee";
   message  = getErrorMessages();
   assertEqual(message, expected, "getErrorMessages returns added errors");
   

   clearErrorMessages();
   for(int i = 0; i < 99; i++) {
      char error[2];
      sprintf(error, "%d", i % 10);
      addErrorMessage(error);
   }
   addErrorMessage("ab");
   expected = "0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,9,0,1,2,3,4,5,6,7,8,ab";
   message  = getErrorMessages();
   assertEqual(message, expected, "maximum number of characters is 200");
   

   clearErrorMessages();
   for(int i = 0; i < 19; i++) {
      char error[11];
      sprintf(error, "%c123456789", 'a' + i);
      addErrorMessage(error);
   }
   expected = "a123456789,b123456789,c123456789,d123456789,e123456789,f123456789,g123456789,h123456789,i123456789,j123456789,k123456789,l123456789,m123456789,n123456789,o123456789,p123456789,q123456789,r123456789";
   message  = getErrorMessages();
   assertEqual(message, expected, "added error gets ignore if it is to long");
  

   clearErrorMessages();
   addErrorMessage("0123456789");
   addErrorMessage("foo");
   clearErrorMessages();
   addErrorMessage("Abc45");
   addErrorMessage("hello world");
   expected = "Abc45,hello world";
   message  = getErrorMessages();
   assertEqual(message, expected, "clearErrorMessages removed previously added errors");
   
   return 0;
}