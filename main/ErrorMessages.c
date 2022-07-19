#include <string.h>

#include "ErrorMessages.h"

static const char ERROR_MESSAGE_SEPARATOR   = ',';

static char errorMessages[MAX_ERROR_MESSAGES_LENGTH + 1];
static size_t errorMessagesLength = 0;

void clearErrorMessages() {
   errorMessages[0] = 0;
   errorMessagesLength = 0;
}

const char * getErrorMessages() {
   errorMessages[errorMessagesLength] = 0;
   return errorMessages;
}

char getErrorMessageSeparator() {
   return ERROR_MESSAGE_SEPARATOR;
}

void addErrorMessage(char const *message) {
   size_t messageLength             = strlen(message);
   size_t separatorLength           = (errorMessagesLength == 0) ? 0 : 1;
   size_t newErrorMessagesLength    = errorMessagesLength + separatorLength + messageLength;
   
   if (newErrorMessagesLength <= MAX_ERROR_MESSAGES_LENGTH) {
      const char *inputChar   = message;
      char *outputChar        = &errorMessages[errorMessagesLength];

      if (errorMessagesLength > 0) {
         *outputChar = ERROR_MESSAGE_SEPARATOR;
         outputChar++;
      }

      while(*inputChar != 0) {
         *outputChar = *inputChar;
         outputChar++;
         inputChar++;
      }

      *outputChar = 0;
      errorMessagesLength = newErrorMessagesLength;
   }
};