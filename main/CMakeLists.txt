idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES LedController StripConfig HttpApi WifiSetup nvs_flash esp_netif esp_event)
