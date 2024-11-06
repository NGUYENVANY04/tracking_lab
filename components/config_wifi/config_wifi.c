#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"
#include "config_wifi.h"
#include "mqtt_communication.h"
#include <driver/gpio.h>
#define STATE_WIFI GPIO_NUM_2
static bool state_connect = false;
static EventGroupHandle_t s_wifi_event_group;
static const int CONNECTED_BIT = BIT0;
static const int ESPTOUCH_DONE_BIT = BIT1;
static const char *TAG = "smartconfig";

#define WIFI_SSID_KEY ""
#define WIFI_PASS_KEY ""
#define TOKEN_KEY ""
#define MAX_RETRY_NUM 20
nvs_handle_t nvs_handle_ssid;
nvs_handle_t nvs_handle_pass;
nvs_handle_t nvs_save_token;
static bool has_wifi_credentials = false;
static uint8_t retry_num = 0;
static void smartconfig_example_task(void *parm)
{
    EventBits_t uxBits;
    ESP_ERROR_CHECK(esp_smartconfig_set_type(SC_TYPE_ESPTOUCH));
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_smartconfig_start(&cfg));
    vTaskDelay(10000 / portTICK_PERIOD_MS); // Wait for a while to finish
    while (1)
    {
        printf("Waiting for smartconfig...\n");
        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, 5000 / portTICK_PERIOD_MS);
        if (uxBits & CONNECTED_BIT)
        {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if (uxBits & ESPTOUCH_DONE_BIT)
        {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data, bool *state_connect)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        if (!has_wifi_credentials)
            xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 10240, NULL, 5, NULL);
        else
            esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (!has_wifi_credentials)
        {
            esp_wifi_connect();
            xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
        }
        else
        {
            if (retry_num < MAX_RETRY_NUM)
            {
                retry_num++;
                ESP_LOGI(TAG, "Retry to connect to the AP. Retry num: %d", retry_num);
                esp_wifi_connect();
            }
            else
            {
                esp_restart();
            }
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "Connected to internet");
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
        gpio_set_level(11, 1);
        mqtt_app_start((char *)token);
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE)
    {
        ESP_LOGI(TAG, "Scan done");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL)
    {
        ESP_LOGI(TAG, "Found channel");
    }
    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD)
    {
        ESP_LOGI(TAG, "Got SSID and password");
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t password_get[200] = {0};
        uint8_t rvd_data[33] = {0};

        // Passwifi/token/l/l

        // Copy SSID and password
        memcpy(ssid_get, evt->ssid, sizeof(evt->ssid));
        memcpy(password_get, evt->password, sizeof(evt->password));
        bzero(&wifi_config, sizeof(wifi_config_t));

        // Tach chuoi su dung strtok
        char *tk = strtok((char *)password_get, "/");
        if (tk != NULL)
            strncpy((char *)passwifi, tk, sizeof(passwifi) - 1);

        tk = strtok(NULL, "/");
        if (tk != NULL)
            strncpy((char *)token, tk, sizeof(token) - 1);

        // token = strtok(NULL, "/");
        // if (token != NULL)
        //     strncpy((char *)userid, token, sizeof(userid) - 1);

        // token = strtok(NULL, "/");
        // if (token != NULL)
        //     strncpy((char *)passmqtt, token, sizeof(passmqtt) - 1);

        // Config WiFi
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, passwifi, sizeof(passwifi));

        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true)
        {
            ESP_LOGI(TAG, "Set MAC address of target AP: " MACSTR " ", MAC2STR(evt->bssid));
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        ESP_LOGI(TAG, "SSID:%s", ssid_get);
        ESP_LOGI(TAG, "PASSWORD:%s", passwifi);
        ESP_LOGI(TAG, "Token ID:%s", token);

        if (evt->type == SC_TYPE_ESPTOUCH_V2)
        {
            ESP_ERROR_CHECK(esp_smartconfig_get_rvd_data(rvd_data, sizeof(rvd_data)));
            ESP_LOGI(TAG, "RVD_DATA:");
            for (int i = 0; i < sizeof(rvd_data); i++)
            {
                printf("%02x ", rvd_data[i]);
            }
            printf("\n");
        }

        // WiFi connect
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());

        // Save WiFi credentials to NVS
        // nvs_handle_t nvs_handle_ssid;
        ESP_ERROR_CHECK(nvs_open("ssid_wifi", NVS_READWRITE, &nvs_handle_ssid));
        ESP_ERROR_CHECK(nvs_set_str(nvs_handle_ssid, WIFI_SSID_KEY, (char *)ssid_get));
        ESP_ERROR_CHECK(nvs_commit(nvs_handle_ssid));
        nvs_close(nvs_handle_ssid);

        // nvs_handle_t nvs_handle_pass;
        ESP_ERROR_CHECK(nvs_open("pass_wifi", NVS_READWRITE, &nvs_handle_pass));
        ESP_ERROR_CHECK(nvs_set_str(nvs_handle_pass, WIFI_PASS_KEY, (char *)passwifi)); // Save only passwifi
        ESP_ERROR_CHECK(nvs_commit(nvs_handle_pass));
        nvs_close(nvs_handle_pass);

        // Save token to NVS
        // nvs_handle_t nvs_save_token;
        ESP_ERROR_CHECK(nvs_open("token", NVS_READWRITE, &nvs_save_token));
        ESP_ERROR_CHECK(nvs_set_str(nvs_save_token, TOKEN_KEY, (char *)token));
        ESP_ERROR_CHECK(nvs_commit(nvs_save_token));
        nvs_close(nvs_save_token);
        // Save clientid to NVS
        // nvs_handle_t nvs_save_clientid;
        // ESP_ERROR_CHECK(nvs_open("clientid", NVS_READWRITE, &nvs_save_clientid));
        // ESP_ERROR_CHECK(nvs_set_str(nvs_save_clientid, CLIENTID_KEY, (char *)clientid));
        // ESP_ERROR_CHECK(nvs_commit(nvs_save_clientid));
        // nvs_close(nvs_save_clientid);

        // // Save passmqtt to NVS
        // // nvs_handle_t nvs_save_passmqtt;
        // ESP_ERROR_CHECK(nvs_open("passmqtt", NVS_READWRITE, &nvs_save_passmqtt));
        // ESP_ERROR_CHECK(nvs_set_str(nvs_save_passmqtt, PASSMQTT_KEY, (char *)passmqtt));
        // ESP_ERROR_CHECK(nvs_commit(nvs_save_passmqtt));
        // nvs_close(nvs_save_passmqtt);
    }

    else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE)
    {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}

