#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
	
#include "../main/Messages.h"

static PENDING_MESSAGES pendingMessages;

static void assertEqual(char const * actual, char const * expected, char const * description) {
   if (strcmp(actual, expected) != 0) {
      printf("ERROR: %s\n", description);
      printf("\texpected: %s\n", expected);
      printf("\tactual  : %s\n\n", actual);
   }
}

static int countNotNullMessages() {
   int notNullMessageCount = 0;
   for (int i = 0; i < MAX_NUMBER_OF_MESSAGES_TO_KEEP; i++) {
      notNullMessageCount += (pendingMessages.message[i] == NULL) ? 0 : 1;
   }
   return notNullMessageCount;
}

static void assertPendingMessagesAreEmpty() {
   if (pendingMessages.count != 0) {
      printf("ERROR: count is %d and not 0\n", pendingMessages.count);
   }
   int notNullMessageCount = countNotNullMessages();
   if (notNullMessageCount > 0) {
      printf("ERROR: %d messages are not NULL\n", notNullMessageCount);
   }
}

static void assertPendingMessagesContainsMessageCount(int exectedNumberOfMessages) {
   if (pendingMessages.count != exectedNumberOfMessages) {
      printf("ERROR: message count is %d instead of %d\n", pendingMessages.count, exectedNumberOfMessages);
   }

   int notNullMessageCount = countNotNullMessages();
   if (notNullMessageCount != exectedNumberOfMessages) {
      printf("ERROR: %d instead of %d messages are not NULL\n", notNullMessageCount, exectedNumberOfMessages);
   }
}

static void assertMessageIsEqualTo(int index, const char *expectedMessage) {
   const char *actual = pendingMessages.message[index];
   if (actual == NULL || strcmp(actual, expectedMessage) != 0) {
      printf("ERROR: index %d is \"%s\" instead of \"%s\"\n", index, (actual == NULL) ? "NULL" : actual, expectedMessage);
   }
}

int main(int argc, char* argv[]) {  
   
   initializePendingMessages(&pendingMessages);
   assertPendingMessagesAreEmpty();

   addToPendingMessages(&pendingMessages, "hello world");
   assertPendingMessagesContainsMessageCount(1);
   assertMessageIsEqualTo(0, "hello world");

   char text[10];
   for(int i = 0; i < 20; i++) {
      sprintf(text, "test %d", i);
      addToPendingMessages(&pendingMessages, text);
   }

   assertPendingMessagesContainsMessageCount(15);
   for (int i = 5; i < 20; i++) {
      sprintf(text, "test %d", i);
      assertMessageIsEqualTo(i - 5, text);
   }

   return 0;
}