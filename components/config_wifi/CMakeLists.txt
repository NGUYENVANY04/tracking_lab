idf_component_register(SRCS "config_wifi.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi wpa_supplicant nvs_flash driver "mqtt_communication" )
