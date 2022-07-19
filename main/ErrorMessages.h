#ifndef windsensor_error_messages_h
#define windsensor_error_messages_h

#define MAX_ERROR_MESSAGES_LENGTH      300

/**
 * Clears the stored error messages. This method typically gets called when stored error messages got delivered sucessfully.
 **/
void clearErrorMessages();

/**
 * Returns the error messages separated by a separator character.
 **/
const char * getErrorMessages();

/**
 * Returns the separator.
 **/
char getErrorMessageSeparator();

/**
 * Stores the provided message together with the messages provided in previous invocations.
 **/
void addErrorMessage(char const *message);

#endif