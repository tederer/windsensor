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
   char maxLengthMessage[MAX_ERROR_MESSAGES_LENGTH + 1];
   char *writePosition = maxLengthMessage;

   for(int i = 0; i < MAX_ERROR_MESSAGES_LENGTH; i++) {
      char error[2];
      sprintf(writePosition++, "%d", i % 10);
   }
   *writePosition = 0;

   addErrorMessage(maxLengthMessage);
   message  = getErrorMessages();
   assertEqual(message, maxLengthMessage, "maximum number of characters");
   
   addErrorMessage("a");
   message  = getErrorMessages();
   assertEqual(message, maxLengthMessage, "added error gets ignore if it is to long");
  
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