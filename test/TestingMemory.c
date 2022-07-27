#include <stddef.h>
#include <stdlib.h>

#include "TestingMemory.h"

#define CAPTOR_SIZE  100

static int capturedSizes[CAPTOR_SIZE];
static int invocationCount = 0;

void* allocate( size_t sizeInBytes ) {
   if (invocationCount < CAPTOR_SIZE) {
      capturedSizes[invocationCount++] = sizeInBytes;
   } 
   return malloc(sizeInBytes);
}

void resetTestingMemory() {
   invocationCount = 0;
   for (int i = 0; i < CAPTOR_SIZE; i++) {
      capturedSizes[i] = -1;
   }
}

int getTestingMemoryInvocationCount() {
   return invocationCount;
}

int getTestingMemoryInvocation(int index) {
   return (index < 0 || index >= invocationCount) ? -1 : capturedSizes[index];
}