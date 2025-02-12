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

#define MAX_RETRIES    1
#define DEFAULT_SCAN_LIST_SIZE 100
#define WIFI_AP_SSID "Mist"

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

static const char *TAG = "Wireless";

static EventGroupHandle_t wifi_event_group;
static esp_event_handler_instance_t instance_any_id;
static esp_event_handler_instance_t instance_got_ip;

const int WIFI_CONNECTED_BIT = BIT0;
static int s_retry_count = 0;

static bool wifi_connected = false;

// This function is called when the root page is requested
// It scan the wifi ssid and list them in the html page
static esp_err_t index_get_handler(httpd_req_t *req)
{
    // Perform Wi-Fi scan
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t *ap_info = malloc(number * sizeof(wifi_ap_record_t));
    if (!ap_info) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP info");
        return ESP_ERR_NO_MEM;
    }

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0, // Use channel_bitmap instead
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {
            .active = {
                .min = 100,
                .max = 300
            }
        },
        // Represents 2.4 GHz channels
        .channel_bitmap = {
            .ghz_2_channels = 0x3FFF 
        }
    };
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&number));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_LOGI(TAG, "Total APs scanned = %u", number);

    // Generate SSID list HTML
    size_t ssid_list_html_size = 2048;
    char *ssid_list_html = malloc(ssid_list_html_size);
    if (!ssid_list_html) {
        ESP_LOGE(TAG, "Failed to allocate memory for SSID list HTML");
        free(ap_info);
        return ESP_ERR_NO_MEM;
    }
    strcpy(ssid_list_html, "");
    for (int i = 0; i < number; i++) {
        size_t ssid_len = strlen((char *)ap_info[i].ssid);
        if (strlen(ssid_list_html) + ssid_len + 50 > ssid_list_html_size) {
            ssid_list_html_size *= 2;
            ssid_list_html = realloc(ssid_list_html, ssid_list_html_size);
            if (!ssid_list_html) {
                ESP_LOGE(TAG, "Failed to reallocate memory for SSID list HTML");
                free(ap_info);
                return ESP_ERR_NO_MEM;
            }
        }
        strcat(ssid_list_html, "<option value=\"");
        strcat(ssid_list_html, (char *)ap_info[i].ssid);
        strcat(ssid_list_html, "\">");
        strcat(ssid_list_html, (char *)ap_info[i].ssid);
        strcat(ssid_list_html, "</option>");
    }
    
    // Read the template HTML into a buffer
    const uint32_t index_len = index_html_end - index_html_start;
    char *index_html = malloc(index_len + 1);
    if (!index_html) {
        ESP_LOGE(TAG, "Failed to allocate memory for root HTML");
        free(ssid_list_html);
        free(ap_info);
        return ESP_ERR_NO_MEM;
    }
    memcpy(index_html, index_html_start, index_len);
    index_html[index_len] = '\0';

    // Find the SSID_LIST placeholder and replace it
    char *insert_pos = strstr(index_html, "<!-- SSID_LIST -->");
    if (insert_pos) {
        // Calculate required buffer size
        size_t prefix_len = insert_pos - index_html;
        size_t suffix_len = strlen(insert_pos + strlen("<!-- SSID_LIST -->"));
        size_t total_len = prefix_len + strlen(ssid_list_html) + suffix_len + 1;
        
        // Create new buffer with the exact size needed
        char *new_html = malloc(total_len);
        if (!new_html) {
            ESP_LOGE(TAG, "Failed to allocate memory for new HTML");
            free(index_html);
            free(ssid_list_html);
            free(ap_info);
            return ESP_ERR_NO_MEM;
        }

        // Copy parts into new buffer
        strncpy(new_html, index_html, prefix_len);
        strcpy(new_html + prefix_len, ssid_list_html);
        strcat(new_html, insert_pos + strlen("<!-- SSID_LIST -->"));

        // Send the response
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, new_html, strlen(new_html));

        free(new_html);
    } else {
        // If placeholder not found, send original HTML
        httpd_resp_send(req, index_html, strlen(index_html));
    }

    free(ssid_list_html);
    free(index_html);
    free(ap_info);

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
    char *ssid = (char *)malloc(ssid_len + 1);
    char *password = (char *)malloc(password_len + 1);
    // Parse the POST data (form data) - in this case, ssid and password
    sscanf(content, "ssid=%49[^&]&password=%49s", ssid, password);
    // Null-terminate the strings
    ssid[ssid_len] = '\0';
    password[password_len] = '\0';

    // Log the parsed ssid and password
    ESP_LOGI(TAG, "SSID: %s, Password: %s", ssid, password);

    // Update the Wi-Fi configuration
    wifi_config_t sta_config;
    esp_wifi_get_config(ESP_IF_WIFI_STA, &sta_config);
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);

    // Retry the WiFi connection with the new credentials
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    return ESP_OK;
}

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

