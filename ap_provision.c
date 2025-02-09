/* Captive Portal Example

    This example code is in the Public Domain (or CC0 licensed, at your option.)

    Unless required by applicable law or agreed to in writing, this
    software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
    CONDITIONS OF ANY KIND, either express or implied.
*/

#include <sys/param.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "lwip/inet.h"

#include "esp_http_server.h"
#include "dns_server.h" 

extern const char root_start[] asm("_binary_root_html_start");
extern const char root_end[] asm("_binary_root_html_end");

extern const char error_start[] asm("_binary_error_html_start");
extern const char error_end[] asm("_binary_error_html_end");

static const char *TAG = "Prvin";


// Task handle to notify when slave has sent over its the address
static TaskHandle_t s_notify = NULL;

static char *s_ssid = NULL;
static char *s_password = NULL;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

static void wifi_init_softap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = "MIST",
            .password = "",
            .authmode = WIFI_AUTH_OPEN,   // No encryption
            .max_connection = 4,
            .channel = 1
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_MODE_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    ESP_LOGI(TAG, "wifi_init_softap finished");
}

static void dhcp_set_captiveportal_url(void) {
    // get the IP of the access point to redirect to
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ip_info);

    char ip_addr[16];
    inet_ntoa_r(ip_info.ip.addr, ip_addr, 16);
    ESP_LOGI(TAG, "Set up softAP with IP: %s", ip_addr);

    // turn the IP into a URI
    char* captiveportal_uri = (char*) malloc(32 * sizeof(char));
    assert(captiveportal_uri && "Failed to allocate captiveportal_uri");
    strcpy(captiveportal_uri, "http://");
    strcat(captiveportal_uri, ip_addr);

    // get a handle to configure DHCP with
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");

    // set the DHCP option 114
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(netif));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, captiveportal_uri, strlen(captiveportal_uri)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(netif));
}

// HTTP GET Handler
static esp_err_t root_get_handler(httpd_req_t *req)
{
    const uint32_t root_len = root_end - root_start;

    ESP_LOGI(TAG, "Serve root");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, root_start, root_len);

    return ESP_OK;
}

static esp_err_t submit_provisioning_post_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "POST request received");
    char content[100];  // Buffer to store form data
    int content_len = req->content_len;

    // Read the POST data from the request
    if (content_len > 0) {
        httpd_req_recv(req, content, content_len);
    }

    // Log the received data (for debugging)
    ESP_LOGI(TAG, "Received data: %s", content);

    // Extract lengths of SSID and password
    char *ssid_start = strstr(content, "ssid=") + 5;
    char *password_start = strstr(content, "&password=") + 10;
    int ssid_len = password_start - ssid_start - 10;
    int password_len = content_len - (password_start - content);

    // Allocate memory for SSID and password
    s_ssid = (char *)malloc(ssid_len + 1);
    s_password = (char *)malloc(password_len + 1);
    
    // Parse the POST data (form data) - in this case, ssid and password
    sscanf(content, "ssid=%49[^&]&password=%49s", s_ssid, s_password);

    // Null-terminate the strings
    s_ssid[ssid_len] = '\0';
    s_password[password_len] = '\0';

    // Log the parsed ssid and password
    ESP_LOGI(TAG, "SSID: %s, Password: %s", s_ssid, s_password);

    // // Send a response to the client
    // const char *response = "<html><body><h1>Form Submitted</h1><p>SSID: %s</p><p>Password: %s</p></body></html>";
    // httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    // Redirect to the "/" root directory
    // httpd_resp_set_hdr(req, "Location", "/success");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Success, your can start pairing your MIST sensor now...", HTTPD_RESP_USE_STRLEN);

    // Notify that provisioning is done
    xTaskNotifyGive(s_notify);

    return ESP_OK;
}

static esp_err_t error_get_handler(httpd_req_t *req)
{
    const uint32_t error_len = error_end - error_start;

    ESP_LOGI(TAG, "Serve error");
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, error_start, error_len);

    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
};

