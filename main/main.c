#include <string.h>
#include <math.h>

#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_intr_alloc.h"
#include "soc/rtc_periph.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "driver/adc.h"
#include "sdkconfig.h"

#include "ErrorMessages.h"
#include "GsmModule.h"
#include "MessageFormatter.h"

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

#define MAX_PULSES_PER_SECOND          89
#define OK_RESPONSE                    200

static const char* TAG                       = "main";
static const char* HTTP_RESPONSE_TIMED_OUT   = "HTTP_RESPONSE_TIMED_OUT";

static void resetMeasuredValues();
static void initializePins();
static void initializeAnemometerInputPin();
static void initializeDirectionVanePin();
static void sendMeasuredValuesToServer();

uint16_t anemometerPulses[MEASUREMENTS_PER_PUBLISHMENT];
uint16_t directionVaneValues[MEASUREMENTS_PER_PUBLISHMENT];
size_t nextIndex = 0;

uint16_t pulseCount;

static xQueueHandle anemometerQueue;

static void debouceTask(void* arg)
{
   uint8_t value;
   size_t debounceDelayInTicks = (1000 / MAX_PULSES_PER_SECOND) / portTICK_RATE_MS;
   
   for(;;) {
      if(xQueueReceive(anemometerQueue, &value, 500 / portTICK_RATE_MS)) {
         pulseCount++;
         vTaskDelay(debounceDelayInTicks);
         xQueueReceive(anemometerQueue, &value, 0);
      }
   }
}

static void valueCollectorTask(void* arg)
{
   for(;;) {
      vTaskDelay(1000 / portTICK_RATE_MS);
      uint16_t pulses         = pulseCount;
      int directionVaneValue  = adc1_get_raw(ADC1_CHANNEL_6) & 0xfff;

      if (nextIndex < MEASUREMENTS_PER_PUBLISHMENT) {
         size_t index = nextIndex++;
         anemometerPulses[index]    = pulses;
         directionVaneValues[index] = directionVaneValue;
         ESP_LOGI(TAG, "[%02d] pulses = %d, direction = %04x", nextIndex - 1, pulses, directionVaneValue);
      }

      if (nextIndex >= MEASUREMENTS_PER_PUBLISHMENT) {
         sendMeasuredValuesToServer();
         resetMeasuredValues();
         nextIndex = 0;
      }
      
      pulseCount = 0;
   }
}

static void resetMeasuredValues() {
   for (size_t i=0; i < MEASUREMENTS_PER_PUBLISHMENT; i++) {
      anemometerPulses[i]    = 0;
      directionVaneValues[i] = 0;
   }
}

static void sendMeasuredValuesToServer() {
   const char* errorMessages = getErrorMessages();
   if (strlen(errorMessages) > 0) {
      ESP_LOGW(TAG, "not yet delivered errorMessages = %s", errorMessages);
   }

   char* jsonMessage = createJsonPayload(anemometerPulses, directionVaneValues, MEASUREMENTS_PER_PUBLISHMENT);
   ESP_LOGI(TAG, "JSON message = %s", jsonMessage);

   int httpResponseCode = 0;
   for (int retries = 0; retries < 2 && httpResponseCode != OK_RESPONSE; retries++) {
      
      httpResponseCode = send(CONFIG_WINDSENSOR_SERVICE_URL, jsonMessage);
      
      if (httpResponseCode != OK_RESPONSE && httpResponseCode != 0) {
         if (httpResponseCode == -1) {
            addErrorMessage(HTTP_RESPONSE_TIMED_OUT);
         } else {
            char message[25];
            sprintf(message, "HTTP_RESPONSE_CODE_%d", httpResponseCode);
            addErrorMessage(message);
         }
      }
   }

   if (httpResponseCode == OK_RESPONSE) {
      clearErrorMessages();
   }

   free(jsonMessage);
}

void app_main() {   
   esp_deep_sleep_disable_rom_logging(); // suppress boot messages
   nextIndex      = 0;
   pulseCount     = 0;
   resetMeasuredValues();
   
   anemometerQueue = xQueueCreate(1, sizeof(uint8_t));
   if (anemometerQueue == NULL) {
      ESP_LOGE(TAG, "failed to create queue for anemometer pulses");
   }
   xTaskCreate(debouceTask, "anemometerInputDebouceTask", 2048, NULL, 10, NULL);
      
   initializePins();

   xTaskCreate(valueCollectorTask, "valueCollectorTask", 2048, NULL, 10, NULL);
}

static void initializePins()
{
   initializeAnemometerInputPin();
   initializeDirectionVanePin();
}

static void IRAM_ATTR onAnemometerPulse(void* arg)
{
   BaseType_t xHigherPriorityTaskWoken;
   uint8_t value = 0;
   xQueueSendFromISR(anemometerQueue, &value, &xHigherPriorityTaskWoken);
}

static void initializeAnemometerInputPin() 
{
   ESP_LOGI(TAG, "initializing anemometer input pin");
   gpio_config_t io_conf = {};
   io_conf.intr_type     = GPIO_INTR_POSEDGE;
   io_conf.mode          = GPIO_MODE_INPUT;
   io_conf.pin_bit_mask  = (1ULL << D25);
   io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
   io_conf.pull_up_en    = GPIO_PULLUP_ENABLE;

   if (gpio_config(&io_conf) != ESP_OK) {
      ESP_LOGE(TAG, "failed to initialize anemomenter input pin");
   }

   if (gpio_install_isr_service(ESP_INTR_FLAG_EDGE) != ESP_OK) {
      ESP_LOGE(TAG, "failed to install the drivers GPIO ISR handler service");
   }
   
   if (gpio_isr_handler_add(D25, onAnemometerPulse, NULL) != ESP_OK) {
      ESP_LOGE(TAG, "failed add ISR handler");
   }
}

static void initializeDirectionVanePin() 
{
   ESP_LOGI(TAG, "initializing analog input GPIO34");
   if (adc1_config_channel_atten(ADC1_CHANNEL_0, ADC_ATTEN_DB_11) != ESP_OK) {
      ESP_LOGE(TAG, "failed to set the attenuation of a particular channel on ADC1");
   }
   if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) {
      ESP_LOGE(TAG, "failed to configure ADC1 capture width");
   }
}