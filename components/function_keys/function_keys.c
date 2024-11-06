#include <stdio.h>
#include "function_keys.h"
#include "esp_log.h"
#include "esp_system.h"
#include "config_wifi.h"
#include "mqtt_communication.h"
static portMUX_TYPE button_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t last_isr_time = 0;
static TaskHandle_t button_task_handle = NULL;
static TaskHandle_t led_task_handle = NULL;

void IRAM_ATTR button_isr_handler_reset(void *arg)
{
    uint32_t current_time = xTaskGetTickCountFromISR();

    if (current_time - last_isr_time > DEBOUNCE_DELAY_MS / portTICK_PERIOD_MS)
    {
        last_isr_time = current_time;
        portENTER_CRITICAL_ISR(&button_mux);
        gpio_intr_disable(RESET_BUTTON_PIN);
        portEXIT_CRITICAL_ISR(&button_mux);
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(button_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void IRAM_ATTR led_isr_handler(void *arg)
{
    uint32_t current_time = xTaskGetTickCountFromISR();

    if (current_time - last_isr_time > DEBOUNCE_DELAY_MS / portTICK_PERIOD_MS)
    {
        last_isr_time = current_time;
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(led_task_handle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void button_task_reset(void *arg)
{
    while (true)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        ESP_LOGI("Reset button", "Nút được nhấn! Đang đặt lại thông tin WiFi...");
        reset_wifi_credentials();
        esp_restart();
    }
}

static void led_task_output(void *arg)
{
    // int state_test = false;
    bool state = false;
    // ESP_LOGI("Log State", "%d", *state_connect);
    while (true)
    {
        // bool *state_connect = (bool *)arg;
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        state = gpio_get_level(GPIO_NUM_38);
        gpio_set_level(GPIO_NUM_38, !state);
        gpio_set_level(GPIO_NUM_10, !state);
        ESP_LOGI("LED Control", " LED state to %d", !state);
        // ESP_LOGW("Log value", "%d", (int)arg);
        if ((void *)arg != NULL)
        {
            // ESP_LOGI("LED Control", "Toggling LED state to %d", state);

            // test
            // state_test = !state_test;

            esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t)arg;
            check_and_report_device_state(client, "manual"); // "manual" is used as a dummy request ID for button press}
        }
    }
}
void init_button_reset(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RESET_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE};
    gpio_config(&io_conf);
    gpio_install_isr_service(0);

    gpio_isr_handler_add(RESET_BUTTON_PIN, button_isr_handler_reset, NULL);

    xTaskCreate(button_task_reset, "button_task", 4096, NULL, 3, &button_task_handle);
}

void init_button_control(void *arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_42),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&io_conf);

    gpio_isr_handler_add(GPIO_NUM_42, led_isr_handler, NULL);
    xTaskCreate(led_task_output, "led_task", 4096, arg, 5, &led_task_handle);
    // ESP_LOGI("Check bug", "%s", "Error in function");
}

void init_state_output(void)
{
    gpio_config_t io_conf_led = {
        .pin_bit_mask = (1ULL << GPIO_NUM_10),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config_t io_conf_relay = {
        .pin_bit_mask = (1ULL << GPIO_NUM_38),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf_led);
    gpio_config(&io_conf_relay);
    gpio_set_level(GPIO_NUM_10, 0);
    gpio_set_level(GPIO_NUM_38, 0);
}

void init_led_state_wifi(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_11),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_11, 0);
}
void init_led_state_mqtt()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_12),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);
    gpio_set_level(GPIO_NUM_12, 0);
}
