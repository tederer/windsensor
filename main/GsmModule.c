#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "ErrorMessages.h"
#include "GsmModule.h"
#include "Utils.h"

#define UART_PORT                                     UART_NUM_2
#define IO_PIN_FOR_PWRKEY                             GPIO_NUM_2
#define IO_PIN_FOR_RELAIS                             GPIO_NUM_4
#define MILLIS_PER_SECOND                             1000
#define SECONDS(value)                                (value * MILLIS_PER_SECOND) 
#define PWR_PIN_HIGH_DURATION                         SECONDS(2)
#define RELAIS_ACTIVE_DURATION                        SECONDS(2)
#define MODULE_POWER_SUPPLY_OFF_DURATION              SECONDS(5)
#define ON_ERROR_RECORD(msg)                          if (!gsmModuleReplied) {addErrorMessage(msg);return false;} 
#define CR                                            0x0d
#define LF                                            0x0a
#define SPACE                                         0x20
#define COLON                                         0x3A
#define PIPE_CHAR                                     0x7c
#define CRLF                                          "\r\n"
#define RESPONSE_BUFFER_SIZE                          1000
#define OK_RESPONSE                                   "OK"
#define NULL_BYTE_LENGTH                              1
#define MAX_INPUT_TIME_MS                             3000
#define HTTP_RESPONSE_OK                              200
#define HTTP_RESPONSE_ERROR                           0
#define FAILED_SEND_ATTEMPTS_TO_RESTART_GSM_MODULE    4
#define ONE_DAY_IN_SECONDS                            (24 * 60 * 60)

typedef struct {
   int count;
   const char **commands;
} AT_COMMANDS;

typedef enum stat {
   GSM_OK,
   GSM_TIMEOUT,
   GSM_ERROR,
   GSM_NOTHING_RECEIVED
} GsmStatus;

static char responseBuffer[RESPONSE_BUFFER_SIZE];
static bool uartAndGpioInitialized = false;
static bool baudrateConfigured     = false;
static bool gsmModuleReady         = false;
static int failedSendAttempts      = 0;
static time_t moduleReadyTime      = 0;

static const char* GSM_MODULE_TAG = "GSM-module";

static const AT_COMMANDS initBearerCommands = { 3, (const char*[]) {       
   "AT+SAPBR=3,1,\"Contype\", \"GPRS\"",
   "AT+SAPBR=3,1,\"APN\",\"CMNET\"",
   "AT+SAPBR=1,1"
   }};

static const AT_COMMANDS initHttpCommands = { 2, (const char*[]) {
   "AT+CLTS?",
   "AT+HTTPINIT"
   }};

const AT_COMMANDS terminateHttpCommands = { 1, (const char*[]) {"AT+HTTPTERM"}};

const AT_COMMANDS terminateBearerCommands = { 1, (const char*[]) {"AT+SAPBR=0,1"}};

static void sleep(TickType_t durationInMs) {
   vTaskDelay( durationInMs / portTICK_PERIOD_MS);
}

