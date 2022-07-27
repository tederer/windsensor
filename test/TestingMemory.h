#ifndef windsensor_testing_memory_h
#define windsensor_testing_memory_h

/**
 * Allocates size bytes of uninitialized storage.
 **/
void* allocate( size_t sizeInBytes );

/**
 * Resets the argument capture.
 **/
void resetTestingMemory();

/**
 * Returns the number of captured invocations.
 **/
int getTestingMemoryInvocationCount();

/**
 * Returns the value of sizeInBytes of the invocation with the corresponding index.
 **/
int getTestingMemoryInvocation(int index);

#endif