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

#define MAX_MESSAGE_SEQUENCE_ID     999
#define MESSAGE_VERSION             "1.0.0"

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

static size_t countPlaceholders(char *text, char *placeholder) {
    size_t placeholderCount = 0;
    char *position = text;
    position = strstr(position, placeholder);
    while(position != 0) {
        placeholderCount++;
        position = strstr(position + strlen(placeholder), placeholder);
    }
    return placeholderCount;
}

static size_t lengthWithoutPlaceholders(char *text) {
    char *stringPlaceholder ="%s";
    char *decimalPlaceholder ="%d";
    size_t stringPlaceHolderCount = countPlaceholders(text, stringPlaceholder);
    size_t decimalPlaceHolderCount = countPlaceholders(text, decimalPlaceholder);
    size_t length = strlen(text) - (stringPlaceHolderCount * strlen(stringPlaceholder)) - (decimalPlaceHolderCount * strlen(decimalPlaceholder));
    return length;
}

// the caller has to free the returned pointer!!!
static char* createJsonPayload() {
    char *format = "{\"version\":\"%s\",\"sequenceId\":%d,\"anemometerPulses\":[%s],\"directionVaneValues\":[%s]}";
    int nullByte = 1;
    int maxDecimalDigitsPerValue = 5; // 2^16 in decimal requires up to 5 digits
    int maxSequenceIdDigits = (int)(ceilf(log10f(MAX_MESSAGE_SEQUENCE_ID)));
    int separatorCount = MEASUREMENTS_PER_PUBLISHMENT - 1;
    int maxDataLength = (MEASUREMENTS_PER_PUBLISHMENT * maxDecimalDigitsPerValue) + separatorCount;
    int maxDataLengthInBytes = (maxDataLength * sizeof(char)) + nullByte;
    
    char *anemometerData = malloc(maxDataLengthInBytes);
    char *directionVaneData = malloc(maxDataLengthInBytes);
    uint32_t *anemometerPulses = &ulp_anemometerPulses;
    uint32_t *directionVaneValues = &ulp_directionVaneValues;
    char *anemometerDataPosition = anemometerData;
    char *directionVaneDataPosition = directionVaneData;
 
    for (int i=0; i < MEASUREMENTS_PER_PUBLISHMENT; i++) {
        uint32_t pulses = (*(anemometerPulses++)) & 0xffff;
        uint32_t direction = (*(directionVaneValues++)) & 0xffff;
        sprintf(anemometerDataPosition, i < 1 ? "%d" : ",%d", pulses);
        sprintf(directionVaneDataPosition, i < 1 ? "%d" : ",%d", direction);
        anemometerDataPosition += strlen(anemometerDataPosition);
        directionVaneDataPosition += strlen(directionVaneDataPosition);
    }
        
    int maxPayloadLength =  lengthWithoutPlaceholders(format) + strlen(MESSAGE_VERSION) + maxSequenceIdDigits + strlen(anemometerData) + strlen(directionVaneData);
    char *payload = malloc((maxPayloadLength * sizeof(char)) + nullByte);
    sprintf(payload, format, MESSAGE_VERSION, getNextSequenceId(), anemometerData, directionVaneData);
    free(directionVaneData);
    free(anemometerData);
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
    } else {
        convertToBinaryRepresentation(ulp_trace & 0xffff);
        ESP_LOGI(TAG, "ULP wakeup trace = %s", binaryData);
        char* jsonMessage = createJsonPayload();
        int httpResponseCode = 0;
        for (int retries = 0; retries < 2 && httpResponseCode != 200; retries++) {
            httpResponseCode = send(CONFIG_WINDSENSOR_SERVICE_URL, jsonMessage);
        }
        free(jsonMessage);
        /*uint32_t *anemometerPulses = &ulp_anemometerPulses;
        uint32_t *directionVaneValues = &ulp_directionVaneValues;
        for(int index=0; index < MEASUREMENTS_PER_PUBLISHMENT; index++) {
            ESP_LOGI(TAG, "\t%d: pulses = %d, direction = %d", 
                        index, 
                        (*(anemometerPulses++)) & 0xffff, 
                        (*(directionVaneValues++)) & 0xffff);
        }*/
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
