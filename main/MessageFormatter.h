#ifndef windsensor_message_formatter_h
#define windsensor_message_formatter_h

#include <stdint.h>
#include <stddef.h>

// the caller has to free the returned pointer!!!
char* createJsonPayload(const uint16_t *anemometerPulses, const uint16_t *directionVaneValues, size_t measurementCount);

#endif