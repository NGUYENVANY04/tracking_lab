#ifndef MQTT_COMMUNICATION_H
#define MQTT_COMMUNICATION_H
#include "mqtt_client.h"

void mqtt_app_start(char *token);
// void button_isr_handler(void *arg);
void check_and_report_device_state(esp_mqtt_client_handle_t client, const char *request_id);
#endif
