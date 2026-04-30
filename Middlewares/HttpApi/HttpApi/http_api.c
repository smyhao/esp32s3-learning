#include "http_api.h"

#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "led_controller.h"
#include "strip_config.h"

static const char *TAG = "http_api";
static httpd_handle_t s_server = NULL;

static esp_err_t send_error(httpd_req_t *req, const char *status_str, const char *message)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status_str);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "message", message);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static esp_err_t send_ok(httpd_req_t *req, cJSON *root)
{
    httpd_resp_set_type(req, "application/json");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    httpd_resp_send(req, json, strlen(json));
    free(json);
    return ESP_OK;
}

static bool parse_color(const char *color, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!color || strlen(color) != 7 || color[0] != '#') return false;

    char *endptr;
    long val = strtol(color + 1, &endptr, 16);
    if (*endptr != '\0' || val < 0 || val > 0xFFFFFF) return false;

    *r = (uint8_t)((val >> 16) & 0xFF);
    *g = (uint8_t)((val >> 8) & 0xFF);
    *b = (uint8_t)(val & 0xFF);
    return true;
}

// GET /api/health
static esp_err_t health_get_handler(httpd_req_t *req)
{
    const device_config_t *cfg = strip_config_get();
    int strip_count = led_controller_get_strip_count();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");

    cJSON *strips = cJSON_AddArrayToObject(root, "strips");
    for (int i = 0; i < strip_count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "gpio", cfg->strips[i].gpio_num);
        cJSON_AddNumberToObject(s, "led_count", cfg->strips[i].led_count);
        cJSON_AddItemToArray(strips, s);
    }

    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));

    return send_ok(req, root);
}

// POST /api/led/set
static esp_err_t led_set_post_handler(httpd_req_t *req)
{
    if (req->content_len > 4096) {
        return send_error(req, "400 Bad Request", "request body too large");
    }

    char *buf = malloc(4097);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    int ret = httpd_req_recv(req, buf, 4096);
    if (ret <= 0) {
        free(buf);
        return send_error(req, "400 Bad Request", "invalid request body");
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        free(buf);
        return send_error(req, "400 Bad Request", "invalid JSON body");
    }

    cJSON *leds = cJSON_GetObjectItem(root, "leds");
    if (!leds || !cJSON_IsArray(leds)) {
        cJSON_Delete(root);
        free(buf);
        return send_error(req, "400 Bad Request", "missing required field: leds");
    }

    int array_size = cJSON_GetArraySize(leds);
    if (array_size == 0) {
        cJSON_Delete(root);
        free(buf);
        led_controller_clear_all();
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddNumberToObject(resp, "applied", 0);
        return send_ok(req, resp);
    }

    int applied = 0;
    for (int i = 0; i < array_size; i++) {
        cJSON *item = cJSON_GetArrayItem(leds, i);

        cJSON *j_gpio = cJSON_GetObjectItem(item, "gpio");
        cJSON *j_index = cJSON_GetObjectItem(item, "index");
        cJSON *j_mode = cJSON_GetObjectItem(item, "mode");
        cJSON *j_color = cJSON_GetObjectItem(item, "color");
        cJSON *j_dur = cJSON_GetObjectItem(item, "duration_ms");

        if (!j_gpio || !cJSON_IsNumber(j_gpio)) {
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", "missing required field: gpio");
        }
        if (!j_index || !cJSON_IsNumber(j_index)) {
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", "missing required field: index");
        }
        if (!j_mode || !cJSON_IsString(j_mode)) {
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", "missing required field: mode");
        }
        if (!j_color || !cJSON_IsString(j_color)) {
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", "missing required field: color");
        }

        // Validate mode
        led_mode_t mode;
        if (strcmp(j_mode->valuestring, "static") == 0) {
            mode = LED_MODE_STATIC;
        } else if (strcmp(j_mode->valuestring, "blink") == 0) {
            mode = LED_MODE_BLINK;
        } else {
            char msg[128];
            snprintf(msg, sizeof(msg), "invalid mode: %s, expected static or blink", j_mode->valuestring);
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", msg);
        }

        // Validate color
        uint8_t r, g, b;
        if (!parse_color(j_color->valuestring, &r, &g, &b)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "invalid color format: %s", j_color->valuestring);
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", msg);
        }

        int gpio = j_gpio->valueint;
        int index = j_index->valueint;
        int duration_ms = j_dur ? j_dur->valueint : 0;

        // Find strip by GPIO (silently ignore if not found)
        int strip_idx = led_controller_find_strip_by_gpio(gpio);
        if (strip_idx < 0) continue;

        // Check index range (silently ignore if out of range)
        if (index < 0 || index >= led_controller_get_strip_led_count(strip_idx)) {
            continue;
        }

        led_controller_set(strip_idx, index, mode, r, g, b, duration_ms);
        applied++;
    }

    cJSON_Delete(root);
    free(buf);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "applied", applied);
    return send_ok(req, resp);
}

