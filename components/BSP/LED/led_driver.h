#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include "esp_err.h"
#include <stdint.h>

#define LED_DRIVER_MAX_LEDS_PER_STRIP 256
#define LED_DRIVER_MAX_STRIPS 8

typedef struct {
    int gpio_num;
    int led_count;
} led_strip_config_t;

typedef struct {
    led_strip_config_t strips[LED_DRIVER_MAX_STRIPS];
    int strip_count;
} led_driver_config_t;

esp_err_t led_driver_init(const led_driver_config_t *config);

esp_err_t led_driver_set_pixel(int strip_index, uint16_t pixel_index,
                               uint8_t r, uint8_t g, uint8_t b);

esp_err_t led_driver_clear_strip(int strip_index);
esp_err_t led_driver_clear_all(void);

esp_err_t led_driver_refresh_strip(int strip_index);
esp_err_t led_driver_refresh_all(void);

int led_driver_find_strip_by_gpio(int gpio_num);

int led_driver_get_strip_led_count(int strip_index);
int led_driver_get_strip_count_total(void);

esp_err_t led_driver_deinit(void);

#endif // LED_DRIVER_H
