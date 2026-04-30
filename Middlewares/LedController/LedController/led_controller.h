#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include "esp_err.h"
#include "led_driver.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    LED_MODE_OFF = 0,
    LED_MODE_STATIC,
    LED_MODE_BLINK
} led_mode_t;

typedef struct {
    led_mode_t mode;
    uint8_t r, g, b;
    int64_t duration_ms;
    int64_t start_time_ms;
    bool blink_on;
    int tick_count;
} led_state_t;

esp_err_t led_controller_init(const led_driver_config_t *config);

esp_err_t led_controller_set(int strip_index, int pixel_index,
                              led_mode_t mode,
                              uint8_t r, uint8_t g, uint8_t b,
                              int duration_ms);

esp_err_t led_controller_clear_all(void);

int led_controller_get_strip_led_count(int strip_index);
int led_controller_get_strip_count(void);
int led_controller_find_strip_by_gpio(int gpio_num);

void led_controller_tick(void);

esp_err_t led_controller_deinit(void);

#endif // LED_CONTROLLER_H
