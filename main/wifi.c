#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "nvs_flash.h" // non-volatile storage library
#include "esp_http_client.h"
#include "sdkconfig.h"

#include "wifi.h"

// The event group allows multiple bits for each event. We're only interested in the connected and fail events:
#define WIFI_CONNECTED_BIT          BIT0
#define WIFI_FAIL_BIT               BIT1
#define WIFI_MAX_CONNECT_RETRIES    2
#define HTTP_CLIENT_MAX_RETRIES     2

static const char* TAG = "wifi";
static EventGroupHandle_t eventGroup;
static int wifiConnectRetries = 0;
static EventBits_t eventBits;

static void initNonVolatileStorage()
{
    ESP_LOGI(TAG, "initializing non-volatile storage");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );
}

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if ((eventBits & WIFI_CONNECTED_BIT) == 0) {
        if (strcmp(event_base, WIFI_EVENT) == 0) {
            switch(event_id) {
                case WIFI_EVENT_STA_START:          ESP_LOGI(TAG, "WIFI_EVENT_STA_START -> connecting to the AP ...");
                                                    ESP_ERROR_CHECK(esp_wifi_connect());
                                                    break;

                case WIFI_EVENT_STA_DISCONNECTED:   if (wifiConnectRetries < WIFI_MAX_CONNECT_RETRIES) {
                                                        wifiConnectRetries++;
                                                        ESP_LOGI(TAG, "%d. retry to connect to the AP", wifiConnectRetries);
                                                        ESP_ERROR_CHECK(esp_wifi_connect());
                                                    } else {
                                                        xEventGroupSetBits(eventGroup, WIFI_FAIL_BIT);
                                                    }
                                                    break;
            }
        } else if (strcmp(event_base, IP_EVENT) == 0) {
            if ( event_id == IP_EVENT_STA_GOT_IP) {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
                wifiConnectRetries = 0;
                xEventGroupSetBits(eventGroup, WIFI_CONNECTED_BIT);
            }
        } else {
            ESP_LOGI(TAG, "%s ID=%d", event_base, event_id);
        }
    }
}

static int sendHttpRequest(const char* url, const char* data) {
    bool statusCode = false;

    esp_http_client_config_t config = {
        .url = url
    };
    ESP_LOGI(TAG, "initializing HTTP client");
    esp_http_client_handle_t client = esp_http_client_init(&config);
    
    ESP_LOGI(TAG, "setting content-type to application/json");
    ESP_ERROR_CHECK(esp_http_client_set_header(client, "Content-Type", "application/json"));
    ESP_LOGI(TAG, "setting method POST");
    ESP_ERROR_CHECK(esp_http_client_set_method(client, HTTP_METHOD_POST));
    ESP_LOGI(TAG, "setting POST data");
    ESP_ERROR_CHECK(esp_http_client_set_post_field(client, data, strlen(data)));

    ESP_LOGI(TAG, "sending data");
    int pendingAttempts = HTTP_CLIENT_MAX_RETRIES;

    esp_err_t result;

    do {
        result = esp_http_client_perform(client);
        pendingAttempts--;
    
        if ( result == ESP_OK) {
            int statusCode = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "statusCode = %d", statusCode);
            ESP_LOGI(TAG, "closing connection");
            esp_err_t closingResult = esp_http_client_close(client);
            if (closingResult == ESP_FAIL ) {
                ESP_LOGW(TAG, "failed to close http connection");
            }
        } else {
            if (pendingAttempts > 0) {
                ESP_LOGW(TAG, "failed to send %s to %s -> retrying it ...", data, url);
            } else {
                ESP_LOGE(TAG, "failed to send %s to %s", data, url);
            }
        }
    } while ((pendingAttempts > 0) && (result != ESP_OK));

    esp_http_client_cleanup(client);

    return statusCode;
}

int send(const char* url, const char* data)
{
    // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_netif.html

    int statusCode = 0;

    ESP_LOGI(TAG, "create event group");
    eventGroup = xEventGroupCreate();
    if (eventGroup == NULL) {
        ESP_LOGI(TAG, "failed to create event group");
    } else {
        initNonVolatileStorage();
        ESP_LOGI(TAG, "esp_netif_init");
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_LOGI(TAG, "esp_event_loop_create_default");
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        ESP_LOGI(TAG, "esp_netif_create_default_wifi_sta");
        esp_netif_create_default_wifi_sta();
        ESP_LOGI(TAG, "esp_wifi_init");
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));

        esp_event_handler_instance_t handlerInstance;
        ESP_LOGI(TAG, "esp_event_handler_instance_register");
        ESP_ERROR_CHECK(esp_event_handler_instance_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, &event_handler, NULL, &handlerInstance));

        ESP_LOGI(TAG, "esp_wifi_set_mode");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_LOGI(TAG, "esp_wifi_set_config");
        wifi_config_t staConfig = {
            .sta = {
                .ssid = CONFIG_WINDSENSOR_WIFI_SSID,
                .password = CONFIG_WINDSENSOR_WIFI_PASSWORD
            },
        };
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &staConfig));
        ESP_LOGI(TAG, "esp_wifi_start");
        ESP_ERROR_CHECK(esp_wifi_start() );
        
        ESP_LOGI(TAG, "waiting for WIFI to get connected ...");
        eventBits = xEventGroupWaitBits(eventGroup, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        bool connected = eventBits & WIFI_CONNECTED_BIT;

        if (connected) {
            statusCode = sendHttpRequest(url, data);
        } else {
            ESP_LOGE(TAG, "failed to connect to WIFI");
        }

        ESP_LOGI(TAG, "esp_event_handler_instance_unregister");
        ESP_ERROR_CHECK(esp_event_handler_instance_unregister(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, &handlerInstance));
        
        if(connected) {
            ESP_LOGI(TAG, "esp_wifi_disconnect");
            ESP_ERROR_CHECK(esp_wifi_disconnect());
        }

        ESP_LOGI(TAG, "deleting event group");
        vEventGroupDelete(eventGroup);
        ESP_LOGI(TAG, "esp_wifi_stop");
        esp_wifi_stop();
    }
    return statusCode;
}

// state diagrams:  https://medium.com/@mahavirj/esp-idf-wifi-networking-3eaebd11eb43
// wifi example:    https://github.com/espressif/esp-idf/blob/f91080637c054fa2b4107192719075d237ecc3ec/examples/wifi/getting_started/station/main/station_example_main.c
// https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/wifi.html