static const httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_get_handler
};

static httpd_uri_t submit_uri = {
    .uri       = "/submit_provisioning",
    .method    = HTTP_POST,
    .handler   = submit_provisioning_post_handler,
};

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
        httpd_register_uri_handler(server, &index_uri);
        httpd_register_uri_handler(server, &submit_uri);
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }

    return server;
}

static void dhcp_set_captiveportal_url(void) 
{
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

/**
 * @brief Event handler for Wi-Fi / IP events
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI("WiFi Event", "Station disconnected from AP");
            if (s_retry_count < MAX_RETRIES) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGI(TAG, "Retrying WiFi connection (%d/%d)", s_retry_count, MAX_RETRIES);
            } else {
                ESP_LOGE(TAG, "Failed to connect to WiFi");
                xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            }
            break;
        case WIFI_EVENT_STA_START:
            ESP_LOGI("WiFi Event", "Station start");
            esp_wifi_connect();
            break;
        case WIFI_EVENT_WIFI_READY:
            ESP_LOGI("WiFi Event", "Wi-Fi ready");
            break;
        case WIFI_EVENT_SCAN_DONE:
            ESP_LOGI("WiFi Event", "Finished scanning AP");
            break;
        case WIFI_EVENT_STA_STOP:
            ESP_LOGI("WiFi Event", "Station stop");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI("WiFi Event", "Station connected to AP");
            break;
        case WIFI_EVENT_STA_AUTHMODE_CHANGE:
            ESP_LOGI("WiFi Event", "The auth mode of AP connected by device's station changed");
            break;
        case WIFI_EVENT_STA_WPS_ER_SUCCESS:
            ESP_LOGI("WiFi Event", "Station WPS succeeds in enrollee mode");
            break;
        case WIFI_EVENT_STA_WPS_ER_FAILED:
            ESP_LOGI("WiFi Event", "Station WPS fails in enrollee mode");
            break;
        case WIFI_EVENT_STA_WPS_ER_TIMEOUT:
            ESP_LOGI("WiFi Event", "Station WPS timeout in enrollee mode");
            break;
        case WIFI_EVENT_STA_WPS_ER_PIN:
            ESP_LOGI("WiFi Event", "Station WPS pin code in enrollee mode");
            break;
        case WIFI_EVENT_STA_WPS_ER_PBC_OVERLAP:
            ESP_LOGI("WiFi Event", "Station WPS overlap in enrollee mode");
            break;
        case WIFI_EVENT_AP_START:
            ESP_LOGI("WiFi Event", "Soft-AP start");
            break;
        case WIFI_EVENT_AP_STOP:
            ESP_LOGI("WiFi Event", "Soft-AP stop");
            break;
        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI("WiFi Event", "A station connected to Soft-AP");
            break;
        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI("WiFi Event", "A station disconnected from Soft-AP");
            break;
        case WIFI_EVENT_AP_PROBEREQRECVED:
            ESP_LOGI("WiFi Event", "Receive probe request packet in soft-AP interface");
            break;
        case WIFI_EVENT_FTM_REPORT:
            ESP_LOGI("WiFi Event", "Receive report of FTM procedure");
            break;
        case WIFI_EVENT_STA_BSS_RSSI_LOW:
            ESP_LOGI("WiFi Event", "AP's RSSI crossed configured threshold");
            break;
        case WIFI_EVENT_ACTION_TX_STATUS:
            ESP_LOGI("WiFi Event", "Status indication of Action Tx operation");
            break;
        case WIFI_EVENT_ROC_DONE:
            ESP_LOGI("WiFi Event", "Remain-on-Channel operation complete");
            break;
        case WIFI_EVENT_STA_BEACON_TIMEOUT:
            ESP_LOGI("WiFi Event", "Station beacon timeout");
            break;
        case WIFI_EVENT_CONNECTIONLESS_MODULE_WAKE_INTERVAL_START:
            ESP_LOGI("WiFi Event", "Connectionless module wake interval start");
            break;
        case WIFI_EVENT_AP_WPS_RG_SUCCESS:
            ESP_LOGI("WiFi Event", "Soft-AP wps succeeds in registrar mode");
            break;
        case WIFI_EVENT_AP_WPS_RG_FAILED:
            ESP_LOGI("WiFi Event", "Soft-AP wps fails in registrar mode");
            break;
        case WIFI_EVENT_AP_WPS_RG_TIMEOUT:
            ESP_LOGI("WiFi Event", "Soft-AP wps timeout in registrar mode");
            break;
        case WIFI_EVENT_AP_WPS_RG_PIN:
            ESP_LOGI("WiFi Event", "Soft-AP wps pin code in registrar mode");
            break;
        case WIFI_EVENT_AP_WPS_RG_PBC_OVERLAP:
            ESP_LOGI("WiFi Event", "Soft-AP wps overlap in registrar mode");
            break;
        case WIFI_EVENT_ITWT_SETUP:
            ESP_LOGI("WiFi Event", "iTWT setup");
            break;
        case WIFI_EVENT_ITWT_TEARDOWN:
            ESP_LOGI("WiFi Event", "iTWT teardown");
            break;
        case WIFI_EVENT_ITWT_PROBE:
            ESP_LOGI("WiFi Event", "iTWT probe");
            break;
        case WIFI_EVENT_ITWT_SUSPEND:
            ESP_LOGI("WiFi Event", "iTWT suspend");
            break;
        case WIFI_EVENT_TWT_WAKEUP:
            ESP_LOGI("WiFi Event", "TWT wakeup");
            break;
        case WIFI_EVENT_BTWT_SETUP:
            ESP_LOGI("WiFi Event", "bTWT setup");
            break;
        case WIFI_EVENT_BTWT_TEARDOWN:
            ESP_LOGI("WiFi Event", "bTWT teardown");
            break;
        case WIFI_EVENT_NAN_STARTED:
            ESP_LOGI("WiFi Event", "NAN Discovery has started");
            break;
        case WIFI_EVENT_NAN_STOPPED:
            ESP_LOGI("WiFi Event", "NAN Discovery has stopped");
            break;
        case WIFI_EVENT_NAN_SVC_MATCH:
            ESP_LOGI("WiFi Event", "NAN Service Discovery match found");
            break;
        case WIFI_EVENT_NAN_REPLIED:
            ESP_LOGI("WiFi Event", "Replied to a NAN peer with Service Discovery match");
            break;
        case WIFI_EVENT_NAN_RECEIVE:
            ESP_LOGI("WiFi Event", "Received a Follow-up message");
            break;
        case WIFI_EVENT_NDP_INDICATION:
            ESP_LOGI("WiFi Event", "Received NDP Request from a NAN Peer");
            break;
        case WIFI_EVENT_NDP_CONFIRM:
            ESP_LOGI("WiFi Event", "NDP Confirm Indication");
            break;
        case WIFI_EVENT_NDP_TERMINATED:
            ESP_LOGI("WiFi Event", "NAN Datapath terminated indication");
            break;
        case WIFI_EVENT_HOME_CHANNEL_CHANGE:
            ESP_LOGI("WiFi Event", "Wi-Fi home channel change, doesn't occur when scanning");
            break;
        case WIFI_EVENT_STA_NEIGHBOR_REP:
            ESP_LOGI("WiFi Event", "Received Neighbor Report response");
            break;
        case WIFI_EVENT_AP_WRONG_PASSWORD:
            ESP_LOGI("WiFi Event", "A station tried to connect with wrong password");
            break;
        case WIFI_EVENT_MAX:
            ESP_LOGI("WiFi Event", "Invalid Wi-Fi event ID");
            break;
        default:
            ESP_LOGI("WiFi Event", "Unknown event");
            break;
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) 
{
    switch (event_id) {
        case IP_EVENT_STA_GOT_IP:
        // Got IP Address, meaning the device has connected to Wifi successfully
            wifi_connected = true;
            s_retry_count = 0;
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        case IP_EVENT_STA_LOST_IP:
            ESP_LOGI(TAG, "Lost IP Address");
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            break;
        default:
            ESP_LOGI(TAG, "Unknown IP event: %d", event_id);
            break;
    }
}

// Initialize Wi-Fi into AP + STA mode ready for provisioning if required
// If provisioning is not required, it will turn off the AP and reset to station mode
void wl_wifi_init() 
{
    // Turn of warnings from HTTP server as AP provisioning redirecting traffic will yield
    // lots of invalid requests
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    
    esp_wifi_restore();
    
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    // Default loop for system events, e.g., wifi ip events
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize both AP and STA interfaces
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    // Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, &instance_got_ip));

    // For simplicity reason, although the root node might be just need to be in station mode, I want to leave the this master node to be in APSTA mode.
    // so it can be easily form the mesh network.
    // Of course, AP mode will also be used for provisioning.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // Configure the AP and STA interfaces
    // AP interface does not require a password
    wifi_config_t ap_config = {
        .ap = {
            .channel = CONFIG_ESPNOW_CHANNEL,
            .ssid = WIFI_AP_SSID,
            .password = "",
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 1,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    wifi_config_t sta_config;
    sta_config.sta.channel = CONFIG_ESPNOW_CHANNEL;
    esp_wifi_get_config(ESP_IF_WIFI_STA, &sta_config);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi APSTA starting...");

    // Configure DNS-based captive portal, if configured
    dhcp_set_captiveportal_url();
     // Start the server for the first time
    httpd_handle_t http_server = start_webserver();
    // Start the DNS server that will redirect all queries to the softAP IP
    dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*" /* all A queries */, "WIFI_AP_DEF" /* softAP netif ID */);
    dns_server_handle_t dns_server = start_dns_server(&config);

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");
    
    ESP_LOGI(TAG, "Stopping DSN and HTTP server");
    httpd_stop(http_server);
    stop_dns_server(dns_server);

    // Start use long range protocols is set for both AP and STA interfaces
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR));

    // Check protocol for STA interface
    uint8_t protocol;
    ESP_ERROR_CHECK(esp_wifi_get_protocol(ESP_IF_WIFI_STA, &protocol));
    if (!(protocol & WIFI_PROTOCOL_LR)) {
        ESP_LOGE(TAG, "Long range protocol is NOT set correctly for STA interface");
    }
    // Check protocol for AP interface
    ESP_ERROR_CHECK(esp_wifi_get_protocol(ESP_IF_WIFI_AP, &protocol));
    if (!(protocol & WIFI_PROTOCOL_LR)) {
        ESP_LOGE(TAG, "Long range protocol is NOT set correctly for AP interface");
    }

    ESP_LOGI(TAG, "WiFi initialized");
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