// POST /api/led/clear
static esp_err_t led_clear_post_handler(httpd_req_t *req)
{
    led_controller_clear_all();

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    return send_ok(req, resp);
}

// GET /api/config
static esp_err_t config_get_handler(httpd_req_t *req)
{
    const device_config_t *cfg = strip_config_get();

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");

    cJSON *strips = cJSON_AddArrayToObject(root, "strips");
    for (int i = 0; i < cfg->strip_count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "gpio", cfg->strips[i].gpio_num);
        cJSON_AddNumberToObject(s, "led_count", cfg->strips[i].led_count);
        cJSON_AddItemToArray(strips, s);
    }

    cJSON_AddNumberToObject(root, "http_port", cfg->http_port);

    return send_ok(req, root);
}

// POST /api/config
static esp_err_t config_post_handler(httpd_req_t *req)
{
    if (req->content_len > 4096) {
        return send_error(req, "400 Bad Request", "request body too large");
    }

    char *buf = malloc(4097);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    int ret = httpd_req_recv(req, buf, 4096);
    if (ret <= 0) {
        free(buf);
        return send_error(req, "400 Bad Request", "invalid request body");
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        free(buf);
        return send_error(req, "400 Bad Request", "invalid JSON body");
    }

    cJSON *j_strips = cJSON_GetObjectItem(root, "strips");
    if (!j_strips || !cJSON_IsArray(j_strips)) {
        cJSON_Delete(root);
        free(buf);
        return send_error(req, "400 Bad Request", "missing required field: strips");
    }

    int count = cJSON_GetArraySize(j_strips);
    if (count <= 0 || count > MAX_STRIPS) {
        cJSON_Delete(root);
        free(buf);
        char msg[64];
        snprintf(msg, sizeof(msg), "strips count must be 1-%d", MAX_STRIPS);
        return send_error(req, "400 Bad Request", msg);
    }

    device_config_t new_cfg = {0};
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(j_strips, i);

        cJSON *j_gpio = cJSON_GetObjectItem(item, "gpio");
        cJSON *j_led_count = cJSON_GetObjectItem(item, "led_count");

        if (!j_gpio || !cJSON_IsNumber(j_gpio) ||
            !j_led_count || !cJSON_IsNumber(j_led_count)) {
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", "each strip needs gpio and led_count");
        }

        int gpio = j_gpio->valueint;
        int led_count = j_led_count->valueint;

        if (gpio < 0 || gpio > 48) {
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", "gpio must be 0-48");
        }
        if (led_count <= 0 || led_count > 256) {
            cJSON_Delete(root);
            free(buf);
            return send_error(req, "400 Bad Request", "led_count must be 1-256");
        }

        new_cfg.strips[i].gpio_num = gpio;
        new_cfg.strips[i].led_count = led_count;
    }
    new_cfg.strip_count = count;

    cJSON *j_port = cJSON_GetObjectItem(root, "http_port");
    new_cfg.http_port = (j_port && cJSON_IsNumber(j_port)) ? j_port->valueint : 80;

    cJSON_Delete(root);
    free(buf);

    esp_err_t err = strip_config_apply(&new_cfg);
    if (err != ESP_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "failed to apply config: %s", esp_err_to_name(err));
        return send_error(req, "500 Internal Server Error", msg);
    }

    const device_config_t *cfg = strip_config_get();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");

    cJSON *r_strips = cJSON_AddArrayToObject(resp, "strips");
    for (int i = 0; i < cfg->strip_count; i++) {
        cJSON *s = cJSON_CreateObject();
        cJSON_AddNumberToObject(s, "gpio", cfg->strips[i].gpio_num);
        cJSON_AddNumberToObject(s, "led_count", cfg->strips[i].led_count);
        cJSON_AddItemToArray(r_strips, s);
    }

    return send_ok(req, resp);
}

static const httpd_uri_t uri_health = {
    .uri = "/api/health",
    .method = HTTP_GET,
    .handler = health_get_handler,
};

static const httpd_uri_t uri_led_set = {
    .uri = "/api/led/set",
    .method = HTTP_POST,
    .handler = led_set_post_handler,
};

static const httpd_uri_t uri_led_clear = {
    .uri = "/api/led/clear",
    .method = HTTP_POST,
    .handler = led_clear_post_handler,
};

static const httpd_uri_t uri_config_get = {
    .uri = "/api/config",
    .method = HTTP_GET,
    .handler = config_get_handler,
};

static const httpd_uri_t uri_config_post = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = config_post_handler,
};

