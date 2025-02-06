#pragma once

void wifi_sta_init(const char *ssid, const char *password);

void wifi_espnow_init(void);

void wifi_shutdown(void);