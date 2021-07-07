#include <string.h>

#include "ErrorMessages.h"
#include "driver/rtc_io.h"
#include "esp_log.h"

#define MAX_ERROR_MESSAGES_LENGTH      200
#define ERROR_MESSAGE_SEPARATOR        ','
#define ERROR_MESSAGE_SEPARATOR_LENGTH 1

static const char* TAG = "errors";
char* separator = 0;

RTC_DATA_ATTR char errorMessages[MAX_ERROR_MESSAGES_LENGTH + 1];

void clearErrorMessages() {
   errorMessages[0] = 0;
}

const char* getErrorMessages() {
   return errorMessages;
}

const char* getErrorMessageSeparator() {
   if (separator == 0) {
      separator = malloc(2);
      *separator = ERROR_MESSAGE_SEPARATOR;
      *(separator + 1) = 0;
   }
   return separator;
}

void addErrorMessage(const char *message) {
   size_t messageLength = strlen(message);
   
   if (messageLength > MAX_ERROR_MESSAGES_LENGTH) {
      ESP_LOGW(TAG, "failed to store error message because it's too long (message: %s)", message);
      return;
   }

   size_t errorMessagesLength       = strlen(errorMessages);
   size_t separatorLength           = (errorMessagesLength == 0) ? 0 : ERROR_MESSAGE_SEPARATOR_LENGTH;
   size_t newErrorMessagesLength    = errorMessagesLength + separatorLength + messageLength;
   
   if (newErrorMessagesLength > MAX_ERROR_MESSAGES_LENGTH) {
      size_t charsToRemove = newErrorMessagesLength - MAX_ERROR_MESSAGES_LENGTH;
      char   *charToMove   = &(errorMessages[charsToRemove]);

      while (*charToMove != 0 && *charToMove != ERROR_MESSAGE_SEPARATOR) {
         charToMove++;
      }
      
      if (*charToMove == 0) {
         errorMessages[0] = 0;
      } else {
         charToMove++;
         size_t index = 0;
         while(*charToMove != 0) {
            errorMessages[index] = *charToMove;
            charToMove++;
            index++;
         }
         errorMessages[index] = 0;
      }
   }

   const char *inputChar   = message;
   char *outputChar        = &errorMessages[strlen(errorMessages)];

   if (outputChar != &errorMessages[0]) {
      *outputChar = ERROR_MESSAGE_SEPARATOR;
      outputChar++;
   }

   while(*inputChar != 0) {
      *outputChar = *inputChar;
      outputChar++;
      inputChar++;
   }

   *outputChar = 0;
};