#include "config_wifi.h"
#include "function_keys.h"
#include <nvs_flash.h>
#include <esp_log.h>
/*
Nút nhấn vào 2 chân GPIO39 và GPIO42
OUTput là GPIO38 relay

3 đèn led báo là vào GPIO10,11,12
GPIO5 input hardware
*/
void app_main(void)
{
    bool state_connect = false;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    init_button_reset();

    init_state_output();
    // ESP_LOGI("log 1", " bug %d", ret);
    init_button_control(NULL);

    // ESP_LOGI("log 2", " bug %d", ret);

    // ESP_LOGI("log 3", " bug %d", ret);
    init_led_state_wifi();
    // ESP_LOGI("log 4", " bug %d", ret);
    init_led_state_mqtt();
    // Configure Wi-Fi
    initialise_wifi();
    // wifi_init_sta();
}
