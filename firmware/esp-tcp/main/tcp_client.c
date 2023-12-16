/* -------------------------------------------------------------------------- */

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include <netdb.h>            // struct addrinfo
#include <arpa/inet.h>
#include "esp_netif.h"
#include "esp_log.h"

#include "tcp_main_defs.h"

#include "esp_task_wdt.h"

/* -------------------------------------------------------------------------- */

#define HOST_IP_ADDR "192.168.1.33"

/* -------------------------------------------------------------------------- */

static const char *TAG = "CLIENT";

static QueueHandle_t *user_evt_queue;
static int32_t active_sock = -1;

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

void tcp_client_task(void *pvParameters)
{
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;

    char rx_buffer[2048] = { 0 };
    int len = 0;

    while (1)
    {
        struct sockaddr_in dest_addr;
        inet_pton(AF_INET, host_ip, &dest_addr.sin_addr);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;

        int sock = socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0)
        {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }

        // Mark the socket as non-blocking
        int flags = fcntl(sock, F_GETFL);
        if( fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1 )
        {
            ESP_LOGW(TAG, "Unable to set socket %d non blocking %i", sock, errno);
        }

        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        if( connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0 )
        {
            if (errno == EINPROGRESS) 
            {
                ESP_LOGD(TAG, "connection in progress");

                fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(sock, &fdset);

                // Wait until the connecting socket is marked as writable, i.e. connection completes
                int res = select( sock+1, NULL, &fdset, NULL, NULL );
                
                if (res < 0)
                {
                    ESP_LOGE(TAG, "Error during connection: select for socket to be writable: errno %d", errno);
                    goto CLEAN_UP;
                }
                else if (res == 0)
                {
                    ESP_LOGE(TAG, "Connection timeout: select for socket to be writable: errno %d", errno);
                    goto CLEAN_UP;
                }
                else
                {
                    int sockerr;
                    socklen_t socklen = (socklen_t)sizeof(int);

                    if( getsockopt(sock, SOL_SOCKET, SO_ERROR, (void*)(&sockerr), &socklen) < 0 ) 
                    {
                        ESP_LOGE(TAG, "Error getting socket using getsockopt(): errno %d", errno);
                        goto CLEAN_UP;
                    }

                    if( sockerr )
                    {
                        ESP_LOGE(TAG, "Connection error: errno %d", errno);
                        goto CLEAN_UP;
                    }
                }
            } 
            else 
            {
                ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
                break;
            }
        }

        ESP_LOGI(TAG, "Successfully connected");
        active_sock = sock;

        while(1)
        {
            len = try_receive( sock, rx_buffer, sizeof(rx_buffer) );

            if( len < 0 )
            {
                ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                break;
            }
            else if( len > 0 ) 
            {
                // Data received
                // ESP_LOGI(TAG, "Received %d bytes from %s", len, host_ip);

                // Post an event to the user-space event queue with the inbound data
                if( user_evt_queue )
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
            
            // TODO: Work out a more suitable yield/delay here?
            vTaskDelay(pdMS_TO_TICKS(1));
        }

CLEAN_UP:
        if (sock != -1)
        {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
}

/* -------------------------------------------------------------------------- */

void tcp_client_send_payload( uint8_t *data, uint32_t length )
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

void tcp_client_register_user_evt_queue( QueueHandle_t *queue )
{
    if( queue )
    {
        user_evt_queue = queue;
    }
}

/* -------------------------------------------------------------------------- */