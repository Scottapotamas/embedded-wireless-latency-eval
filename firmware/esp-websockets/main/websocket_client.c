/* -------------------------------------------------------------------------- */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_netif.h"
#include "esp_log.h"
#include "esp_websocket_client.h"

#include "websocket_client.h"

/* -------------------------------------------------------------------------- */

#define HOST_IP_ADDR "192.168.1.33"

/* -------------------------------------------------------------------------- */

static const char *TAG = "CLIENT";

static QueueHandle_t *user_evt_queue;

static TimerHandle_t shutdown_signal_timer;
static SemaphoreHandle_t shutdown_sema;

esp_websocket_client_handle_t client;

/* -------------------------------------------------------------------------- */

static void log_error_if_nonzero( const char *message, int error_code )
{
    if (error_code != 0) {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

/* -------------------------------------------------------------------------- */

static void shutdown_signaler( TimerHandle_t xTimer )
{
    ESP_LOGI(TAG, "No data received for %d seconds, signaling shutdown", NO_DATA_TIMEOUT_SEC );
    xSemaphoreGive( shutdown_sema );
}

/* -------------------------------------------------------------------------- */

static void websocket_event_handler(void *handler_args, 
                                    esp_event_base_t base, 
                                    int32_t event_id, 
                                    void *event_data )
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id) 
    {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
            log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
            if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
            {
                log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
            }
        break;

        case WEBSOCKET_EVENT_DATA:
            // ESP_LOGI(TAG, "WEBSOCKET_EVENT_DATA");
            // ESP_LOGI(TAG, "Received opcode=%d", data->op_code);

            // Text data
            if( data->op_code == 0x01 && data->data_len )
            {
                ESP_LOGI(TAG, "Received=%.*s", data->data_len, (char *)data->data_ptr);
            }

            // Binary data inbound
            if( data->op_code == 0x02 && data->data_len )
            {
                 // Post an event to the user-space event queue with the inbound data
                if( user_evt_queue )
                {
                    bench_event_t evt;
                    bench_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
                    evt.id = BENCH_RECV_CB;

                    // Allocate a sufficiently large chunk of memory and copy the payload into it
                    // User task is responsible for freeing this memory
                    recv_cb->data = malloc( data->data_len );
                    if( recv_cb->data == NULL )
                    {
                        ESP_LOGE(TAG, "RX Malloc fail");
                        return;
                    }

                    memcpy(recv_cb->data, data->data_ptr, data->data_len);
                    recv_cb->data_len = data->data_len;

                    // Put the event into the queue for processing
                    if( xQueueSend(user_evt_queue, &evt, 512) != pdTRUE )
                    {
                        ESP_LOGW(TAG, "RX event failed to enqueue");
                        free(recv_cb->data);
                    }
                }

            }
           
            // ESP_LOGW(TAG, "Total payload length=%d, data_len=%d, current payload offset=%d\r\n", data->payload_len, data->data_len, data->payload_offset);
            xTimerReset(shutdown_signal_timer, portMAX_DELAY);
        break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
            log_error_if_nonzero("HTTP status code",  data->error_handle.esp_ws_handshake_status_code);
            if (data->error_handle.error_type == WEBSOCKET_ERROR_TYPE_TCP_TRANSPORT)
            {
                log_error_if_nonzero("reported from esp-tls", data->error_handle.esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", data->error_handle.esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno",  data->error_handle.esp_transport_sock_errno);
            }
        break;
    }
}

/* -------------------------------------------------------------------------- */

void websocket_client_task(void *pvParameters)
{
    esp_websocket_client_config_t websocket_cfg = {};

    shutdown_signal_timer = xTimerCreate("Websocket shutdown timer", 
                                         NO_DATA_TIMEOUT_SEC * 1000 / portTICK_PERIOD_MS,
                                         pdFALSE, 
                                         NULL,
                                         shutdown_signaler );
    shutdown_sema = xSemaphoreCreateBinary();

    websocket_cfg.uri = CONFIG_WEBSOCKET_SERVER_URI;
    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    while(1)
    {
        client = esp_websocket_client_init( &websocket_cfg );
        esp_websocket_register_events( client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client );

        esp_websocket_client_start(client);
        xTimerStart(shutdown_signal_timer, portMAX_DELAY);

        // Wait for websocket connection to close due to no traffic.
        // Server should use ping to keep it alive
        xSemaphoreTake(shutdown_sema, portMAX_DELAY);
        esp_websocket_client_close(client, portMAX_DELAY);
        ESP_LOGI(TAG, "Websocket Stopped");
        esp_websocket_client_destroy(client);

        vTaskDelay(pdMS_TO_TICKS(50));
    }

}

/* -------------------------------------------------------------------------- */

void websocket_client_send_payload( uint8_t *data, uint32_t length )
{
    if( esp_websocket_client_is_connected(client) )
    {
        esp_websocket_client_send_bin(client, (char*)data, length, portMAX_DELAY );
    }
}

/* -------------------------------------------------------------------------- */

void websocket_client_register_user_evt_queue( QueueHandle_t *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */
