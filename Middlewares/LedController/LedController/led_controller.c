#include "led_controller.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "led_ctrl";

static led_state_t *s_states[LED_DRIVER_MAX_STRIPS];
static int s_strip_led_counts[LED_DRIVER_MAX_STRIPS];
static int s_strip_count = 0;

static SemaphoreHandle_t s_mutex = NULL;
static esp_timer_handle_t s_tick_timer = NULL;

static void tick_timer_cb(void *arg)
{
    led_controller_tick();
}

static void cleanup_states(void)
{
    for (int i = 0; i < LED_DRIVER_MAX_STRIPS; i++) {
        if (s_states[i]) {
            free(s_states[i]);
            s_states[i] = NULL;
        }
        s_strip_led_counts[i] = 0;
    }
    s_strip_count = 0;
}

esp_err_t led_controller_init(const led_driver_config_t *config)
{
    if (!config || config->strip_count <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = led_driver_init(config);
    if (err != ESP_OK) {
        return err;
    }

    // Use actual strip count from driver (may be less than requested)
    s_strip_count = led_driver_get_strip_count_total();
    for (int i = 0; i < s_strip_count; i++) {
        int count = led_driver_get_strip_led_count(i);
        s_strip_led_counts[i] = count;
        s_states[i] = calloc(count, sizeof(led_state_t));
        if (!s_states[i]) {
            ESP_LOGE(TAG, "Failed to alloc state for strip %d", i);
            cleanup_states();
            return ESP_ERR_NO_MEM;
        }
    }

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        cleanup_states();
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t timer_args = {
        .callback = tick_timer_cb,
        .name = "led_tick",
        .dispatch_method = ESP_TIMER_TASK,
    };
    err = esp_timer_create(&timer_args, &s_tick_timer);
    if (err != ESP_OK) {
        cleanup_states();
        return err;
    }

    err = esp_timer_start_periodic(s_tick_timer, 100000); // 100ms
    if (err != ESP_OK) {
        cleanup_states();
        return err;
    }

    ESP_LOGI(TAG, "Initialized: %d strips, tick timer 100ms", s_strip_count);
    return ESP_OK;
}

esp_err_t led_controller_set(int strip_index, int pixel_index,
                              led_mode_t mode,
                              uint8_t r, uint8_t g, uint8_t b,
                              int duration_ms)
{
    if (strip_index < 0 || strip_index >= s_strip_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pixel_index < 0 || pixel_index >= s_strip_led_counts[strip_index]) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    led_state_t *st = &s_states[strip_index][pixel_index];
    st->mode = mode;
    st->r = r;
    st->g = g;
    st->b = b;
    st->duration_ms = duration_ms;
    st->start_time_ms = esp_timer_get_time() / 1000;
    st->tick_count = 0;
    st->blink_on = true;

    if (mode == LED_MODE_STATIC) {
        led_driver_set_pixel(strip_index, pixel_index, r, g, b);
    } else if (mode == LED_MODE_BLINK) {
        led_driver_set_pixel(strip_index, pixel_index, r, g, b);
    }
    led_driver_refresh_strip(strip_index);

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

esp_err_t led_controller_clear_all(void)
{
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (int s = 0; s < s_strip_count; s++) {
        memset(s_states[s], 0, s_strip_led_counts[s] * sizeof(led_state_t));
    }
    led_driver_clear_all();
    led_driver_refresh_all();

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

void led_controller_tick(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    for (int s = 0; s < s_strip_count; s++) {
        bool changed = false;
        int count = s_strip_led_counts[s];

        for (int p = 0; p < count; p++) {
            led_state_t *st = &s_states[s][p];
            if (st->mode == LED_MODE_OFF) continue;

            // Duration timeout
            if (st->duration_ms > 0) {
                if ((now_ms - st->start_time_ms) >= st->duration_ms) {
                    st->mode = LED_MODE_OFF;
                    led_driver_set_pixel(s, p, 0, 0, 0);
                    changed = true;
                    continue;
                }
            }

            // Blink toggle
            if (st->mode == LED_MODE_BLINK) {
                st->tick_count++;
                if (st->tick_count >= 5) {
                    st->tick_count = 0;
                    st->blink_on = !st->blink_on;
                    if (st->blink_on) {
                        led_driver_set_pixel(s, p, st->r, st->g, st->b);
                    } else {
                        led_driver_set_pixel(s, p, 0, 0, 0);
                    }
                    changed = true;
                }
            }
        }

        if (changed) {
            led_driver_refresh_strip(s);
        }
    }

    xSemaphoreGive(s_mutex);
}

int led_controller_get_strip_led_count(int strip_index)
{
    if (strip_index < 0 || strip_index >= s_strip_count) {
        return 0;
    }
    return s_strip_led_counts[strip_index];
}

int led_controller_get_strip_count(void)
{
    return s_strip_count;
}

int led_controller_find_strip_by_gpio(int gpio_num)
{
    return led_driver_find_strip_by_gpio(gpio_num);
}

esp_err_t led_controller_deinit(void)
{
    if (s_tick_timer) {
        esp_timer_stop(s_tick_timer);
        esp_timer_delete(s_tick_timer);
        s_tick_timer = NULL;
    }

    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    cleanup_states();

    esp_err_t err = led_driver_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Driver deinit failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}
