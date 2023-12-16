/* -------------------------------------------------------------------------- */

#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#include "tcp_main_defs.h"

/* -------------------------------------------------------------------------- */

static const char *TAG = "SERVER";

/* -------------------------------------------------------------------------- */

static QueueHandle_t *user_evt_queue;
static int active_sock = -1;

/* -------------------------------------------------------------------------- */

/**
 * Modified from https://github.com/espressif/esp-idf/blob/master/examples/protocols/sockets/non_blocking/main/non_blocking_socket_example.c
 * Non-blocking read,
 *
 * @param[in] tag Logging tag
 * @param[in] sock Socket for reception
 * @param[out] data Data pointer to write the received data
 * @param[in] max_len Maximum size of the allocated space for receiving data
 * @return
 *          >0 : Size of received data
 *          =0 : No data available
 *          -1 : Error occurred during socket read operation
 *          -2 : Socket is not connected, to distinguish between an actual socket error and active disconnection
 */
static int try_receive(const int sock, char * data, size_t max_len)
{
    int len = recv(sock, data, max_len, 0);
    if (len < 0) 
    {
        if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) 
        {
            return 0;   // Not an error
        }

        if (errno == ENOTCONN) 
        {
            ESP_LOGW(TAG, "[sock=%d]: Connection closed", sock);
            return -2;  // Socket has been disconnected
        }

        ESP_LOGW(TAG, "[sock=%d]: Rx Error", sock);
        return -1;
    }

    return len;
}

/* -------------------------------------------------------------------------- */

void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    int keepAlive = 1;
    int keepIdle = KEEPALIVE_IDLE;
    int keepInterval = KEEPALIVE_INTERVAL;
    int keepCount = KEEPALIVE_COUNT;
    struct sockaddr_storage dest_addr;
    
    active_sock = -128;

    // ipv4
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);
    ip_protocol = IPPROTO_IP;
    
    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0)
    {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    ESP_LOGI(TAG, "Socket created");

    // Marking the socket as non-blocking
    int flags = fcntl(listen_sock, F_GETFL);
    if( fcntl(listen_sock, F_SETFL, flags | O_NONBLOCK) == -1 )
    {
        ESP_LOGE(TAG, "Unable to set socket non blocking: errno %d", errno);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket marked as non blocking");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    // Inbound temp buffer
    char rx_buffer[128];

    while (1) 
    {
        if( active_sock < 0 )
        {
            struct sockaddr_storage source_addr;
            socklen_t addr_len = sizeof(source_addr);
            int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            
            if( sock < 0 )
            {
                if( errno == EWOULDBLOCK ) 
                {
                    // The listener socket did not accepts any connection
                    // continue to serve open connections and try to accept again upon the next iteration
                    ESP_LOGV(TAG, "No pending connections...");
                }
                else 
                {
                    ESP_LOGI(TAG, "[sock=%d]: Error when accepting connection %d", sock, errno);
                    goto CLEAN_UP;
                }
            }
            else 
            {
                // Client connected

                // Convert ip address to string
                if (source_addr.ss_family == PF_INET)
                {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                }

                ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

                // Set the client's socket to be non-blocking
                flags = fcntl(sock, F_GETFL);

                if( fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1 ) 
                {
                    ESP_LOGE(TAG, "Unable to set socket non blocking: errno %d", errno);
                    goto CLEAN_UP;
                }
                ESP_LOGI(TAG, "[sock=%d]: Socket marked as non blocking", sock);

                // Set tcp keepalive option
                setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepAlive, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepIdle, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepInterval, sizeof(int));
                setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &keepCount, sizeof(int));

                active_sock = sock;
            }

        }

        if( active_sock >= 0 )
        {
            // This is an open socket -> try to serve it
            int len = try_receive(active_sock, rx_buffer, sizeof(rx_buffer));

            if( len < 0 ) 
            {
                // Error occurred within this client's socket -> close and mark invalid
                ESP_LOGI(TAG, "[sock=%d]: try_receive() returned %d -> closing the socket", active_sock, len);
                close(active_sock);
                active_sock = -1;
            } 
            else if( len > 0 ) 
            {
                ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

                // Post an event to the user-space event queue with the inbound data
                if( user_evt_queue && len)
                {
                    bench_event_t evt;
                    bench_event_recv_cb_t *recv_cb = &evt.data.recv_cb;
                    evt.id = BENCH_RECV_CB;

                    // Allocate a sufficiently large chunk of memory and copy the payload into it
                    // User task is responsible for freeing this memory
                    recv_cb->data = malloc( len );
                    if( recv_cb->data == NULL )
                    {
                        ESP_LOGE(TAG, "RX Malloc fail");
                        return;
                    }

                    memcpy(recv_cb->data, rx_buffer, len);
                    recv_cb->data_len = len;

                    // Put the event into the queue for processing
                    if( xQueueSend(user_evt_queue, &evt, 512) != pdTRUE )
                    {
                        ESP_LOGW(TAG, "RX event failed to enqueue");
                        free(recv_cb->data);
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

CLEAN_UP:
    active_sock = -1;

    close(listen_sock);
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------- */

void tcp_server_send_payload( uint8_t *data, uint32_t length )
{
    if (active_sock < 0)
    {
        ESP_LOGE(TAG, "No active connection to send data");
        return;
    }

    // send() can return less bytes than supplied length.
    // Walk-around for robust implementation.
    int to_write = length;
    while (to_write > 0)
    {
        int written = send(active_sock, data + (length - to_write), to_write, 0);
        if (written < 0)
        {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
            return;
        }
        to_write -= written;
    }

}

/* -------------------------------------------------------------------------- */

void tcp_server_register_user_evt_queue( QueueHandle_t *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */
