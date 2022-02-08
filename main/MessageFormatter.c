#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "MessageFormatter.h"
#include "ErrorMessages.h"

#define MAX_MESSAGE_SEQUENCE_ID        999
#define MESSAGE_VERSION                "1.0.0"

static int nextSequenceId = 0;

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

char* createJsonPayload(const uint16_t *anemometerPulses, const uint16_t *directionVaneValues, size_t measurementCount) {
   char *format                     = "{\"version\":\"%s\",\"sequenceId\":%d,\"anemometerPulses\":[%s],\"directionVaneValues\":[%s],\"errors\":[%s]}";
   int nullByteLength               = 1;
   int maxDecimalDigitsPerValue     = 5; // 2^16 in decimal requires up to 5 digits
   int maxSequenceIdDigits          = (int)(ceilf(log10f(MAX_MESSAGE_SEQUENCE_ID)));
   int separatorCount               = measurementCount - 1;
   int maxDataLengthInDigits        = (measurementCount * maxDecimalDigitsPerValue) + separatorCount;
   int maxDataLengthInBytes         = (maxDataLengthInDigits * sizeof(char)) + nullByteLength;
   const char* errors               = getErrorMessages();
   bool noErrors                    = strlen(errors) == 0;
   char errorSeparatorAsString[2];
   errorSeparatorAsString[0]        = getErrorMessageSeparator();
   errorSeparatorAsString[1]        = 0;
   int errorCount                   = noErrors ? 0: countSubstrings(errors, errorSeparatorAsString) + 1;
   int doubleQuotesCount            = 2 * errorCount;
   int errorsDataLengthInBytes      = (noErrors ? 0 : strlen(errors) + doubleQuotesCount) + nullByteLength;
   char *anemometerData             = malloc(maxDataLengthInBytes);
   char *directionVaneData          = malloc(maxDataLengthInBytes);
   char *errorsData                 = malloc(errorsDataLengthInBytes);
   char *anemometerDataPosition     = anemometerData;
   char *directionVaneDataPosition  = directionVaneData;

   for (int i = 0; i < measurementCount; i++) {
      uint32_t pulses    = anemometerPulses[i];
      uint32_t direction = directionVaneValues[i];
      sprintf(anemometerDataPosition, i < 1 ? "%d" : ",%d", pulses);
      sprintf(directionVaneDataPosition, i < 1 ? "%d" : ",%d", direction);
      anemometerDataPosition += strlen(anemometerDataPosition);
      directionVaneDataPosition += strlen(directionVaneDataPosition);
   }
   
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

   int maxPayloadLength = lengthWithoutPlaceholders(format) + strlen(MESSAGE_VERSION) + maxSequenceIdDigits + strlen(anemometerData) + strlen(directionVaneData) + strlen(errorsData);
   char *payload = malloc((maxPayloadLength * sizeof(char)) + nullByteLength);
   sprintf(payload, format, MESSAGE_VERSION, getNextSequenceId(), anemometerData, directionVaneData, errorsData);
   free(copyOfErrors);
   free(directionVaneData);
   free(anemometerData);
   free(errorsData);
   return payload;
}
