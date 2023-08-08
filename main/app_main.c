
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs.h"
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
#include "driver/gpio.h"

static const char *TAG = "MQTT_EXAMPLE";

#define TAG "NTP TIME"
#define BLINK_GPIO 2
#define NVS_NAMESPACE "my_namespace"
#define NVS_KEY "long_value"
static char mqtt_payload[50];

SemaphoreHandle_t got_time_semaphore;
bool BUSY = 0;

char OnStatus[5] = "OFF";
char ScheduledTime[50] = "Not Set";
char Current_State[150];

long int scheduled_seconds = 0;

void Set_Schedule(long int seconds);

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

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


        msg_id = esp_mqtt_client_subscribe(client, "time_topic", 2);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
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

        char received_msg[40];
        sprintf(received_msg, "%.*s", event->topic_len, event->data);

       long int seconds = atoi(received_msg);
        printf("Debugging: %d seconds\n", seconds);
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

void Current_status(void *paramss){
   

     xSemaphoreTake(got_time_semaphore, portMAX_DELAY);

    time_t now;
    struct tm time_info;

    

    while (1) {
        time(&now);
        long int current_seconds = (long int ) now;
        localtime_r(&now,  &time_info);
        //printf("Current time: %lld\n", now);
        strftime(mqtt_payload, sizeof(mqtt_payload), "%c",  &time_info);
        // sprintf(Current_State, "Current time: %s - Relay Turned on Till: %s - Relay Status: %s\n", mqtt_payload, ScheduledTime, OnStatus);
        // esp_mqtt_client_publish(client, "time_topic", Current_State, 0, 2, 0);

        nvs_handle_t nvs_handle;
        esp_err_t err;
        err = nvs_flash_init();
        if (err != ESP_OK) {
        printf("NVS Flash init failed\n");
       
        }

        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
        if (err != ESP_OK) {
        printf("NVS Open failed\n");
        
        }

        err = nvs_get_i64(nvs_handle, NVS_KEY, &scheduled_seconds);
        if (err == ESP_OK) {
        printf("2.Read value from NVS: %ld\n", scheduled_seconds);
        } 
        else if (err == ESP_ERR_NVS_NOT_FOUND) {
        printf("NVS Key not found\n");
        } 
        else {
        printf("NVS Get failed\n");
        }

        struct tm *scheduled_time;
        time_t scheduled_time_t = (time_t)scheduled_seconds;

        scheduled_time = localtime(&scheduled_time_t);

        char formatted_time[50];
        strftime(formatted_time, sizeof(formatted_time), "%c", scheduled_time);

    

        if(current_seconds >= scheduled_seconds) {
            //  strcpy(ScheduledTime, "Not set");
            //  strcpy(OnStatus,"OFF");
            //  BUSY = 0;
            //  scheduled_seconds = 0;
              gpio_set_level(BLINK_GPIO, 0);

        }
        else{
              gpio_set_level(BLINK_GPIO, 1);
            //   strcpy(OnStatus,"ON");
            //   strcpy(ScheduledTime, formatted_time);
            //  BUSY = 1;


        }

        //sprintf(Current_State, "Current time: %s - Relay Turned on Till: %s - Relay Status: %s\n", mqtt_payload, ScheduledTime, OnStatus);
        //esp_mqtt_client_publish(client, "time_topic", Current_State, 0, 2, 0);
        nvs_close(nvs_handle);


        vTaskDelay(1000 / portTICK_PERIOD_MS);

        
    }

}