// POST /api/config/strip
static esp_err_t config_strip_add_handler(httpd_req_t *req)
{
    if (req->content_len > 4096) {
        return send_error(req, "400 Bad Request", "request body too large");
    }

    char *buf = malloc(4097);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    int ret = httpd_req_recv(req, buf, 4096);
    if (ret <= 0) {
        free(buf);
        return send_error(req, "400 Bad Request", "invalid request body");
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return send_error(req, "400 Bad Request", "invalid JSON body");
    }

    cJSON *j_gpio = cJSON_GetObjectItem(root, "gpio");
    cJSON *j_count = cJSON_GetObjectItem(root, "led_count");

    if (!j_gpio || !cJSON_IsNumber(j_gpio)) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "missing required field: gpio");
    }
    if (!j_count || !cJSON_IsNumber(j_count)) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "missing required field: led_count");
    }

    int gpio = j_gpio->valueint;
    int led_count = j_count->valueint;
    cJSON_Delete(root);

    esp_err_t err = strip_config_add_strip(gpio, led_count);
    if (err == ESP_ERR_INVALID_ARG) {
        return send_error(req, "400 Bad Request", "gpio must be 0-48, led_count must be 1-256");
    }
    if (err == ESP_ERR_NO_MEM) {
        return send_error(req, "507 Insufficient Storage", "max 8 strips reached");
    }
    if (err != ESP_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "failed to add strip: %s", esp_err_to_name(err));
        return send_error(req, "500 Internal Server Error", msg);
    }

    const device_config_t *cfg = strip_config_get();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "gpio", gpio);
    cJSON_AddNumberToObject(resp, "led_count", led_count);
    cJSON_AddNumberToObject(resp, "total_strips", cfg->strip_count);
    return send_ok(req, resp);
}

// POST /api/config/strip/remove
static esp_err_t config_strip_remove_handler(httpd_req_t *req)
{
    if (req->content_len > 4096) {
        return send_error(req, "400 Bad Request", "request body too large");
    }

    char *buf = malloc(4097);
    if (!buf) {
        return send_error(req, "500 Internal Server Error", "out of memory");
    }
    int ret = httpd_req_recv(req, buf, 4096);
    if (ret <= 0) {
        free(buf);
        return send_error(req, "400 Bad Request", "invalid request body");
    }
    buf[ret] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) {
        return send_error(req, "400 Bad Request", "invalid JSON body");
    }

    cJSON *j_gpio = cJSON_GetObjectItem(root, "gpio");
    if (!j_gpio || !cJSON_IsNumber(j_gpio)) {
        cJSON_Delete(root);
        return send_error(req, "400 Bad Request", "missing required field: gpio");
    }

    int gpio = j_gpio->valueint;
    cJSON_Delete(root);

    esp_err_t err = strip_config_remove_strip(gpio);
    if (err == ESP_ERR_NOT_FOUND) {
        char msg[64];
        snprintf(msg, sizeof(msg), "gpio %d not found in config", gpio);
        return send_error(req, "404 Not Found", msg);
    }
    if (err != ESP_OK) {
        char msg[128];
        snprintf(msg, sizeof(msg), "failed to remove strip: %s", esp_err_to_name(err));
        return send_error(req, "500 Internal Server Error", msg);
    }

    const device_config_t *cfg = strip_config_get();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "status", "ok");
    cJSON_AddNumberToObject(resp, "removed_gpio", gpio);
    cJSON_AddNumberToObject(resp, "total_strips", cfg->strip_count);
    return send_ok(req, resp);
}

static const httpd_uri_t uri_strip_add = {
    .uri = "/api/config/strip",
    .method = HTTP_POST,
    .handler = config_strip_add_handler,
};

static const httpd_uri_t uri_strip_remove = {
    .uri = "/api/config/strip/remove",
    .method = HTTP_POST,
    .handler = config_strip_remove_handler,
};

esp_err_t http_api_start(int port)
{
    if (s_server) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.stack_size = 8192;
    config.max_uri_handlers = 8;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_register_uri_handler(s_server, &uri_health);
    httpd_register_uri_handler(s_server, &uri_led_set);
    httpd_register_uri_handler(s_server, &uri_led_clear);
    httpd_register_uri_handler(s_server, &uri_config_get);
    httpd_register_uri_handler(s_server, &uri_config_post);
    httpd_register_uri_handler(s_server, &uri_strip_add);
    httpd_register_uri_handler(s_server, &uri_strip_remove);

    ESP_LOGI(TAG, "HTTP server started on port %d", port);
    return ESP_OK;
}

esp_err_t http_api_stop(void)
{
    if (!s_server) return ESP_OK;
    esp_err_t err = httpd_stop(s_server);
    s_server = NULL;
    return err;
}