static httpd_uri_t submit_uri = {
    .uri       = "/submit_provisioning",
    .method    = HTTP_POST,
    .handler   = submit_provisioning_post_handler,
};

static const httpd_uri_t error_uri = {
    .uri = "/error",
    .method = HTTP_GET,
    .handler = error_get_handler
};


// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root");
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &submit_uri);
        httpd_register_uri_handler(server, &error_uri);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }

    return server;
}


// static bool wl_get_wifi_credential() {


//     // nvs_iterator_t it = NULL;
//     // esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
//     // while(err == ESP_OK) {
//     //     nvs_entry_info_t info;
//     //     nvs_entry_info(it, &info); // Can omit error check if parameters are guaranteed to be non-NULL
//     //     printf("key '%s', type '%d', namespace '%s' \n", info.key, info.type, info.namespace_name);
//     //     err = nvs_entry_next(&it);
//     // }
//     // nvs_release_iterator(it);




//     nvs_handle_t nvs;
//     esp_err_t err = nvs_open("nvs", NVS_READONLY, &nvs);
//     printf("NVS open status: %s\n", esp_err_to_name(err));
//     if (err == ESP_OK) {
//         size_t ssid_len = 0;
//         size_t password_len = 0;

//         // Get the length of the stored SSID and password
//         err = nvs_get_str(nvs, "sta.ssid", NULL, &ssid_len);
//         if (err == ESP_OK) {
//             err = nvs_get_str(nvs, "sta.pswd", NULL, &password_len);
//         } else {
//             // Does not have stored Wi-Fi credentials
//             return false;
//         }

//         if (err == ESP_OK) {
//             // Allocate memory for SSID and password
//             s_ssid = (char *)malloc(ssid_len);
//             s_password = (char *)malloc(password_len);

//             // Retrieve the stored SSID and password
//             err = nvs_get_str(nvs, "ssid", s_ssid, &ssid_len);
//             if (err == ESP_OK) {
//                 err = nvs_get_str(nvs, "password", s_password, &password_len);
//             }

//             if (err == ESP_OK) {
//                 // Wi-Fi credentials are stored
//                 printf("Stored Wi-Fi SSID: %s\n", (char *)s_ssid);
//                 printf("Stored Wi-Fi Password: %s\n", (char *)s_password);
//                 nvs_close(nvs);
//                 return true;
//             } else {
//                 // Free allocated memory if retrieval failed
//                 free(s_ssid);
//                 free(s_password);
//                 s_ssid = NULL;
//                 s_password = NULL;
//             }
//         }
//         nvs_close(nvs);

//         return true;
//     } else {
//         // NVS initialization failed
//         printf("NVS initialization failed.\n");
//         return false;
//     }
// }

// // Function to read Wi-Fi credentials from NVS
// esp_err_t read_wifi_credentials(char *ssid, char *password) {
//     wifi_config_t wifi_config;
//     esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
//     if (err != ESP_OK) {
//         ESP_LOGE(TAG, "Failed to get Wi-Fi configuration");
//         return err;
//     }

//     // Copy SSID and password
//     strncpy(ssid, (char *)wifi_config.sta.ssid, 32);
//     strncpy(password, (char *)wifi_config.sta.password, 64);

//     return ESP_OK;
// }

// void wl_try_ap_provisioning(char **out_ssid, char **out_password)
// {
//     /*
//         Turn of warnings from HTTP server as redirecting traffic will yield
//         lots of invalid requests
//     */
//     esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
//     esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
//     esp_log_level_set("httpd_parse", ESP_LOG_ERROR);












//     // Initialize NVS
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);

//     // Initialize Wi-Fi
//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());

//     esp_netif_create_default_wifi_sta();
    
//     char ssid[32] = {0};
//     char password[64] = {0};
//     // Read Wi-Fi credentials from NVS
//     esp_err_t err = read_wifi_credentials(ssid, password);
//     if (err != ESP_OK || strlen(ssid) == 0 || strlen(password) == 0) {
//         ESP_LOGI(TAG, "No credentials found, starting provisioning...");
        



