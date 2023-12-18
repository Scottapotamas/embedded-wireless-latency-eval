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

#include "udp_main_defs.h"

/* -------------------------------------------------------------------------- */

static const char *TAG = "SERVER";

// #define DEST_IP_ADDR "192.168.1.20"
#define DEST_IP_ADDR "192.168.1.12"

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
    struct sockaddr_in source_addr; // For storing the sender's address
    socklen_t addr_len = sizeof(source_addr);

    int len = recvfrom(sock, data, max_len, 0, (struct sockaddr *)&source_addr, &addr_len);

    if (len < 0) 
    {
        if( errno == EAGAIN || errno == EWOULDBLOCK ) 
        {
            return 0;   // Not an error
        }

        ESP_LOGW(TAG, "[sock=%d]: Rx Error", sock);
        return -1;
    }

    // TODO: Consider passing the sender's address back to the caller

    return len;
}

/* -------------------------------------------------------------------------- */

void udp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = AF_INET;
    int ip_protocol = 0;
    struct sockaddr_storage dest_addr;
    
    active_sock = -128;

    // ipv4
    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);
    ip_protocol = IPPROTO_IP;
    
    int listen_sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
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

    // Inbound temp buffer
    char rx_buffer[2048];

    active_sock = listen_sock;

    while (1) 
    {
        // This is an open socket -> try to serve it
        int len = try_receive(active_sock, rx_buffer, sizeof(rx_buffer));

        // struct sockaddr_storage source_addr;
        // socklen_t addr_len = sizeof(source_addr);

        if( len < 0 ) 
        {
            // Error occurred within this client's socket -> close and mark invalid
            ESP_LOGI(TAG, "[sock=%d]: try_receive() returned %d -> closing the socket", active_sock, len);
            close(active_sock);
            active_sock = -1;
        } 
        else if( len > 0 ) 
        {
            // ESP_LOGI(TAG, "Received %d bytes: %s", len, rx_buffer);

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
        
        vTaskDelay(pdMS_TO_TICKS(1));
    }

CLEAN_UP:
    active_sock = -1;

    close(listen_sock);
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------- */

void udp_server_send_payload( uint8_t *data, uint32_t length )
{
    // Uses hard-coded IP + port
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = inet_addr(DEST_IP_ADDR);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(PORT);

    int sent = sendto(active_sock, data, length, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (sent < 0)
    {
        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        return;
    }

    // Optionally log the number of bytes sent
    // ESP_LOGI(TAG, "Sent %d bytes", sent);
}

/* -------------------------------------------------------------------------- */

void udp_server_register_user_evt_queue( QueueHandle_t *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */
