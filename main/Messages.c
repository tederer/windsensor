#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "Messages.h"

static void removeOldestMessage(PENDING_MESSAGES *pendingMessages);
static void clear(PENDING_MESSAGES *pendingMessages, bool freeMemory);

void initializePendingMessages(PENDING_MESSAGES *pendingMessages) {
   clear(pendingMessages, false);
}

void addToPendingMessages(PENDING_MESSAGES *pendingMessages, const char* messageToAdd) {
   if (pendingMessages->count >= MAX_NUMBER_OF_MESSAGES_TO_KEEP) {
      removeOldestMessage(pendingMessages);
   }

   char *copyOfMessageToAdd = malloc(strlen(messageToAdd) * sizeof(char) + 1);
   strcpy(copyOfMessageToAdd, messageToAdd); 
    
   pendingMessages->message[pendingMessages->count] = copyOfMessageToAdd;
   pendingMessages->count++;
}

void clearPendingMessages(PENDING_MESSAGES *pendingMessages) {
   clear(pendingMessages, true);
}

static void clear(PENDING_MESSAGES *pendingMessages, bool freeMemory) {
   for (int i = 0; i < MAX_NUMBER_OF_MESSAGES_TO_KEEP; i++) {
      if (pendingMessages->message[i] != NULL) {
         if (freeMemory) {
            free(pendingMessages->message[i]);
         }
         pendingMessages->message[i] = NULL;
      }
   }
   pendingMessages->count = 0;
}

static void removeOldestMessage(PENDING_MESSAGES *pendingMessages) {
   if (pendingMessages->count > 0) {
      free(pendingMessages->message[0]);
      for(int i = 0; i < pendingMessages->count; i++) {
         pendingMessages->message[i] = (i == (pendingMessages->count - 1)) ? NULL : pendingMessages->message[i + 1];
      }
      pendingMessages->count--;
   }
}