//         // Store the handle of the current handshake task
//         s_notify = xTaskGetCurrentTaskHandle();


//         // Initialize Wi-Fi including netif with default config
//         esp_netif_create_default_wifi_ap();

//         // Initialise ESP32 in SoftAP mode
//         wifi_init_softap();

//         // Configure DNS-based captive portal, if configured
//         dhcp_set_captiveportal_url();

//         // Start the server for the first time
//         httpd_handle_t http_server = start_webserver();

//         // Start the DNS server that will redirect all queries to the softAP IP
//         dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
//         dns_server_handle_t dns_server = start_dns_server(&config);

//         while(true) {
//             if(ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS) == 1) {
//                 ESP_LOGI(TAG, "Exiting loop as signaled");
//                 break;
//             }
//         }

//         ESP_LOGI(TAG, "Stopping DSN + HTTP server");
//         httpd_stop(http_server);
//         stop_dns_server(dns_server);

//         // Assign the pointers to the output parameters
//         *out_ssid = s_ssid;
//         *out_password = s_password;

//         // Clean up Wi-Fi AP handler
//         ESP_ERROR_CHECK(esp_wifi_stop());
//         ESP_ERROR_CHECK(esp_wifi_deinit());



//         return;
//     }
//     ESP_LOGI(TAG, "Wi-Fi credentials found, SSID: %s, Password: %s", ssid, password);

















//     // // Initialize networking stack
//     // ESP_ERROR_CHECK(esp_netif_init());

//     // // Initialize NVS needed by Wi-Fi
//     // ESP_ERROR_CHECK(nvs_flash_init());

//     // if (!wl_get_wifi_credential()) {
//     //     // Store the handle of the current handshake task
//     //     s_notify = xTaskGetCurrentTaskHandle();

//     //     // // Create default event loop needed by the  main app
//     //     // ESP_ERROR_CHECK(esp_event_loop_create_default());

//     //     // Initialize Wi-Fi including netif with default config
//     //     esp_netif_create_default_wifi_ap();

//     //     // Initialise ESP32 in SoftAP mode
//     //     wifi_init_softap();

//     //     // Configure DNS-based captive portal, if configured
//     //     dhcp_set_captiveportal_url();

//     //     // Start the server for the first time
//     //     httpd_handle_t http_server = start_webserver();

//     //     // Start the DNS server that will redirect all queries to the softAP IP
//     //     dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
//     //     dns_server_handle_t dns_server = start_dns_server(&config);

//     //     while(true) {
//     //         if(ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS) == 1) {
//     //             ESP_LOGI(TAG, "Exiting loop as signaled");
//     //             break;
//     //         }
//     //     }

//     //     ESP_LOGI(TAG, "Stopping DSN + HTTP server");
//     //     httpd_stop(http_server);
//     //     stop_dns_server(dns_server);

//     //     // Assign the pointers to the output parameters
//     //     *out_ssid = s_ssid;
//     //     *out_password = s_password;

//     //     // Clean up Wi-Fi AP handler
//     //     ESP_ERROR_CHECK(esp_wifi_stop());
//     //     ESP_ERROR_CHECK(esp_wifi_deinit());
//     // }

// }





#define MAX_RETRIES    5

static EventGroupHandle_t wifi_event_group;
static esp_event_handler_instance_t instance_any_id;
static esp_event_handler_instance_t instance_got_ip;

const int PRO_WIFI_CONNECTED_BIT = BIT0;
static int retry_count = 0;

static bool wifi_connected = false;
static bool s_is_provisioned = false;
/**
 * @brief Event handler for Wi-Fi / IP events
 */
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi STA Start");
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < MAX_RETRIES) {
            ESP_ERROR_CHECK(esp_wifi_connect());
            retry_count++;
            ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)", retry_count, MAX_RETRIES);
        } else {
            ESP_LOGE(TAG, "Failed to connect to WiFi");
            xEventGroupClearBits(wifi_event_group, PRO_WIFI_CONNECTED_BIT);
        }
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        wifi_connected = true;
        retry_count = 0;
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));

        wifi_config_t wifi_config;
        esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);

        xEventGroupSetBits(wifi_event_group, PRO_WIFI_CONNECTED_BIT);
    } else {
        ESP_LOGI(TAG, "Unknown event: %d", event_id);
        xEventGroupSetBits(wifi_event_group, PRO_WIFI_CONNECTED_BIT);
    }
}


