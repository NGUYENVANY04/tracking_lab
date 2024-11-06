#include "mqtt_communication.h"
#include <stdio.h>
#include <cJSON.h>
#include "config_wifi.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "function_keys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// test
//  #include "led_strip.h"
//  static led_strip_handle_t led_strip;
// end_test

static const char *TAG = "log_MQTT";
char MQTT_Data_Receive[5000];

const int RELAY_GPIO = GPIO_NUM_38;
#define DEBOUNCE_DELAY_MS 200

static portMUX_TYPE button_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t last_isr_time = 0;
static esp_mqtt_client_handle_t mqtt_client = NULL;

static char *request_topic = "v1/devices/me/rpc/request/+";
static char result[6]; // "true" or "false" + null terminator
static char response_topic[256];
static char attributes[50];
// static const char *user_id = "dfw2bwli6lt3f8wmojsi";

//--------- MQTT --------------
static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

void check_and_report_device_state(esp_mqtt_client_handle_t client, const char *request_id)
{
    int gpio_state = gpio_get_level(GPIO_NUM_38);

    if (strcmp(request_id, "manual") == 0)
    {
        ESP_LOGI(TAG, "Request ID: manual");

        // Sử dụng response_topic cho topic telemetry
        snprintf(response_topic, sizeof(response_topic), "v1/devices/me/attributes");
        snprintf(attributes, sizeof(attributes), "{\"status\":%d}", gpio_state);
        ESP_LOGI(TAG, "Publishing telemetry to topic %s: %s", response_topic, attributes);
        int error = esp_mqtt_client_publish(client, response_topic, attributes, 0, 0, 0);
        ESP_LOGI(TAG, "Publish successful, msg_id=%d", error);
    }
    else
    {
        if (gpio_state)
            snprintf(result, sizeof(result), "true");
        else
            snprintf(result, sizeof(result), "false");
        snprintf(response_topic, sizeof(response_topic), "v1/devices/me/rpc/response/%s", request_id);
        ESP_LOGI(TAG, "Publishing to topic %s: %s", response_topic, result);
        esp_mqtt_client_publish(client, response_topic, result, 0, 0, 0);

        snprintf(response_topic, sizeof(response_topic), "v1/devices/me/attributes");
        snprintf(attributes, sizeof(attributes), "{\"status\":%d}", gpio_state);
        ESP_LOGI(TAG, "Publishing attributes to topic %s: %s", response_topic, attributes);
        int error = esp_mqtt_client_publish(client, response_topic, attributes, 0, 0, 0);
        ESP_LOGI(TAG, "Publish successful, msg_id=%d", error);
    }
}

void extract_request_id(const char *topic, char *request_id)
{
    const char *last_slash = strrchr(topic, '/');
    if (last_slash != NULL)
    {
        const char *json_start = strchr(last_slash, '{');
        if (json_start != NULL)
        {
            size_t id_length = json_start - (last_slash + 1);
            strncpy(request_id, last_slash + 1, id_length);
            request_id[id_length] = '\0';
        }
        else
        {
            request_id[0] = '\0';
        }
    }
    else
    {
        request_id[0] = '\0';
    }
}

static void handle_incoming_message(esp_mqtt_client_handle_t client, const char *topic, const char *data)
{
    ESP_LOGI(TAG, "Handling incoming message from topic %s: %s", topic, data);

    char request_id[256]; // Dung lượng đủ lớn để chứa request_id
    extract_request_id(topic, request_id);
    printf("Request ID: %s\n", request_id);

    cJSON *json = cJSON_Parse(data);
    if (json == NULL)
    {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return;
    }

    cJSON *method = cJSON_GetObjectItem(json, "method");
    if (method && cJSON_IsString(method))
    {
        if (strcmp(method->valuestring, "status") == 0)
        {
            ESP_LOGI(TAG, "Received 'getState' request.");
            check_and_report_device_state(client, request_id);
        }
        else if (strcmp(method->valuestring, "setState") == 0)
        {
            ESP_LOGI(TAG, "Received 'setState' request.");
            cJSON *params = cJSON_GetObjectItem(json, "params");

            if (params && cJSON_IsBool(params))
            {
                bool new_state = cJSON_IsTrue(params);
                ESP_LOGI(TAG, "Received 'params' request.");
                ESP_LOGI(TAG, "New State: %s", new_state ? "true" : "false");

                gpio_set_level(RELAY_GPIO, new_state);
                gpio_set_level(GPIO_NUM_10, new_state);

                ESP_LOGI(TAG, "Relay state set to: %s", new_state ? "true" : "false");

                // update
                check_and_report_device_state(client, request_id);
            }
            else
            {
                ESP_LOGE(TAG, "Invalid 'params' received.");
            }

            cJSON_Delete(json);
        }
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch (event->event_id)
    {
    case MQTT_EVENT_CONNECTED:
        int gpio_state = gpio_get_level(GPIO_NUM_38);
        snprintf(attributes, sizeof(attributes), "{\"status\":%d}", gpio_state);
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        ESP_LOGI(TAG, "Subscribing to topic: %s", request_topic);
        esp_mqtt_client_subscribe(client, request_topic, 0);
        esp_mqtt_client_publish(client, "v1/devices/me/attributes", attributes, 0, 0, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        esp_mqtt_client_start(client);
        break;
    case MQTT_EVENT_DATA:
        memset(MQTT_Data_Receive, 0, sizeof(MQTT_Data_Receive));
        memcpy(MQTT_Data_Receive, event->data, event->data_len);
        MQTT_Data_Receive[event->data_len] = '\0';
        ESP_LOGI(TAG, "MQTT_EVENT_DATA: %s", MQTT_Data_Receive);
        handle_incoming_message(client, event->topic, MQTT_Data_Receive);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(char *token)
{
    ESP_LOGI(TAG, "Token ID:%s", token);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://iot.bstar-badminton.com",
        .credentials.username = token,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
    gpio_set_level(GPIO_NUM_12, 1);

    gpio_isr_handler_remove(GPIO_NUM_38);
    init_button_control((void *)client);
}