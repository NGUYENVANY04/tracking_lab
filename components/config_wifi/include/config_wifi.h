#ifndef CONFIG_WIFI_H
#define CONFIG_WIFI_H
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
static uint8_t passwifi[50] = {0};
static uint8_t ssid_get[32] = {0};
static uint8_t token[100] = {0};
// static uint8_t userid[50] = {0};
// static uint8_t passmqtt[50] = {0};
// Function prototypes
void initialise_wifi(void);
void reset_wifi_credentials(void);

// void wifi_init_sta(void);

#endif // CONFIG_WIFI_H
