idf_component_register(SRCS "mqtt_communication.c"
                    INCLUDE_DIRS "include"
                    REQUIRES mqtt "function_keys" "config_wifi"
                    PRIV_REQUIRES json)
