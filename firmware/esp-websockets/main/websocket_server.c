/* -------------------------------------------------------------------------- */

#include <string.h>
#include <sys/param.h>
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "esp_eth.h"
#include "protocol_examples_common.h"

#include <esp_http_server.h>

#include "websocket_server.h"

/* -------------------------------------------------------------------------- */

static const char *TAG = "SERVER";

/* -------------------------------------------------------------------------- */

static esp_err_t ws_server_handler(httpd_req_t *req);

static httpd_handle_t start_webserver( void );
static esp_err_t stop_webserver( httpd_handle_t server );

static void disconnect_handler( void* arg, 
                                esp_event_base_t event_base,
                                int32_t event_id, 
                                void* event_data);

static void connect_handler(void* arg, 
                            esp_event_base_t event_base,
                            int32_t event_id, 
                            void* event_data );

/* -------------------------------------------------------------------------- */

static QueueHandle_t *user_evt_queue;

struct async_resp_arg
{
    httpd_handle_t hd;
    int fd;
};

static const httpd_uri_t ws = {
        .uri        = "/ws",
        .method     = HTTP_GET,
        .handler    = ws_server_handler,
        .user_ctx   = NULL,
        .is_websocket = true
};

static httpd_handle_t server = NULL;

/* -------------------------------------------------------------------------- */

static esp_err_t ws_server_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        ESP_LOGI(TAG, "WS Handshake done!");
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    /* Set max_len = 0 to get the frame len */
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    // ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);

    // Handle inbound packet by passing it to the benchmark task via queue
    if( user_evt_queue && ws_pkt.len && ws_pkt.type == HTTPD_WS_TYPE_BINARY )
    {
        bench_event_t evt;
        bench_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
        evt.id = BENCH_RECV_CB;

        // Allocate a sufficiently large chunk of memory for the data
        // User task is responsible for freeing this memory
        recv_cb->data = malloc( ws_pkt.len );
        if( recv_cb->data == NULL )
        {
            ESP_LOGE(TAG, "RX Malloc fail");
            return ESP_FAIL;
        }

        // Get the data
        ws_pkt.payload = recv_cb->data;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed" );
            free(recv_cb->data);
            return ret;
        }

        recv_cb->data_len = ws_pkt.len;

        // Put the event into the queue for processing
        if( xQueueSend(user_evt_queue, &evt, 512) != pdTRUE )
        {
            ESP_LOGW(TAG, "RX event failed to enqueue");
            free(recv_cb->data);
        }
    }

    return ret;
}

/* -------------------------------------------------------------------------- */

static httpd_handle_t start_webserver( void )
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        // Registering the ws handler
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &ws);

        return server;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return NULL;
}

static esp_err_t stop_webserver( httpd_handle_t server )
{
    // Stop the httpd server
    return httpd_stop(server);
}

static void disconnect_handler( void* arg, 
                                esp_event_base_t event_base,
                                int32_t event_id, 
                                void* event_data)
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server)
    {
        ESP_LOGI(TAG, "Stopping webserver");
        if( stop_webserver(*server) == ESP_OK )
        {
            *server = NULL;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to stop http server");
        }
    }
}

static void connect_handler(void* arg, 
                            esp_event_base_t event_base,
                            int32_t event_id, 
                            void* event_data )
{
    httpd_handle_t* server = (httpd_handle_t*) arg;
    if (*server == NULL)
    {
        ESP_LOGI(TAG, "Starting webserver");
        *server = start_webserver();
    }
}

/* -------------------------------------------------------------------------- */

void websocket_server_task(void *pvParameters)
{
    ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &connect_handler, &server) );
    ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &disconnect_handler, &server) );

    server = start_webserver();

    // Inbound temp buffer
    char rx_buffer[128];

    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------- */

static int client_fds[CONFIG_LWIP_MAX_LISTENING_TCP] = { 0 };

void websocket_server_send_payload( uint8_t *data, uint32_t length )
{
    esp_err_t ret = ESP_FAIL;
    if( server )
    {
        // Get the list of http clients
        memset(&client_fds, 0, sizeof(client_fds));
        size_t fds = CONFIG_LWIP_MAX_LISTENING_TCP;

        ret = httpd_get_client_list(server, &fds, client_fds);

        // There are some clients?
        if( ret == ESP_OK )
        {
            // Walk through the list, finding websockets clients
            for( int i = 0; i < fds; i++ ) 
            {
                httpd_ws_client_info_t client_info = httpd_ws_get_fd_info(server, client_fds[i]);
                if( client_info == HTTPD_WS_CLIENT_WEBSOCKET )
                {
                    // Form a websockets packet
                    httpd_ws_frame_t ws_pkt;
                    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));

                    ws_pkt.type = HTTPD_WS_TYPE_BINARY;
                    ws_pkt.payload = data;
                    ws_pkt.len = length;

                    // Send it!
                    ret = httpd_ws_send_data( server, client_fds[i], &ws_pkt );
                    return;
                }
            }
        }

    }
}

/* -------------------------------------------------------------------------- */

void websocket_server_register_user_evt_queue( QueueHandle_t *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */
