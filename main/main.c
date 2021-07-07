#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "soc/rtc_periph.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/adc.h"
#include "esp32/ulp.h"
#include "ulp_main.h"
#include "sdkconfig.h"

#include "ErrorMessages.h"
//#include "wifi.h"
#include "GsmModule.h"

#define MEASUREMENTS_PER_PUBLISHMENT 60

// digital outputs
#define D4   GPIO_NUM_4
#define D15  GPIO_NUM_15

// digital inputs
#define D12  GPIO_NUM_12
#define D13  GPIO_NUM_13
#define D14  GPIO_NUM_14
#define D25  GPIO_NUM_25
#define D26  GPIO_NUM_26
#define D27  GPIO_NUM_27
#define D32  GPIO_NUM_32
#define D33  GPIO_NUM_33

// analog inputs
#define D36  GPIO_NUM_36

#define MILLIS(ms) ((ms) * 1000)

#define MAX_MESSAGE_SEQUENCE_ID        999
#define MESSAGE_VERSION                "1.0.0"

static const char* TAG = "main";

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static void initUlp();
static void loadUlpProgram();
static void startUlpProgram();

static char binaryData[9];

RTC_DATA_ATTR int nextSequenceId;

static int getNextSequenceId() {
   int result = nextSequenceId;
   nextSequenceId = (nextSequenceId + 1) % (MAX_MESSAGE_SEQUENCE_ID + 1);
   return result;
}

static void convertToBinaryRepresentation(uint32_t data) {
   int index = 0;

   while(index < 9) {
      int lsb = data & 1;
      binaryData[8 - index] = lsb == 1 ? '1' : '0';
      data = data >> 1; 
      if(index == 3) {
         index++;
         binaryData[8 - index] = ' ';
      }
      index++;
   }
}

static size_t countSubstrings(const char *text, const char *substring) {
   size_t count = 0;
   const char *position = text;
   position = strstr(position, substring);
   while(position != 0) {
      count++;
      position = strstr(position + strlen(substring), substring);
   }
   return count;
}

static size_t lengthWithoutPlaceholders(char *text) {
   char *stringPlaceholder ="%s";
   char *decimalPlaceholder ="%d";
   size_t stringPlaceHolderCount = countSubstrings(text, stringPlaceholder);
   size_t decimalPlaceHolderCount = countSubstrings(text, decimalPlaceholder);
   size_t length = strlen(text) - (stringPlaceHolderCount * strlen(stringPlaceholder)) - (decimalPlaceHolderCount * strlen(decimalPlaceholder));
   return length;
}

// the caller has to free the returned pointer!!!
static char* createJsonPayload() {
   char *format                     = "{\"version\":\"%s\",\"sequenceId\":%d,\"anemometerPulses\":[%s],\"directionVaneValues\":[%s],\"errors\":[%s]}";
   int nullByteLength               = 1;
   int maxDecimalDigitsPerValue     = 5; // 2^16 in decimal requires up to 5 digits
   int maxSequenceIdDigits          = (int)(ceilf(log10f(MAX_MESSAGE_SEQUENCE_ID)));
   int separatorCount               = MEASUREMENTS_PER_PUBLISHMENT - 1;
   int maxDataLengthInDigits        = (MEASUREMENTS_PER_PUBLISHMENT * maxDecimalDigitsPerValue) + separatorCount;
   int maxDataLengthInBytes         = (maxDataLengthInDigits * sizeof(char)) + nullByteLength;
   const char* errors               = getErrorMessages();
   bool noErrors                    = strlen(errors) == 0;
   const char* errorSeparator       = getErrorMessageSeparator();
   int errorCount                   = noErrors ? 0: countSubstrings(errors, errorSeparator) + 1;
   int doubleQuotesCount            = 2 * errorCount;
   int errorsDataLengthInBytes      = (noErrors ? 0 : strlen(errors) + doubleQuotesCount) + nullByteLength;

   char *anemometerData             = malloc(maxDataLengthInBytes);
   char *directionVaneData          = malloc(maxDataLengthInBytes);
   char *errorsData                 = malloc(errorsDataLengthInBytes);
   uint32_t *anemometerPulses       = &ulp_anemometerPulses;
   uint32_t *directionVaneValues    = &ulp_directionVaneValues;
   char *anemometerDataPosition     = anemometerData;
   char *directionVaneDataPosition  = directionVaneData;

   for (int i = 0; i < MEASUREMENTS_PER_PUBLISHMENT; i++) {
      uint32_t pulses = (*(anemometerPulses++)) & 0x00ff;
      uint32_t direction = (*(directionVaneValues++)) & 0xffff;
      sprintf(anemometerDataPosition, i < 1 ? "%d" : ",%d", pulses);
      sprintf(directionVaneDataPosition, i < 1 ? "%d" : ",%d", direction);
      anemometerDataPosition += strlen(anemometerDataPosition);
      directionVaneDataPosition += strlen(directionVaneDataPosition);
   }
   
   *errorsData = 0;
   int offset  = 0;
   char copyOfErrors[strlen(errors)]; // this copy is needed because strtok inserts null characters at the end of each token
   strcpy(copyOfErrors, errors);

   char* token = strtok(copyOfErrors, errorSeparator);

   while(token != NULL) {
      char* separator = (offset == 0) ? "" : ",";
      sprintf(errorsData + offset, "%s\"%s\"", separator, token);
      offset += strlen(token) + 2 + strlen(separator);
      token = strtok(NULL, errorSeparator);
   }

   int maxPayloadLength = lengthWithoutPlaceholders(format) + strlen(MESSAGE_VERSION) + maxSequenceIdDigits + strlen(anemometerData) + strlen(directionVaneData) + strlen(errorsData);
   char *payload = malloc((maxPayloadLength * sizeof(char)) + nullByteLength);
   sprintf(payload, format, MESSAGE_VERSION, getNextSequenceId(), anemometerData, directionVaneData, errorsData);
   free(directionVaneData);
   free(anemometerData);
   free(errorsData);
   return payload;
}

void app_main()
{   
   esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
   if (cause != ESP_SLEEP_WAKEUP_ULP) {
      ESP_LOGI(TAG, "first startup -> initializing ULP");
      initUlp();
      loadUlpProgram();
      nextSequenceId = 0;
      clearErrorMessages();
   } else {
      convertToBinaryRepresentation(ulp_trace & 0xffff);
      ESP_LOGI(TAG, "ULP wakeup trace = %s", binaryData);
      const char* errorMessages = getErrorMessages();
      if (strlen(errorMessages) > 0) {
         ESP_LOGW(TAG, "not yet delivered errorMessages = %s", errorMessages);
      }
      char* jsonMessage = createJsonPayload();
      
      int httpResponseCode = 0;
      int ok = 200;
      for (int retries = 0; retries < 2 && httpResponseCode != ok; retries++) {
         httpResponseCode = send(CONFIG_WINDSENSOR_SERVICE_URL, jsonMessage);
         if (httpResponseCode != ok && httpResponseCode != 0) {
            char message[25];
            sprintf(message, "HTTP_RESPONSE_CODE_%d", httpResponseCode);
            addErrorMessage(message);
         }
      }

      if (httpResponseCode == ok) {
         clearErrorMessages();
      }

      free(jsonMessage);
   }

   startUlpProgram();

   ESP_LOGI(TAG, "disabling all wakeup sources");
   ESP_ERROR_CHECK( esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) );
   ESP_LOGI(TAG, "enabling ULP wakeup");
   ESP_ERROR_CHECK( esp_sleep_enable_ulp_wakeup() );
   ESP_LOGI(TAG, "Entering deep sleep");
   esp_deep_sleep_start();
}

static void initializeDigitalOutputs() 
{
   gpio_num_t digitalOutputs[] = {D4, D15};

   for (int i=0; i < 2; i++) {
      gpio_num_t gpio_num = digitalOutputs[i];
      ESP_LOGI(TAG, "initializing output GPIO %d (RTC %d)", digitalOutputs[i], rtc_io_desc[gpio_num].rtc_num);
      rtc_gpio_init(gpio_num);
      rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_OUTPUT_ONLY);
   }
}

static void initializeDigitalInputs() 
{
   gpio_num_t digitalInputs[] = {D12, D13, D14, D25, D26, D27, D32, D33};

   for (int i=0; i < 8; i++) {
      gpio_num_t gpio_num = digitalInputs[i];
      ESP_LOGI(TAG, "initializing digital input GPIO %d (RTC %d)", digitalInputs[i], rtc_io_desc[gpio_num].rtc_num);
      rtc_gpio_init(gpio_num);
      rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
   }
}

static void initializeAnalogInputs() 
{
   ESP_LOGI(TAG, "initializing analog input GPIO 36");
   adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11);
   adc1_config_width(ADC_WIDTH_BIT_12);
   adc1_ulp_enable();
}

static void initUlp()
{
   initializeDigitalOutputs();
   initializeDigitalInputs();
   initializeAnalogInputs();
   esp_deep_sleep_disable_rom_logging(); // suppress boot messages
}

static void loadUlpProgram()
{
   esp_err_t err = ulp_load_binary(0, ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
   ESP_ERROR_CHECK(err);
   /* initialize shared variables */
   ulp_measurementsPerPublishment = MEASUREMENTS_PER_PUBLISHMENT;
}

static void startUlpProgram()
{
   ESP_LOGI(TAG, "starting ULP program");
   
   /* initialize shared variables */
   ulp_initialize = 1;
   
   /* Set ULP wake up period to 1000ms */
   ulp_set_wakeup_period(0, MILLIS(1000));

   /* Start the ULP program */
   esp_err_t err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
   ESP_ERROR_CHECK(err);
}
