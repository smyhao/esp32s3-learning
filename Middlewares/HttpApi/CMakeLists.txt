set(HTTPAPI_SRC_DIRS "HttpApi")
set(HTTPAPI_INCLUDE_DIRS "HttpApi")
set(HTTPAPI_REQUIRES LedController StripConfig esp_http_server json)

idf_component_register(
    SRC_DIRS ${HTTPAPI_SRC_DIRS}
    INCLUDE_DIRS ${HTTPAPI_INCLUDE_DIRS}
    REQUIRES ${HTTPAPI_REQUIRES}
)
