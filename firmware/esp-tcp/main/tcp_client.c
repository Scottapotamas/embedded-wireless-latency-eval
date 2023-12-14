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

void tcp_client_task(void *pvParameters)
{
    char host_ip[] = HOST_IP_ADDR;
    int addr_family = 0;
    int ip_protocol = 0;

    char rx_buffer[128] = { 0 };
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

        ESP_LOGI(TAG, "Socket created, connecting to %s:%d", host_ip, PORT);

        int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0)
        {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Successfully connected");
        active_sock = sock;

        // Set timeout
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500;
        // setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);

        fd_set readfds;
        int retval;

        while(1)
        {
            FD_ZERO(&readfds);
            FD_SET(sock, &readfds);

            retval = select(sock + 1, &readfds, NULL, NULL, &timeout);

            if( retval == -1 ) 
            {
                ESP_LOGE(TAG, "select() error: errno %d", errno);
                break;
            } 
            else if( retval ) 
            {
                len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
                // len = recv(sock, rx_buffer, sizeof(rx_buffer)-1);

    if (len < 0) {
        
        log_socket_error(tag, sock, errno, "Error occurred during receiving");
        return -1;
    }

    if (errno == EINPROGRESS || errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;   // Not an error
        }
        if (errno == ENOTCONN) {
            ESP_LOGW(tag, "[sock=%d]: Connection closed", sock);
            return -2;  // Socket has been disconnected
        }

                if( len < 0 )
                {
                    ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
                    break;
                }
                else 

                else if( len > 0 ) 
                {
                    // Data received
                    ESP_LOGI(TAG, "Received %d bytes from %s", len, host_ip);

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

                        ESP_LOGI(TAG, " copy");
                        memcpy(recv_cb->data, rx_buffer, len);
                        recv_cb->data_len = len;

                        // Put the event into the queue for processing
                        if( xQueueSend(*user_evt_queue, &evt, 128) != pdTRUE )
                        {
                            ESP_LOGW(TAG, "RX event failed to enqueue");
                            free(recv_cb->data);
                        }
                        ESP_LOGI(TAG, " sent");
                    }
                }
            }   // end select retval

            vTaskDelay(1 / portTICK_PERIOD_MS);
        }

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