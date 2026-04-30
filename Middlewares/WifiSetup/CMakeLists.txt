set(WIFISETUP_SRC_DIRS "WifiSetup")
set(WIFISETUP_INCLUDE_DIRS "WifiSetup")
set(WIFISETUP_REQUIRES nvs_flash esp_wifi esp_netif esp_http_server)

idf_component_register(
    SRC_DIRS ${WIFISETUP_SRC_DIRS}
    INCLUDE_DIRS ${WIFISETUP_INCLUDE_DIRS}
    REQUIRES ${WIFISETUP_REQUIRES}
)
