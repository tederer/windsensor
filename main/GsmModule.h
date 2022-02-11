#ifndef windsensor_gsm_module_h
#define windsensor_gsm_module_h

/**
 * Sends data to the URL and returns the HTTP status code. In case of problems the returned status code is 0.
 **/
int send(const char* url, const char* data);

/**
 * Initializes the serial connection to the GSM module and also the GSM module itself. This method gets called
 * automatically when you call send(...).
 */
void initializeGsmModule();

#endif