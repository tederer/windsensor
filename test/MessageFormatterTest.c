#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
	
#include "../main/MessageFormatter.h"
#include "../main/ErrorMessages.h"
#include "../main/Messages.h"
#include "TestingMemory.h"

static PENDING_MESSAGES pendingMessages;

static void assertEqual(char const * actual, char const * expected, char const * description) {
   if (strcmp(actual, expected) != 0) {
      printf("ERROR: %s\n", description);
      printf("\texpected: %s\n", expected);
      printf("\tactual  : %s\n\n", actual);
   }
}

static void assertIntEqual(int actual, int expected, char const * description) {
   if (actual != expected) {
      printf("ERROR: %s\n", description);
      printf("\texpected: %d\n", expected);
      printf("\tactual  : %d\n\n", actual);
   }
}

int main(int argc, char* argv[]) {  

   size_t measurementCount = 60;
   uint16_t anemometerPulses[measurementCount];
   uint16_t directionVaneValues[measurementCount];

   for (size_t i = 0; i < measurementCount; i++) {
      anemometerPulses[i] = i;
      directionVaneValues[i] = i * 10;
   }

   clearErrorMessages();
   
   char *expected = "{\"anemometerPulses\":[],\"directionVaneValues\":[],\"secondsSincePreviousMessage\":0}";
   char *message  = createJsonPayload(anemometerPulses, directionVaneValues, 0, 0);	
   assertEqual(message, expected, "empty message");
   free(message);
   
   expected = "{\"anemometerPulses\":[0,1,2,3,4],\"directionVaneValues\":[0,10,20,30,40],\"secondsSincePreviousMessage\":67}";
   message  = createJsonPayload(anemometerPulses, directionVaneValues, 5, 67);	
   assertEqual(message, expected, "message with 5 measurements and secondsSincePreviousMessage greater 0");
   free(message);
 
   expected = "{\"anemometerPulses\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59],\"directionVaneValues\":[0,10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170,180,190,200,210,220,230,240,250,260,270,280,290,300,310,320,330,340,350,360,370,380,390,400,410,420,430,440,450,460,470,480,490,500,510,520,530,540,550,560,570,580,590],\"secondsSincePreviousMessage\":126}";
   message = createJsonPayload(anemometerPulses, directionVaneValues, 60, 126);	
   assertEqual(message, expected, "message with 60 measurements and some secondsSincePreviousMessage");
   free(message);
 
   expected = "{\"version\":\"2.0.0\",\"sequenceId\":0,\"messages\":[],\"errors\":[]}";
   char *envelope = createJsonEnvelope(&pendingMessages);
   assertEqual(envelope, expected, "message envelope without messages");
   free(envelope);

   addToPendingMessages(&pendingMessages, "this is a test");
   expected = "{\"version\":\"2.0.0\",\"sequenceId\":1,\"messages\":[this is a test],\"errors\":[]}";
   envelope = createJsonEnvelope(&pendingMessages);
   assertEqual(envelope, expected, "message envelope with one message");
   free(envelope);
   
   addToPendingMessages(&pendingMessages, "2nd test");
   expected = "{\"version\":\"2.0.0\",\"sequenceId\":2,\"messages\":[this is a test,2nd test],\"errors\":[]}";
   envelope = createJsonEnvelope(&pendingMessages);
   assertEqual(envelope, expected, "message envelope with two message");
   free(envelope);
   
   expected = "{\"version\":\"2.0.0\",\"sequenceId\":999,\"messages\":[this is a test,2nd test],\"errors\":[]}";
   envelope = createJsonEnvelope(&pendingMessages);
   for(int i = 3; i < 999; i++) {
      free(envelope);
      envelope = createJsonEnvelope(&pendingMessages);
   }
   assertEqual(envelope, expected, "message with max sequence ID");
   free(envelope);
   
   expected = "{\"version\":\"2.0.0\",\"sequenceId\":0,\"messages\":[this is a test,2nd test],\"errors\":[]}";
   envelope = createJsonEnvelope(&pendingMessages);
   assertEqual(envelope, expected, "message with wrap around of sequence ID");
   free(envelope);
   
   clearPendingMessages(&pendingMessages);
   clearErrorMessages();
   addErrorMessage("error I");
   expected = "{\"version\":\"2.0.0\",\"sequenceId\":1,\"messages\":[],\"errors\":[\"error I\"]}";
   envelope = createJsonEnvelope(&pendingMessages);
   assertEqual(envelope, expected, "message with an error");
   free(envelope);
   
   addErrorMessage("second ERR");
   expected = "{\"version\":\"2.0.0\",\"sequenceId\":2,\"messages\":[],\"errors\":[\"error I\",\"second ERR\"]}";
   envelope = createJsonEnvelope(&pendingMessages);
   assertEqual(envelope, expected, "message with another error");
   free(envelope);

   resetTestingMemory();
   clearPendingMessages(&pendingMessages);
   clearErrorMessages();

   int expectedErrorsDataLength  = 1;
   int expectedMessagesLength    = 13;
   int expectedErrorsLength      = 1;
   int expectedTotalLength       = 73;

   addToPendingMessages(&pendingMessages, "0123456789");
   envelope = createJsonEnvelope(&pendingMessages);
   assertIntEqual(getTestingMemoryInvocationCount(), 4, "createJsonEnvelope: memory allocation (A)");
   assertIntEqual(getTestingMemoryInvocation(0), expectedErrorsDataLength, "createJsonEnvelope: memory allocation (A) - errorsDataLength");
   assertIntEqual(getTestingMemoryInvocation(1), expectedMessagesLength,   "createJsonEnvelope: memory allocation (A) - messagesLength");
   assertIntEqual(getTestingMemoryInvocation(2), expectedErrorsLength,     "createJsonEnvelope: memory allocation (A) - errorsLength");
   assertIntEqual(getTestingMemoryInvocation(3), expectedTotalLength,      "createJsonEnvelope: memory allocation (A) - totalLength");
   free(envelope);

   resetTestingMemory();
   clearPendingMessages(&pendingMessages);
   clearErrorMessages();

   expectedErrorsDataLength  = 9;
   expectedMessagesLength    = 3;
   expectedErrorsLength      = 7;
   expectedTotalLength       = 71;

   addErrorMessage("123456");
   envelope = createJsonEnvelope(&pendingMessages);
   assertIntEqual(getTestingMemoryInvocationCount(), 4, "createJsonEnvelope: memory allocation (B)");
   assertIntEqual(getTestingMemoryInvocation(0), expectedErrorsDataLength, "createJsonEnvelope: memory allocation (B) - errorsDataLength");
   assertIntEqual(getTestingMemoryInvocation(1), expectedMessagesLength,   "createJsonEnvelope: memory allocation (B) - messagesLength");
   assertIntEqual(getTestingMemoryInvocation(2), expectedErrorsLength,     "createJsonEnvelope: memory allocation (B) - errorsLength");
   assertIntEqual(getTestingMemoryInvocation(3), expectedTotalLength,      "createJsonEnvelope: memory allocation (B) - totalLength");
   free(envelope);

   resetTestingMemory();
   clearPendingMessages(&pendingMessages);
   clearErrorMessages();

   expectedErrorsDataLength  = 13;
   expectedMessagesLength    = 9;
   expectedErrorsLength      = 9;
   expectedTotalLength       = 81;
   
   addToPendingMessages(&pendingMessages, "123");
   addToPendingMessages(&pendingMessages, "45");
   addErrorMessage("123456");
   addErrorMessage("7");
   envelope = createJsonEnvelope(&pendingMessages);
   assertIntEqual(getTestingMemoryInvocationCount(), 4, "createJsonEnvelope: memory allocation (C)");
   assertIntEqual(getTestingMemoryInvocation(0), expectedErrorsDataLength, "createJsonEnvelope: memory allocation (C) - errorsDataLength");
   assertIntEqual(getTestingMemoryInvocation(1), expectedMessagesLength,   "createJsonEnvelope: memory allocation (C) - messagesLength");
   assertIntEqual(getTestingMemoryInvocation(2), expectedErrorsLength,     "createJsonEnvelope: memory allocation (C) - errorsLength");
   assertIntEqual(getTestingMemoryInvocation(3), expectedTotalLength,      "createJsonEnvelope: memory allocation (C) - totalLength");
   free(envelope);

   resetTestingMemory();
   int expectedAnemometerDataLength    = 360;
   int expectedDirectionVaneDataLength = 360;
   expectedTotalLength                 = 480;

   message = createJsonPayload(anemometerPulses, directionVaneValues, 60, 126);	
   assertIntEqual(getTestingMemoryInvocation(0), expectedAnemometerDataLength,      "createJsonPayload: memory allocation - anemometerDataLength");
   assertIntEqual(getTestingMemoryInvocation(1), expectedDirectionVaneDataLength,   "createJsonPayload: memory allocation - directionVaneDataLength");
   assertIntEqual(getTestingMemoryInvocation(2), expectedTotalLength,               "createJsonPayload: memory allocation - totalLength");
   free(message);

   return 0;
}