int count = 0;
void start_wifi_sta() {
    ESP_LOGI(TAG, "Starting station");

    // Initialize default station as network interface instance
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // Register our custom event handler for Wi-Fi, IP events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    // Set Wi-Fi mode to STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    // wifi_config_t wifi_config = {
    //     .sta = {
    //         .ssid = "asdfasdfa.4G",
    //         .password = "sdfasfads",
    //         .threshold = {
    //             .authmode = WIFI_AUTH_WPA2_PSK,
    //         },
    //     },
    // };
    
    wifi_config_t wifi_config;
    esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);

    if(s_is_provisioned) {
        strncpy((char*)wifi_config.sta.ssid, s_ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char*)wifi_config.sta.password, s_password, sizeof(wifi_config.sta.password) - 1);
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    

    while(true) {
        // Wait for Wi-Fi connection
        xEventGroupWaitBits(wifi_event_group, PRO_WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void start_provisioning() {
    // Store the handle of the current handshake task
    s_notify = xTaskGetCurrentTaskHandle();

    // Initialize Wi-Fi including netif with default config
    esp_netif_create_default_wifi_ap();

    // Initialise ESP32 in SoftAP mode
    wifi_init_softap();

    // Configure DNS-based captive portal, if configured
    dhcp_set_captiveportal_url();

    // Start the server for the first time
    httpd_handle_t http_server = start_webserver();

    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    dns_server_handle_t dns_server = start_dns_server(&config);

    while(true) {
        if(ulTaskNotifyTake(pdTRUE, 1000 / portTICK_PERIOD_MS) == 1) {
            s_is_provisioned = true;

            ESP_LOGI(TAG, "Exiting provision loop as signaled");
            break;
        }
    }

    ESP_LOGI(TAG, "Stopping DSN + HTTP server");
    httpd_stop(http_server);
    stop_dns_server(dns_server);

    // Clean up Wi-Fi AP handler
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_deinit());
}

void start() 
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // find the stored Wi-Fi credentials 
    // nvs_iterator_t it = NULL;
    // esp_err_t err = nvs_entry_find("nvs", NULL, NVS_TYPE_ANY, &it);
    // while(err == ESP_OK) {
    //     nvs_entry_info_t info;
    //     nvs_entry_info(it, &info); // Can omit error check if parameters are guaranteed to be non-NULL
    //     printf("key '%s', type '%d', namespace '%s' \n", info.key, info.type, info.namespace_name);
    //     err = nvs_entry_next(&it);
    // }
    // nvs_release_iterator(it);


    
    wifi_config_t wifi_config;
    esp_wifi_get_config(ESP_IF_WIFI_STA, &wifi_config);
    ESP_LOGI(TAG, "Stored Wi-Fi SSID: %s", (char *)wifi_config.sta.ssid);
    ESP_LOGI(TAG, "Stored Wi-Fi Password: %s", (char *)wifi_config.sta.password);
    if (strlen((char*)wifi_config.sta.ssid) == 0 || strlen((char*)wifi_config.sta.password) == 0) {
        while(!wifi_connected) {
            start_provisioning();
            start_wifi_sta();
        }
    } else {
        start_wifi_sta();

        while(!wifi_connected) {
            start_provisioning();
            start_wifi_sta();
        }
    }
    


    // if station mode wifi connection failed, starts provisioning loop
    // In the loop:
    //      1. Starts a softAP
    //      2. Wait for user to connect to the softAP and provide wifi credentials
    //      3. Start station mode with the provided credentials
    //      4. If station mode wifi connection failed, repeat the loop, else exit the loop
    
}