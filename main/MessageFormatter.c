#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "MessageFormatter.h"
#include "ErrorMessages.h"

#define MAX_MESSAGE_SEQUENCE_ID        999
#define MESSAGE_VERSION                "2.0.0"

#define NULL_BYTE_LENGTH               1

static int nextSequenceId = 0;

static int getNumberOfDigits(int value);
static int getNextSequenceId();
static size_t countSubstrings(const char *text, const char *substring);
static size_t lengthWithoutPlaceholders(const char *text);

char* createJsonPayload(const uint16_t *anemometerPulses, const uint16_t *directionVaneValues, size_t measurementCount, const uint16_t secondsSincePreviousMessage) {
   char *format                          = "{\"anemometerPulses\":[%s],\"directionVaneValues\":[%s],\"secondsSincePreviousMessage\":%d}";
   int maxDecimalDigitsPerValue          = 5; // 2^16 in decimal requires up to 5 digits
   int secondsSincePreviousMessageDigits = getNumberOfDigits(secondsSincePreviousMessage);
   int separatorCount                    = (measurementCount > 0) ? measurementCount - 1 : 0;
   int maxDataLengthInDigits             = (measurementCount * maxDecimalDigitsPerValue) + separatorCount;
   int maxDataLengthInBytes              = (maxDataLengthInDigits * sizeof(char)) + NULL_BYTE_LENGTH;
   char *anemometerData                  = malloc(maxDataLengthInBytes);
   char *directionVaneData               = malloc(maxDataLengthInBytes);
   char *anemometerDataPosition          = anemometerData;
   char *directionVaneDataPosition       = directionVaneData;

   for (int i = 0; i < measurementCount; i++) {
      uint32_t pulses    = anemometerPulses[i];
      uint32_t direction = directionVaneValues[i];
      sprintf(anemometerDataPosition, i < 1 ? "%d" : ",%d", pulses);
      sprintf(directionVaneDataPosition, i < 1 ? "%d" : ",%d", direction);
      anemometerDataPosition += strlen(anemometerDataPosition);
      directionVaneDataPosition += strlen(directionVaneDataPosition);
   }
   
   int maxPayloadLength = lengthWithoutPlaceholders(format) + strlen(anemometerData) + strlen(directionVaneData) + secondsSincePreviousMessageDigits;
   char *payload = malloc((maxPayloadLength * sizeof(char)) + NULL_BYTE_LENGTH);
   sprintf(payload, format, anemometerData, directionVaneData, secondsSincePreviousMessage);
   free(directionVaneData);
   free(anemometerData);
   return payload;
}

char* createJsonEnvelope(PENDING_MESSAGES *pendingMessages) {
   char *format                     = "{\"version\":\"%s\",\"sequenceId\":%d,\"messages\":[%s],\"errors\":[%s]}";
   int maxSequenceIdDigits          = getNumberOfDigits(MAX_MESSAGE_SEQUENCE_ID);
   const char* errors               = getErrorMessages();
   char errorSeparatorAsString[2];
   errorSeparatorAsString[0]        = getErrorMessageSeparator();
   errorSeparatorAsString[1]        = 0;
   bool noErrors                    = strlen(errors) == 0;
   int errorCount                   = noErrors ? 0: countSubstrings(errors, errorSeparatorAsString) + 1;
   int doubleQuotesCount            = 2 * errorCount;
   int errorsDataLengthInBytes      = (noErrors ? 0 : strlen(errors) + doubleQuotesCount) + NULL_BYTE_LENGTH;
   char *errorsData                 = malloc(errorsDataLengthInBytes);
   
   int messageSeparatorCount        = pendingMessages->count - 1;
   int messagesLength               = strlen("[]") + messageSeparatorCount;

   for (int i = 0; i < pendingMessages->count; i++) {
      messagesLength += strlen(pendingMessages->message[i]);
   }

   char *messagesData               = malloc(messagesLength * sizeof(char) + NULL_BYTE_LENGTH);
   char *messagesPosition           = messagesData;
   *messagesPosition                = 0;

   for (int i = 0; i < pendingMessages->count; i++) {
      char* separator = (i == 0) ? "" : ",";
      char *msg = pendingMessages->message[i];
      sprintf(messagesPosition, "%s%s", separator, msg);
      messagesPosition += strlen(msg) + strlen(separator);
   }
   *messagesPosition = 0;
   
   *errorsData = 0;
   int offset  = 0;
   char *copyOfErrors = malloc(strlen(errors) + 1); // this copy is needed because strtok inserts null characters at the end of each token
   strcpy(copyOfErrors, errors);
   
   char* token = strtok(copyOfErrors, errorSeparatorAsString);

   while(token != NULL) {
      char* separator = (offset == 0) ? "" : ",";
      sprintf(errorsData + offset, "%s\"%s\"", separator, token);
      offset += strlen(token) + 2 + strlen(separator);
      token = strtok(NULL, errorSeparatorAsString);
   }
   int payloadLength = lengthWithoutPlaceholders(format) + strlen(MESSAGE_VERSION) + maxSequenceIdDigits + strlen(messagesData) + strlen(errorsData);
   int payloadSizeInBytes = (payloadLength * sizeof(char)) + NULL_BYTE_LENGTH;
   char *payload = malloc(payloadSizeInBytes);
   sprintf(payload, format, MESSAGE_VERSION, getNextSequenceId(), messagesData, errorsData);
   free(copyOfErrors);
   free(messagesData);
   free(errorsData);
   
   return payload;
}

static int getNextSequenceId() {
   int result = nextSequenceId;
   nextSequenceId = (nextSequenceId + 1) % (MAX_MESSAGE_SEQUENCE_ID + 1);
   return result;
}

static size_t countSubstrings(const char *text, const char *substring) {
   size_t count = 0;
   const char *position = text;
   position = strstr(position, substring);
   while(position != 0) {
      count++;
      position = strstr(position + strlen(substring), substring);
   }
   return count;
}

static size_t lengthWithoutPlaceholders(const char *text) {
   char *stringPlaceholder ="%s";
   char *decimalPlaceholder ="%d";
   size_t stringPlaceHolderCount = countSubstrings(text, stringPlaceholder);
   size_t decimalPlaceHolderCount = countSubstrings(text, decimalPlaceholder);
   size_t length = strlen(text) - (stringPlaceHolderCount * strlen(stringPlaceholder)) - (decimalPlaceHolderCount * strlen(decimalPlaceholder));
   return length;
}

static int getNumberOfDigits(int value) {
   if (value == 0) {
      return 1;
   }

   int numberOfDigits = 0;

   while (value > 0) {
      value /= 10;
      numberOfDigits++;
   }
   
   return numberOfDigits; 
} 

