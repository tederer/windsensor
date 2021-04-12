#ifndef windsensor_wifi_h
#define windsensor_wifi_h

/**
 * Sends data to the URL and returns the HTTP status code. In case of problems the returned status code is 0.
 **/
int send(const char* url, const char* data);

#endif