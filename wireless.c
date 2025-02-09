#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_now.h>
#include <esp_random.h>
#include <esp_netif.h>
#include <string.h>
#include "wireless.h"

static const char *TAG = "wireless";

#define MAX_RETRIES    5

static EventGroupHandle_t wifi_event_group;
static esp_event_handler_instance_t instance_any_id;
static esp_event_handler_instance_t instance_got_ip;

const int WIFI_CONNECTED_BIT = BIT0;
static int retry_count = 0;

/* ESPNOW can work in both station and softap mode. It is configured in menuconfig. */
#if CONFIG_ESPNOW_WIFI_MODE_STATION
#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_STA
#else
#define ESPNOW_WIFI_MODE WIFI_MODE_AP
#define ESPNOW_WIFI_IF   ESP_IF_WIFI_AP
#endif

/* Event handler for WiFi events */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < MAX_RETRIES) {
            esp_wifi_connect();
            retry_count++;
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)", retry_count, MAX_RETRIES);
        } else {
            ESP_LOGE(TAG, "Failed to connect to WiFi");
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        retry_count = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wl_wifi_shutdown(void) {
    ESP_LOGI(TAG, "WiFi deinit starting...");
    // Stop WiFi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());

    // Unregister event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));

    // Delete the event group
    if (wifi_event_group) {
        vEventGroupDelete(wifi_event_group);
        wifi_event_group = NULL;
    }

    // Clean up the default event loop
    ESP_ERROR_CHECK(esp_event_loop_delete_default());

    ESP_LOGI(TAG, "WiFi deinit completed");
}

/* Initialize WiFi station */
void wl_wifi_init()
{
    ESP_LOGI(TAG, "WiFi station mode initialization starting...");
    // wifi_event_group = xEventGroupCreate();


    start();


    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());

    
    // // Try AP provisioning, if wifi provisioning is not done
    // char *ssid = NULL;
    // char *password = NULL;
    // wl_try_ap_provisioning(&ssid, &password);








    // // Switch back to default sta mode
    // esp_netif_create_default_wifi_sta();

    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
    //                                                     ESP_EVENT_ANY_ID,
    //                                                     &wifi_event_handler,
    //                                                     NULL,
    //                                                     &));
    // ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
    //                                                     IP_EVENT_STA_GOT_IP,
    //                                                     &wifi_event_handler,
    //                                                     NULL,
    //                                                     &instance_got_ip));

    // wifi_config_t wifi_config = {
    //     .sta = {
    //         .ssid = "",
    //         .password = "",
    //         .threshold = {
    //             .authmode = WIFI_AUTH_WPA2_PSK,
    //         },
    //     },
    // };
    // // Copy the SSID and password to the wifi_config structure
    // strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    // strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    // if (ssid && password) {
    //     printf("Provisioned SSID: %s\n", ssid);
    //     printf("Provisioned Password: %s\n", password);

    //     // Free the allocated memory
    //     free(ssid);
    //     free(password);
    // }

    // ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    // // ESP32C6 seems like can use WIFI and ESPNOW at the same time.
    // // So I just set the mode to ESPNOW_WIFI_MODE
    // ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // ESP_LOGI(TAG, "Set to ESPNOW_WIFI_MODE");
    // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // ESP_ERROR_CHECK(esp_wifi_start());

    // // Wait for WiFi connection
    // ESP_LOGI(TAG, "Waiting for WiFi connection...");
    // xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    // ESP_LOGI(TAG, "WiFi connected!");


















    // esp_netif_create_default_wifi_sta();

    // wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
    //                                                     ESP_EVENT_ANY_ID,
    //                                                     &wifi_event_handler,
    //                                                     NULL,
    //                                                     &instance_any_id));
    // ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
    //                                                     IP_EVENT_STA_GOT_IP,
    //                                                     &wifi_event_handler,
    //                                                     NULL,
    //                                                     &instance_got_ip));

    // wifi_config_t wifi_config = {
    //     .sta = {
    //         .ssid = "",
    //         .password = "",
    //         .threshold = {
    //             .authmode = WIFI_AUTH_WPA2_PSK,
    //         },
    //     },
    // };
    // // Copy the SSID and password to the wifi_config structure
    // strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    // strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    // ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    // // ESP32C6 seems like can use WIFI and ESPNOW at the same time.
    // // So I just set the mode to ESPNOW_WIFI_MODE
    // ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
    // ESP_LOGI(TAG, "Set to ESPNOW_WIFI_MODE");
    // ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    // ESP_ERROR_CHECK(esp_wifi_start());

    // ESP_LOGI(TAG, "WiFi initialization finished");

    // // Wait for WiFi connection
    // ESP_LOGI(TAG, "Waiting for WiFi connection...");
    // xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    // ESP_LOGI(TAG, "WiFi connected!");
}