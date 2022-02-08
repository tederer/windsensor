#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
	
#include "../main/MessageFormatter.h"
#include "../main/ErrorMessages.h"

static void assertEqual(char const * actual, char const * expected, char const * description) {
   if (strcmp(actual, expected) != 0) {
      printf("ERROR: %s\n", description);
      printf("\texpected: %s\n", expected);
      printf("\tactual  : %s\n\n", actual);
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
   char *expected = "{\"version\":\"1.0.0\",\"sequenceId\":0,\"anemometerPulses\":[],\"directionVaneValues\":[],\"errors\":[]}";
   char *message  = createJsonPayload(anemometerPulses, directionVaneValues, 0);	
   assertEqual(message, expected, "empty message");

   
   clearErrorMessages();
   expected = "{\"version\":\"1.0.0\",\"sequenceId\":1,\"anemometerPulses\":[0,1,2,3,4],\"directionVaneValues\":[0,10,20,30,40],\"errors\":[]}";
   message  = createJsonPayload(anemometerPulses, directionVaneValues, 5);	
   assertEqual(message, expected, "message with 5 measurements and no errors");

   clearErrorMessages();
   addErrorMessage("hello world");
   expected = "{\"version\":\"1.0.0\",\"sequenceId\":2,\"anemometerPulses\":[0,1,2,3,4],\"directionVaneValues\":[0,10,20,30,40],\"errors\":[\"hello world\"]}";
   message  = createJsonPayload(anemometerPulses, directionVaneValues, 5);	
   assertEqual(message, expected, "message with 5 measurements and one error");

   clearErrorMessages();
   addErrorMessage("foo");
   addErrorMessage("bar");
   addErrorMessage("sun");
   expected = "{\"version\":\"1.0.0\",\"sequenceId\":3,\"anemometerPulses\":[0,1,2,3,4],\"directionVaneValues\":[0,10,20,30,40],\"errors\":[\"foo\",\"bar\",\"sun\"]}";
   message  = createJsonPayload(anemometerPulses, directionVaneValues, 5);	
   assertEqual(message, expected, "message with 5 measurements and 3 errors");

   clearErrorMessages();
   expected = "{\"version\":\"1.0.0\",\"sequenceId\":999,\"anemometerPulses\":[0,1,2,3,4],\"directionVaneValues\":[0,10,20,30,40],\"errors\":[]}";
   for (size_t i = 3; i < 999; i++) {
      message = createJsonPayload(anemometerPulses, directionVaneValues, 5);	
   }
   assertEqual(message, expected, "message with maximum sequenceId 999");

   clearErrorMessages();
   expected = "{\"version\":\"1.0.0\",\"sequenceId\":0,\"anemometerPulses\":[0,1,2,3,4],\"directionVaneValues\":[0,10,20,30,40],\"errors\":[]}";
   message = createJsonPayload(anemometerPulses, directionVaneValues, 5);	
   assertEqual(message, expected, "sequenceId wrap around");

   clearErrorMessages();
   addErrorMessage("abc de");
   expected = "{\"version\":\"1.0.0\",\"sequenceId\":1,\"anemometerPulses\":[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59],\"directionVaneValues\":[0,10,20,30,40,50,60,70,80,90,100,110,120,130,140,150,160,170,180,190,200,210,220,230,240,250,260,270,280,290,300,310,320,330,340,350,360,370,380,390,400,410,420,430,440,450,460,470,480,490,500,510,520,530,540,550,560,570,580,590],\"errors\":[\"abc de\"]}";
   message = createJsonPayload(anemometerPulses, directionVaneValues, 60);	
   assertEqual(message, expected, "message with 60 measurements and one error");

   free(message);
   return 0;
}