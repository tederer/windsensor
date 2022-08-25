#ifndef windsensor_messages_h
#define windsensor_messages_h

#define MAX_NUMBER_OF_MESSAGES_TO_KEEP 5

typedef struct {
   int count;
   char *message[MAX_NUMBER_OF_MESSAGES_TO_KEEP];
} PENDING_MESSAGES;

/**
 * Initializes the PENDING_MESSAGES structure by setting all messages to NULL and count to 0.
 **/
void initializePendingMessages(PENDING_MESSAGES *pendingMessages);

/**
 * Adds a copy of the provided messageToAdd and removes the oldest one if all storage places are in use.
 **/
void addToPendingMessages(PENDING_MESSAGES *pendingMessages, const char* messageToAdd);

/**
 * Frees the memory occupied by the messages, sets their pointers to NULL and sets count to 0.
 **/
void clearPendingMessages(PENDING_MESSAGES *pendingMessages);

#endif