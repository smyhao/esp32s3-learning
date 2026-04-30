#ifndef HTTP_API_H
#define HTTP_API_H

#include "esp_err.h"

esp_err_t http_api_start(int port);
esp_err_t http_api_stop(void);

#endif // HTTP_API_H
