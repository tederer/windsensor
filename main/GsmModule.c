#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/uart.h"

#include "ErrorMessages.h"
#include "GsmModule.h"

#define UART_PORT                  UART_NUM_2
#define IO_PIN_FOR_PWRKEY          GPIO_NUM_2
#define IO_PIN_FOR_RELAIS          GPIO_NUM_4
#define MILLIS_PER_SECOND          1000
#define SECONDS(value)             (value * MILLIS_PER_SECOND) 
#define PWR_PIN_HIGH_DURATION      SECONDS(1.6)
#define RELAIS_ACTIVE_DURATION     SECONDS(1)
#define CR                         0x0d
#define LF                         0x0a
#define PIPE_CHAR                  0x7c
#define CRLF                       "\r\n"
#define RESPONSE_BUFFER_SIZE       128
#define OK_RESPONSE                "OK"
#define NULL_BYTE_LENGTH           1
#define MAX_INPUT_TIME_MS          3000

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

char responseBuffer[RESPONSE_BUFFER_SIZE];
bool uartAndGpioInitialized = false;

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

static GsmStatus readNextLine(char *outputBuffer, int outputBufferSize, TickType_t timeoutInMs) {
   uint8_t nextByte;
   int responseBufferIndex       = strlen(responseBuffer);
   bool lineCopiedToOutputBuffer = false;
   bool lfReceived               = false;
   bool timedOut                 = false;
   bool responseBufferFull       = responseBufferIndex >= (RESPONSE_BUFFER_SIZE - 1);
   TickType_t passedMillis       = 0;
   TickType_t ticksAtStart       = xTaskGetTickCount();

   while (!lfReceived && !timedOut && !responseBufferFull) {
      if (readNextByte(&nextByte, timeoutInMs - passedMillis)) {
         responseBuffer[responseBufferIndex++] = nextByte;
         responseBuffer[responseBufferIndex]   = 0;
         lfReceived                            = nextByte == LF;
         responseBufferFull                    = responseBufferIndex >= (RESPONSE_BUFFER_SIZE - 1);
      }
      passedMillis = (xTaskGetTickCount() - ticksAtStart) * portTICK_PERIOD_MS;
      timedOut = passedMillis >= timeoutInMs;
   }

   if (lfReceived) {
      // remove trailing null byte, CR and LF
      char lastChar = responseBuffer[responseBufferIndex];
      while (lastChar == 0 || lastChar == CR || lastChar == LF) {
         responseBufferIndex--;
         lastChar = responseBuffer[responseBufferIndex];
      }
      responseBuffer[responseBufferIndex + 1] = 0;
      
      // skip leading CR and LF
      char *start = responseBuffer;
      while (*start == CR || *start == LF) {
         start++;
      }

      if (strlen(responseBuffer) > (outputBufferSize - 1)) {
         ESP_LOGE(GSM_MODULE_TAG, "cannot copy received line (\"%s\") to outputBuffer because buffer is too small -> ignoring it.", responseBuffer);
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

   if (responseBufferFull) {
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
         bool matchFound        = false;
         for (int i = 0; i < responseCount && !matchFound; i++) {
               matchFound = strcmp(buffer, allowedResponses[i]) == 0;
         }
         expectedResponseReceived = expectedResponseReceived || matchFound;
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
   return assertResponse("OK", SECONDS(1));
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
      GsmStatus status = readNextLine(buffer, RESPONSE_BUFFER_SIZE, timeoutInMs - passedMilliseconds);

      if (status == GSM_OK) {
         if (strlen(buffer) > 0) {
               ESP_LOGI(GSM_MODULE_TAG, "in:  \"%s\"", buffer);
         }
         
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
   bool gsmModuleReplied       = false;
   bool registeredSuccessfully = false;
   
   ESP_LOGI(GSM_MODULE_TAG, "--- waiting for GSM module to get available ...");
   
   // it is necessary to repeat it twice because the GSM module could already be available -> then it needs to be restarted to be in a defined state
   for(int retry = 0; retry < 2 && !gsmModuleReplied; retry++) {
      setPwrPinHighFor(PWR_PIN_HIGH_DURATION);

      for(int i = 0; i < 5 && !gsmModuleReplied; i++) {
         sendCommand("AT");
         gsmModuleReplied = assertOkResponse() == GSM_OK;
      }
   }
   
   if (gsmModuleReplied) {
      sendCommand("ATE0");
      gsmModuleReplied = assertOkResponse() == GSM_OK;
   }

   if (gsmModuleReplied) {
      ESP_LOGI(GSM_MODULE_TAG, "--- waiting for READY message");
      gsmModuleReplied = assertResponse("+CPIN: READY", SECONDS(5)) == GSM_OK;
   }

   if (!gsmModuleReplied) {
      addErrorMessage("GSM_MODULE_DID_NOT_REPLY");
   } else {
      bool timedOut               = false;
      TickType_t timeoutInMs      = SECONDS(20);
      TickType_t passedMillis     = 0;
      TickType_t ticksAtStart     = xTaskGetTickCount();
      int nothingReceivedCount    = 0;
      int maxNothingReceivedCount = 2;

      ESP_LOGI(GSM_MODULE_TAG, "--- waiting for network registration");

      while(!timedOut && !registeredSuccessfully && nothingReceivedCount < maxNothingReceivedCount) {
         sendCommand("AT+CREG?");
         GsmStatus status = assertResponse("+CREG: 0,1|+CREG: 0,5", min(SECONDS(1), timeoutInMs - passedMillis));
         if (status == GSM_OK) {
               status = assertOkResponse();
         }
         registeredSuccessfully = status == GSM_OK;
         nothingReceivedCount   = status == GSM_NOTHING_RECEIVED ? nothingReceivedCount + 1 : 0;
         
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
   }

   return registeredSuccessfully;
}

static void powerOffGsmModule() {
   ESP_LOGI(GSM_MODULE_TAG, "--- power off via command ...");
   sendCommand("AT+CPOWD=1");
   GsmStatus status = assertResponse("NORMAL POWER DOWN", SECONDS(1));

   if (status != GSM_OK) {
      ESP_LOGI(GSM_MODULE_TAG, "--- power off via pin ...");
      setPwrPinHighFor(PWR_PIN_HIGH_DURATION);
      assertResponse("NORMAL POWER DOWN", SECONDS(5));
   }
}

int charCountOf(int number) {
   return ceil(log10(number));
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

static bool activateGsmModule() {
   bool moduleIsAvailable = false;

   size_t maxRetries = 2;
   for (size_t retry = 0; retry < maxRetries && !moduleIsAvailable; retry++) {
      moduleIsAvailable = waitForGsmModuleToGetAvailable();
      if (!moduleIsAvailable && (retry < (maxRetries -1))) {
         activateRelaisFor(RELAIS_ACTIVE_DURATION);
      }
   }

   return moduleIsAvailable;
}

int send(const char* url, const char* data)
{        
   int httpStatusCode = 0;
   responseBuffer[0] = 0;

   if (!uartAndGpioInitialized) {
      ESP_LOGI(GSM_MODULE_TAG, "--- initializing IO pins and UART ...");
      uartAndGpioInitialized = true;
      initPwrKeyPin();
      initRelaisPin();
      initUart();
   }

   waitTillGsmModuleAcceptsPowerKey();
   ESP_ERROR_CHECK(uart_flush(UART_PORT));
   
   if (activateGsmModule()) {
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

   powerOffGsmModule();
   return httpStatusCode;
}