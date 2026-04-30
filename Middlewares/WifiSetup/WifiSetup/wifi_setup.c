#include "wifi_setup.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define NVS_NS "wifi_cfg"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASS "password"
#define AP_SSID "ESP32-LED-Setup"
#define MAX_RETRY 5

static const char *TAG = "wifi";
static EventGroupHandle_t s_evt_group = NULL;
static esp_netif_t *s_sta_netif = NULL;
static int s_retry_count = 0;
static char s_ip_str[16] = "";

#define CONNECTED_BIT BIT0
#define FAIL_BIT      BIT1

// --- NVS helpers ---

static esp_err_t load_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) return err;

    err = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) { nvs_close(h); return err; }

    err = nvs_get_str(h, NVS_KEY_PASS, password, &pass_len);
    nvs_close(h);
    return err;
}

static esp_err_t save_credentials(const char *ssid, const char *password)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;

    err = nvs_set_str(h, NVS_KEY_SSID, ssid);
    if (err == ESP_OK) err = nvs_set_str(h, NVS_KEY_PASS, password);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

// --- AP config page helpers ---

static int hex_val(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t url_decode(const char *src, size_t src_len, char *dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; i < src_len && j < dst_size - 1; i++) {
        if (src[i] == '%' && i + 2 < src_len) {
            int hi = hex_val(src[i + 1]);
            int lo = hex_val(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                dst[j++] = (char)(hi * 16 + lo);
                i += 2;
                continue;
            }
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            continue;
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
    return j;
}

static bool parse_form_field(const char *body, const char *key, char *out, size_t out_size)
{
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "%s=", key);
    const char *start = strstr(body, prefix);
    if (!start) return false;
    start += strlen(prefix);

    const char *end = strchr(start, '&');
    size_t len = end ? (size_t)(end - start) : strlen(start);

    url_decode(start, len, out, out_size);
    return true;
}

// --- AP HTTP handlers ---

static const char HTML_FORM[] =
    "<html><head><title>ESP32 LED Setup</title></head><body>"
    "<h1>ESP32 LED WiFi Setup</h1>"
    "<form method='POST' action='/save'>"
    "SSID:<br><input name='ssid' size='32'><br><br>"
    "Password:<br><input name='password' type='password' size='64'><br><br>"
    "<button type='submit'>Save &amp; Reboot</button>"
    "</form></body></html>";

static esp_err_t ap_index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_FORM, sizeof(HTML_FORM) - 1);
    return ESP_OK;
}

static esp_err_t ap_save_handler(httpd_req_t *req)
{
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    char ssid[33] = {0}, password[65] = {0};
    if (!parse_form_field(buf, "ssid", ssid, sizeof(ssid)) || strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }
    parse_form_field(buf, "password", password, sizeof(password));

    esp_err_t err = save_credentials(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req,
        "<html><body><h1>Saved!</h1><p>Rebooting...</p></body></html>");

    ESP_LOGI(TAG, "WiFi credentials saved, rebooting in 1s...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// --- AP config mode (blocks until reboot) ---

static void start_ap_config_mode(void)
{
    ESP_LOGI(TAG, "Starting AP config mode: SSID=%s", AP_SSID);

    esp_netif_create_default_wifi_ap();

    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = 0,
            .channel = 1,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = 4,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP started, connect to '%s' and open http://192.168.4.1", AP_SSID);

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.server_port = 80;
    httpd_handle_t config_server = NULL;
    ESP_ERROR_CHECK(httpd_start(&config_server, &http_cfg));

    httpd_uri_t uri_index = { .uri = "/", .method = HTTP_GET, .handler = ap_index_handler };
    httpd_uri_t uri_save  = { .uri = "/save", .method = HTTP_POST, .handler = ap_save_handler };
    httpd_register_uri_handler(config_server, &uri_index);
    httpd_register_uri_handler(config_server, &uri_save);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// --- STA event handler ---

static void sta_event_handler(void *arg, esp_event_base_t evt_base,
                               int32_t evt_id, void *evt_data)
{
    if (evt_base == WIFI_EVENT && evt_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (evt_base == WIFI_EVENT && evt_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_retry_count++;
        if (s_retry_count < MAX_RETRY) {
            esp_wifi_connect();
            ESP_LOGW(TAG, "STA disconnected, retry %d/%d", s_retry_count, MAX_RETRY);
        } else {
            ESP_LOGE(TAG, "STA connect failed after %d retries", MAX_RETRY);
            xEventGroupSetBits(s_evt_group, FAIL_BIT);
        }
    } else if (evt_base == IP_EVENT && evt_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)evt_data;
        snprintf(s_ip_str, sizeof(s_ip_str), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Got IP: %s", s_ip_str);
        s_retry_count = 0;
        xEventGroupSetBits(s_evt_group, CONNECTED_BIT);
    }
}

// --- Public API ---

esp_err_t wifi_setup_init(void)
{
    s_evt_group = xEventGroupCreate();
    if (!s_evt_group) return ESP_ERR_NO_MEM;

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &sta_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &sta_event_handler, NULL, NULL));

    char ssid[33] = {0}, password[65] = {0};
    if (load_credentials(ssid, sizeof(ssid), password, sizeof(password)) != ESP_OK) {
        ESP_LOGI(TAG, "No WiFi credentials in NVS");
        start_ap_config_mode();
        return ESP_FAIL;
    }

    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_config_t wifi_cfg = {0};
    strlcpy((char *)wifi_cfg.sta.ssid, ssid, sizeof(wifi_cfg.sta.ssid));
    strlcpy((char *)wifi_cfg.sta.password, password, sizeof(wifi_cfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    return ESP_OK;
}

esp_err_t wifi_setup_wait_connected(int timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(s_evt_group,
                                            CONNECTED_BIT | FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(timeout_ms));

    if (bits & CONNECTED_BIT) {
        return ESP_OK;
    }

    if (bits & FAIL_BIT) {
        ESP_LOGW(TAG, "Connection failed, falling back to AP config mode");
        esp_wifi_stop();
        start_ap_config_mode();
    }

    return ESP_ERR_TIMEOUT;
}

const char *wifi_setup_get_ip(void)
{
    return s_ip_str;
}