void initialise_wifi(void)
{
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    // Check if WiFi credentials are stored in NVS

    esp_err_t err_ssid = nvs_open("ssid_wifi", NVS_READONLY, &nvs_handle_ssid);
    esp_err_t err_pass = nvs_open("pass_wifi", NVS_READONLY, &nvs_handle_pass);
    esp_err_t err_token = nvs_open("token", NVS_READONLY, &nvs_save_token);

    if (err_ssid == ESP_ERR_NVS_NOT_FOUND && err_pass == ESP_ERR_NVS_NOT_FOUND && err_token == ESP_ERR_NVS_NOT_FOUND)
    {

        // Namespace "storage" does not exist, create it by writing default values
        ESP_LOGI(TAG, "Namespace 'ssid_wifi' and 'pass_wifi' not found, creating it");
        ESP_ERROR_CHECK(nvs_open("ssid_wifi", NVS_READWRITE, &nvs_handle_ssid));
        ESP_ERROR_CHECK(nvs_set_str(nvs_handle_ssid, WIFI_SSID_KEY, ""));
        ESP_ERROR_CHECK(nvs_commit(nvs_handle_ssid));
        nvs_close(nvs_handle_ssid);

        ESP_ERROR_CHECK(nvs_open("pass_wifi", NVS_READWRITE, &nvs_handle_pass));
        ESP_ERROR_CHECK(nvs_set_str(nvs_handle_pass, WIFI_PASS_KEY, ""));
        ESP_ERROR_CHECK(nvs_commit(nvs_handle_pass));
        nvs_close(nvs_handle_pass);

        ESP_ERROR_CHECK(nvs_open("token", NVS_READWRITE, &nvs_save_token));
        ESP_ERROR_CHECK(nvs_set_str(nvs_save_token, TOKEN_KEY, ""));
        ESP_ERROR_CHECK(nvs_commit(nvs_save_token));
        nvs_close(nvs_save_token);

        // Re-open the namespace in read-only mode
        err_ssid = nvs_open("ssid_wifi", NVS_READONLY, &nvs_handle_ssid);
        err_pass = nvs_open("pass_wifi", NVS_READONLY, &nvs_handle_pass);
        err_token = nvs_open("token", NVS_READONLY, &nvs_save_token);
    }
    ESP_ERROR_CHECK(err_ssid);
    ESP_ERROR_CHECK(err_pass);
    ESP_ERROR_CHECK(err_token);

    size_t ssid_len = 31;
    size_t pass_len = 49;
    size_t token_len = 99;

    esp_err_t ssid_err = nvs_get_str(nvs_handle_ssid, WIFI_SSID_KEY, (char *)ssid_get, &ssid_len);
    esp_err_t pass_err = nvs_get_str(nvs_handle_pass, WIFI_PASS_KEY, (char *)passwifi, &pass_len);
    esp_err_t token_err = nvs_get_str(nvs_save_token, TOKEN_KEY, (char *)token, &token_len);
    nvs_close(nvs_handle_ssid);
    nvs_close(nvs_handle_pass);
    nvs_close(nvs_save_token);

    if (ssid_err == ESP_OK && pass_err == ESP_OK && strlen((char *)ssid_get) > 0 && strlen((char *)passwifi) > 0)
    {
        has_wifi_credentials = true;
        printf("Using stored WiFi credentials: SSID: '%s', Password: '%s'\n", ssid_get, passwifi);
        // WiFi credentials found in NVS, use them to connect
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));
        strncpy((char *)wifi_config.sta.ssid, (char *)ssid_get, sizeof(wifi_config.sta.ssid));
        strncpy((char *)wifi_config.sta.password, (char *)passwifi, sizeof(wifi_config.sta.password));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
    else
    {
        has_wifi_credentials = false;
        esp_event_handler_instance_t instance_sc;
        ESP_ERROR_CHECK(esp_event_handler_instance_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_sc));
        // No WiFi credentials found, start smartconfig
        ESP_LOGI(TAG, "Starting smartconfig");
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}