static void initUart() {
   ESP_LOGI(GSM_MODULE_TAG, "initializing UART %d ...", UART_PORT);
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
   };
   // Configure UART parameters
   ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
   ESP_LOGI(GSM_MODULE_TAG, "setting pins ...");
   ESP_ERROR_CHECK(uart_set_pin(UART_PORT, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
   ESP_LOGI(GSM_MODULE_TAG, "loading driver ...");
   const int rxBufferSize = (1024 * 2);
   const int txBufferSize = 0;
   ESP_ERROR_CHECK(uart_driver_install(UART_PORT, rxBufferSize, txBufferSize, 0, NULL, 0));
}

static void sendCommand(const char* message) {
   int messageLength = strlen(message);
   ESP_LOGI(GSM_MODULE_TAG, "out: \"%s\"", message);
   char *messageWithCr = malloc(messageLength + 1);
   strcpy(messageWithCr, message); 
   messageWithCr[messageLength] = CR;
   uart_write_bytes(UART_PORT, messageWithCr, messageLength + 1);
   free(messageWithCr);
}

static bool readNextByte(uint8_t *data, TickType_t timeoutInMs) {
   int readByteCount = uart_read_bytes(UART_PORT, data, 1, timeoutInMs / portTICK_PERIOD_MS);
   return readByteCount == 1;
}

static bool responseBufferFull() {
   int bytesUsed = strlen(responseBuffer) + 1;
   return bytesUsed >= RESPONSE_BUFFER_SIZE;
}

static GsmStatus readNextLine(char *outputBuffer, int outputBufferSize, TickType_t timeoutInMs) {
   uint8_t nextByte;
   int responseBufferIndex       = strlen(responseBuffer);
   bool lineCopiedToOutputBuffer = false;
   bool lfReceived               = false;
   bool timedOut                 = false;
   TickType_t passedMillis       = 0;
   TickType_t ticksAtStart       = xTaskGetTickCount();

   while (!lfReceived && !timedOut && !responseBufferFull()) {
      if (readNextByte(&nextByte, timeoutInMs - passedMillis)) {
         responseBuffer[responseBufferIndex++] = nextByte;
         responseBuffer[responseBufferIndex]   = 0;
         lfReceived                            = nextByte == LF;
      }
      passedMillis = (xTaskGetTickCount() - ticksAtStart) * portTICK_PERIOD_MS;
      timedOut = passedMillis >= timeoutInMs;
   }

   if (lfReceived) {
      // remove trailing null byte, CR and LF
      char lastChar = responseBuffer[responseBufferIndex];
      while (lastChar == 0 || lastChar == CR || lastChar == LF) {
         responseBufferIndex--;
         if (responseBufferIndex < 0) {
            break;
         }
         lastChar = responseBuffer[responseBufferIndex];
      }
      responseBuffer[responseBufferIndex + 1] = 0;
      
      // skip leading CR and LF
      char *start = responseBuffer;
      while (*start == CR || *start == LF) {
         start++;
      }

      if ((strlen(start) + NULL_BYTE_LENGTH) > outputBufferSize) {
         ESP_LOGE(GSM_MODULE_TAG, "cannot copy received line (\"%s\") to outputBuffer because buffer is too small -> ignoring it.", start);
      } else {
         strcpy(outputBuffer, start);
         lineCopiedToOutputBuffer = true;
      }
      responseBuffer[0] = 0;
   } 

   GsmStatus status = lineCopiedToOutputBuffer ? GSM_OK : GSM_ERROR;

   if (timedOut) {
      ESP_LOGE(GSM_MODULE_TAG, "response timed out");
      status = GSM_TIMEOUT;
   }

   if (responseBufferFull()) {
      ESP_LOGE(GSM_MODULE_TAG, "overflow of response buffer -> flushing it (content: %s)", responseBuffer);
      responseBuffer[0] = 0;
   }

   return status;
}

static int countExpectedResponses(const char *expectedResponses) {
   int separatorCount = 0;
   int responseCount  = 0;

   if (expectedResponses != NULL && strlen(expectedResponses) > 0) {
      for(int i = 0; *(expectedResponses + i) != 0; i++) {
         if (*(expectedResponses + i) == PIPE_CHAR) {
               separatorCount++;
         }
      }
      responseCount = separatorCount + 1;
   }

   return responseCount;
}

/*
 * Returns the following status ...
 *
 *     GSM_OK                 when one of the expectedResponses was received
 *     GSM_TIMEOUT            when at least one line was received but it did not match and a timeout happened
 *     GSM_NOTHING_RECEIVED   when no line was received and a timeout happened
 *
 * expectedResponses     a string containing the expected responses separated by a "|". 
 *
 * timeoutInMs           timeout in milliseconds
 */
static GsmStatus assertResponse(const char *expectedResponses, TickType_t timeoutInMs) {
   char buffer[RESPONSE_BUFFER_SIZE];
   TickType_t ticksAtStart       = xTaskGetTickCount();
   TickType_t passedMilliseconds = 0;
   bool timedOut                 = false;
   bool expectedResponseReceived = false;
   bool atLeastOneLineReceived   = false;
   int responseCount             = countExpectedResponses(expectedResponses);
   char **allowedResponses       = malloc(responseCount * sizeof(char*));
   char *copyOfExpectedResponses = malloc((responseCount > 0 ? strlen(expectedResponses) : 0) + NULL_BYTE_LENGTH);
         
   if (responseCount > 0) {
      strcpy(copyOfExpectedResponses, expectedResponses);

      for (int i = 0; i <= (responseCount - 1); i++) {
         char *start = strtok(i == 0 ? copyOfExpectedResponses : NULL, "|");
         allowedResponses[i] = start;
      }
   }

   while (!timedOut && !expectedResponseReceived) {
      if (readNextLine(buffer, RESPONSE_BUFFER_SIZE, timeoutInMs - passedMilliseconds) == GSM_OK && strlen(buffer) > 0) {
         ESP_LOGI(GSM_MODULE_TAG, "in:  \"%s\"", buffer);
         atLeastOneLineReceived = true;
         for (int i = 0; i < responseCount && !expectedResponseReceived; i++) {
               expectedResponseReceived = strcmp(buffer, allowedResponses[i]) == 0;
         }
      }

      passedMilliseconds = (xTaskGetTickCount() - ticksAtStart) * portTICK_PERIOD_MS;
      timedOut = passedMilliseconds >= timeoutInMs;
   }

   
   GsmStatus status = GSM_NOTHING_RECEIVED;
   if (atLeastOneLineReceived) {
      status = expectedResponseReceived ? GSM_OK : GSM_TIMEOUT;
   }

   free(allowedResponses);
   free(copyOfExpectedResponses);

   return status;
}

static GsmStatus assertOkResponse() {
   return assertResponse("OK", SECONDS(5));
}

static bool isRedirection(int statusCode) {
   return statusCode == 301 || statusCode == 302 || statusCode == 303 || statusCode == 307 || statusCode == 308;
}

static void logRedirectionLocation(int statusCode) {
   if (isRedirection(statusCode)) {  
      char buffer[RESPONSE_BUFFER_SIZE];
      bool timedOut                 = false;
      bool okReceived               = false;
      TickType_t timeoutInMs        = SECONDS(10);
      TickType_t ticksAtStart       = xTaskGetTickCount();
      TickType_t passedMilliseconds = 0;
      
      sendCommand("AT+HTTPHEAD");
      
      while (!timedOut && !okReceived) {
         if ((readNextLine(buffer, RESPONSE_BUFFER_SIZE, timeoutInMs - passedMilliseconds) == GSM_OK) && (strlen(buffer) > 0)) {
            ESP_LOGI(GSM_MODULE_TAG, "in:  \"%s\"", buffer);
            okReceived = (strstr(buffer, "OK") == buffer);
            if ((strstr(buffer, "location") != NULL) || (strstr(buffer, "Location") != NULL)) {
               char *start = buffer;
               while(*start != 0 && *start != COLON) {
                  start++;
               }
               if (*start == COLON) {
                  start++;
               }
               while(*start != 0 && *start == SPACE) {
                  start++;
               }
               if (strlen(start) > 0) {
                  addErrorMessage(start);
               }
            }
         }

         passedMilliseconds = (xTaskGetTickCount() - ticksAtStart) * portTICK_PERIOD_MS;
         timedOut = passedMilliseconds >= timeoutInMs;
      }
   }
}


/**
 * Returns the HTTP status code or -1 if no response received.
 */
static int waitForHttpStatusCode() {
   char buffer[RESPONSE_BUFFER_SIZE];
   int statusCode                = -1;
   bool timedOut                 = false;
   bool statusCodeReceived       = false;
   TickType_t timeoutInMs        = SECONDS(10);
   TickType_t ticksAtStart       = xTaskGetTickCount();
   TickType_t passedMilliseconds = 0;
   
   while (!timedOut && !statusCodeReceived) {
      if ((readNextLine(buffer, RESPONSE_BUFFER_SIZE, timeoutInMs - passedMilliseconds) == GSM_OK) && (strlen(buffer) > 0)) {
         ESP_LOGI(GSM_MODULE_TAG, "in:  \"%s\"", buffer);
      
         if (strstr(buffer, "+HTTPACTION:") == buffer) {
            const char* separators[3] = {":", ",", ","};
            char *token;

            for (int i = 0; i < 3; i++) {
               token = strtok(i == 0 ? buffer : NULL, separators[i]);
               
               if (token == NULL) {
                  ESP_LOGI(GSM_MODULE_TAG, "failed to parse action response (index=%d, separator=%s)", i, separators[i]);
                  break;
               }    
            }

            if (token != NULL) {
               statusCode         = atoi(token);
               statusCodeReceived = true;
               ESP_LOGI(GSM_MODULE_TAG, "status code: %d", statusCode);
               logRedirectionLocation(statusCode);
            }       
         }
      }

      passedMilliseconds = (xTaskGetTickCount() - ticksAtStart) * portTICK_PERIOD_MS;
      timedOut = passedMilliseconds >= timeoutInMs;
   }

   return statusCode;
}

static bool executeCommands(const AT_COMMANDS *commands) {

   bool executedAllCommandsSuccessful = true;

   for(int index = 0; index < commands->count; index++) {
      sendCommand(commands->commands[index]);
      if (assertOkResponse() != GSM_OK) {
         executedAllCommandsSuccessful = false;
         ESP_LOGE(GSM_MODULE_TAG, "failed to execute command %d of %d", index + 1, commands->count);
         break;
      };
   }

   return executedAllCommandsSuccessful;
}

static void initPwrKeyPin() {
   gpio_config_t pinConfig;
   pinConfig.intr_type    = GPIO_INTR_DISABLE;
   pinConfig.mode         = GPIO_MODE_OUTPUT;
   pinConfig.pin_bit_mask = (1ULL << IO_PIN_FOR_PWRKEY);
   pinConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
   pinConfig.pull_up_en   = GPIO_PULLUP_DISABLE;
   ESP_ERROR_CHECK(gpio_config(&pinConfig));
   ESP_ERROR_CHECK(gpio_set_level(IO_PIN_FOR_PWRKEY, 0));
   ESP_LOGI(GSM_MODULE_TAG, "initialized pwrkey pin and set it to low");
}

static void initRelaisPin() {
   gpio_config_t pinConfig;
   pinConfig.intr_type    = GPIO_INTR_DISABLE;
   pinConfig.mode         = GPIO_MODE_OUTPUT;
   pinConfig.pin_bit_mask = (1ULL << IO_PIN_FOR_RELAIS);
   pinConfig.pull_down_en = GPIO_PULLDOWN_ENABLE;
   pinConfig.pull_up_en   = GPIO_PULLUP_DISABLE;
   ESP_ERROR_CHECK(gpio_config(&pinConfig));
   ESP_ERROR_CHECK(gpio_set_level(IO_PIN_FOR_PWRKEY, 0));
   ESP_LOGI(GSM_MODULE_TAG, "initialized relais pin and set it to low");
}

static void setPwrPinHighFor(int durationInMs) {
   ESP_LOGI(GSM_MODULE_TAG, "setting pwr pin to high for %d ms", durationInMs);
   ESP_ERROR_CHECK(gpio_set_level(IO_PIN_FOR_PWRKEY, 1));
   sleep( durationInMs);
   ESP_ERROR_CHECK(gpio_set_level(IO_PIN_FOR_PWRKEY, 0));
}

static void activateRelaisFor(int durationInMs) {
   ESP_LOGI(GSM_MODULE_TAG, "setting relais pin to high for %d ms", durationInMs);
   ESP_ERROR_CHECK(gpio_set_level(IO_PIN_FOR_RELAIS, 1));
   sleep( durationInMs);
   ESP_ERROR_CHECK(gpio_set_level(IO_PIN_FOR_RELAIS, 0));
}

static void waitTillGsmModuleAcceptsPowerKey() {
   TickType_t durationInMs = SECONDS(1);
   ESP_LOGI(GSM_MODULE_TAG, "waiting %d ms for GSM module till it starts accepting power key interactions", durationInMs);
   sleep(durationInMs);
}

static TickType_t min(TickType_t a, TickType_t b) {
   return a < b ? a : b;
}

static bool waitForGsmModuleToGetAvailable() {
   ESP_LOGI(GSM_MODULE_TAG, "--- waiting for GSM module to get available ...");
   bool gsmModuleReplied = false;
   
   // it is necessary to repeat it twice because the GSM module could already be available -> then it needs to be restarted to be in a defined state
   for(int retry = 0; retry < 2 && !gsmModuleReplied; retry++) {
      setPwrPinHighFor(PWR_PIN_HIGH_DURATION);
      gsmModuleReplied = assertResponse("RDY", 5000) == GSM_OK;
   }
   
   ON_ERROR_RECORD("GSM_MODULE_DID_NOT_SEND_RDY")

   ESP_LOGI(GSM_MODULE_TAG, "--- waiting for READY message");
   gsmModuleReplied = assertResponse("+CPIN: READY", SECONDS(5)) == GSM_OK;
   
   ON_ERROR_RECORD("GSM_MODULE_DID_NOT_SEND_CPIN_READY");
   
   sendCommand("ATE0");
   gsmModuleReplied = assertOkResponse() == GSM_OK;
   
   ON_ERROR_RECORD("GSM_MODULE_NO_ANSWER_FOR_ATE0_CMD")

   return true;
}

static bool waitForNetworkRegistration() {
   bool timedOut               = false;
   TickType_t timeoutInMs      = SECONDS(20);
   TickType_t passedMillis     = 0;
   TickType_t ticksAtStart     = xTaskGetTickCount();
   int nothingReceivedCount    = 0;
   int maxNothingReceivedCount = 2;
   bool registeredSuccessfully = false;
   
   ESP_LOGI(GSM_MODULE_TAG, "--- waiting for network registration");

   while(!timedOut && !registeredSuccessfully && nothingReceivedCount < maxNothingReceivedCount) {
      sendCommand("AT+CREG?");
      GsmStatus status = assertResponse("+CREG: 0,1|+CREG: 0,5", min(SECONDS(1), timeoutInMs - passedMillis));
      if (status == GSM_OK) {
            status = assertOkResponse();
      }
      registeredSuccessfully = (status == GSM_OK);
      nothingReceivedCount   = (status == GSM_NOTHING_RECEIVED) ? nothingReceivedCount + 1 : 0;
      
      if (!registeredSuccessfully && nothingReceivedCount < maxNothingReceivedCount) {
            passedMillis = (xTaskGetTickCount() - ticksAtStart) * portTICK_PERIOD_MS;
            sleep(min(SECONDS(1), timeoutInMs - passedMillis));    
      }
   
      passedMillis = (xTaskGetTickCount() - ticksAtStart) * portTICK_PERIOD_MS;
      timedOut     = passedMillis >= timeoutInMs;
   }

   if (!registeredSuccessfully) {
      addErrorMessage("GSM_MODULE_DID_NOT_REGISTER");
   }

   return registeredSuccessfully;
}

static void powerDownGsmModule() {
   ESP_LOGI(GSM_MODULE_TAG, "--- power down via command ...");
   sendCommand("AT+CPOWD=1");
   GsmStatus status = assertResponse("NORMAL POWER DOWN", SECONDS(1));

   if (status != GSM_OK) {
      ESP_LOGI(GSM_MODULE_TAG, "--- power down via pin ...");
      setPwrPinHighFor(PWR_PIN_HIGH_DURATION);
      assertResponse("NORMAL POWER DOWN", SECONDS(5));
   }
}

bool sendHttpPostRequest(const char* url, const char* data) {
   bool sentSuccessfully = false;
   int urlCommandLength  = strlen(url) + strlen("AT+HTTPPARA=\"URL\",\"\"") + NULL_BYTE_LENGTH;
   char *urlCommand      = malloc(urlCommandLength);
   sprintf(urlCommand, "AT+HTTPPARA=\"URL\",\"%s\"", url);
   
   const AT_COMMANDS configureHttpCommands = { 3, (const char*[]) {        
      "AT+HTTPPARA=\"CID\",1",
      urlCommand,
      "AT+HTTPPARA=\"CONTENT\",\"application/json\""
   }};

   const AT_COMMANDS triggerPostActionCommands = { 1, (const char*[]) { "AT+HTTPACTION=1", urlCommand }};

   ESP_LOGI(GSM_MODULE_TAG, "--- triggering HTTP POST action ...");
   if(executeCommands(&configureHttpCommands)) {
      int dataLength        = strlen(data);
      int dataCommandLength = dataLength + strlen("AT+HTTPDATA=,") + charCountOf(dataLength) + charCountOf(MAX_INPUT_TIME_MS) + NULL_BYTE_LENGTH;
      char *dataCommand     = malloc(dataCommandLength);
      sprintf(dataCommand, "AT+HTTPDATA=%d,%d", dataLength, MAX_INPUT_TIME_MS);
      sendCommand(dataCommand);
      free(dataCommand);

      if(assertResponse("DOWNLOAD", SECONDS(1)) == GSM_OK) {
         sendCommand(data);
         if (assertOkResponse() == GSM_OK) {
               sentSuccessfully = executeCommands(&triggerPostActionCommands);
         }
      }
   }
   free(urlCommand);

   if (!sentSuccessfully) {
      addErrorMessage("GSM_MODULE_FAILED_TO_SEND_HTTP_POST_REQUEST");
   }

   return sentSuccessfully;
}

static void activateGsmModule() {
   if (!baudrateConfigured) {
      return;
   }

   bool isReady = false;

   size_t maxRetries = 2;
   for (size_t retry = 0; retry < maxRetries && !isReady; retry++) {
      isReady = waitForGsmModuleToGetAvailable();
      if (!isReady && (retry < (maxRetries - 1))) {
         ESP_LOGI(GSM_MODULE_TAG, "interrupting power supply of GSM module for %d ms ...", RELAIS_ACTIVE_DURATION);
         addErrorMessage("GSM_MODULE_INTERRUPT_POWER");
         activateRelaisFor(RELAIS_ACTIVE_DURATION);
         waitTillGsmModuleAcceptsPowerKey();
      }
      if(isReady) {
         isReady = waitForNetworkRegistration();
      }
   }

   moduleReadyTime = isReady ? time(NULL) : 0;
   gsmModuleReady  = isReady;
}

static void configureBaudrateOfGsmModule() {
   ESP_LOGI(GSM_MODULE_TAG, "--- setting fixed baudrate of gsm module ...");
   
   ESP_ERROR_CHECK(uart_flush(UART_PORT));
   waitTillGsmModuleAcceptsPowerKey();
   
   GsmStatus status = GSM_ERROR;

   ESP_LOGI(GSM_MODULE_TAG, "checking if gsm modules replies with 19200 ...");
   ESP_ERROR_CHECK(uart_set_baudrate(UART_PORT, 19200));
   // it is necessary to repeat it twice because the GSM module could already be available -> then it needs to be restarted to be in a defined state
   for(int i = 0; (i < 2) && (status != GSM_OK); i++) {
      setPwrPinHighFor(PWR_PIN_HIGH_DURATION);
      status = assertResponse("RDY", 5000);
   }

   if (status != GSM_OK) {
      ESP_LOGI(GSM_MODULE_TAG, "no RDY received at 19200 -> trying auto baud detection at 115200 ...");
      ESP_ERROR_CHECK(uart_set_baudrate(UART_PORT, 115200));
      // it is necessary to repeat it twice because the GSM module could already be available -> then it needs to be restarted to be in a defined state
      for (int i = 0; (i < 2) && (status != GSM_OK); i++) {
         setPwrPinHighFor(PWR_PIN_HIGH_DURATION);
         for(int j = 0; (j < 10) && (status != GSM_OK); j++) {
            sendCommand("AT");
            status = assertResponse("OK", 1000);
         }
      }
   }

   if (status == GSM_OK) {
      status = GSM_ERROR;
      
      sendCommand("ATE0");
      status = assertOkResponse();
      
      if (status == GSM_OK) {
         sendCommand("AT+IPR=19200");
         status = assertOkResponse();
      }

      ESP_ERROR_CHECK(uart_set_baudrate(UART_PORT, 19200));
      
      if (status == GSM_OK) {
         sendCommand("AT+IPR?");
         status = assertResponse("+IPR: 19200", 1000);
      }
      if (status == GSM_OK) {
         sendCommand("AT&W");
         status = assertOkResponse();
      }
   }

   baudrateConfigured = (status == GSM_OK);
   if (baudrateConfigured) {
      ESP_LOGI(GSM_MODULE_TAG, "successfully set baudrate of gsm module");
      powerDownGsmModule();
   } else {
      addErrorMessage("GSM_MODULE_FAILED_TO_SET_BAUDRATE");
      ESP_LOGE(GSM_MODULE_TAG, "failed to set baudrate of gsm module");   
   }   
}

static void interruptPowerSupply() {
   if (gsmModuleReady) {
      powerDownGsmModule();
   }
   activateRelaisFor(MODULE_POWER_SUPPLY_OFF_DURATION);
   failedSendAttempts = 0;
   baudrateConfigured = false;
   gsmModuleReady     = false;
}

void initializeGsmModule() {
   if (!uartAndGpioInitialized) {
      ESP_LOGI(GSM_MODULE_TAG, "--- initializing IO pins and UART ...");
      initPwrKeyPin();
      initRelaisPin();
      initUart();
      uartAndGpioInitialized = true;
   }

   if (!baudrateConfigured) {
      configureBaudrateOfGsmModule();
   }
}

int send(const char* url, const char* data)
{        
   int httpStatusCode = HTTP_RESPONSE_ERROR;
   responseBuffer[0] = 0;

   if (!gsmModuleReady) {
      initializeGsmModule();
      activateGsmModule();
   }
   
   if (!gsmModuleReady) {
      ESP_LOGE(GSM_MODULE_TAG, "gsm module not ready -> interrupting power supply of gsm module ...");
      addErrorMessage("GSM_MODULE_NOT_READY");
      interruptPowerSupply();
   } else { 
      ESP_LOGI(GSM_MODULE_TAG, "--- initializing bearer ...");
      if (!executeCommands(&initBearerCommands)) {
         addErrorMessage("GSM_MODULE_FAILED_TO_INIT_BEARER");
      } else {
         ESP_LOGI(GSM_MODULE_TAG, "--- initializing HTTP ...");
         if (!executeCommands(&initHttpCommands)) {
            addErrorMessage("GSM_MODULE_FAILED_TO_INIT_HTTP");
         } else {
            sendHttpPostRequest(url, data);
            ESP_LOGI(GSM_MODULE_TAG, "--- waiting for HTTP response ...");
            httpStatusCode = waitForHttpStatusCode();
            ESP_LOGI(GSM_MODULE_TAG, "--- terminating HTTP ...");
            executeCommands(&terminateHttpCommands);
         }
         ESP_LOGI(GSM_MODULE_TAG, "--- terminating bearer ...");
         executeCommands(&terminateBearerCommands);
      }
   }

   if (httpStatusCode == HTTP_RESPONSE_OK) {
      failedSendAttempts = 0;
   } else {
      failedSendAttempts++;
   }
 
   time_t moduleReadyDurationInSeconds = time(NULL) - moduleReadyTime;

   if (gsmModuleReady && moduleReadyDurationInSeconds >= ONE_DAY_IN_SECONDS) {
      ESP_LOGI(GSM_MODULE_TAG, "performing daily restart of gsm module ...");
      interruptPowerSupply();
   }
   
   if (failedSendAttempts >= FAILED_SEND_ATTEMPTS_TO_RESTART_GSM_MODULE) {
      ESP_LOGI(GSM_MODULE_TAG, "number (%d) of maximum failed send attempts reached -> interrupting power supply of gsm module ...", FAILED_SEND_ATTEMPTS_TO_RESTART_GSM_MODULE);
      addErrorMessage("GSM_MODULE_RESET_POWER");
      interruptPowerSupply();
   }
   
   return httpStatusCode;
}