void Set_Schedule(long int seconds){
    /*
        get current seconds
        compare current seconds with received seconds
        if currents seconds are >= to receive seconds then publish message that event can not be scheduled
        else compare received seconds with scheduled seconds
        if scheduled seconds are >= to receive seconds then publish message that event can not be 
        scheduled as received seconds are beheind schedule
        else overwrite scheduled seconds and publish message that new schedule is received seconds
    */
            nvs_handle_t nvs_handle;
            esp_err_t err;
            err = nvs_flash_init();
            if (err != ESP_OK) {
                printf("NVS Flash init failed\n");
                }


            err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
            if (err != ESP_OK) {
                printf("NVS Open failed\n");
            }

             time_t now1;
              time(&now1);
            long int current_seconds = (long int)now1;

            if(current_seconds >= seconds) {
                char msg[] = "Invalid time entry: Behind current time\n";
                esp_mqtt_client_publish(client, "time_topic",msg , 0, 2, 0);
                 }
            else{
                err = nvs_get_i64(nvs_handle, NVS_KEY, &scheduled_seconds);
            if (err == ESP_OK) {
                printf("1. Read value from NVS: %ld\n", scheduled_seconds);
            } 
            else if (err == ESP_ERR_NVS_NOT_FOUND) {
                printf("NVS Key not found\n");
            }
             else {
                printf("NVS Get failed\n");
            }

            if(scheduled_seconds >= seconds) {
                char msg[] = "Invalid time entry: Behind scheduled time\n" ;
                esp_mqtt_client_publish(client, "time_topic",msg , 0, 2, 0);
                
            }
            else{
                 err = nvs_set_i64(nvs_handle, NVS_KEY, seconds);
            if (err != ESP_OK) {
                printf("NVS Set failed\n");
             }

             err = nvs_commit(nvs_handle);
             printf((err != ESP_OK) ? "Failed!\n" : "Done\n");


             err = nvs_get_i64(nvs_handle, NVS_KEY, &scheduled_seconds);
            if (err == ESP_OK) {
                printf("1. Read value from NVS: %ld\n", scheduled_seconds);
            } 
            else if (err == ESP_ERR_NVS_NOT_FOUND) {
                printf("NVS Key not found\n");
            }
             else {
                printf("NVS Get failed\n");
            }

            struct tm *scheduled_time;
            time_t scheduled_time_t = (time_t)scheduled_seconds;

            scheduled_time = localtime(&scheduled_time_t);

            char formatted_time[50];
            strftime(formatted_time, sizeof(formatted_time), "%c", scheduled_time);
            char msg[50];
            strcpy(ScheduledTime, formatted_time);
            sprintf(msg,"Scheduled a new time: %s\n", formatted_time);
                
            esp_mqtt_client_publish(client, "time_topic",msg , 0, 2, 0);

             
            gpio_set_level(BLINK_GPIO, 1);
            // strcpy(OnStatus,"ON");
            // BUSY = 1;


            }


            }

           
             nvs_close(nvs_handle);



    // if (BUSY)
    // {
    //     esp_mqtt_client_publish(client, "time_topic", "The relay event is already scheduled", 0, 2, 0);
    // }
    // else{
    //         nvs_handle_t nvs_handle;
    //         esp_err_t err;
    //         err = nvs_flash_init();
    //         if (err != ESP_OK) {
    //             printf("NVS Flash init failed\n");
                
    //          }


    //         err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    //         if (err != ESP_OK) {
    //             printf("NVS Open failed\n");
            
    //         }


    //         time_t now1;

    //         time(&now1);
    //         long int total_seconds = (long int)now1;
           
    //         total_seconds += seconds;
    //         err = nvs_set_i64(nvs_handle, NVS_KEY, total_seconds);
    //         if (err != ESP_OK) {
    //             printf("NVS Set failed\n");
    //          }

    //          err = nvs_commit(nvs_handle);
    //          printf((err != ESP_OK) ? "Failed!\n" : "Done\n");


    //          err = nvs_get_i64(nvs_handle, NVS_KEY, &scheduled_seconds);
    //         if (err == ESP_OK) {
    //             printf("1. Read value from NVS: %ld\n", scheduled_seconds);
    //         } 
    //         else if (err == ESP_ERR_NVS_NOT_FOUND) {
    //             printf("NVS Key not found\n");
    //         }
    //          else {
    //             printf("NVS Get failed\n");
    //         }
    //         // scheduled_seconds = total_seconds;
    //         // printf("seconds to be scheduled: %ld\n ", scheduled_seconds);
            
    //         struct tm *scheduled_time;
    //         time_t scheduled_time_t = (time_t)scheduled_seconds;

    //         scheduled_time = localtime(&scheduled_time_t);

    //         char formatted_time[50];
    //         strftime(formatted_time, sizeof(formatted_time), "%c", scheduled_time);
    //         char msg[50];
    //         strcpy(ScheduledTime, formatted_time);
    //         sprintf(msg,"Scheduled a new time: %s\n", formatted_time);
                
    //         esp_mqtt_client_publish(client, "time_topic",msg , 0, 2, 0);

             
    //         gpio_set_level(BLINK_GPIO, 1);
    //         strcpy(OnStatus,"ON");
    //         BUSY = 1;

    //         nvs_close(nvs_handle);

        

    // }
    
}



void app_main(void)
{
     got_time_semaphore = xSemaphoreCreateBinary();

    setenv("TZ", "AEST-10AEDT-11,M10.5.0/02:00:00,M4.1.0/03:00:00", 1);
    tzset();
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);

    printf("first time print\n");
    print_time();           

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    Time_init();
    mqtt_app_start();
    xTaskCreate(Current_status, "time_task", configMINIMAL_STACK_SIZE * 7, NULL, 5, NULL);

    
}