void reset_wifi_credentials(void)
{
    nvs_flash_erase();
    ESP_LOGI(TAG, "WiFi credentials reset");
}

// // test
// int s_retry_num = 0;
// #define WIFI_CONNECTED_BIT BIT0
// #define WIFI_FAIL_BIT BIT1
// static void event_handler_test(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
// {
//     if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
//     {
//         esp_wifi_connect();
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
//     {
//         if (s_retry_num < 20)
//         {
//             esp_wifi_connect();
//             s_retry_num++;
//             ESP_LOGI("wifi", "retry to connect to the AP");
//         }
//         else
//         {
//             xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
//         }
//     }
//     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
//     {
//         ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
//         ESP_LOGI("wifi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
//         s_retry_num = 0;
//         xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
//         gpio_set_level(11, 1);
//         mqtt_app_start();
//     }
// }

// void wifi_init_sta(void)
// {
//     s_wifi_event_group = xEventGroupCreate();
//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     esp_netif_create_default_wifi_sta();
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//     esp_event_handler_instance_t instance_any_id;
//     esp_event_handler_instance_t instance_got_ip;
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler_test, NULL, &instance_any_id));
//     ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler_test, NULL, &instance_got_ip));
//     wifi_config_t wifi_config = {
//         .sta = {
//             .ssid = "khongbug",                       // Đảm bảo SSID đúng
//             .password = "khongbug",                   // Đảm bảo mật khẩu đúng
//             .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Cài đặt chế độ bảo mật chính xác
//         },
//     };
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
//     ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
//     ESP_ERROR_CHECK(esp_wifi_start());

//     ESP_LOGI("Wifi Init", "wifi_init_sta finished.");
// }
