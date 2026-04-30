#include "strip_config.h"

#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "led_controller.h"

#define NVS_NAMESPACE "dev_cfg"
#define NVS_KEY_CONFIG "config"

static const char *TAG = "strip_cfg";
static device_config_t s_cached;

static void load_kconfig_defaults(device_config_t *cfg)
{
    memset(cfg, 0, sizeof(device_config_t));

    int idx = 0;

#ifdef CONFIG_STRIP0_GPIO
    if (CONFIG_STRIP0_GPIO >= 0 && CONFIG_STRIP0_COUNT > 0) {
        cfg->strips[idx].gpio_num = CONFIG_STRIP0_GPIO;
        cfg->strips[idx].led_count = CONFIG_STRIP0_COUNT;
        idx++;
    }
#endif

#ifdef CONFIG_STRIP1_GPIO
    if (CONFIG_STRIP1_GPIO >= 0 && CONFIG_STRIP1_COUNT > 0) {
        cfg->strips[idx].gpio_num = CONFIG_STRIP1_GPIO;
        cfg->strips[idx].led_count = CONFIG_STRIP1_COUNT;
        idx++;
    }
#endif

#ifdef CONFIG_STRIP2_GPIO
    if (CONFIG_STRIP2_GPIO >= 0 && CONFIG_STRIP2_COUNT > 0) {
        cfg->strips[idx].gpio_num = CONFIG_STRIP2_GPIO;
        cfg->strips[idx].led_count = CONFIG_STRIP2_COUNT;
        idx++;
    }
#endif

#ifdef CONFIG_STRIP3_GPIO
    if (CONFIG_STRIP3_GPIO >= 0 && CONFIG_STRIP3_COUNT > 0) {
        cfg->strips[idx].gpio_num = CONFIG_STRIP3_GPIO;
        cfg->strips[idx].led_count = CONFIG_STRIP3_COUNT;
        idx++;
    }
#endif

    cfg->strip_count = idx;
    cfg->http_port = CONFIG_HTTP_SERVER_PORT;
}

esp_err_t strip_config_load(device_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);

    if (err == ESP_OK) {
        size_t required_size = sizeof(device_config_t);
        err = nvs_get_blob(handle, NVS_KEY_CONFIG, config, &required_size);
        nvs_close(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS config not found, using Kconfig defaults");
        load_kconfig_defaults(config);
    } else {
        ESP_LOGI(TAG, "Loaded config from NVS");
    }

    memcpy(&s_cached, config, sizeof(device_config_t));

    for (int i = 0; i < config->strip_count; i++) {
        ESP_LOGI(TAG, "  strip %d: gpio=%d, count=%d",
                 i, config->strips[i].gpio_num, config->strips[i].led_count);
    }
    ESP_LOGI(TAG, "  http_port=%d", config->http_port);

    return ESP_OK;
}

esp_err_t strip_config_save(const device_config_t *config)
{
    if (!config) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, NVS_KEY_CONFIG, config, sizeof(device_config_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        memcpy(&s_cached, config, sizeof(device_config_t));
        ESP_LOGI(TAG, "Config saved to NVS");
    }
    return err;
}

const device_config_t *strip_config_get(void)
{
    return &s_cached;
}

esp_err_t strip_config_apply(const device_config_t *new_cfg)
{
    if (!new_cfg || new_cfg->strip_count < 0 || new_cfg->strip_count > MAX_STRIPS) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < new_cfg->strip_count; i++) {
        if (new_cfg->strips[i].gpio_num < 0 || new_cfg->strips[i].led_count <= 0) {
            ESP_LOGE(TAG, "Invalid strip %d: gpio=%d, count=%d",
                     i, new_cfg->strips[i].gpio_num, new_cfg->strips[i].led_count);
            return ESP_ERR_INVALID_ARG;
        }
    }

    // Backup current config for rollback
    device_config_t backup;
    memcpy(&backup, &s_cached, sizeof(device_config_t));

    esp_err_t err = strip_config_save(new_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(err));
        return err;
    }

    err = led_controller_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinit controller: %s", esp_err_to_name(err));
        return err;
    }

    led_driver_config_t drv_cfg = {0};
    for (int i = 0; i < new_cfg->strip_count && i < LED_DRIVER_MAX_STRIPS; i++) {
        drv_cfg.strips[i].gpio_num = new_cfg->strips[i].gpio_num;
        drv_cfg.strips[i].led_count = new_cfg->strips[i].led_count;
    }
    drv_cfg.strip_count = new_cfg->strip_count;

    err = led_controller_init(&drv_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reinit controller: %s", esp_err_to_name(err));
        return err;
    }

    // Update NVS with actually initialized strips (may be fewer than requested)
    int actual_count = led_controller_get_strip_count();
    if (actual_count < new_cfg->strip_count) {
        ESP_LOGW(TAG, "Only %d/%d strips initialized, updating NVS", actual_count, new_cfg->strip_count);
        device_config_t actual_cfg = {0};
        actual_cfg.strip_count = actual_count;
        actual_cfg.http_port = new_cfg->http_port;
        for (int i = 0; i < actual_count; i++) {
            actual_cfg.strips[i].gpio_num = new_cfg->strips[i].gpio_num;
            actual_cfg.strips[i].led_count = new_cfg->strips[i].led_count;
        }
        strip_config_save(&actual_cfg);
    }

    ESP_LOGI(TAG, "Config applied: %d strips", actual_count);
    return ESP_OK;
}

esp_err_t strip_config_add_strip(int gpio_num, int led_count)
{
    if (gpio_num < 0 || gpio_num > 48 || led_count <= 0 || led_count > 256) {
        return ESP_ERR_INVALID_ARG;
    }

    device_config_t new_cfg;
    memcpy(&new_cfg, &s_cached, sizeof(device_config_t));

    // If gpio already exists, update its led_count
    for (int i = 0; i < new_cfg.strip_count; i++) {
        if (new_cfg.strips[i].gpio_num == gpio_num) {
            new_cfg.strips[i].led_count = led_count;
            return strip_config_apply(&new_cfg);
        }
    }

    // Add new strip
    if (new_cfg.strip_count >= MAX_STRIPS) {
        ESP_LOGE(TAG, "No room for more strips (max %d)", MAX_STRIPS);
        return ESP_ERR_NO_MEM;
    }

    new_cfg.strips[new_cfg.strip_count].gpio_num = gpio_num;
    new_cfg.strips[new_cfg.strip_count].led_count = led_count;
    new_cfg.strip_count++;

    return strip_config_apply(&new_cfg);
}

esp_err_t strip_config_remove_strip(int gpio_num)
{
    device_config_t new_cfg;
    memcpy(&new_cfg, &s_cached, sizeof(device_config_t));

    int found = -1;
    for (int i = 0; i < new_cfg.strip_count; i++) {
        if (new_cfg.strips[i].gpio_num == gpio_num) {
            found = i;
            break;
        }
    }

    if (found < 0) {
        ESP_LOGW(TAG, "GPIO %d not found in config", gpio_num);
        return ESP_ERR_NOT_FOUND;
    }

    // Remove by shifting remaining entries
    for (int i = found; i < new_cfg.strip_count - 1; i++) {
        new_cfg.strips[i] = new_cfg.strips[i + 1];
    }
    new_cfg.strip_count--;
    memset(&new_cfg.strips[new_cfg.strip_count], 0, sizeof(strip_entry_t));

    return strip_config_apply(&new_cfg);
}
