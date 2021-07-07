#ifndef windsensor_error_messages_h
#define windsensor_error_messages_h

/**
 * Clears the stored error messages. This method typically gets called when stored error messages got delivered sucessfully.
 **/
void clearErrorMessages();

/**
 * Returns the error messages separated by a comma.
 **/
const char* getErrorMessages();

/**
 * Returns the separator as null terminated string.
 **/
const char* getErrorMessageSeparator();

/**
 * Stores the provided message together with the messages provided in previous invocations.
 **/
void addErrorMessage(const char *message);

#endif