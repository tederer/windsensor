#ifndef windsensor_message_formatter_h
#define windsensor_message_formatter_h

#include <stdint.h>
#include <stddef.h>

#include "Messages.h"

/**
 * Creates a JSON message containing the provided measurements. 
 *
 * The caller has to free the returned pointer!!!
 **/
char* createJsonPayload(const uint16_t *anemometerPulses, const uint16_t *directionVaneValues, size_t measurementCount, const uint16_t secondsSincePreviousMessage);

/**
 * Creates a JSON message containing the pending messages and some meta data (e.g. version, sequence number, ...)
 *
 * The caller has to free the returned pointer!!!
 **/
char* createJsonEnvelope(PENDING_MESSAGES *pendingMessages);
#endif