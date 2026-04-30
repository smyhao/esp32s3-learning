#include "led_driver.h"

#include <stdlib.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_rom_sys.h"
#include "soc/gpio_struct.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "led_driver";

#define WS2812_RESET_US  300

// WS2812 timing in CPU cycles (240MHz)
#define T0H  (96)    // 0.4µs
#define T0L  (204)   // 0.85µs
#define T1H  (192)   // 0.8µs
#define T1L  (108)   // 0.45µs

typedef struct {
    int gpio_num;
    uint32_t gpio_mask;
    int led_count;
    uint8_t *pixels;
    bool ready;
} strip_context_t;

static strip_context_t s_strips[LED_DRIVER_MAX_STRIPS];
static int s_strip_count = 0;

static inline uint32_t IRAM_ATTR ccycles(void)
{
    return esp_cpu_get_cycle_count();
}

static inline void IRAM_ATTR wait_until(uint32_t target)
{
    while (ccycles() < target);
}

static void IRAM_ATTR ws2812_send(strip_context_t *ctx)
{
    uint32_t mask = ctx->gpio_mask;
    uint8_t *px = ctx->pixels;
    int len = ctx->led_count * 3;

    portDISABLE_INTERRUPTS();

    for (int i = 0; i < len; i++) {
        uint8_t byte = px[i];
        for (int bit = 7; bit >= 0; bit--) {
            uint32_t t;
            if ((byte >> bit) & 1) {
                GPIO.out_w1ts = mask;
                t = ccycles() + T1H;
                wait_until(t);
                GPIO.out_w1tc = mask;
                t = ccycles() + T1L;
                wait_until(t);
            } else {
                GPIO.out_w1ts = mask;
                t = ccycles() + T0H;
                wait_until(t);
                GPIO.out_w1tc = mask;
                t = ccycles() + T0L;
                wait_until(t);
            }
        }
    }

    portENABLE_INTERRUPTS();

    esp_rom_delay_us(WS2812_RESET_US);
}

static void cleanup_all_impl(void)
{
    for (int i = 0; i < LED_DRIVER_MAX_STRIPS; i++) {
        strip_context_t *ctx = &s_strips[i];
        if (ctx->pixels) {
            free(ctx->pixels);
            ctx->pixels = NULL;
        }
        ctx->ready = false;
    }
    s_strip_count = 0;
}

esp_err_t led_driver_init(const led_driver_config_t *config)
{
    if (!config || config->strip_count <= 0 || config->strip_count > LED_DRIVER_MAX_STRIPS) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(s_strips, 0, sizeof(s_strips));

    int ok_count = 0;
    for (int i = 0; i < config->strip_count; i++) {
        strip_context_t *ctx = &s_strips[i];
        ctx->gpio_num = config->strips[i].gpio_num;
        ctx->gpio_mask = 1UL << ctx->gpio_num;
        ctx->led_count = config->strips[i].led_count;

        if (ctx->led_count <= 0 || ctx->led_count > LED_DRIVER_MAX_LEDS_PER_STRIP) {
            ESP_LOGE(TAG, "Strip %d: invalid led_count=%d", i, ctx->led_count);
            cleanup_all_impl();
            return ESP_ERR_INVALID_ARG;
        }

        gpio_config_t io_conf = {
            .pin_bit_mask = ctx->gpio_mask,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Strip %d gpio=%d: config failed: %s",
                     i, ctx->gpio_num, esp_err_to_name(err));
            ctx->gpio_num = -1;
            ctx->led_count = 0;
            continue;
        }
        gpio_set_level(ctx->gpio_num, 0);

        ctx->pixels = calloc(ctx->led_count * 3, sizeof(uint8_t));
        if (!ctx->pixels) {
            ESP_LOGE(TAG, "Strip %d: alloc failed", i);
            cleanup_all_impl();
            return ESP_ERR_NO_MEM;
        }

        ctx->ready = true;
        ESP_LOGI(TAG, "Strip %d: gpio=%d, count=%d", ok_count, ctx->gpio_num, ctx->led_count);
        ok_count++;
    }

    if (ok_count == 0) {
        ESP_LOGE(TAG, "No strips initialized successfully");
        return ESP_ERR_NOT_FOUND;
    }

    if (ok_count < config->strip_count) {
        strip_context_t compacted[LED_DRIVER_MAX_STRIPS] = {0};
        int dst = 0;
        for (int i = 0; i < config->strip_count; i++) {
            if (s_strips[i].ready) {
                compacted[dst++] = s_strips[i];
            }
        }
        memcpy(s_strips, compacted, sizeof(s_strips));
    }

    s_strip_count = ok_count;
    ESP_LOGI(TAG, "Initialized %d/%d strips (bit-bang)", ok_count, config->strip_count);
    return ESP_OK;
}

esp_err_t led_driver_set_pixel(int strip_index, uint16_t pixel_index,
                               uint8_t r, uint8_t g, uint8_t b)
{
    if (strip_index < 0 || strip_index >= s_strip_count) {
        return ESP_ERR_INVALID_ARG;
    }
    strip_context_t *ctx = &s_strips[strip_index];
    if (!ctx->ready || pixel_index >= ctx->led_count) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t *p = &ctx->pixels[pixel_index * 3];
    p[0] = g;
    p[1] = r;
    p[2] = b;
    return ESP_OK;
}

esp_err_t led_driver_clear_strip(int strip_index)
{
    if (strip_index < 0 || strip_index >= s_strip_count) {
        return ESP_ERR_INVALID_ARG;
    }
    strip_context_t *ctx = &s_strips[strip_index];
    if (!ctx->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    memset(ctx->pixels, 0, ctx->led_count * 3);
    return ESP_OK;
}

esp_err_t led_driver_clear_all(void)
{
    for (int i = 0; i < s_strip_count; i++) {
        if (s_strips[i].ready) {
            memset(s_strips[i].pixels, 0, s_strips[i].led_count * 3);
        }
    }
    return ESP_OK;
}

esp_err_t led_driver_refresh_strip(int strip_index)
{
    if (strip_index < 0 || strip_index >= s_strip_count) {
        return ESP_ERR_INVALID_ARG;
    }
    strip_context_t *ctx = &s_strips[strip_index];
    if (!ctx->ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ws2812_send(ctx);
    return ESP_OK;
}

esp_err_t led_driver_refresh_all(void)
{
    for (int i = 0; i < s_strip_count; i++) {
        if (s_strips[i].ready) {
            ws2812_send(&s_strips[i]);
        }
    }
    return ESP_OK;
}

int led_driver_find_strip_by_gpio(int gpio_num)
{
    for (int i = 0; i < s_strip_count; i++) {
        if (s_strips[i].gpio_num == gpio_num && s_strips[i].ready) {
            return i;
        }
    }
    return -1;
}

int led_driver_get_strip_led_count(int strip_index)
{
    if (strip_index < 0 || strip_index >= s_strip_count) {
        return 0;
    }
    return s_strips[strip_index].led_count;
}

int led_driver_get_strip_count_total(void)
{
    return s_strip_count;
}

esp_err_t led_driver_deinit(void)
{
    cleanup_all_impl();
    ESP_LOGI(TAG, "Driver deinitialized");
    return ESP_OK;
}
