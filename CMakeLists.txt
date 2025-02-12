idf_component_register(
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_http_server
    SRCS "wireless.c" "dns_server.c"
    INCLUDE_DIRS "."
    EMBED_FILES index.html
)