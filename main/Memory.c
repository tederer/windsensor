#include <stddef.h>
#include <stdlib.h>

#include "Memory.h"

void* allocate( size_t sizeInBytes ) {
   return malloc(sizeInBytes);
}