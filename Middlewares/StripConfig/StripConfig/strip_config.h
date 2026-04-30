#ifndef STRIP_CONFIG_H
#define STRIP_CONFIG_H

#include "esp_err.h"
#include <stdint.h>

#define MAX_STRIPS 8

typedef struct {
    int gpio_num;
    int led_count;
} strip_entry_t;

typedef struct {
    strip_entry_t strips[MAX_STRIPS];
    int strip_count;
    int http_port;
} device_config_t;

esp_err_t strip_config_load(device_config_t *config);
esp_err_t strip_config_save(const device_config_t *config);
const device_config_t *strip_config_get(void);

esp_err_t strip_config_apply(const device_config_t *new_cfg);

esp_err_t strip_config_add_strip(int gpio_num, int led_count);

esp_err_t strip_config_remove_strip(int gpio_num);

#endif // STRIP_CONFIG_H
