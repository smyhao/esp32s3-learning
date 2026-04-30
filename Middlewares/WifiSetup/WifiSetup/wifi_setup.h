#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

#include "esp_err.h"

esp_err_t wifi_setup_init(void);
esp_err_t wifi_setup_wait_connected(int timeout_ms);
const char *wifi_setup_get_ip(void);

#endif // WIFI_SETUP_H
