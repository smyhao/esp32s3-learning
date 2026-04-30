#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "strip_config.h"
#include "led_controller.h"
#include "http_api.h"
#include "wifi_setup.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32 LED Locator starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    device_config_t dev_cfg;
    ESP_ERROR_CHECK(strip_config_load(&dev_cfg));

    led_driver_config_t drv_cfg = {0};
    for (int i = 0; i < dev_cfg.strip_count && i < LED_DRIVER_MAX_STRIPS; i++) {
        drv_cfg.strips[i].gpio_num = dev_cfg.strips[i].gpio_num;
        drv_cfg.strips[i].led_count = dev_cfg.strips[i].led_count;
    }
    drv_cfg.strip_count = dev_cfg.strip_count;

    ESP_ERROR_CHECK(led_controller_init(&drv_cfg));

    ESP_ERROR_CHECK(wifi_setup_init());
    ESP_ERROR_CHECK(wifi_setup_wait_connected(30000));
    ESP_LOGI(TAG, "WiFi connected, IP: %s", wifi_setup_get_ip());

    ESP_ERROR_CHECK(http_api_start(dev_cfg.http_port));
    ESP_LOGI(TAG, "HTTP server on port %d", dev_cfg.http_port);
}
