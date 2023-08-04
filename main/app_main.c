/* MQTT (over TCP) Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_sntp.h"

static const char *TAG = "MQTT_EXAMPLE";

#define TAG "NTP TIME"

static char mqtt_payload[50];

SemaphoreHandle_t got_time_semaphore;
bool BUSY = 0;
void Set_Schedule(int seconds);

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}




/*
 * @brief Event handler registered to receive MQTT events
 *
 *  This function is called by the MQTT client event loop.
 *
 * @param handler_args user data registered to the event.
 * @param base Event base for the handler(always MQTT Base in this example).
 * @param event_id The id for the received event.
 * @param event_data The data for the event, esp_mqtt_event_handle_t.
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id = 0;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "time_topic", "CONNECTION Established", 0, 2, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "time_topic", 2);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        // msg_id = esp_mqtt_client_unsubscribe(client, "my_topic");
        // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
       {
        if (strncmp(event->topic, "time_topic", event->topic_len) == 0){
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");

        

        int seconds = atoi(event->data);

        if (seconds == 0 && *event->data != '0'){
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
        }

        else{
            Set_Schedule(seconds);
        }

        }
         else{}
        
        break;
       } 

        // msg_id = esp_mqtt_client_unsubscribe(client, "my_topic");
        // ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno)); 

        }
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

esp_mqtt_client_handle_t client = NULL;

static void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
    };
     client = esp_mqtt_client_init(&mqtt_cfg);
    /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}

void print_time()
{
    time_t now = 0;
    time(&now);
    struct tm *time_info = localtime(&now);

    char time_buffer[50];
    strftime(time_buffer, sizeof(time_buffer), "%c", time_info);
    // esp_mqtt_client_publish(client, "time_topic", time_buffer, 0, 2, 0);


    ESP_LOGI(TAG, "************ %s ***********", time_buffer);
}

void on_got_time(struct timeval *tv)
{
    printf("on got callback %lld\n", tv->tv_sec);
    print_time();

    xSemaphoreGive(got_time_semaphore);
}


void Time_init(){
    esp_sntp_init();
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_setservername(0, "pool.ntp.org");
     esp_sntp_set_time_sync_notification_cb(on_got_time);

}

void Set_gpio(void *param);

void Current_time(void *paramss){
   

     xSemaphoreTake(got_time_semaphore, portMAX_DELAY);

    time_t now;
    struct tm time_info;

    while (1) {
        time(&now);
        localtime_r(&now,  &time_info);
        printf("Current time: %lld\n", now);
        strftime(mqtt_payload, sizeof(mqtt_payload), "%c",  &time_info);
        esp_mqtt_client_publish(client, "time_topic", mqtt_payload, 0, 2, 0);

        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }

}



void OnConnect(void *params);

void Set_Schedule(int seconds){
    if (BUSY)
    {
        esp_mqtt_client_publish(client, "time_topic", "The relay event is already scheduled", 0, 2, 0);
    }
    else{
        time_t now1;
                struct tm schedule_time;
                struct tm current_time;
                printf("time to be added: %s\n", seconds);
                time(&now1);
                
                localtime_r(&now1,  &current_time);

                // Calculate scheduled time by adding seconds
                now1 += seconds;
                localtime_r(&now1,  &schedule_time);
                //mktime( &schedule_time);

                char scheduled_time_str[30];
                char message[] = " schedule time is: ";
    
                strftime(scheduled_time_str, sizeof(scheduled_time_str), "%c", &schedule_time);
                printf("scheduled time is: %s\n", scheduled_time_str);
                   strcat(message, scheduled_time_str);

                // Publish the scheduled time back to a topic
                esp_mqtt_client_publish(client, "scheduled_time_topic", message, 0, 2, 0);
    }
    
}



void app_main(void)
{
     got_time_semaphore = xSemaphoreCreateBinary();

    setenv("TZ", "AEST-10AEDT-11,M10.5.0/02:00:00,M4.1.0/03:00:00", 1);
    tzset();

    printf("first time print\n");
    print_time();           

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    Time_init();
    mqtt_app_start();
    xTaskCreate(Current_time, "time_task", configMINIMAL_STACK_SIZE * 5, NULL, 5, NULL);

    
}
//}
